/*
 *      This file is part of frostzone.
 *
 *      frostzone is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 3, as
 *      published by the Free Software Foundation.
 *
 *
 *      frostzone is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frostzone.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors: Daniele Lacamera
 * 
 */
#include <stdint.h>
#include "armv8m_tz.h"

/* Example syscall numbers */
#define SG_CLAIM_PERIPHERAL     0
#define SG_MAP_MEMORY_REGION    1
#define SG_VFORK                2



/* Non-secure caller context struct (optional, can carry metadata) */
typedef struct {
    uint32_t syscall_id;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
} sg_request_t;

/* Secure stub: called from non-secure world via NSC region */
__attribute__((section(".ns_callable"), cmse_nonsecure_entry))
uint32_t sg_syscall_entry(sg_request_t *req)
{
    if (!req) {
        return (uint32_t)-1;
    }

    switch (req->syscall_id) {
        case SG_CLAIM_PERIPHERAL:
            /* stub implementation */
            return 0;

        case SG_MAP_MEMORY_REGION:
            /* stub implementation */
            return 0;

        case SG_VFORK:
            /* stub implementation */
            return 1234; /* dummy PID */

        default:
            return (uint32_t)-1;
    }
}

