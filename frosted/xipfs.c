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
 *      Authors:
 *
 */

#include "frosted.h"
#include <string.h>
#include "bflt.h"
#include "kprintf.h"
#include "sys/fs/xipfs.h"
#define GDB_PATH "frosted-userland/gdb/"

static struct module mod_xipfs;
volatile int phase0_bflt_trace_tag;

/*
 * Lazy xipfs: mount is O(1).  The blob pointer is stored in the
 * mount-point fnode's ->priv.  Individual files are looked up by
 * scanning the FAT on demand.  File fnodes store the payload pointer
 * directly in ->priv (no intermediate xipfs_fnode struct).
 */

#define SECTOR_SIZE (512)

#ifdef CONFIG_SHLIB
/* --- Shared library registry --- */
#define MAX_SHLIBS 8

struct loaded_shlib {
    uint8_t  lib_id;
    uint32_t version;
    const uint8_t *flash_base;     /* pointer to bFLT start in flash */
    const uint8_t *text_base;      /* .text start (XIP) */
    uint32_t text_len;
    uint32_t data_len;
    uint32_t bss_len;
    uint32_t export_count;
    const uint32_t *export_table;  /* ordinal→offset array in flash */
};

static struct loaded_shlib shlibs[MAX_SHLIBS];
static const uint8_t *xipfs_blob_ptr; /* cached blob pointer from mount */
#endif

/* Helper: scan FAT for entry at given index.  Returns offset to fhdr, or -1. */
static int xipfs_fat_offset(const uint8_t *blob, int idx)
{
    const struct xipfs_fat *fat = (const struct xipfs_fat *)blob;
    const struct xipfs_fhdr *f;
    int i;
    int offset;

    if (!fat || idx < 0 || idx >= (int)fat->fs_files)
        return -1;

    offset = sizeof(struct xipfs_fat);
    for (i = 0; i < idx; i++) {
        f = (const struct xipfs_fhdr *)(blob + offset);
        if (f->magic == XIPFS_MAGIC
#ifdef CONFIG_SHLIB
                || f->magic == XIPFS_MAGIC_SHLIB
#endif
           )
            offset += f->len + sizeof(struct xipfs_fhdr);
        else
            offset += sizeof(struct xipfs_fhdr);
        while ((offset % 4) != 0)
            offset++;
    }
    return offset;
}

static int xipfs_read(struct fnode *fno, void *buf, unsigned int len)
{
    void *payload;
    uint32_t off = task_fd_get_off(fno);
    if (len <= 0)
        return len;

    payload = FNO_MOD_PRIV(fno, &mod_xipfs);
    if (!payload)
        return -1;

    if (fno->size <= off)
        return -1;

    if (len > (fno->size - off))
        len = fno->size - off;

    memcpy(buf, (char *)payload + off, len);
    off += len;
    task_fd_set_off(fno, off);
    return len;
}

static int xipfs_block_read(struct fnode *fno, void *buf, uint32_t sector, int offset, int count)
{
    uint32_t off = sector * SECTOR_SIZE + offset;
    task_fd_set_off(fno, off);
    if (off > fno->size) {
        task_fd_set_off(fno, 0);
        return -1;
    }
    if (xipfs_read(fno, buf, count) == count)
        return 0;
    return -1;
}

static int xipfs_write(struct fnode *fno, const void *buf, unsigned int len)
{
    return -1; /* Cannot write! */
}

static int xipfs_poll(struct fnode *fno, uint16_t events, uint16_t *revents)
{
    return -1;
}

static int xipfs_seek(struct fnode *fno, int off, int whence)
{
    return -1;
}

static int xipfs_close(struct fnode *fno)
{
    return 0;
}

static int xipfs_creat(struct fnode *fno)
{
    return -1;
}

static int xipfs_exe(struct fnode *fno, void *arg, struct task_exec_info *info)
{
    void *payload = fno->priv;
    void *reloc_text, *reloc_data, *reloc_bss;
    size_t stack_size;
    void *init = NULL;
    uint32_t text_size, data_size;
    void *got_loc;
    memset(info, 0, sizeof(struct task_exec_info));

    if (!payload)
        return -1;

    if (strcmp(fno->fname, "fresh") == 0)
        phase0_bflt_trace_tag = 1;
    else if (strcmp(fno->fname, "phase0_memfs") == 0)
        phase0_bflt_trace_tag = 2;
    else
        phase0_bflt_trace_tag = 0;

    /* payload is the bFLT load address */
#ifdef CONFIG_SHLIB
    info->extra_mmap_count = 0;
    if (bflt_load((uint8_t *)payload, &reloc_text, &reloc_data, &reloc_bss,
                  &init, &stack_size, (uint32_t *)&got_loc, &text_size, &data_size,
                  info->extra_mmap, &info->extra_mmap_count))
#else
    if (bflt_load((uint8_t *)payload, &reloc_text, &reloc_data, &reloc_bss,
                  &init, &stack_size, (uint32_t *)&got_loc, &text_size, &data_size,
                  NULL, NULL))
#endif
    {
        phase0_bflt_trace_tag = 0;
        kprintf("xipfs: bFLT loading failed.\n");
        return -1;
    }

    phase0_bflt_trace_tag = 0;

    kprintf("xipfs: GDB: add-symbol-file %s%s.gdb 0x%p -s .data 0x%p -s .bss 0x%p\n",
            GDB_PATH, fno->fname, reloc_text, reloc_data, reloc_bss);

    info->init = init;
    info->flags |= EXEC_TYPE_BFLT;
    info->mmap_base = reloc_data;
    info->got_loc = got_loc;
    info->text_size = text_size;
    info->data_size = data_size;
    return 0;
}

static int xipfs_unlink(struct fnode *fno)
{
    return -1; /* Cannot unlink */
}

/*
 * Lazy lookup: scan the FAT for a file by name.
 * Creates an fnode on demand from the static pool.
 * For ICELINK entries, creates a symlink to /bin/icebox.
 */
static struct fnode *xipfs_lookup(struct fnode *dir, const char *name)
{
    const uint8_t *blob = (const uint8_t *)dir->priv;
    const struct xipfs_fat *fat;
    const struct xipfs_fhdr *f;
    struct fnode *fno;
    int i, offset;

    if (!blob)
        return NULL;

    fat = (const struct xipfs_fat *)blob;
    offset = sizeof(struct xipfs_fat);

    for (i = 0; i < (int)fat->fs_files; i++) {
        f = (const struct xipfs_fhdr *)(blob + offset);
        if ((f->magic != XIPFS_MAGIC) && (f->magic != XIPFS_MAGIC_ICELINK)
#ifdef CONFIG_SHLIB
                && (f->magic != XIPFS_MAGIC_SHLIB)
#endif
           )
            return NULL;

        if (strcmp(f->name, name) == 0) {
            if (f->magic == XIPFS_MAGIC
#ifdef CONFIG_SHLIB
                    || f->magic == XIPFS_MAGIC_SHLIB
#endif
               ) {
                /* Regular executable or shared library */
                fno = fno_create(&mod_xipfs, name, dir);
                if (!fno)
                    return NULL;
                fno->priv = (void *)f->payload;
                fno->flags |= FL_EXEC;
                fno->size = f->len;
                return fno;
            } else {
                /* ICELINK: create symlink to /bin/icebox */
                fno = fno_create(&mod_xipfs, name, dir);
                if (!fno)
                    return NULL;
                fno->flags |= FL_LINK;
                strncpy(fno->linkname, "/bin/icebox", MAX_FILE - 1);
                fno->linkname[MAX_FILE - 1] = '\0';
                return fno;
            }
        }

        if (f->magic == XIPFS_MAGIC
#ifdef CONFIG_SHLIB
                || f->magic == XIPFS_MAGIC_SHLIB
#endif
           )
            offset += f->len + sizeof(struct xipfs_fhdr);
        else
            offset += sizeof(struct xipfs_fhdr);
        while ((offset % 4) != 0)
            offset++;
    }
    return NULL;
}

/*
 * Lazy readdir: walk the FAT using cursor as the file index.
 * Returns 0 on success, -1 when no more entries.
 */
static int xipfs_readdir(struct fnode *dir, uint32_t *cursor, struct dirent *ep)
{
    const uint8_t *blob = (const uint8_t *)dir->priv;
    const struct xipfs_fat *fat;
    const struct xipfs_fhdr *f;
    int offset;

    if (!blob)
        return -1;

    fat = (const struct xipfs_fat *)blob;
    if (*cursor >= fat->fs_files)
        return -1;

    offset = xipfs_fat_offset(blob, *cursor);
    if (offset < 0)
        return -1;

    f = (const struct xipfs_fhdr *)(blob + offset);
    if ((f->magic != XIPFS_MAGIC) && (f->magic != XIPFS_MAGIC_ICELINK)
#ifdef CONFIG_SHLIB
            && (f->magic != XIPFS_MAGIC_SHLIB)
#endif
       )
        return -1;

    ep->d_ino = 0;
    strncpy(ep->d_name, f->name, sizeof(ep->d_name));
    ep->d_name[sizeof(ep->d_name) - 1] = '\0';
    (*cursor)++;
    return 0;
}

#ifdef CONFIG_SHLIB
/*
 * Scan xipfs for a shared library bFLT with the given lib_id.
 * Parse its header and register it in shlibs[].  Returns the
 * registry entry, or NULL on failure.
 */
static struct loaded_shlib *shlib_register(uint8_t lib_id)
{
    const uint8_t *blob = xipfs_blob_ptr;
    const struct xipfs_fat *fat;
    const struct xipfs_fhdr *f;
    struct flat_hdr hdr;
    int i, offset;
    uint32_t export_off, export_cnt, flags;
    struct loaded_shlib *sl;

    if (!blob)
        return NULL;

    fat = (const struct xipfs_fat *)blob;
    offset = sizeof(struct xipfs_fat);

    for (i = 0; i < (int)fat->fs_files; i++) {
        f = (const struct xipfs_fhdr *)(blob + offset);

        if (f->magic == XIPFS_MAGIC_SHLIB) {
            const uint8_t *payload = ((const uint8_t *)f) + sizeof(struct xipfs_fhdr);
            /* Check if this library's bFLT has the right lib_id */
            memcpy(&hdr, payload, sizeof(struct flat_hdr));
            flags = long_be(hdr.flags);
            if ((flags & FLAT_FLAG_SHLIB) &&
                (long_be(hdr.filler[FLAT_SHLIB_LIB_ID]) == lib_id)) {
                /* Found it — register */
                int slot;
                for (slot = 0; slot < MAX_SHLIBS; slot++) {
                    if (shlibs[slot].lib_id == 0)
                        break;
                }
                if (slot >= MAX_SHLIBS) {
                    kprintf("xipfs: shlib registry full\n");
                    return NULL;
                }
                sl = &shlibs[slot];
                sl->lib_id = lib_id;
                sl->flash_base = payload;
                sl->text_base = payload + sizeof(struct flat_hdr);
                sl->text_len = long_be(hdr.data_start) - sizeof(struct flat_hdr);
                sl->data_len = long_be(hdr.data_end) - long_be(hdr.data_start);
                sl->bss_len = long_be(hdr.bss_end) - long_be(hdr.data_end);

                export_off = long_be(hdr.filler[FLAT_SHLIB_EXPORT_OFF]);
                export_cnt = long_be(hdr.filler[FLAT_SHLIB_EXPORT_CNT]);
                sl->export_count = export_cnt;

                /* Export table: version(4) + count(4) + offsets[] */
                if (export_off && export_cnt) {
                    const uint32_t *etab = (const uint32_t *)(payload + export_off);
                    sl->version = long_be(etab[0]);
                    /* etab[1] is count (redundant), offsets start at etab[2] */
                    sl->export_table = &etab[2];
                } else {
                    sl->export_table = NULL;
                }
                kprintf("xipfs: registered shlib id=%d (%s) version=%lu exports=%lu\n",
                        lib_id, f->name, sl->version, (unsigned long)export_cnt);
                return sl;
            }
        }

        if (f->magic == XIPFS_MAGIC || f->magic == XIPFS_MAGIC_SHLIB)
            offset += f->len + sizeof(struct xipfs_fhdr);
        else
            offset += sizeof(struct xipfs_fhdr);
        while ((offset % 4) != 0)
            offset++;
    }
    return NULL;
}

/* Look up a shared library by ID.  Registers on first access. */
const struct loaded_shlib *xipfs_shlib_find(uint8_t lib_id)
{
    int i;
    for (i = 0; i < MAX_SHLIBS; i++) {
        if (shlibs[i].lib_id == lib_id)
            return &shlibs[i];
    }
    return shlib_register(lib_id);
}
#endif

static int xipfs_mount(char *source, char *tgt, uint32_t flags, void *arg)
{
    struct fnode *tgt_dir = NULL;
    const struct xipfs_fat *fat;

    /* Source must NOT be NULL */
    if (!source)
        return -1;

    /* Target must be a valid dir */
    if (!tgt)
        return -1;

    tgt_dir = fno_search(tgt);
    if (!tgt_dir || ((tgt_dir->flags & FL_DIR) == 0))
        return -1;

    /* Validate the FAT header */
    fat = (const struct xipfs_fat *)source;
    if (fat->fs_magic != XIPFS_MAGIC)
        return -1;
    if (!fat->fs_files && !fat->fs_size)
        return -1;

    /* O(1) mount: just store the blob pointer */
    tgt_dir->owner = &mod_xipfs;
    tgt_dir->priv = source;
#ifdef CONFIG_SHLIB
    xipfs_blob_ptr = (const uint8_t *)source;
#endif
    return 0;
}

static int xipfs_mount_info(struct fnode *fno, char *buf, int len)
{
    const char desc[] = "Applications and executables in bFLT format";
    if (len < 0)
        return -1;
    strncpy(buf, desc, len);
    if (len > (int)strlen(desc)) {
        len = strlen(desc);
        buf[len++] = 0;
    } else
        buf[len - 1] = 0;
    return len;
}


void xipfs_init(void)
{
    mod_xipfs.family = FAMILY_FILE;
    mod_xipfs.mount = xipfs_mount;
    strcpy(mod_xipfs.name, "xipfs");
    mod_xipfs.mount_info = xipfs_mount_info;
    mod_xipfs.ops.read = xipfs_read;
    mod_xipfs.ops.poll = xipfs_poll;
    mod_xipfs.ops.write = xipfs_write;
    mod_xipfs.ops.seek = xipfs_seek;
    mod_xipfs.ops.creat = xipfs_creat;
    mod_xipfs.ops.unlink = xipfs_unlink;
    mod_xipfs.ops.close = xipfs_close;
    mod_xipfs.ops.exe = xipfs_exe;
    mod_xipfs.ops.block_read = xipfs_block_read;
    mod_xipfs.ops.lookup = xipfs_lookup;
    mod_xipfs.ops.readdir = xipfs_readdir;
    register_module(&mod_xipfs);
}
