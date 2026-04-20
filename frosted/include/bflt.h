 /*
 */

#include <frosted_api.h>
#include "flat.h"

#ifndef _BFLT_H_
#define _BFLT_H_

static inline uint32_t long_be(uint32_t le)
{
    uint8_t *b = (uint8_t *)&le;
    return b[3] + ((uint32_t)b[2] << 8) + ((uint32_t)b[1] << 16) + ((uint32_t)b[0] << 24);
}

int bflt_load(uint8_t* from, void **reloc_text, void **reloc_data, void **reloc_bss,
              void **entry_point, size_t *stack_size, uint32_t *got_loc, uint32_t *text_len, uint32_t *data_len,
              void **extra_mmap, uint32_t *extra_mmap_count);

#ifdef CONFIG_SHLIB
/* Shared library support */
struct loaded_shlib {
    uint8_t  lib_id;
    uint32_t version;
    const uint8_t *flash_base;
    const uint8_t *text_base;
    uint32_t text_len;
    uint32_t data_len;
    uint32_t bss_len;
    uint32_t export_count;
    const uint32_t *export_offsets;
    const uint32_t *export_name_offsets;
    const char *export_strings;
};

struct shlib_runtime {
    uint8_t *alloc_base;
    uint8_t *trampoline_base;
    uint8_t *data_base;
    uint32_t got_loc;
};

#define SHLIB_TRAMPOLINE_SIZE 32U

const struct loaded_shlib *xipfs_shlib_find(uint8_t lib_id);

int shlib_resolve_got(uint32_t *got_start, uint32_t got_words,
                      const uint8_t *app_text_base, uint8_t *app_data_base,
                      void **extra_mmap_out, uint32_t *extra_mmap_count_out);
int shlib_runtime_load(const struct loaded_shlib *sl, uint16_t owner_pid,
                       struct shlib_runtime *runtime);
#endif

#endif
