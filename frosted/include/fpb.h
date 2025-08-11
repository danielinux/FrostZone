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

#ifndef FROSTED_FPB_INC
#define FROSTED_FPB_INC
#include <stdint.h>

int fpb_init(void);
int fpb_setbrk(int pid, void *bpoint, int n);
int fpb_delbrk(int n);
/* fpb.h - Flash Patch and Breakpoint (FPB) unit registers and definitions */


/* FPB register base */
#define FPB_BASE_ADDR         (0xE0002000UL)

/* FPB register definitions */
#define FPB_CTRL              (*(volatile uint32_t *)(FPB_BASE_ADDR + 0x000))
#define FPB_REMAP             (*(volatile uint32_t *)(FPB_BASE_ADDR + 0x004))
#define FPB_COMP              ((volatile uint32_t *)(FPB_BASE_ADDR + 0x008))
#define FPB_LAR               (*(volatile uint32_t *)(FPB_BASE_ADDR + 0xFB0))
#define FPB_LSR               (*(volatile uint32_t *)(FPB_BASE_ADDR + 0xFB4))

/* FPB Control Register Bits */
#define FPB_CTRL_ENABLE        (1UL << 0)
#define FPB_CTRL_KEY           (1UL << 1) /* Must be 1 for writes to take effect */
#define FPB_CTRL_NUM_CODE(x)   (((x) >> 4) & 0x7UL) /* Number of code comparators */

/* FPB Comparator Register Bits */
#define FPB_COMP_ENABLE        (1UL << 0)
#define FPB_COMP_REPLACE_LOWER (0x0UL << 30)
#define FPB_COMP_REPLACE_UPPER (0x1UL << 30)
#define FPB_COMP_REPLACE_BOTH  (0x2UL << 30)

/* FPB Lock Access Register Key */
#define FPB_LAR_KEY            (0xC5ACCE55UL)

/* FPB Lock Status Register */
#define FPB_LSR_PRESENT        (1UL << 0)
#define FPB_LSR_LOCKED         (1UL << 1)
#define FPB_LSR_ACCESS_OK      (1UL << 2)


#endif
