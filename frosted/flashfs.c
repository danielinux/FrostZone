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
#define PART_SIZE 0x10000
#define PART_MAX_PAGES (PART_SIZE / FLASH_PAGE_SIZE)

#if defined(TARGET_rp2350)
#define PART_MAP_BASE (0x101F0000U)
#elif defined(TARGET_stm32h563)
#define PART_MAP_BASE (0x081F0000U)
#else
#error "FlashFS partition is not defined for this target"
#endif
#define MAX_FNAME 128

int secure_flash_write_page(uint32_t off, uint8_t *page);

#ifdef CONFIG_JEDEC_SPI_FLASH
#include "jedec_spi_flash.h"
static struct jedec_spi_flash *registered_jedec;
#endif

struct __attribute__((packed)) flashfs_file_hdr {
    uint16_t fname_len;
    uint16_t fsize;
};

#define CONFIG_MAX_FLASHFS_FNODES 16

struct flashfs_fnode {
    struct fnode *fno;
    uint32_t startpage;
#ifdef CONFIG_JEDEC_SPI_FLASH
    const struct jedec_spi_flash *jedec;
#endif
};

POOL_DEFINE(flashfs_fnode_pool, struct flashfs_fnode, CONFIG_MAX_FLASHFS_FNODES);

/* Bitmap in last flash page. Inverted logic (starts at 0xFFFFFFFF when formatted */
static uint8_t *fs_bmp = (uint8_t *)PART_MAP_BASE + PART_SIZE - FLASH_PAGE_SIZE;

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
    memcpy(buf, (void *)PART_MAP_BASE + page * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE);
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

    if (flashfs_read_page(jedec, PART_MAX_PAGES - 1, cache_bmp) < 0)
        return -EIO;
    cache_bmp[page / 8] |= 1 << (page & 7);
    if (flashfs_write_page(jedec, PART_MAX_PAGES - 1, cache_bmp) != 0)
        return -EIO;
    return 0;
}

static int fs_bmp_set(const struct jedec_spi_flash *jedec, uint32_t page)
{
    static uint8_t cache_bmp[FLASH_PAGE_SIZE];
    if (flashfs_read_page(jedec, PART_MAX_PAGES - 1, cache_bmp) < 0)
        return -EIO;
    cache_bmp[page / 8] &= ~(1 << (page & 7));
    if (flashfs_write_page(jedec, PART_MAX_PAGES - 1, cache_bmp) != 0)
        return -EIO;
    return 0;
}

static int fs_bmp_test(const struct jedec_spi_flash *jedec, uint32_t page)
{
    uint8_t cache_bmp[FLASH_PAGE_SIZE];

    if (flashfs_read_page(jedec, PART_MAX_PAGES - 1, cache_bmp) < 0)
        return 0;
    return !(cache_bmp[page / 8] & (1 << (page & 7)));
}

static int fs_bmp_find_free(const struct jedec_spi_flash *jedec, int pages)
{
    int i;
    int sz = 0;
    int first = -1;
    if (pages <= 0)
        return -1;
    for (i = 0; i < PART_MAX_PAGES; i++) {
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
    int r;
    uint16_t first_page_size = FLASH_PAGE_SIZE - (sizeof(struct flashfs_file_hdr) + strlen(filename) + 1);
    uint16_t nxt_page_size;
    if (sz <= first_page_size)
        return 1;

    r = 1;
    while (sz > 0) {
        nxt_page_size = FLASH_PAGE_SIZE -
            (sizeof(struct flashfs_file_hdr) + strlen(filename) + 1);
        if (sz < nxt_page_size)
            nxt_page_size = sz;
        sz -= nxt_page_size;
        r++;
    }
    return r;
}

struct flashfs_file_hdr *get_page_header(uint16_t page)
{
    struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)(PART_MAP_BASE +
            page * FLASH_PAGE_SIZE);
    if ((hdr->fname_len == 0xFFFF) || (hdr->fname_len == 0x0000)
            || (hdr->fname_len == F_PREV_PAGE))
        return NULL;
    return hdr;
}

char *get_page_filename(uint16_t page)
{
    struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)(PART_MAP_BASE +
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
    if (*((char *)PART_MAP_BASE + page * FLASH_PAGE_SIZE +
            sizeof(struct flashfs_file_hdr) + hdr->fname_len) != 0) {
        fs_bmp_clear(NULL, page);
        return NULL;
    }
    return (char *)hdr + sizeof(struct flashfs_file_hdr);
}

int get_content_size(uint16_t page)
{
    struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)(PART_MAP_BASE +
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
    struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)(PART_MAP_BASE +
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
    struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)(PART_MAP_BASE +
            page * FLASH_PAGE_SIZE);
    if ((hdr->fname_len == 0xFFFF) || (hdr->fname_len == 0x0000)
            || (hdr->fname_len == F_PREV_PAGE))
        return (uint8_t *)PART_MAP_BASE + page * FLASH_PAGE_SIZE +
                sizeof(struct flashfs_file_hdr);
    else
        return (uint8_t *)PART_MAP_BASE + page * FLASH_PAGE_SIZE +
                sizeof(struct flashfs_file_hdr) + hdr->fname_len;
} 

static int flash_commit_file_info(struct fnode *fno)
{
    struct flashfs_fnode *mfno;
    struct flashfs_file_hdr *hdr;
    uint8_t page_cache[FLASH_PAGE_SIZE];

    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -ENOENT;

    if (flashfs_read_page(mfno->jedec, mfno->startpage, page_cache) < 0)
        return -EIO;

    hdr = (struct flashfs_file_hdr *)page_cache;
    hdr->fsize = fno->size;
    hdr->fname_len = strlen(fno->fname);
    memcpy(page_cache + sizeof(struct flashfs_file_hdr), fno->fname,
            hdr->fname_len + 1);
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
    int i;
    if (len <= 0)
        return len;

    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -ENOENT;

    off = task_fd_get_off(fno);

    if (fno->size <= off)
        return 0;

    if (len > (fno->size - off))
        len = fno->size - off;

    while (take < len) {
        if (off < FLASH_PAGE_SIZE - (sizeof(struct flashfs_file_hdr) + strlen(fno->fname) + 1)) {
            page_off = off + sizeof(struct flashfs_file_hdr) + strlen(fno->fname) + 1;
            size_in_page = FLASH_PAGE_SIZE - (sizeof(struct flashfs_file_hdr) + strlen(fno->fname) + 1 + off);
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
            int page_count = get_page_count_on_flash(fno->fname, off) - 1;
            if (page_count < 0)
                return -EIO;
            page_off = off - FLASH_PAGE_SIZE;
            page_off += sizeof(struct flashfs_file_hdr) + strlen(fno->fname) + 1;
            while (page_off > FLASH_PAGE_SIZE) {
                page_off -= FLASH_PAGE_SIZE;
                page_off += sizeof(struct flashfs_file_hdr);
            }
            size_in_page = FLASH_PAGE_SIZE - page_off;
            if (mfno->jedec) {
                uint32_t addr = (mfno->startpage + page_count) * FLASH_PAGE_SIZE + page_off;
                if (jedec_spi_flash_read(mfno->jedec, addr, buf + take, size_in_page) < 0)
                    return take > 0 ? take : -EIO;
            } else {
                content = get_page_content(mfno->startpage + page_count);
                memcpy(buf + take,  content + page_off, size_in_page);
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
    int i;
    static uint8_t page_cache[FLASH_PAGE_SIZE];

    if (len <= 0)
        return len;

    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -ENOENT;

    off = task_fd_get_off(fno);

    if (fno->size < (off + len)) {
        int new_page_count, old_page_count;
        old_page_count = get_page_count_on_flash(fno->fname, fno->size);
        new_page_count = get_page_count_on_flash(fno->fname, off + len);
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
        struct flashfs_file_hdr *hdr = (struct flashfs_file_hdr *)page_cache;
        if (off < FLASH_PAGE_SIZE - (sizeof(struct flashfs_file_hdr) +
                    strlen(fno->fname)) + 1) {
            if (flashfs_read_page(mfno->jedec, mfno->startpage, page_cache) < 0) {
                mutex_unlock(flashfs_lock);
                return -EIO;
            }
            page_off = off + sizeof(struct flashfs_file_hdr) + strlen(fno->fname) + 1;
            size_in_page = FLASH_PAGE_SIZE - (sizeof(struct flashfs_file_hdr) +
                    strlen(fno->fname) + 1 + off);
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
            int current_page_n = get_page_count_on_flash(fno->fname, off) - 1;
            int page_count_verify = 0;
            if (current_page_n < 0)
                return -EIO;
            /* First page */
            page_off = off - FLASH_PAGE_SIZE;
            page_off += sizeof(struct flashfs_file_hdr) + strlen(fno->fname) + 1;
            page_count_verify++;

            /* Subsequent pages */
            while (page_off > FLASH_PAGE_SIZE) {
                page_off -= FLASH_PAGE_SIZE;
                page_off += sizeof(struct flashfs_file_hdr);
                page_count_verify++;
            }
            if ((page_count_verify - 1) != current_page_n) {
                return -EIO;
            }
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
    int page_count = get_page_count_on_flash(fno->fname, fno->size);
    int first_page;
    int page;
    if (fno && fno->parent)
        jedec = (const struct jedec_spi_flash *)fno->parent->priv;
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
    if (!fno)
        return -ENOENT;
    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (!mfno)
        return -EPERM;
    page_count = get_page_count_on_flash(fno->fname, fno->size);
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
    struct flashfs_file_hdr *hdr;
    int old_page_count, new_page_count;
    if (!fno)
        return -ENOENT;
    mfno = FNO_MOD_PRIV(fno, &mod_flashfs);
    if (mfno) {
        if (fno->size <= newsize) {
            /* Nothing to do here. */
            return 0;
        }
        old_page_count = get_page_count_on_flash(fno->fname, fno->size);
        new_page_count = get_page_count_on_flash(fno->fname, newsize);
        while (old_page_count > new_page_count) {
            if (fs_bmp_clear(mfno->jedec, mfno->startpage + (--old_page_count)) != 0)
                return -EIO;
        }
        fno->size = newsize;
        return 0;
    }
    return -EFAULT;
}

static int flashfs_mount(char *source, char *tgt, uint32_t flags, void *arg)
{
    struct fnode *tgt_dir = NULL, *fno = NULL;
    struct flashfs_fnode *fpriv;
    int page;
    struct flashfs_file_hdr *hdr;
#ifdef CONFIG_JEDEC_SPI_FLASH
    const struct jedec_spi_flash *jedec = NULL;
    uint8_t page_cache[FLASH_PAGE_SIZE];
    uint8_t bitmap_cache[FLASH_PAGE_SIZE];
#endif

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

    /* Target must be a valid dir */
    if (!tgt)
        return -EINVAL;

    tgt_dir = fno_search(tgt);

    if (!tgt_dir || ((tgt_dir->flags & FL_DIR) == 0)) {
        /* Not a valid mountpoint. */
        return -ENOTDIR;
    }
    tgt_dir->owner = &mod_flashfs;
#ifdef CONFIG_JEDEC_SPI_FLASH
    tgt_dir->priv = (void *)jedec;
    if (jedec) {
        kprintf("flashfs: mounting JEDEC source %s on %s\n",
                source ? source : "/dev/spiflash0", tgt);
        if (flashfs_read_page(jedec, PART_MAX_PAGES - 1, bitmap_cache) < 0)
            return -EIO;
    }
#else
    tgt_dir->priv = NULL;
#endif
    for (page = 0; page < PART_MAX_PAGES; page++) {
        int page_used;

#ifdef CONFIG_JEDEC_SPI_FLASH
        if (jedec) {
            if ((page & 0x1F) == 0)
                kprintf("flashfs: scan page %d/%d\n", page, PART_MAX_PAGES);
            page_used = !(bitmap_cache[page / 8] & (1 << (page & 7)));
        } else
#endif
        {
            page_used = fs_bmp_test(NULL, page);
        }

        if (page_used) {
            char *fname = NULL;
            uint32_t fsz = 0;
#ifdef CONFIG_JEDEC_SPI_FLASH
            if (jedec) {
                if (flashfs_read_jedec_page(jedec, page, page_cache) < 0)
                    return -EIO;
                hdr = (struct flashfs_file_hdr *)page_cache;
                if ((hdr->fname_len == 0xFFFF) || (hdr->fname_len == 0x0000)
                        || (hdr->fname_len == F_PREV_PAGE))
                    hdr = NULL;
                if (hdr && hdr->fname_len <= MAX_FNAME &&
                        page_cache[sizeof(struct flashfs_file_hdr) + hdr->fname_len] == 0) {
                    fname = (char *)page_cache + sizeof(struct flashfs_file_hdr);
                    fsz = hdr->fsize;
                }
            } else
#endif
            {
                hdr = get_page_header(page);
                if (hdr) {
                    fname = get_page_filename(page);
                    if (fname)
                        fsz = get_content_size(page);
                }
            }
            if (hdr) {
                if (fname) {
                    fpriv = pool_alloc(&flashfs_fnode_pool);
                    if (!fpriv)
                        return -ENOMEM;
                    fno = fno_create_raw(&mod_flashfs, fname, tgt_dir);
                    if (!fno) {
                        pool_free(&flashfs_fnode_pool, fpriv);
                        return -ENOMEM;
                    }
                    fno->priv = fpriv;
                    fpriv->fno = fno;
                    fpriv->startpage = page;
#ifdef CONFIG_JEDEC_SPI_FLASH
                    fpriv->jedec = jedec;
#endif
                    fno->size = fsz; /* Size of content */
#ifdef CONFIG_JEDEC_SPI_FLASH
                    fno->flags = jedec ? (FL_RDONLY | FL_EXEC) : (FL_RDWR | FL_EXEC);
#else
                    fno->flags = FL_RDWR | FL_EXEC;
#endif
                }
            }
        }
    }
    return 0;
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
    uint32_t usable_pages = PART_MAX_PAGES - 1; /* last page is bitmap */
#ifdef CONFIG_JEDEC_SPI_FLASH
    const struct jedec_spi_flash *jedec = mnt ? (const struct jedec_spi_flash *)mnt->priv : NULL;
#else
    const struct jedec_spi_flash *jedec = NULL;
#endif

    if (!out)
        return -1;

    if (flashfs_read_page(jedec, PART_MAX_PAGES - 1, bmp) < 0)
        return -1;

    for (page = 0; page < (int)usable_pages; page++) {
        /* Inverted bitmap: bit=1 means free, bit=0 means used */
        if (!(bmp[page / 8] & (1 << (page & 7))))
            used++;
    }

    out->block_size = FLASH_PAGE_SIZE;
    out->total_blocks = usable_pages;
    out->free_blocks = usable_pages - used;
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
