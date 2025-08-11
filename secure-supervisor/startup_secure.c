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

extern void main(void);
extern uint32_t  __data_start__, __data_end__, __bss_start__, __bss_end__, __etext;

void Reset_Handler(void) {
    uint32_t *src = &__etext;
    uint32_t *dst = &__data_start__;
    while (dst < &__data_end__)
        *(dst++) = *(src++);
    for (dst = &__bss_start__; dst < &__bss_end__; dst++)
        *dst = 0;
    main();
    while (1);
}

