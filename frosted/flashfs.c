/*
 *      This file is part of frosted.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *
 *      frosted is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frosted.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors: Daniele Lacamera
 *
 */

#include "frosted.h"
#include "pool.h"
#include "string.h"
#include "locks.h"

#ifdef CONFIG_FLASHFS

static struct module mod_flashfs;
static mutex_t *flashfs_lock;

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 256
#endif

#define F_PREV_PAGE 0xFFFE
#define F_DIR_FLAG  0x8000   /* top bit of fname_len marks a directory entry */

#if defined(TARGET_rp2350)
#define PART_MAP_BASE_DEFAULT (0x101F0000U)
#define PART_SIZE_DEFAULT     (0x10000U)
#define SECRETS_BASE          0  /* no secrets partition on rp2350 */
#elif defined(TARGET_stm32h563)
#define PART_MAP_BASE_DEFAULT (0x081F0000U)
#define PART_SIZE_DEFAULT     (0x10000U)
#define SECRETS_BASE          (0x081FC000U)
#define FLASH_SECTOR_SIZE     (0x2000U)
#else
#error "FlashFS partition is not defined for this target"
#endif

#include "sys/fs/xipfs.h"

static uint32_t part_map_base;
static uint32_t part_size;
#define PART_MAX_PAGES (part_size / FLASH_PAGE_SIZE)
#define BITS_PER_BMP_PAGE (FLASH_PAGE_SIZE * 8)

#define MAX_FNAME 128

int secure_flash_write_page(uint32_t off, uint8_t *page);

#ifdef CONFIG_JEDEC_SPI_FLASH
#include "jedec_spi_flash.h"
static struct jedec_spi_flash *registered_jedec;
#endif

/**
 * Return the effective page count for a flashfs instance.
 * JEDEC-backed mounts use the probed device geometry;
 * internal-flash mounts use the runtime-computed partition size.
 */
#ifdef CONFIG_JEDEC_SPI_FLASH
static inline uint32_t flashfs_effective_pages(const struct jedec_spi_flash *jedec)
{
    if (jedec && jedec->page_count > 0)
        return jedec->page_count;
    return PART_MAX_PAGES;
}
#else
static inline uint32_t flashfs_effective_pages(const void *unused)
{
    (void)unused;
    return PART_MAX_PAGES;
}
#endif

struct __attribute__((packed)) flashfs_file_hdr {
    uint16_t fname_len;
    uint16_t fsize;
};

#define CONFIG_MAX_FLASHFS_FNODES 64

struct flashfs_fnode {
    struct fnode *fno;
    uint32_t startpage;
    uint16_t on_flash_fname_len; /* length of full relative path stored on flash */
#ifdef CONFIG_JEDEC_SPI_FLASH
    const struct jedec_spi_flash *jedec;
#endif
};

POOL_DEFINE(flashfs_fnode_pool, struct flashfs_fnode, CONFIG_MAX_FLASHFS_FNODES);

/* Build the full relative path from mount point for a file fnode.
 * Walks fno->parent chain up to (but not including) the mount directory.
 * Returns length written (excluding null terminator), or -1 if truncated.
 */
static int flashfs_build_relpath(struct fnode *fno, char *buf, int buflen)
{
    /* Collect path components by walking up the parent chain.
     * Stop at the mount directory (FL_DIR node whose parent is not flashfs-owned
     * or is the VFS root). Do NOT include the mount dir itself.
     */
    struct fnode *chain[8];
    int depth = 0;
    struct fnode *cur = fno;
    while (cur && depth < 8) {
        /* Stop if cur is the mount directory (its parent is not flashfs-owned) */
        if (!cur->parent || cur->parent == cur ||
            cur->parent->owner != &mod_flashfs) {
            if (cur->flags & FL_DIR)
                break; /* cur is the mount point — don't include it */
        }
        chain[depth++] = cur;
        cur = cur->parent;
    }
    if (depth == 0) {
        /* Shouldn't happen — fno is the mount dir itself */
        buf[0] = '\0';
        return 0;
    }
    if (depth == 1) {
        /* File directly under mount point */
        int len = strlen(fno->fname);
        if (len >= buflen)
            return -1;
        memcpy(buf, fno->fname, len + 1);
        return len;
    }
    /* Build path from outermost to innermost */
    int pos = 0;
    for (int i = depth - 1; i >= 0; i--) {
        int slen = strlen(chain[i]->fname);
        if (pos + slen + (i > 0 ? 1 : 0) >= buflen)
            return -1;
        memcpy(buf + pos, chain[i]->fname, slen);
        pos += slen;
        if (i > 0)
            buf[pos++] = '/';
    }
    buf[pos] = '\0';
    return pos;
}

/* Walk parent chain to find the jedec handle for a file in a subdirectory */
static const struct jedec_spi_flash *flashfs_find_jedec(struct fnode *fno)
{
#ifdef CONFIG_JEDEC_SPI_FLASH
    struct fnode *cur = fno->parent;
    while (cur && cur->parent != cur) {
        if (cur->owner == &mod_flashfs && cur->priv &&
            (cur->flags & FL_DIR))
            return (const struct jedec_spi_flash *)cur->priv;
        cur = cur->parent;
    }
#endif
    return NULL;
}

/* Bitmap in last N flash pages. Inverted logic (starts at 0xFF when formatted).
 * With multi-page bitmap, page p maps to bitmap page (total - bmp_count + p/BITS_PER_BMP_PAGE),
 * byte offset (p % BITS_PER_BMP_PAGE) / 8, bit (p & 7). */

static inline uint32_t flashfs_bmp_page_count(const void *jedec)
{
    uint32_t total = flashfs_effective_pages(jedec);
    return (total + BITS_PER_BMP_PAGE - 1) / BITS_PER_BMP_PAGE;
}

static inline uint32_t flashfs_usable_pages(const void *jedec)
{
    return flashfs_effective_pages(jedec) - flashfs_bmp_page_count(jedec);
}

/* Flash page number of the bitmap page covering data page p */
static inline uint32_t bmp_flash_page_for(const void *jedec, uint32_t p)
{
    uint32_t total = flashfs_effective_pages(jedec);
    uint32_t bmp_count = flashfs_bmp_page_count(jedec);
    return (total - bmp_count) + (p / BITS_PER_BMP_PAGE);
}

#ifdef CONFIG_JEDEC_SPI_FLASH
static int flashfs_read_jedec_page(const struct jedec_spi_flash *jedec, uint16_t page,
        uint8_t *buf)
{
    int ret;

    if (!jedec || !buf)
        return -EINVAL;
    ret = jedec_spi_flash_read(jedec, (uint32_t)page * FLASH_PAGE_SIZE,
            buf, FLASH_PAGE_SIZE);
    if (ret < 0)
        kprintf("flashfs: JEDEC read page %u failed (%d)\n", page, ret);
    return ret;
}

static int flashfs_write_jedec_page(const struct jedec_spi_flash *jedec, uint16_t page,
        const uint8_t *buf)
{
    if (!jedec || !buf)
        return -EINVAL;
    return jedec_spi_flash_write_page(jedec, (uint32_t)page * FLASH_PAGE_SIZE,
            buf, FLASH_PAGE_SIZE);
}

static const struct jedec_spi_flash *flashfs_lookup_jedec_source(const char *source)
{
    struct fnode *src_fno;

    if (!registered_jedec || !registered_jedec->probed || !source)
        goto from_devnode;
    if (registered_jedec->dev_path && strcmp(source, registered_jedec->dev_path) == 0)
        return registered_jedec;
    if (strcmp(source, "/dev/spiflash0") == 0)
        return registered_jedec;

from_devnode:
    if (!source)
        return NULL;
    src_fno = fno_search(source);
    if (!src_fno || !src_fno->owner || !src_fno->priv)
        return NULL;
    if (strcmp(src_fno->owner->name, "jedecflash") != 0)
        return NULL;
    return (const struct jedec_spi_flash *)src_fno->priv;
}
#endif

static int flashfs_read_page(const struct jedec_spi_flash *jedec, uint16_t page,
        uint8_t *buf)
{
#ifdef CONFIG_JEDEC_SPI_FLASH
    if (jedec)
        return flashfs_read_jedec_page(jedec, page, buf);
#endif
    memcpy(buf, (void *)(uintptr_t)(part_map_base + page * FLASH_PAGE_SIZE), FLASH_PAGE_SIZE);
    return 0;
}

static int flashfs_write_page(const struct jedec_spi_flash *jedec, uint16_t page,
        const uint8_t *buf)
{
#ifdef CONFIG_JEDEC_SPI_FLASH
    if (jedec)
        return flashfs_write_jedec_page(jedec, page, buf);
#endif
    if (secure_flash_write_page(page * FLASH_PAGE_SIZE, (uint8_t *)buf) != 0)
        return -EIO;
    return 0;
}

static int fs_bmp_clear(const struct jedec_spi_flash *jedec, uint32_t page)
{
    static uint8_t cache_bmp[FLASH_PAGE_SIZE];

    uint32_t bp = bmp_flash_page_for(jedec, page);
    uint32_t byte_off = (page % BITS_PER_BMP_PAGE) / 8;
    if (flashfs_read_page(jedec, bp, cache_bmp) < 0)
        return -EIO;
    cache_bmp[byte_off] |= 1 << (page & 7);
    if (flashfs_write_page(jedec, bp, cache_bmp) != 0)
        return -EIO;
    return 0;
}

static int fs_bmp_set(const struct jedec_spi_flash *jedec, uint32_t page)
{
    static uint8_t cache_bmp[FLASH_PAGE_SIZE];
    uint32_t bp = bmp_flash_page_for(jedec, page);
    uint32_t byte_off = (page % BITS_PER_BMP_PAGE) / 8;
    if (flashfs_read_page(jedec, bp, cache_bmp) < 0)
        return -EIO;
    cache_bmp[byte_off] &= ~(1 << (page & 7));
    if (flashfs_write_page(jedec, bp, cache_bmp) != 0)
        return -EIO;
    return 0;
}

static int fs_bmp_test(const struct jedec_spi_flash *jedec, uint32_t page)
{
    uint8_t cache_bmp[FLASH_PAGE_SIZE];
    uint32_t bp = bmp_flash_page_for(jedec, page);
    uint32_t byte_off = (page % BITS_PER_BMP_PAGE) / 8;
    if (flashfs_read_page(jedec, bp, cache_bmp) < 0)
        return 0;
    return !(cache_bmp[byte_off] & (1 << (page & 7)));
}

static int fs_bmp_find_free(const struct jedec_spi_flash *jedec, int pages)
{
    int i;
    int sz = 0;
    int first = -1;
    uint32_t max_pages = flashfs_usable_pages(jedec);
    if (pages <= 0)
        return -1;
    for (i = 0; i < (int)max_pages; i++) {
        if (!fs_bmp_test(jedec, i)){
            if (sz++ == 0)
                first = i;
            if (sz == pages)
                return i;
        }
        else sz = 0;
    }
    return -1;
}

static int get_page_count_on_flash(const char *filename, uint16_t sz)
{
    uint16_t first_cap = FLASH_PAGE_SIZE - (sizeof(struct flashfs_file_hdr) + strlen(filename) + 1);
    uint16_t cont_cap = FLASH_PAGE_SIZE - sizeof(struct flashfs_file_hdr);
    if (sz <= first_cap)
        return 1;
    sz -= first_cap;
    return 1 + (sz + cont_cap - 1) / cont_cap;
}

struct flashfs_file_hdr *get_page_header(uint16_t page)
{
    struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)(part_map_base +
            page * FLASH_PAGE_SIZE);
    if ((hdr->fname_len == 0xFFFF) || (hdr->fname_len == 0x0000)
            || (hdr->fname_len == F_PREV_PAGE))
        return NULL;
    return hdr;
}

char *get_page_filename(uint16_t page)
{
    struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)(part_map_base +
            page * FLASH_PAGE_SIZE);
    if ((hdr->fname_len == 0xFFFF) || (hdr->fname_len == 0x0000)
            || (hdr->fname_len == F_PREV_PAGE))
        return NULL;
    if (hdr->fname_len > MAX_FNAME) {
        fs_bmp_clear(NULL, page);
        return NULL;
    }
    /* Force null-termination for filename in RAM copy */
    char *fname = (char *)hdr + sizeof(struct flashfs_file_hdr);
    fname[MAX_FNAME] = '\0';
    if (*((char *)part_map_base + page * FLASH_PAGE_SIZE +
            sizeof(struct flashfs_file_hdr) + hdr->fname_len) != 0) {
        fs_bmp_clear(NULL, page);
        return NULL;
    }
    return (char *)hdr + sizeof(struct flashfs_file_hdr);
}

int get_content_size(uint16_t page)
{
    struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)(part_map_base +
            page * FLASH_PAGE_SIZE);
    uint32_t size = 0;
    if ((hdr->fname_len == 0xFFFF) || (hdr->fname_len == 0x0000)
            || (hdr->fname_len == F_PREV_PAGE))
        return -1;
    size = hdr->fsize;
    return size;
}

int get_size_in_page(uint16_t page)
{
    struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)(part_map_base +
            page * FLASH_PAGE_SIZE);
    uint32_t size = 0;
    int total_fsize = get_content_size(page);
    if (total_fsize < 0)
        return -1;
    if ((hdr->fname_len == 0xFFFF) || (hdr->fname_len == 0x0000))
        return -1;
    if ((hdr->fname_len == F_PREV_PAGE))
        size = FLASH_PAGE_SIZE - sizeof(struct flashfs_file_hdr);
    else 
        size = FLASH_PAGE_SIZE - (sizeof(struct flashfs_file_hdr) +
            hdr->fname_len + 1);
    return size;
}

uint8_t *get_page_content(uint16_t page)
{
    struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)(part_map_base +
            page * FLASH_PAGE_SIZE);
    if ((hdr->fname_len == 0xFFFF) || (hdr->fname_len == 0x0000)
            || (hdr->fname_len == F_PREV_PAGE))
        return (uint8_t *)part_map_base + page * FLASH_PAGE_SIZE +
                sizeof(struct flashfs_file_hdr);
    else
        return (uint8_t *)part_map_base + page * FLASH_PAGE_SIZE +
                sizeof(struct flashfs_file_hdr) + hdr->fname_len;
} 

static int flash_commit_file_info(struct fnode *fno)
{
    struct flashfs_fnode *mfno;
    struct flashfs_file_hdr *hdr;
    uint8_t page_cache[FLASH_PAGE_SIZE];
    char relpath[MAX_FNAME];
    int pathlen;

    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -ENOENT;

    pathlen = flashfs_build_relpath(fno, relpath, MAX_FNAME);
    if (pathlen < 0)
        return -ENAMETOOLONG;

    if (flashfs_read_page(mfno->jedec, mfno->startpage, page_cache) < 0)
        return -EIO;

    hdr = (struct flashfs_file_hdr *)page_cache;
    hdr->fsize = fno->size;
    hdr->fname_len = pathlen;
    mfno->on_flash_fname_len = pathlen;
    memcpy(page_cache + sizeof(struct flashfs_file_hdr), relpath,
            pathlen + 1);
    if (flashfs_write_page(mfno->jedec, mfno->startpage, page_cache) != 0)
        return -EIO;
    return 0;
}

static int relocate_file(struct fnode *fno, uint16_t newpage,
        uint16_t old_page_count, uint16_t new_page_count)
{
    struct flashfs_fnode *mfno;
    struct flashfs_file_hdr *hdr; 
    uint8_t page_cache[FLASH_PAGE_SIZE];
    int i;

    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -ENOENT;

    mutex_lock(flashfs_lock);
    for (i = 0; i < old_page_count; i++)
    {
        if (flashfs_read_page(mfno->jedec, mfno->startpage + i, page_cache) < 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
        if (flashfs_write_page(mfno->jedec, newpage + i, page_cache) != 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
    } 
    memset(page_cache, 0xFF, FLASH_PAGE_SIZE);
    for (i = old_page_count; i < new_page_count; i++)
    {
        if (flashfs_write_page(mfno->jedec, newpage + i, page_cache) != 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
    }
    for (i = 0; i < new_page_count; i++)  {
        if (fs_bmp_set(mfno->jedec, newpage + i) != 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
    }
    for (i = 0; i < old_page_count; i++) {
        if (fs_bmp_clear(mfno->jedec, mfno->startpage + i) != 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
    }
    mfno->startpage = newpage;
    mutex_unlock(flashfs_lock);
    return 0;
}


static int flashfs_read(struct fnode *fno, void *buf, unsigned int len)
{
    struct flashfs_fnode *mfno;
    uint32_t off;
    uint8_t *content;
    int page_off;
    int take = 0;
    int size_in_page = 0;
    int fname_len;
    if (len <= 0)
        return len;

    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -ENOENT;

    fname_len = mfno->on_flash_fname_len;
    off = task_fd_get_off(fno);

    if (fno->size <= off)
        return 0;

    if (len > (fno->size - off))
        len = fno->size - off;

    while (take < len) {
        int first_cap = FLASH_PAGE_SIZE - (sizeof(struct flashfs_file_hdr) + fname_len + 1);
        int cont_cap = FLASH_PAGE_SIZE - sizeof(struct flashfs_file_hdr);
        if (off < (uint32_t)first_cap) {
            page_off = off + sizeof(struct flashfs_file_hdr) + fname_len + 1;
            size_in_page = first_cap - off;
            if (size_in_page > len - take)
                size_in_page = len - take;
            if (mfno->jedec) {
                uint32_t addr = mfno->startpage * FLASH_PAGE_SIZE + page_off;
                if (jedec_spi_flash_read(mfno->jedec, addr, buf + take, size_in_page) < 0)
                    return take > 0 ? take : -EIO;
            } else {
                memcpy(buf + take, get_page_content(mfno->startpage) + off,
                        size_in_page);
            }
            take += size_in_page;
            off += size_in_page;
        } else {
            int data_off = off - first_cap;
            int page_idx = 1 + data_off / cont_cap;
            page_off = sizeof(struct flashfs_file_hdr) + data_off % cont_cap;
            size_in_page = FLASH_PAGE_SIZE - page_off;
            if (size_in_page > len - take)
                size_in_page = len - take;
            if (mfno->jedec) {
                uint32_t addr = (mfno->startpage + page_idx) * FLASH_PAGE_SIZE + page_off;
                if (jedec_spi_flash_read(mfno->jedec, addr, buf + take, size_in_page) < 0)
                    return take > 0 ? take : -EIO;
            } else {
                content = (uint8_t *)part_map_base +
                    (mfno->startpage + page_idx) * FLASH_PAGE_SIZE + page_off;
                memcpy(buf + take, content, size_in_page);
            }
            take += size_in_page;
            off += size_in_page;
        }
    }
    task_fd_set_off(fno, off);
    return take;
}


static int flashfs_write(struct fnode *fno, const void *buf, unsigned int len)
{
    struct flashfs_fnode *mfno;
    uint32_t off;
    int written = 0;
    int ret = 0;
    int page_off;
    int size_in_page = 0;
    int fname_len;
    char relpath[MAX_FNAME];
    static uint8_t page_cache[FLASH_PAGE_SIZE];

    if (len <= 0)
        return len;

    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -ENOENT;

    fname_len = mfno->on_flash_fname_len;
    off = task_fd_get_off(fno);

    if (fno->size < (off + len)) {
        int new_page_count, old_page_count;
        if (flashfs_build_relpath(fno, relpath, MAX_FNAME) < 0)
            return -ENAMETOOLONG;
        old_page_count = get_page_count_on_flash(relpath, fno->size);
        new_page_count = get_page_count_on_flash(relpath, off + len);
        if (new_page_count > old_page_count) {
            int i, n_new_pages;
            n_new_pages = new_page_count - old_page_count;
            for (i = 0; i < n_new_pages; i++) {
                if (fs_bmp_test(mfno->jedec, mfno->startpage + old_page_count + i)) {
                    int new_loc = fs_bmp_find_free(mfno->jedec, new_page_count);
                    if (new_loc > 0) {
                        int r = relocate_file(fno, new_loc, old_page_count, new_page_count);
                        if (r != 0)
                            return r;
                        mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
                        if (!mfno)
                            return -ENOENT;
                        goto actual_write;
                    } else
                        return -ENOSPC;
                } else {
                    mutex_lock(flashfs_lock);
                    if (fs_bmp_set(mfno->jedec, mfno->startpage + old_page_count + i) != 0) {
                        mutex_unlock(flashfs_lock);
                        return -EIO;
                    }
                    mutex_unlock(flashfs_lock);
                }
            }
        }
        fno->size = off + len;
    }

actual_write:
    mutex_lock(flashfs_lock);
    while (written < len) {
        int first_cap = FLASH_PAGE_SIZE - (sizeof(struct flashfs_file_hdr) + fname_len + 1);
        int cont_cap = FLASH_PAGE_SIZE - sizeof(struct flashfs_file_hdr);
        if (off < (uint32_t)first_cap) {
            if (flashfs_read_page(mfno->jedec, mfno->startpage, page_cache) < 0) {
                mutex_unlock(flashfs_lock);
                return -EIO;
            }
            page_off = off + sizeof(struct flashfs_file_hdr) + fname_len + 1;
            size_in_page = first_cap - off;
            if (size_in_page > len - written)
                size_in_page = len - written;
            memcpy(page_cache + page_off, buf + written, size_in_page);
            written += size_in_page;
            off += size_in_page;
            ret = flashfs_write_page(mfno->jedec, mfno->startpage, page_cache);
            if (ret != 0) {
                mutex_unlock(flashfs_lock);
                return ret;
            }
        } else {
            int data_off = off - first_cap;
            int current_page_n = 1 + data_off / cont_cap;
            page_off = sizeof(struct flashfs_file_hdr) + data_off % cont_cap;
            if (flashfs_read_page(mfno->jedec, mfno->startpage + current_page_n,
                        page_cache) < 0) {
                mutex_unlock(flashfs_lock);
                return -EIO;
            }
            size_in_page = FLASH_PAGE_SIZE - page_off;
            if (size_in_page > len - written)
                size_in_page = len - written;
            memcpy(page_cache + page_off, buf + written, size_in_page);
            written += size_in_page;
            off += size_in_page;
            ret = flashfs_write_page(mfno->jedec, mfno->startpage + current_page_n, page_cache);
            if (ret != 0) {
                mutex_unlock(flashfs_lock);
                return ret;
            }
        }
    }
    task_fd_set_off(fno, off);
    ret = flash_commit_file_info(fno);
    mutex_unlock(flashfs_lock);
    if (ret != 0)
        return ret;
    return len;
}

static int flashfs_poll(struct fnode *fno, uint16_t events, uint16_t *revents)
{
    *revents = events;
    return 1;
}

static int flashfs_seek(struct fnode *fno, int off, int whence)
{
    struct flashfs_fnode *mfno;
    int new_off;
    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -1;
    switch(whence) {
        case SEEK_CUR:
            new_off = task_fd_get_off(fno) + off;
            break;
        case SEEK_SET:
            new_off = off;
            break;
        case SEEK_END:
            new_off = fno->size + off;
            break;
        default:
            return -EINVAL;
    }

    if (new_off < 0)
        new_off = 0;

    if (new_off > fno->size) {
        int new_page_count, old_page_count;
        old_page_count = get_page_count_on_flash(fno->fname, fno->size);
        new_page_count = get_page_count_on_flash(fno->fname, new_off);
        if (new_page_count > old_page_count) {
            int i, n_new_pages;
            n_new_pages = new_page_count - old_page_count;
            for (i = 0; i < n_new_pages; i++) {
                if (fs_bmp_test(mfno->jedec, mfno->startpage + old_page_count + i)) {
                    int new_loc = fs_bmp_find_free(mfno->jedec, new_page_count);
                    if (new_loc > 0)
                        return relocate_file(fno, new_loc, old_page_count, new_page_count);
                    else
                        return -ENOSPC;
                } else {
                    if (fs_bmp_set(mfno->jedec, mfno->startpage + old_page_count + i) != 0)
                        return -EIO;
                }
            }
        }
        mutex_lock(flashfs_lock);
        fno->size = new_off;
        if (flash_commit_file_info(fno) != 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
        mutex_unlock(flashfs_lock);
    }
    task_fd_set_off(fno, new_off);
    return new_off;
}

static int flashfs_close(struct fnode *fno)
{
    struct flashfs_fnode *mfno;
    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -1;
    return 0;
}

static int flashfs_creat(struct fnode *fno)
{
    struct flashfs_fnode *mfs;
    const struct jedec_spi_flash *jedec = NULL;
    char relpath[MAX_FNAME];
    int pathlen;
    int page_count, first_page, page;

    if (!fno)
        return -EINVAL;
    /* Directory fnodes have no on-flash representation — they're
     * materialised from file-path prefixes at lookup time. */
    if (fno->flags & FL_DIR)
        return 0;
    jedec = flashfs_find_jedec(fno);
#ifdef CONFIG_JEDEC_SPI_FLASH
    /* JEDEC-backed mounts are read-only here; writing would corrupt the
     * backing image with zero-byte placeholder headers. */
    if (jedec)
        return -EROFS;
#endif

    pathlen = flashfs_build_relpath(fno, relpath, MAX_FNAME);
    if (pathlen < 0)
        return -ENAMETOOLONG;
    page_count = get_page_count_on_flash(relpath, fno->size);
    first_page = fs_bmp_find_free(jedec, page_count);

    if (first_page < 0)
        return -ENOSPC;

    mutex_lock(flashfs_lock);
    page = first_page;
    while (page < first_page + page_count) {
        if (fs_bmp_set(jedec, page++) != 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
    }

    mfs = pool_alloc(&flashfs_fnode_pool);
    if (mfs) {
        mfs->fno = fno;
        mfs->startpage = first_page;
        mfs->on_flash_fname_len = pathlen;
#ifdef CONFIG_JEDEC_SPI_FLASH
        mfs->jedec = jedec;
#endif
        fno->priv = mfs;
        if (flash_commit_file_info(fno) == 0) {
            mutex_unlock(flashfs_lock);
            return 0;
        }
        mutex_unlock(flashfs_lock);
        return -EIO;
    }
    mutex_unlock(flashfs_lock);
    return -1;
}

static int flashfs_unlink(struct fnode *fno)
{
    struct flashfs_fnode *mfno;
    int page, page_count;
    char relpath[MAX_FNAME];
    int pathlen;
    if (!fno)
        return -ENOENT;
    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -EPERM;
    pathlen = flashfs_build_relpath(fno, relpath, MAX_FNAME);
    if (pathlen < 0)
        return -ENAMETOOLONG;
    page_count = get_page_count_on_flash(relpath, fno->size);
    page = mfno->startpage;
    while(page_count--) {
        if (fs_bmp_clear(mfno->jedec, page++) != 0)
            return -EIO;
    }
    kfree(mfno);
    return 0;
}

static int flashfs_truncate(struct fnode *fno, unsigned int newsize)
{
    struct flashfs_fnode *mfno;
    int old_page_count, new_page_count;
    char relpath[MAX_FNAME];
    if (!fno)
        return -ENOENT;
    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (mfno) {
        if (fno->size <= newsize)
            return 0;
        if (flashfs_build_relpath(fno, relpath, MAX_FNAME) < 0)
            return -ENAMETOOLONG;
        old_page_count = get_page_count_on_flash(relpath, fno->size);
        new_page_count = get_page_count_on_flash(relpath, newsize);
        while (old_page_count > new_page_count) {
            if (fs_bmp_clear(mfno->jedec, mfno->startpage + (--old_page_count)) != 0)
                return -EIO;
        }
        fno->size = newsize;
        return 0;
    }
    return -EFAULT;
}

/*
 * Read the header and filename for `page` into caller-provided buffers.
 * Returns 1 if the page holds a valid entry (file or directory), 2 for a
 * directory entry, 0 if the page is used but is a continuation or
 * unreadable header, -EIO on read error.
 * On success, `*out_hdr` has fname_len stripped of the DIR flag, and
 * `out_fname` is null-terminated. out_fname must be at least
 * MAX_FNAME+1 bytes.
 */
#define FLASHFS_ENTRY_FILE 1
#define FLASHFS_ENTRY_DIR  2

static int flashfs_read_entry(const struct jedec_spi_flash *jedec, uint32_t page,
        struct flashfs_file_hdr *out_hdr, char *out_fname)
{
    uint8_t page_cache[FLASH_PAGE_SIZE];
    struct flashfs_file_hdr *hdr;
    uint16_t raw_len;
    int is_dir;
    uint16_t name_len;

#ifdef CONFIG_JEDEC_SPI_FLASH
    if (jedec) {
        if (flashfs_read_jedec_page(jedec, page, page_cache) < 0)
            return -EIO;
        hdr = (struct flashfs_file_hdr *)page_cache;
    } else
#endif
    {
        /* For internal flash, read straight from the XIP-mapped partition. */
        hdr = (struct flashfs_file_hdr *)(uintptr_t)
            (part_map_base + page * FLASH_PAGE_SIZE);
    }

    raw_len = hdr->fname_len;
    if (raw_len == 0xFFFF || raw_len == 0x0000 || raw_len == F_PREV_PAGE)
        return 0;

    is_dir = (raw_len & F_DIR_FLAG) != 0;
    name_len = (uint16_t)(raw_len & ~F_DIR_FLAG);
    if (name_len == 0 || name_len > MAX_FNAME)
        return 0;

#ifdef CONFIG_JEDEC_SPI_FLASH
    if (jedec) {
        if (page_cache[sizeof(*hdr) + name_len] != 0)
            return 0;
        memcpy(out_hdr, hdr, sizeof(*hdr));
        out_hdr->fname_len = name_len;
        memcpy(out_fname, page_cache + sizeof(*hdr), name_len);
        out_fname[name_len] = '\0';
        return is_dir ? FLASHFS_ENTRY_DIR : FLASHFS_ENTRY_FILE;
    }
#endif
    {
        const char *fname = (const char *)hdr + sizeof(*hdr);
        if (fname[name_len] != 0)
            return 0;
        memcpy(out_hdr, hdr, sizeof(*hdr));
        out_hdr->fname_len = name_len;
        memcpy(out_fname, fname, name_len);
        out_fname[name_len] = '\0';
    }
    return is_dir ? FLASHFS_ENTRY_DIR : FLASHFS_ENTRY_FILE;
}

/*
 * Build the on-flash relative path of a directory fnode `dir`, by walking up
 * the parent chain until we reach the mount point (first ancestor whose
 * parent is not owned by flashfs). Writes "" if `dir` is the mount dir.
 * Returns the written length, or -1 on truncation/overflow.
 */
static int flashfs_dir_relpath(struct fnode *dir, char *buf, int buflen)
{
    struct fnode *chain[8];
    int depth = 0;
    struct fnode *cur = dir;
    int pos = 0;
    int i;

    while (cur && depth < 8) {
        if (!cur->parent || cur->parent == cur ||
                cur->parent->owner != &mod_flashfs)
            break; /* cur is the mount dir — don't include */
        chain[depth++] = cur;
        cur = cur->parent;
    }

    if (depth == 0) {
        if (buflen < 1)
            return -1;
        buf[0] = '\0';
        return 0;
    }

    for (i = depth - 1; i >= 0; i--) {
        int slen = strlen(chain[i]->fname);
        if (pos + slen + (i > 0 ? 1 : 0) >= buflen)
            return -1;
        memcpy(buf + pos, chain[i]->fname, slen);
        pos += slen;
        if (i > 0)
            buf[pos++] = '/';
    }
    buf[pos] = '\0';
    return pos;
}

/*
 * Walk up from `fno` to find the mount-point fnode (first flashfs-owned
 * ancestor whose parent is not flashfs). Returns NULL if not inside a
 * flashfs tree.
 */
static struct fnode *flashfs_find_mount(struct fnode *fno)
{
    struct fnode *cur = fno;
    while (cur && cur->owner == &mod_flashfs) {
        if (!cur->parent || cur->parent == cur ||
                cur->parent->owner != &mod_flashfs)
            return cur;
        cur = cur->parent;
    }
    return NULL;
}

static const struct jedec_spi_flash *flashfs_jedec_for(struct fnode *fno)
{
#ifdef CONFIG_JEDEC_SPI_FLASH
    struct fnode *mnt = flashfs_find_mount(fno);
    if (mnt)
        return (const struct jedec_spi_flash *)mnt->priv;
#endif
    (void)fno;
    return NULL;
}

/*
 * Lazy mount: record the backing device on the mount fnode and return.
 * No eager scan of files; entries are materialised on demand in
 * flashfs_lookup / flashfs_readdir, and released naturally when the
 * fnode pool needs the slots.
 */
static int flashfs_mount(char *source, char *tgt, uint32_t flags, void *arg)
{
    struct fnode *tgt_dir = NULL;
#ifdef CONFIG_JEDEC_SPI_FLASH
    const struct jedec_spi_flash *jedec = NULL;
#endif
    (void)flags;
    (void)arg;

#ifdef CONFIG_JEDEC_SPI_FLASH
    if (source) {
        jedec = flashfs_lookup_jedec_source(source);
        if (!jedec)
            return -ENODEV;
    }
#else
    if (source)
        return -ENODEV;
#endif

    if (!tgt)
        return -EINVAL;

    tgt_dir = fno_search(tgt);
    if (!tgt_dir || ((tgt_dir->flags & FL_DIR) == 0))
        return -ENOTDIR;

    tgt_dir->owner = &mod_flashfs;
#ifdef CONFIG_JEDEC_SPI_FLASH
    tgt_dir->priv = (void *)jedec;
#else
    tgt_dir->priv = NULL;
#endif
    return 0;
}

/*
 * Single-page bitmap cache used by scan loops (lookup / readdir).
 * The tag pairs (jedec pointer, bitmap flash-page number) uniquely identify
 * the cached content.
 */
static uint8_t flashfs_bmp_cache[FLASH_PAGE_SIZE];
static const struct jedec_spi_flash *flashfs_bmp_cache_jedec;
static uint32_t flashfs_bmp_cache_page = 0xFFFFFFFFu;

/*
 * On-demand page_used probe. Keeps one bitmap page cached across calls,
 * so sequential scans don't thrash the backing store.
 */
static int flashfs_page_used(const struct jedec_spi_flash *jedec, uint32_t page)
{
#ifdef CONFIG_JEDEC_SPI_FLASH
    if (jedec) {
        uint32_t bp = bmp_flash_page_for(jedec, page);
        uint32_t byte_off;
        if (jedec != flashfs_bmp_cache_jedec || bp != flashfs_bmp_cache_page) {
            if (flashfs_read_page(jedec, bp, flashfs_bmp_cache) < 0)
                return -1;
            flashfs_bmp_cache_jedec = jedec;
            flashfs_bmp_cache_page = bp;
        }
        byte_off = (page % BITS_PER_BMP_PAGE) / 8;
        return !(flashfs_bmp_cache[byte_off] & (1 << (page & 7)));
    }
#endif
    return fs_bmp_test(NULL, page);
}

/*
 * Cache eviction. flashfs fnodes are materialised on demand by lookup, so
 * the VFS fnode pool effectively acts as a bounded cache over the on-flash
 * entries. When it fills up, prune a flashfs-owned file fnode that isn't
 * currently open (usage_count == 0, FL_INUSE clear). Never evicts the
 * mount dir or any other directory — those are cheap to keep live and
 * re-creating them would require another flash prefix scan.
 *
 * This does NOT go through fno_unlink / ops.unlink: that path intentionally
 * clears the on-flash bitmap (a real delete). Eviction is purely in-RAM.
 */
static int flashfs_evict_one(void)
{
    uint32_t i;
    for (i = 0; i < flashfs_fnode_pool.capacity; i++) {
        struct flashfs_fnode *mfs;
        struct fnode *fno;
        /* freemap bit == 1 means slot is free, 0 means in use. */
        if (flashfs_fnode_pool.freemap[i >> 5] & (1u << (i & 31)))
            continue;
        mfs = ((struct flashfs_fnode *)flashfs_fnode_pool.base) + i;
        fno = mfs->fno;
        if (!fno || fno->owner != &mod_flashfs)
            continue;
        if (fno->flags & (FL_DIR | FL_INUSE))
            continue;
        if (fno->usage_count != 0)
            continue;
        /* Detach without invoking ops.unlink — purely a cache eviction,
         * not a real delete against the backing store. */
        fno->priv = NULL;
        pool_free(&flashfs_fnode_pool, mfs);
        fno_detach(fno);
        return 1;
    }
    return 0;
}

/*
 * Allocate an fnode under `dir` named `name`, evicting a stale cache entry
 * if the pool is exhausted.
 *
 * Directory fnodes synthesised here are purely in-RAM views of an on-flash
 * path prefix, so we MUST NOT route them through fno_mkdir: that helper
 * would invoke parent->owner->ops.creat (= flashfs_creat), which allocates
 * a flash page and commits a zero-byte file header. Doing so on every
 * lookup polluted the backing image with phantom entries named after each
 * subdirectory ("collections", "html", ...). We also skip mkdir_links —
 * lazy dirs don't need "." / ".." fnodes consuming pool slots.
 */
static struct fnode *flashfs_create_cached(struct fnode *dir, const char *name,
        int is_dir)
{
    struct fnode *fno;
    int retry;
    for (retry = 0; retry < 2; retry++) {
        fno = fno_create_raw(&mod_flashfs, name, dir);
        if (fno) {
            if (is_dir)
                fno->flags |= FL_DIR;
            return fno;
        }
        if (!flashfs_evict_one())
            return NULL;
    }
    return NULL;
}

/*
 * Attach an existing unclaimed fnode (created by fno_create_raw) to a
 * newly-allocated flashfs_fnode describing the on-flash file at `page`.
 */
static int flashfs_attach_file(struct fnode *fno,
        const struct jedec_spi_flash *jedec, uint32_t page, uint32_t fsize,
        int fname_total_len)
{
    struct flashfs_fnode *mfs = pool_alloc(&flashfs_fnode_pool);
    if (!mfs)
        return -ENOMEM;
    mfs->fno = fno;
    mfs->startpage = page;
    mfs->on_flash_fname_len = fname_total_len;
#ifdef CONFIG_JEDEC_SPI_FLASH
    mfs->jedec = jedec;
#else
    (void)jedec;
#endif
    fno->priv = mfs;
    fno->size = fsize;
#ifdef CONFIG_JEDEC_SPI_FLASH
    fno->flags = jedec ? (FL_RDONLY | FL_EXEC) : (FL_RDWR | FL_EXEC);
#else
    fno->flags = FL_RDWR | FL_EXEC;
#endif
    return 0;
}

/*
 * Lazy lookup: scan the on-flash bitmap + headers for a file whose on-flash
 * path matches "<dir_relpath>/<name>" (or just "<name>" if dir is the mount
 * point). If no exact file match but some file lives under that name as a
 * directory prefix, synthesise a directory fnode. Returns NULL if the name
 * is not present in the mount.
 */
static struct fnode *flashfs_lookup(struct fnode *dir, const char *name)
{
    const struct jedec_spi_flash *jedec;
    char dir_path[MAX_FNAME + 1];
    char target[MAX_FNAME + 1];
    char fname[MAX_FNAME + 1];
    struct flashfs_file_hdr hdr;
    uint32_t max_pages;
    uint32_t page;
    int dlen;
    int tlen;
    struct fnode *fno;

    if (!dir || !name)
        return NULL;

    dlen = flashfs_dir_relpath(dir, dir_path, sizeof(dir_path));
    if (dlen < 0)
        return NULL;
    {
        int nlen = strlen(name);
        if (dlen == 0) {
            if (nlen >= (int)sizeof(target))
                return NULL;
            memcpy(target, name, nlen + 1);
            tlen = nlen;
        } else {
            if (dlen + 1 + nlen >= (int)sizeof(target))
                return NULL;
            memcpy(target, dir_path, dlen);
            target[dlen] = '/';
            memcpy(target + dlen + 1, name, nlen + 1);
            tlen = dlen + 1 + nlen;
        }
    }

    jedec = flashfs_jedec_for(dir);
    max_pages = flashfs_usable_pages(jedec);

    for (page = 0; page < max_pages; page++) {
        int used = flashfs_page_used(jedec, page);
        int rc;
        int cmp;
        if (used < 0)
            return NULL;
        if (!used)
            continue;
        rc = flashfs_read_entry(jedec, page, &hdr, fname);
        if (rc <= 0)
            continue;

        /* Sorted layout lets us short-circuit once we're past the target
         * window. "target" < "target/..." bytewise (NUL < '/'), so the
         * first entry with fname[0..tlen-1] > target terminates the scan. */
        cmp = strncmp(fname, target, tlen);
        if (cmp > 0)
            break;
        if (cmp == 0 && fname[tlen] != '\0' && fname[tlen] != '/')
            break;
        if (cmp < 0)
            continue;

        /* Exact name match — could be a FILE entry or a DIR entry. */
        if (fname[tlen] == '\0') {
            if (rc == FLASHFS_ENTRY_DIR) {
                fno = flashfs_create_cached(dir, name, 1);
                if (!fno)
                    return NULL;
#ifdef CONFIG_JEDEC_SPI_FLASH
                if (jedec)
                    fno->flags |= FL_RDONLY;
#endif
                return fno;
            }
            fno = flashfs_create_cached(dir, name, 0);
            if (!fno)
                return NULL;
            if (flashfs_attach_file(fno, jedec, page, hdr.fsize,
                        (int)strlen(fname)) < 0) {
                fno_detach(fno);
                return NULL;
            }
            return fno;
        }

        /* Fallback prefix match: an older image that pre-dates explicit
         * DIR entries can still be walked by inferring directories from
         * the file paths underneath them. */
        if (fname[tlen] == '/') {
            fno = flashfs_create_cached(dir, name, 1);
            if (!fno)
                return NULL;
#ifdef CONFIG_JEDEC_SPI_FLASH
            if (jedec)
                fno->flags |= FL_RDONLY;
#endif
            return fno;
        }
    }
    return NULL;
}

/*
 * Scratch state for readdir's implicit-directory fallback. Only used when
 * we encounter entries from an older image that has no explicit DIR
 * entries and we have to synthesise directory names by looking at path
 * prefixes. Kept here so the single-element dedup survives between
 * readdir calls but resets at the start of each scan.
 *
 * Caveat: non-reentrant. Two concurrent readdir scans on different dir
 * fnodes within flashfs would clobber each other. Shell-style single-
 * enumeration use is fine.
 */
static struct fnode *flashfs_readdir_dir;
static char flashfs_readdir_last[CONFIG_MAX_FNAME];
static int flashfs_readdir_last_len;

/*
 * Lazy readdir: enumerate entries living directly under `dir`. With
 * explicit DIR entries on-flash, each child (file or subdir) appears as
 * exactly one entry whose name can be emitted directly — no dedup needed.
 * The fallback path (entries whose name is "dir_path/child/..." but no
 * DIR entry for "dir_path/child") handles older images that pre-date the
 * DIR-entry encoding; it uses a single-element dedup guard.
 */
static int flashfs_readdir(struct fnode *dir, uint32_t *cursor,
        struct dirent *ep)
{
    const struct jedec_spi_flash *jedec;
    char dir_path[MAX_FNAME + 1];
    char fname[MAX_FNAME + 1];
    struct flashfs_file_hdr hdr;
    uint32_t max_pages;
    int dlen;

    if (!dir || !cursor || !ep)
        return -1;

    dlen = flashfs_dir_relpath(dir, dir_path, sizeof(dir_path));
    if (dlen < 0)
        return -1;

    /* Reset fallback dedup state at the start of a fresh scan. */
    if (*cursor == 0 || flashfs_readdir_dir != dir) {
        flashfs_readdir_dir = dir;
        flashfs_readdir_last_len = 0;
    }

    jedec = flashfs_jedec_for(dir);
    max_pages = flashfs_usable_pages(jedec);

    while (*cursor < max_pages) {
        uint32_t page = *cursor;
        const char *sub;
        int sub_len;
        int used = flashfs_page_used(jedec, page);
        int rc;
        int has_more_slashes;

        (*cursor)++;

        if (used <= 0)
            continue;
        rc = flashfs_read_entry(jedec, page, &hdr, fname);
        if (rc <= 0)
            continue;

        /* Is this entry under our directory? */
        if (dlen == 0) {
            sub = fname;
        } else {
            if (strncmp(fname, dir_path, dlen) != 0 || fname[dlen] != '/')
                continue;
            sub = fname + dlen + 1;
        }

        /* First '/'-separated component of sub — what we'd emit. */
        sub_len = 0;
        while (sub[sub_len] && sub[sub_len] != '/' &&
                sub_len < CONFIG_MAX_FNAME - 1)
            sub_len++;
        if (sub_len == 0)
            continue;
        has_more_slashes = (sub[sub_len] == '/');

        /* Unified dedup: skip the candidate if its first component matches
         * the previously-emitted one. Covers both
         *   - nested paths re-synthesising the same dir name, and
         *   - an explicit DIR entry followed by its own child files
         *     (bytewise sort puts "foo" before "foo/...").
         */
        if (sub_len == flashfs_readdir_last_len &&
                memcmp(sub, flashfs_readdir_last, sub_len) == 0)
            continue;
        memcpy(flashfs_readdir_last, sub, sub_len);
        flashfs_readdir_last_len = sub_len;
        (void)has_more_slashes;

        ep->d_ino = 0;
        if (sub_len >= (int)sizeof(ep->d_name))
            sub_len = sizeof(ep->d_name) - 1;
        memcpy(ep->d_name, sub, sub_len);
        ep->d_name[sub_len] = '\0';
        return 0;
    }

    /* Scan done — drop stale state so the next enumeration starts fresh. */
    if (flashfs_readdir_dir == dir) {
        flashfs_readdir_dir = NULL;
        flashfs_readdir_last_len = 0;
    }
    return -1;
}

static int flashfs_mount_info(struct fnode *fno, char *buf, int len)
{
    const char *desc = "NVM (internal flash): small config and log files";
#ifdef CONFIG_JEDEC_SPI_FLASH
    if (fno && fno->priv)
        desc = "SPI NOR flash (external): mounted via /dev/spiflash0";
#endif
    if (len < 0)
        return -1;
    strncpy(buf,desc,len);
    if (len > strlen(desc)) {
        len = strlen(desc);
        buf[len++] = 0;
    } else
        buf[len - 1] = 0;
    return len;
}

static int flashfs_mount_stat(struct fnode *mnt, struct fs_usage *out)
{
    uint8_t bmp[FLASH_PAGE_SIZE];
    int page;
    uint32_t used = 0;
#ifdef CONFIG_JEDEC_SPI_FLASH
    const struct jedec_spi_flash *jedec = mnt ? (const struct jedec_spi_flash *)mnt->priv : NULL;
#else
    const struct jedec_spi_flash *jedec = NULL;
#endif
    uint32_t total_pages = flashfs_effective_pages(jedec);
    uint32_t bmp_count = flashfs_bmp_page_count(jedec);
    uint32_t usable = total_pages - bmp_count;

    if (!out)
        return -1;

    for (page = 0; page < (int)usable; page++) {
        /* Reload bitmap cache at each bitmap page boundary */
        if ((page % BITS_PER_BMP_PAGE) == 0) {
            uint32_t bp = bmp_flash_page_for(jedec, page);
            if (flashfs_read_page(jedec, bp, bmp) < 0)
                return -1;
        }
        {
            uint32_t byte_off = (page % BITS_PER_BMP_PAGE) / 8;
            /* Inverted bitmap: bit=1 means free, bit=0 means used */
            if (!(bmp[byte_off] & (1 << (page & 7))))
                used++;
        }
    }

    out->block_size = FLASH_PAGE_SIZE;
    out->total_blocks = usable;
    out->free_blocks = usable - used;
    out->avail_blocks = out->free_blocks;
    out->files = flashfs_fnode_pool.used;
    out->free_files = pool_available(&flashfs_fnode_pool);
    out->fstype = "flashfs";
    return 0;
}

void flashfs_init(void)
{
    pool_init(&flashfs_fnode_pool);
    mod_flashfs.family = FAMILY_FILE;
    strcpy(mod_flashfs.name,"flashfs");
    flashfs_lock = mutex_init();

#if defined(TARGET_stm32h563) && SECRETS_BASE
    {
        const struct xipfs_fat *fat = (const struct xipfs_fat *)(uintptr_t)CONFIG_APPS_ORIGIN;
        if (fat->fs_magic == XIPFS_MAGIC && fat->fs_size > 0) {
            uint32_t xipfs_end = CONFIG_APPS_ORIGIN + fat->fs_size;
            part_map_base = (xipfs_end + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
            part_size = SECRETS_BASE - part_map_base;
        } else {
            part_map_base = PART_MAP_BASE_DEFAULT;
            part_size = PART_SIZE_DEFAULT;
        }
    }
#else
    part_map_base = PART_MAP_BASE_DEFAULT;
    part_size = PART_SIZE_DEFAULT;
#endif

    mod_flashfs.mount = flashfs_mount;
    mod_flashfs.mount_info = flashfs_mount_info;
    mod_flashfs.mount_stat = flashfs_mount_stat;

    mod_flashfs.ops.read = flashfs_read;
    mod_flashfs.ops.poll = flashfs_poll;
    mod_flashfs.ops.write = flashfs_write;
    mod_flashfs.ops.seek = flashfs_seek;
    mod_flashfs.ops.creat = flashfs_creat;
    mod_flashfs.ops.unlink = flashfs_unlink;
    mod_flashfs.ops.close = flashfs_close;
    mod_flashfs.ops.truncate = flashfs_truncate;
    mod_flashfs.ops.lookup = flashfs_lookup;
    mod_flashfs.ops.readdir = flashfs_readdir;
    register_module(&mod_flashfs);
}

/*
 * flashfs_set_backend - switch active flashfs backend.
 * When CONFIG_JEDEC_SPI_FLASH is enabled, this function can be called
 * at runtime to switch from internal flash to an external SPI NOR chip.
 */
#ifdef CONFIG_JEDEC_SPI_FLASH
int flashfs_register_jedec(struct jedec_spi_flash *jedec_flash)
{
    if (!jedec_flash)
        return -EINVAL;
    if (!jedec_flash->probed)
        return -ENODEV;
    registered_jedec = jedec_flash;
    kprintf("flashfs: external SPI NOR available as %s (%lu pages)\n",
            jedec_flash->dev_path ? jedec_flash->dev_path : "/dev/spiflash0",
            (unsigned long)jedec_flash->page_count);
    return 0;
}
#endif

#endif /* CONFIG_FLASHFS */
