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

struct __attribute__((packed)) flashfs_file_hdr {
    uint16_t fname_len;
    uint16_t fsize;
};

struct flashfs_fnode {
    struct fnode *fno;
    uint32_t startpage;
};

/* Bitmap in last flash page. Inverted logic (starts at 0xFFFFFFFF when formatted */
static uint8_t *fs_bmp = (uint8_t *)PART_MAP_BASE + PART_SIZE - FLASH_PAGE_SIZE;

static int fs_bmp_clear(uint32_t page)
{
    static uint8_t cache_bmp[FLASH_PAGE_SIZE];

    memcpy(cache_bmp, fs_bmp, FLASH_PAGE_SIZE); 
    cache_bmp[page / 8] |= 1 << (page & 7);
    if (secure_flash_write_page((PART_MAX_PAGES - 1) * FLASH_PAGE_SIZE,
            cache_bmp) != 0)
        return -EIO;
    return 0;
}

static int fs_bmp_set(uint32_t page)
{
    static uint8_t cache_bmp[FLASH_PAGE_SIZE];
    memcpy(cache_bmp, fs_bmp, FLASH_PAGE_SIZE); 
    cache_bmp[page / 8] &= ~(1 << (page & 7));
    if (secure_flash_write_page((PART_MAX_PAGES - 1) * FLASH_PAGE_SIZE,
            cache_bmp) != 0)
        return -EIO;
    return 0;
}

static int fs_bmp_test(uint32_t page)
{
    return !(fs_bmp[page / 8] & (1 << (page & 7)));
}

static int fs_bmp_find_free(int pages)
{
    int i;
    int sz = 0;
    int first = -1;
    if (pages <= 0)
        return -1;
    for (i = 0; i < PART_MAX_PAGES; i++) {
        if (!fs_bmp_test(i)){
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
        fs_bmp_clear(page);
        return NULL;
    }
    if (*((char *)PART_MAP_BASE + page * FLASH_PAGE_SIZE +
            sizeof(struct flashfs_file_hdr) + hdr->fname_len) != 0) {
        fs_bmp_clear(page);
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

    memcpy(page_cache, (void *)PART_MAP_BASE + mfno->startpage * FLASH_PAGE_SIZE,
            FLASH_PAGE_SIZE);

    hdr = (struct flashfs_file_hdr *)page_cache;
    hdr->fsize = fno->size;
    hdr->fname_len = strlen(fno->fname);
    strncpy(page_cache + sizeof(struct flashfs_file_hdr), fno->fname, 128);
    if (secure_flash_write_page(mfno->startpage * FLASH_PAGE_SIZE, page_cache) != 0)
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
        memcpy(page_cache, (void *)PART_MAP_BASE +
                mfno->startpage * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE);
        if (secure_flash_write_page((newpage + i) * FLASH_PAGE_SIZE, page_cache) != 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
    } 
    memset(page_cache, 0xFF, FLASH_PAGE_SIZE);
    for (i = old_page_count; i < new_page_count; i++)
    {
        if (secure_flash_write_page((newpage + i) * FLASH_PAGE_SIZE, page_cache) != 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
    }
    for (i = 0; i < new_page_count; i++)  {
        if (fs_bmp_set(newpage + i) != 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
    }
    for (i = 0; i < old_page_count; i++) {
        if (fs_bmp_clear(mfno->startpage + i) != 0) {
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
            memcpy(buf + take, get_page_content(mfno->startpage) + off,
                    size_in_page);
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
            content = get_page_content(mfno->startpage + page_count);
            memcpy(buf + take,  content + page_off, size_in_page);
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
                if (fs_bmp_test(mfno->startpage + old_page_count + i)) {
                    int new_loc = fs_bmp_find_free(new_page_count);
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
                    if (fs_bmp_set(mfno->startpage + old_page_count + i) != 0) {
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
            memcpy(page_cache, get_page_content(mfno->startpage), FLASH_PAGE_SIZE);
            page_off = off + sizeof(struct flashfs_file_hdr) + strlen(fno->fname) + 1;
            size_in_page = FLASH_PAGE_SIZE - (sizeof(struct flashfs_file_hdr) +
                    strlen(fno->fname) + 1 + off);
            if (size_in_page > len - written)
                size_in_page = len - written;
            memcpy(page_cache + page_off, buf + written, size_in_page);
            written += size_in_page;
            off += size_in_page;
            ret = secure_flash_write_page(mfno->startpage * FLASH_PAGE_SIZE, page_cache);
            if (ret != 0) {
                mutex_unlock(flashfs_lock);
                return ret;
            }
        } else {
            int current_page_n = get_page_count_on_flash(fno->fname, off) - 1;
            int page_count_verify = 0;
            uint8_t *content;
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
            memcpy(page_cache, get_page_content(mfno->startpage + current_page_n),
                    FLASH_PAGE_SIZE);
            size_in_page = FLASH_PAGE_SIZE - page_off;
            content = get_page_content(mfno->startpage + current_page_n);
            written += size_in_page;
            off += size_in_page;
            ret = secure_flash_write_page((mfno->startpage + current_page_n) * FLASH_PAGE_SIZE, page_cache);
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
                if (fs_bmp_test(mfno->startpage + old_page_count + i)) {
                    int new_loc = fs_bmp_find_free(new_page_count);
                    if (new_loc > 0)
                        return relocate_file(fno, new_loc, old_page_count, new_page_count);
                    else
                        return -ENOSPC;
                } else {
                    if (fs_bmp_set(mfno->startpage + old_page_count + i) != 0)
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
    int page_count = get_page_count_on_flash(fno->fname, fno->size);
    int first_page = fs_bmp_find_free(page_count);
    int page;
    if (first_page < 0)
        return -ENOSPC;

    mutex_lock(flashfs_lock);
    page = first_page;
    while (page < first_page + page_count) {
        if (fs_bmp_set(page++) != 0) {
            mutex_unlock(flashfs_lock);
            return -EIO;
        }
    }

    mfs = kalloc(sizeof(struct flashfs_fnode));
    if (mfs) {
        mfs->fno = fno;
        mfs->startpage = first_page;
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
        if (fs_bmp_clear(page++) != 0)
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
            if (fs_bmp_clear(mfno->startpage + (--old_page_count)) != 0)
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

    /* Source must be NULL */
    if (source)
        return -1;

    /* Target must be a valid dir */
    if (!tgt)
        return -1;

    tgt_dir = fno_search(tgt);

    if (!tgt_dir || ((tgt_dir->flags & FL_DIR) == 0)) {
        /* Not a valid mountpoint. */
        return -1;
    }
    tgt_dir->owner = &mod_flashfs;
    for (page = 0; page < PART_MAX_PAGES; page++) {
        if (fs_bmp_test(page)) {
            char *fname = NULL;
            uint32_t fsz = 0;
            hdr = get_page_header(page);
            if (hdr) {
                fname = get_page_filename(page);
                if (fname) {
                    fpriv = kalloc(sizeof(struct flashfs_fnode));
                    if (!fpriv)
                        return -ENOMEM;
                    fno = fno_create_raw(&mod_flashfs, fname, tgt_dir);
                    if (!fno) {
                        kfree(fpriv);
                        return -ENOMEM;
                    }
                    fno->priv = fpriv;
                    fpriv->fno = fno;
                    fpriv->startpage = page;
                    fno->size = get_content_size(page); /* Size of content */
                    fno->flags = FL_RDWR | FL_EXEC;
                }
            }
        }
    }
    return 0;
}

static int flashfs_mount_info(struct fnode *fno, char *buf, int len)
{
    const char desc[] = "NVM (internal flash): small config and log files";
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

void flashfs_init(void)
{
    mod_flashfs.family = FAMILY_FILE;
    strcpy(mod_flashfs.name,"flashfs");
    flashfs_lock = mutex_init();

    mod_flashfs.mount = flashfs_mount;
    mod_flashfs.mount_info = flashfs_mount_info;

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

#endif /* CONFIG_FLASHFS */
