/*  
 *      This file is part of frosted.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as 
 *      published by the free Software Foundation.
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
 *      Authors: Daniele Lacamera, Maxime Vincent
 *
 */  
#include "frosted.h"
#include "bflt.h"
#include "string.h"

extern volatile int phase0_bflt_trace_tag;


#ifndef kprintf
#define kprintf(...) do {} while(0)
#endif

/*************************
 * bFLT start 
 *************************/
#define RELOC_FAILED 0xff00ff01     /* Relocation incorrect somewhere */

static inline uint16_t short_be(uint16_t le)
{
    return (uint16_t)(((le & 0xFFu) << 8) | ((le >> 8u) & 0xFFu));
}

/* long_be() is now in bflt.h */

static void load_header(struct flat_hdr * to_hdr, struct flat_hdr * from_hdr) {
    memcpy((uint8_t*)to_hdr, (uint8_t*)from_hdr, sizeof(struct flat_hdr));
}

static int check_header(struct flat_hdr * header) {
    if (memcmp(header->magic, "bFLT", 4) != 0) {
        kprintf("bFLT: Magic number does not match\r\n");
        return -1;
    }
    if (long_be(header->rev) != FLAT_VERSION){
        kprintf("bFLT: Version number does not match\r\n");
        return -1;
    }

    /* check for unsupported flags */
    if (long_be(header->flags) & (FLAT_FLAG_GZIP | FLAT_FLAG_GZDATA)) {
        kprintf("bFLT: Unsupported flags detected - GZip'd data is not supported\r\n");
        return -1;
    }
    return 0;
}

static unsigned long * calc_reloc(uint8_t * base, uint32_t offset)
{
    /* the library id is in top byte of offset */
    int id = (offset >> 24) & 0x000000FFu;
    if (id)
    {
#ifdef CONFIG_SHLIB
        /* Shared library import: return sentinel so process_got_relocs()
         * leaves this GOT entry unchanged for shlib_resolve_got(). */
        return NULL;
#else
        kprintf("bFLT: No shared library support\r\n");
        return (unsigned long *)RELOC_FAILED;
#endif
    }
    return (unsigned long *)(base + (offset & 0x00FFFFFFu));
}

static int process_got_relocs(struct flat_hdr *hdr, uint8_t *base, uint8_t *got_start,
                              int *has_shlib_imports)
{
    /*
     * Addresses in header are relative to start of FILE (so including flat_hdr)
     * Addresses in the relocs are relative to start of .text (so excluding flat_hdr)
     *
     * For a shared library linked at a fixed flash address, GOT entries hold
     * absolute link-time VMAs (base = sl->text_base). Normalise them to
     * "offsets from .text start" (which is what the logic below expects) by
     * subtracting the library's link-time text VMA before processing.
     */
    unsigned long *rp = (unsigned long *)got_start;
    unsigned long data_start = long_be(hdr->data_start) - sizeof(struct flat_hdr);
    unsigned long bss_end = long_be(hdr->bss_end) - sizeof(struct flat_hdr);
    uint8_t * text_start_dest = base + sizeof(struct flat_hdr);
    uint8_t * data_start_dest = got_start;
    unsigned long got_words = (long_be(hdr->data_end) - long_be(hdr->data_start))
                              / sizeof(unsigned long);
    unsigned long idx;
    unsigned long lib_text_vma = 0;
    unsigned long lib_vma_end = 0;

#ifdef CONFIG_SHLIB
    if ((long_be(hdr->flags) & FLAT_FLAG_SHLIB) != 0) {
        lib_text_vma = (unsigned long)text_start_dest;
        /* Upper bound of the library's link-time VMA range, so we only
         * normalise values that look like pointers into the library. */
        lib_vma_end = lib_text_vma + bss_end;
    }
#endif

    if (has_shlib_imports)
        *has_shlib_imports = 0;

    for (idx = 0; idx < got_words; idx++, rp++) {
        if (*rp == 0xffffffff)
            return 0;
        if (*rp) {
            unsigned long addr;
            unsigned long val = *rp;
#ifdef CONFIG_SHLIB
            /* For shared libs linked at flash address: values are VMAs
             * (e.g. 0x080600xx). Normalise to file-relative offsets BEFORE
             * the shlib-import (lib_id) check, otherwise the 0x08 top byte
             * of the VMA collides with the lib_id tag and legitimate intra-
             * library pointers get skipped. */
            if (lib_text_vma && val >= lib_text_vma && val < lib_vma_end)
                val -= lib_text_vma;
            /* Check for shared library import (top byte is lib_id) */
            if ((val >> 24) & 0xFFu) {
                /* Leave this GOT entry as-is for shlib_resolve_got() */
                if (has_shlib_imports)
                    *has_shlib_imports = 1;
                continue;
            }
#endif
            addr = RELOC_FAILED;
            if (val < data_start) {
                /* reloc is in .text section: BASE == text_start  -- addr == relative to .text */
                addr = (unsigned long)calc_reloc(text_start_dest, val);
            } else if (val < bss_end) {
                /* reloc is in .data section: BASE == data_start  -- addr == relative to .text - (start of data) */
                addr = (unsigned long)calc_reloc(data_start_dest, val - data_start);
            }

            /* this will remap pointers starting from address 0x0, to wherever they are actually loaded in the memory map (.text reloc) */
            if (addr == RELOC_FAILED) {
                //errno = -ENOEXEC;
                return -1;
            }
            *rp = addr;
        }
    }
    kprintf("bFLT: GOT terminator missing from data segment\r\n");
    return -1;
}

/* works only for FLAT v4 */
/* reloc starts just after data_end */
int process_relocs(struct flat_hdr * hdr, unsigned long * base, unsigned long data_start_dest,  unsigned long *reloc, int relocs)
{
    int i;
    /* Relocations in the table point to an address,
     * at that address(1), there is an adress(2) that needs fixup,
     * and needs to be written at it's original address(1), after it's been fixed
     */
    /*
     * Addresses in header are relative to start of FILE (so including flat_hdr)
     * Addresses in the relocs are relative to start of .text (so excluding flat_hdr)
     */
    unsigned long data_start = long_be(hdr->data_start) - sizeof(struct flat_hdr);
    unsigned long data_end = long_be(hdr->data_end) - sizeof(struct flat_hdr); /* relocs must be located in .data segment for GOTPIC */
    unsigned long bss_end = long_be(hdr->bss_end) - sizeof(struct flat_hdr);
    unsigned long text_start_dest = ((unsigned long)base) + sizeof(struct flat_hdr); /* original RELOC is relative to text_start (.bss in ROM/Flash/source) */
    unsigned long lib_text_vma = 0;
    unsigned long lib_vma_end = 0;

#ifdef CONFIG_SHLIB
    /* Shared libraries are linked at a fixed flash address; normalise VMAs
     * in relocated words back to file-relative offsets. */
    if ((long_be(hdr->flags) & FLAT_FLAG_SHLIB) != 0) {
        lib_text_vma = text_start_dest;
        lib_vma_end = lib_text_vma + bss_end;
    }
#endif
    /*
     * Now run through the relocation entries.
     * We've got to be careful here as C++ produces relocatable zero
     * entries in the constructor and destructor tables which are then
     * tested for being not zero (which will always occur unless we're
     * based from address zero).  This causes an endless loop as __start
     * is at zero.  The solution used is to not relocate zero addresses.
     * This has the negative side effect of not allowing a global data
     * reference to be statically initialised to _stext (I've moved
     * __start to address 4 so that is okay).
     */
    for (i=0; i < relocs; i++) {
        unsigned long addr, *fixup_addr;
        unsigned long *relocd_addr = (unsigned long *)RELOC_FAILED;
        
        /* Get the address of the pointer to be
           relocated (of course, the address has to be
           relocated first).  */
        fixup_addr = (unsigned long *)long_be(reloc[i]);
        /* two cases: reloc_addr < text_end: For now we only support GOTPIC, which cannot have relocations in .text segment!
         *        or  reloc_addr > data_start
         */
        /* TODO: Make common between GOT and regular relocs ? */



        if ((unsigned long)fixup_addr < data_start)
        {
            /* FAIL -- non GOTPIC, cannot write to ROM/.text */
            return -1;
        } else if ((unsigned long)fixup_addr < data_end) {
            /* Reloc is in .data section (must be for GOTPIC), now make this point to the .data source (in the DEST ram!), and dereference */
            fixup_addr = (unsigned long *)calc_reloc((uint8_t *)((unsigned long)data_start_dest - (unsigned long)data_start), (unsigned long)fixup_addr);
            if (fixup_addr == (unsigned long *)RELOC_FAILED)
                return -1;

            {
                unsigned long val = *fixup_addr;
                /* Normalise VMAs to file-relative offsets for shared libs,
                 * but only when val looks like a pointer into the library. */
                if (lib_text_vma && val >= lib_text_vma && val < lib_vma_end)
                    val -= lib_text_vma;
                /* Again 2 cases: reloc points to .text -- or to .data/.bss */
                if (val < data_start) {
                    /* reloc is in .text section: BASE == text_start  -- addr == relative to .text */
                    relocd_addr = (unsigned long *)calc_reloc((uint8_t *)text_start_dest, val);
                } else if (val < bss_end) {
                    /* reloc is in .data section: BASE == data_start  -- addr == relative to .text - (start of data) */
                    relocd_addr = (unsigned long *)calc_reloc((uint8_t *)data_start_dest, val - data_start);
                } else {
                    relocd_addr = (unsigned long *)RELOC_FAILED;
                    return -1;
                }
            }
            /* write the relocated/offsetted value back were it was read */
            *fixup_addr = (unsigned long)relocd_addr;
        }

        if (relocd_addr == (unsigned long *)RELOC_FAILED) {
            kprintf("bFLT: Unable to calculate relocation address\r\n");
            return -1;
        }

    }

    return 0;
}


#ifdef CONFIG_SHLIB
/*
 * Shared library GOT resolver.
 *
 * After process_got_relocs() runs, GOT entries with a non-zero library ID
 * in the top byte are left untouched (they contain lib_id<<24 | ordinal).
 * This function resolves them:
 *
 *  1. Looks up the library in the xipfs shlib registry
 *  2. Allocates per-process RAM for trampolines + library .data/.bss
 *  3. Relocates the library's GOT (in .data copy) for this process
 *  4. Generates an r9-swapping trampoline for each imported function
 *  5. Patches the application's GOT entry to point to the trampoline
 *
 * Trampoline layout (20 bytes, Thumb-2):
 *   push  {r9, lr}       // E92D 4200
 *   ldr   r9, [pc, #4]   // F8DF 9004
 *   blx   <lib_func>     // F000 E800 (patched)
 *   pop   {r9, pc}        // E8BD 8200
 *   .word <lib_got>       // per-process library GOT base
 */

/* Per-library per-process state during resolution */
struct shlib_proc {
    uint8_t  lib_id;
    uint8_t *trampoline_base;
    uint8_t *data_base;       /* library .data copy */
    uint32_t got_loc;         /* library GOT = data_base */
    uint32_t n_trampolines;   /* how many generated so far */
};

#define MAX_PROC_SHLIBS 4

/*
 * Generate a 24-byte Thumb trampoline that swaps r9 (GOT base)
 * from the application's to the library's before calling lib_func_addr.
 *
 * Cortex-M does not support BLX (register). Use an explicit odd return
 * label so the library returns to "mov r9, r5" before the final pop.
 *
 * Layout:
 *   0x00  push {r4, r5, r6, lr}  B570
 *   0x02  mov  r5, r9            464D
 *   0x04  ldr  r6, [pc, #16]     4E04       ; -> 0x18: lib_got
 *   0x06  mov  r9, r6            46B1
 *   0x08  ldr  r4, [pc, #16]     4C04       ; -> 0x1C: lib_func_addr
 *   0x0A  addw r6, pc, #8        F20F0608   ; -> 0x14
 *   0x0E  adds r6, #1            3601
 *   0x10  mov  lr, r6            46B6
 *   0x12  bx   r4                4720
 *   0x14  mov  r9, r5            46A9
 *   0x16  pop  {r4, r5, r6, pc}  BD70
 *   0x18  .word  lib_got
 *   0x1C  .word  lib_func_addr
 */
static void generate_trampoline(uint8_t *dst, uint32_t lib_func_addr, uint32_t lib_got)
{
    uint8_t *p = dst;
    uint32_t *w;

    /* push {r4, r5, r6, lr} */
    p[0] = 0x70; p[1] = 0xB5;
    /* mov r5, r9 */
    p[2] = 0x4D; p[3] = 0x46;
    /* ldr r6, [pc, #16] */
    p[4] = 0x04; p[5] = 0x4E;
    /* mov r9, r6 */
    p[6] = 0xB1; p[7] = 0x46;
    /* ldr r4, [pc, #16] */
    p[8] = 0x04; p[9] = 0x4C;
    /* addw r6, pc, #8 */
    p[10] = 0x0F; p[11] = 0xF2; p[12] = 0x08; p[13] = 0x06;
    /* adds r6, #1 */
    p[14] = 0x01; p[15] = 0x36;
    /* mov lr, r6 */
    p[16] = 0xB6; p[17] = 0x46;
    /* bx r4 */
    p[18] = 0x20; p[19] = 0x47;
    /* mov r9, r5 */
    p[20] = 0xA9; p[21] = 0x46;
    /* pop {r4, r5, r6, pc} */
    p[22] = 0x70; p[23] = 0xBD;
    /* Literal pool */
    w = (uint32_t *)(p + 24);
    w[0] = lib_got;
    w[1] = lib_func_addr;
}

int shlib_resolve_got(uint32_t *got_start, uint32_t got_words,
                      const uint8_t *app_text_base, uint8_t *app_data_base,
                      void **extra_mmap_out, uint32_t *extra_mmap_count_out)
{
    struct shlib_proc libs[MAX_PROC_SHLIBS];
    int n_libs = 0;
    uint32_t idx;
    uint32_t max_ordinals[MAX_PROC_SHLIBS];

    memset(libs, 0, sizeof(libs));
    memset(max_ordinals, 0, sizeof(max_ordinals));

    /* First pass: count imports per library to size trampoline blocks */
    for (idx = 0; idx < got_words; idx++) {
        uint32_t entry = got_start[idx];
        uint8_t lib_id;
        int li;

        if (entry == 0xFFFFFFFF)
            break;
        lib_id = (entry >> 24) & 0xFF;
        if (lib_id == 0)
            continue;

        /* Find or create lib slot */
        for (li = 0; li < n_libs; li++) {
            if (libs[li].lib_id == lib_id)
                break;
        }
        if (li == n_libs) {
            if (n_libs >= MAX_PROC_SHLIBS) {
                kprintf("bFLT: too many shared libraries\r\n");
                return -1;
            }
            libs[li].lib_id = lib_id;
            n_libs++;
        }
        libs[li].n_trampolines++;
    }

    if (n_libs == 0) {
        kprintf("bFLT: shlib: no imports found\r\n");
        return 0;
    }

    kprintf("bFLT: shlib: %d libs, imports:", n_libs);
    {
        int k;
        for (k = 0; k < n_libs; k++)
            kprintf(" lib%d:%lu", libs[k].lib_id, (unsigned long)libs[k].n_trampolines);
    }
    kprintf("\r\n");

    if (extra_mmap_count_out)
        *extra_mmap_count_out = 0;

    /* Allocate per-process blocks for each library */
    {
    int li;
    for (li = 0; li < n_libs; li++) {
        const struct loaded_shlib *sl = xipfs_shlib_find(libs[li].lib_id);
        uint32_t tramp_size, total;
        uint8_t *mem;
        struct flat_hdr lib_hdr;

        if (!sl || !sl->export_offsets) {
            kprintf("bFLT: shared library id=%d not found\r\n", libs[li].lib_id);
            return -1;
        }

        memcpy(&lib_hdr, sl->flash_base, sizeof(struct flat_hdr));

        if ((check_header(&lib_hdr) != 0) ||
            ((long_be(lib_hdr.flags) & FLAT_FLAG_SHLIB) == 0)) {
            kprintf("bFLT: shlib header check failed\r\n");
            return -1;
        }

        tramp_size = libs[li].n_trampolines * SHLIB_TRAMPOLINE_SIZE;
        /* Round up to 4-byte alignment */
        tramp_size = (tramp_size + 3) & ~3u;
        total = tramp_size + sl->data_len + sl->bss_len;

        mem = secure_mmap(total, 0, MMAP_NEWPAGE);
        if (!mem) {
            kprintf("bFLT: shlib alloc failed (%lu bytes)\r\n", (unsigned long)total);
            return -1;
        }

        /* Record allocation so caller can chown it to the task */
        if (extra_mmap_out && extra_mmap_count_out && *extra_mmap_count_out < 4)
            extra_mmap_out[(*extra_mmap_count_out)++] = mem;

        libs[li].trampoline_base = mem;
        libs[li].data_base = mem + tramp_size;
        libs[li].got_loc = (uint32_t)libs[li].data_base;
        libs[li].n_trampolines = 0;  /* reset counter for second pass */

        /* Copy library .data from flash */
        memcpy(libs[li].data_base,
               sl->flash_base + long_be(((struct flat_hdr *)sl->flash_base)->data_start),
               sl->data_len);
        /* Zero .bss */
        memset(libs[li].data_base + sl->data_len, 0, sl->bss_len);

        /* Relocate library's GOT in the per-process .data copy */
        {
            uint8_t *lib_relocs_src;
            int32_t lib_relocs;
            int rc;

            rc = process_got_relocs(&lib_hdr, (uint8_t *)sl->flash_base,
                                    libs[li].data_base, NULL);
            if (rc != 0) {
                kprintf("bFLT: shlib GOT relocation failed (rc=%d)\r\n", rc);
                return -1;
            }

            lib_relocs = long_be(lib_hdr.reloc_count);
            lib_relocs_src = (uint8_t *)sl->flash_base + long_be(lib_hdr.reloc_start);
            rc = process_relocs(&lib_hdr, (unsigned long *)sl->flash_base,
                                (uint32_t)libs[li].data_base,
                                (unsigned long *)lib_relocs_src,
                                lib_relocs);
            if (rc != 0) {
                kprintf("bFLT: shlib relocation failed (rc=%d)\r\n", rc);
                return -1;
            }
        }
        kprintf("bFLT: shlib: lib%d loaded: tramp=0x%08lx data=0x%08lx got=0x%08lx\r\n",
                libs[li].lib_id,
                (unsigned long)libs[li].trampoline_base,
                (unsigned long)libs[li].data_base,
                (unsigned long)libs[li].got_loc);
    }
    }

    /* Second pass: generate trampolines and patch GOT entries */
    for (idx = 0; idx < got_words; idx++) {
        uint32_t entry = got_start[idx];
        uint8_t lib_id;
        uint32_t ordinal;
        int li;
        const struct loaded_shlib *sl;
        uint32_t func_offset, func_addr;
        uint8_t *tramp;

        if (entry == 0xFFFFFFFF)
            break;
        lib_id = (entry >> 24) & 0xFF;
        if (lib_id == 0)
            continue;

        ordinal = entry & 0x00FFFFFFu;

        /* Find lib slot */
        for (li = 0; li < n_libs; li++) {
            if (libs[li].lib_id == lib_id)
                break;
        }

        sl = xipfs_shlib_find(lib_id);
        if (!sl || ordinal >= sl->export_count) {
            kprintf("bFLT: bad shlib import lib=%d ord=%lu\r\n",
                    lib_id, (unsigned long)ordinal);
            return -1;
        }

        /* Look up the exported function's .text-relative offset */
        func_offset = long_be(sl->export_offsets[ordinal]);
        func_addr = (uint32_t)sl->text_base + func_offset;
        if ((func_offset >= sl->text_len) ||
            (func_addr < (uint32_t)sl->text_base) ||
            (func_addr >= ((uint32_t)sl->text_base + sl->text_len))) {
            kprintf("bFLT: shlib target out of range lib=%d ord=%lu off=%lu base=0x%08lx len=%lu addr=0x%08lx\r\n",
                    lib_id, (unsigned long)ordinal, (unsigned long)func_offset,
                    (unsigned long)sl->text_base, (unsigned long)sl->text_len,
                    (unsigned long)func_addr);
            return -1;
        }

        /* Generate trampoline */
        tramp = libs[li].trampoline_base +
                libs[li].n_trampolines * SHLIB_TRAMPOLINE_SIZE;
        generate_trampoline(tramp, func_addr | 1, libs[li].got_loc);
        libs[li].n_trampolines++;

        /* Patch app's GOT entry: point to trampoline with Thumb bit */
        got_start[idx] = (uint32_t)tramp | 1;
        kprintf("bFLT: shlib: GOT[%lu] lib%d:ord%lu → tramp=0x%08lx func=0x%08lx\r\n",
                (unsigned long)idx, lib_id, (unsigned long)ordinal,
                (unsigned long)((uint32_t)tramp | 1),
                (unsigned long)(func_addr | 1));
    }

    return 0;
}

int shlib_runtime_load(const struct loaded_shlib *sl, uint16_t owner_pid,
                       struct shlib_runtime *runtime)
{
    struct flat_hdr lib_hdr;
    uint32_t tramp_size;
    uint32_t total;
    uint8_t *mem;
    uint8_t *lib_relocs_src;
    int32_t lib_relocs;
    int rc;
    uint32_t ordinal;

    if (!sl || !runtime || !sl->export_offsets || (sl->export_count == 0))
        return -EINVAL;

    memset(runtime, 0, sizeof(*runtime));
    memcpy(&lib_hdr, sl->flash_base, sizeof(struct flat_hdr));

    if ((check_header(&lib_hdr) != 0) ||
        ((long_be(lib_hdr.flags) & FLAT_FLAG_SHLIB) == 0)) {
        kprintf("bFLT: shlib header check failed\r\n");
        return -ENOEXEC;
    }

    tramp_size = sl->export_count * SHLIB_TRAMPOLINE_SIZE;
    tramp_size = (tramp_size + 3u) & ~3u;
    total = tramp_size + sl->data_len + sl->bss_len;

    mem = secure_mmap(total, owner_pid, MMAP_NEWPAGE);
    if (!mem) {
        kprintf("bFLT: shlib alloc failed (%lu bytes)\r\n", (unsigned long)total);
        return -ENOMEM;
    }

    runtime->alloc_base = mem;
    runtime->trampoline_base = mem;
    runtime->data_base = mem + tramp_size;
    runtime->got_loc = (uint32_t)runtime->data_base;

    memcpy(runtime->data_base,
           sl->flash_base + long_be(((struct flat_hdr *)sl->flash_base)->data_start),
           sl->data_len);
    memset(runtime->data_base + sl->data_len, 0, sl->bss_len);

    rc = process_got_relocs(&lib_hdr, (uint8_t *)sl->flash_base,
                            runtime->data_base, NULL);
    if (rc != 0) {
        secure_munmap(runtime->alloc_base, owner_pid);
        memset(runtime, 0, sizeof(*runtime));
        return -ENOEXEC;
    }

    lib_relocs = long_be(lib_hdr.reloc_count);
    lib_relocs_src = (uint8_t *)sl->flash_base + long_be(lib_hdr.reloc_start);
    rc = process_relocs(&lib_hdr, (unsigned long *)sl->flash_base,
                        (uint32_t)runtime->data_base,
                        (unsigned long *)lib_relocs_src,
                        lib_relocs);
    if (rc != 0) {
        secure_munmap(runtime->alloc_base, owner_pid);
        memset(runtime, 0, sizeof(*runtime));
        return -ENOEXEC;
    }

    for (ordinal = 0; ordinal < sl->export_count; ordinal++) {
        uint32_t func_offset = long_be(sl->export_offsets[ordinal]);
        uint32_t func_addr = (uint32_t)sl->text_base + func_offset;
        uint8_t *tramp = runtime->trampoline_base +
                         (ordinal * SHLIB_TRAMPOLINE_SIZE);

        if ((func_offset >= sl->text_len) ||
            (func_addr < (uint32_t)sl->text_base) ||
            (func_addr >= ((uint32_t)sl->text_base + sl->text_len))) {
            secure_munmap(runtime->alloc_base, owner_pid);
            memset(runtime, 0, sizeof(*runtime));
            return -ENOEXEC;
        }

        generate_trampoline(tramp, func_addr | 1u, runtime->got_loc);
    }

    return 0;
}
#endif

/* BFLT file structure:
 *
 * +------------------------+   0x0
 * | BFLT header            |
 * +------------------------+
 * | padding                |
 * +------------------------+   entry
 * | .text section          |
 * |                        |
 * +------------------------+   data_start
 * | .data section          |
 * |                        |   
 * +------------------------+   data_end, relocs_start, bss_start
 * | relocations (and .bss) |
 * |........................|   relocs_end   <- BFLT ends here
 * | (.bss section)         |
 * +------------------------+   bss_end
 */

int bflt_load(uint8_t* from, void **reloc_text, void **reloc_data, void **reloc_bss,
              void ** entry_point, size_t *stack_size, uint32_t *got_loc, uint32_t *text_len, uint32_t *data_len,
              void **extra_mmap, uint32_t *extra_mmap_count)
{
    struct flat_hdr hdr;
    void * mem = NULL;
    uint32_t bss_len, stack_len, flags, alloc_len, entry_point_offset;
    uint8_t *relocs_src, *text_src, *data_dest;
    uint8_t *address_zero = from;
    int32_t relocs, rev;

    //kprintf("bFLT: Loading from 0x%p\r\n", from);

    if (!address_zero) {
        goto error;
    }

    load_header(&hdr, (struct flat_hdr *)address_zero);
    if (check_header(&hdr) != 0) {
        kprintf("bFLT: Bad FLT header\r\n");
        goto error;
    }

    /* Calculate all the sizes */
    *text_len           = long_be(hdr.data_start) - sizeof(struct flat_hdr);
    *data_len           = long_be(hdr.data_end) - long_be(hdr.data_start);
    bss_len             = long_be(hdr.bss_end) - long_be(hdr.data_end);
    stack_len           = long_be(hdr.stack_size);
    relocs              = long_be(hdr.reloc_count);
    flags               = long_be(hdr.flags);
    rev                 = long_be(hdr.rev);
    /* Calculate source addresses */
    text_src            = address_zero + sizeof(struct flat_hdr);
    relocs_src          = address_zero + long_be(hdr.reloc_start);
    entry_point_offset  = (long_be(hdr.entry) & 0xFFFFFFFE) - sizeof(struct flat_hdr); /* offset inside .text + reset THUMB bit */
    *stack_size         = stack_len;

    /*
     * calculate the extra space we need to malloc
     */
    /* relocs are located in the .bss part of the BFLT binary, so we need whichever is biggest */
    if ((relocs * sizeof(uint32_t)) > bss_len)
        alloc_len = relocs * sizeof(uint32_t);
    else
        alloc_len = bss_len;
    alloc_len += *data_len;


    /*
     * there are a couple of cases here:
     *  -> the fully copied to RAM case which lumps it all together (RAM flag)
     *  -> the separate code/data case (GOTPIC flag, w/o RAM flag)
     */
    if (flags & FLAT_FLAG_GZIP) {
        kprintf("bFLT: GZIP compression not supported\r\n");
        goto error;
    }

    if (flags & FLAT_FLAG_GOTPIC) {
        uint8_t  *mem, *copy_src;
        uint32_t data_offset = 0;
        uint32_t copy_len = *data_len;

        if (flags & FLAT_FLAG_RAM) {
            alloc_len += *text_len;
            copy_len += *text_len;
            data_offset = *text_len;
        }

        /* Allocate enough memory for .data, .bss and possibly .text */
        mem = secure_mmap(alloc_len, 0, MMAP_NEWPAGE);
        if (!mem)
        {
            kprintf("bFLT: Could not allocate enough memory for process\r\n");
            goto error;
        }

        /* .text is only relocated when RAM flag is set */
        if (flags & FLAT_FLAG_RAM) {
            *reloc_text = mem;
            copy_src = text_src;
        } else {
            *reloc_text = text_src;
            copy_src = text_src + *text_len;
        }
        /* .data is always relocated */
        data_dest = mem + data_offset;

        *entry_point = *reloc_text + entry_point_offset;
        *reloc_data = data_dest;
        *reloc_bss = data_dest + *data_len;

        /* copy segments .data segment and possibly .text */
        memcpy(mem, copy_src, copy_len);
        /* zero-init .bss */
        memset(data_dest + *data_len, 0, bss_len);
    } else {
        /* GZIP or FULL RAM bFLTs not supported for now */
        kprintf("bFLT: Only GOTPIC bFLT binaries are supported\r\n");
        goto error;
    }

    /*
     * We just load the allocations into some temporary memory to
     * help simplify all this mumbo jumbo
     *
     * We've got two different sections of relocation entries.
     * The first is the GOT which resides at the beginning of the data segment
     * and is terminated with a -1.  This one can be relocated in place.
     * The second is the extra relocation entries tacked after the image's
     * data segment. These require a little more processing as the entry is
     * really an offset into the image which contains an offset into the
     * image.
     */

#ifdef CONFIG_SHLIB
    /*
     * Resolve shared library imports BEFORE relocation.
     * At this point the GOT still has raw values: small .text/.data offsets
     * for normal entries, and (lib_id << 24 | ordinal) for imports.
     * shlib_resolve_got() patches import entries to trampoline addresses.
     * The subsequent process_got_relocs() will skip these patched entries
     * (they have non-zero top byte) and only relocate normal entries.
     */
    if (flags & FLAT_FLAG_GOTPIC) {
        unsigned long got_words = (long_be(hdr.data_end) - long_be(hdr.data_start))
                                  / sizeof(unsigned long);
        kprintf("bFLT: shlib_resolve_got: data_dest=0x%p got_words=%lu text_src=0x%p\r\n",
                data_dest, got_words, text_src);
        if (shlib_resolve_got((uint32_t *)data_dest, got_words,
                              text_src, data_dest,
                              extra_mmap, extra_mmap_count) != 0)
            goto error;
        kprintf("bFLT: shlib_resolve_got: done\r\n");
    }
#endif

        /* init relocations */
    if (flags & FLAT_FLAG_GOTPIC) {
        //printf("GOT-PIC!\n");
        if (process_got_relocs(&hdr, address_zero, data_dest, NULL)) // .data section is beginning of GOT
            goto error;
        *got_loc = (uint32_t)data_dest;
    }

    /*
     * Now run through the relocation entries.
     */
    if (process_relocs(&hdr, (unsigned long *)address_zero, (uint32_t)data_dest,
                       (unsigned long *)relocs_src, relocs) != 0)
        goto error;

    return 0;

error:
    if (mem) kfree(mem);
    *reloc_text  = NULL;
    *reloc_data  = NULL;
    *reloc_bss   = NULL;
    *entry_point = NULL;
    kprintf("bFLT: Caught error - exiting\r\n");
    return -1;
}
