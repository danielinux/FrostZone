/*
 *      This file is part of frosted shared kernel-userspace headers.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU Lesser General Public License version 2.1, as
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

#ifndef GDB_REGS
#define GDB_REGS

struct user {
    uint32_t    regs[16]; /* r0 - r12, sp, lr, pc, psr */
    uint32_t    tsize; /* Text size, in bytes  */
    uint32_t    dsize; /* Data size, in bytes  */
    uint32_t    ssize; /* Stack size, in bytes */
    uint32_t    start_code; /* Init function */
    uint32_t    start_data;
    uint32_t    start_stack;
    int32_t     signal;
    int32_t     reserved;
    uint32_t    magic;
    int8_t      comm[32];
    uint32_t    debugreg[8];
};

#endif
