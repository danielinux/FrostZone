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
              void **entry_point, size_t *stack_size, uint32_t *got_loc, uint32_t *text_len, uint32_t *data_len);

#ifdef CONFIG_SHLIB
/* Shared library support */
struct loaded_shlib;
const struct loaded_shlib *xipfs_shlib_find(uint8_t lib_id);

int shlib_resolve_got(uint32_t *got_start, uint32_t got_words,
                      const uint8_t *app_text_base, uint8_t *app_data_base);
#endif

#endif
