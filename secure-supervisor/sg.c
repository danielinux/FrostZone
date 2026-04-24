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
#include <stddef.h>
#include "armv8m_tz.h"
#include "stm32h563.h"

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

static const sg_request_t *ns_request_range_check(const sg_request_t *req)
{
    uintptr_t start;
    uintptr_t end;

    if (req == NULL)
        return NULL;

    start = (uintptr_t)req;
    if (start > (UINTPTR_MAX - (sizeof(*req) - 1u)))
        return NULL;
    end = start + sizeof(*req) - 1u;

    if ((start >= SAU_RAM_NS_START) && (end <= SAU_RAM_NS_END))
        return req;
    if ((start >= SAU_FLASH_NS_START) && (end <= SAU_FLASH_NS_END))
        return req;
    return NULL;
}

/* Secure stub: called from non-secure world via NSC region */
__attribute__((section(".ns_callable"), cmse_nonsecure_entry))
uint32_t sg_syscall_entry(sg_request_t *req)
{
    sg_request_t req_copy;
    const sg_request_t *ns_req = ns_request_range_check(req);

    if (!ns_req) {
        return (uint32_t)-1;
    }

    req_copy.syscall_id = ns_req->syscall_id;
    req_copy.arg0 = ns_req->arg0;
    req_copy.arg1 = ns_req->arg1;
    req_copy.arg2 = ns_req->arg2;
    req_copy.arg3 = ns_req->arg3;

    switch (req_copy.syscall_id) {
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
