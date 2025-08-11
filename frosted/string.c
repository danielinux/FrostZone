/*
 *      This file is part of frostzone.
 *
 *      frostzone is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
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
#include "string.h"

size_t strlen(const char *s); /* forward declaration */

int islower(int c)
{
    return (c >= 'a' && c <= 'z');
}

int isupper(int c)
{
    return (c >= 'A' && c <= 'Z');
}

int tolower(int c)
{
    return isupper(c) ? c - 'A' + 'a' : c;
}

int toupper(int c)
{
    return islower(c) ? c - 'a' + 'A' : c;
}

int isalpha(int c)
{
    return (isupper(c) || islower(c));
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *d = (unsigned char *)s;

    while (n--) {
        *d++ = (unsigned char)c;
    }

    return s;
}

char *strcat(char *dest, const char *src)
{
    size_t i = 0;
    size_t j = strlen(dest);

    for (i = 0; i < strlen(src); i++) {
        dest[j++] = src[i];
    }
    dest[j] = '\0';

    return dest;
}

int strcmp(const char *s1, const char *s2)
{
    int diff = 0;

    while (!diff && *s1) {
        diff = (int)*s1 - (int)*s2;
        s1++;
        s2++;
    }

    return diff;
}

int strcasecmp(const char *s1, const char *s2)
{
    int diff = 0;

    while (!diff && *s1) {
        diff = (int)*s1 - (int)*s2;

        if ((diff == 'A' - 'a') || (diff == 'a' - 'A'))
            diff = 0;

        s1++;
        s2++;
    }

    return diff;
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    int diff = 0;
    size_t i = 0;

    while (!diff && *s1) {
        diff = (int)*s1 - (int)*s2;

        if ((diff == 'A' - 'a') || (diff == 'a' - 'A'))
            diff = 0;

        s1++;
        s2++;
        if (++i > n)
            break;
    }
    return diff;
}

char *strncat(char *dest, const char *src, size_t n)
{
    size_t i = 0;
    size_t j = strlen(dest);

    for (i = 0; i < strlen(src); i++) {
        if (j >= (n - 1)) {
            break;
        }
        dest[j++] = src[i];
    }
    dest[j] = '\0';

    return dest;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    int diff = 0;

    while (n > 0) {
        diff = (unsigned char)*s1 - (unsigned char)*s2;
        if (diff || !*s1)
            break;
        s1++;
        s2++;
        n--;
    }

    return diff;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        dst[i] = src[i];
        if (src[i] == '\0')
            break;
    }

    return dst;
}

char *strcpy(char *dst, const char *src)
{
   size_t i = 0;

    while(1) {
        dst[i] = src[i];
        if (src[i] == '\0')
            break;
        i++;
    }

    return dst;
}

int memcmp(const void *_s1, const void *_s2, size_t n)
{
    int diff = 0;
    const unsigned char *s1 = (const unsigned char *)_s1;
    const unsigned char *s2 = (const unsigned char *)_s2;

    while (!diff && n) {
        diff = (int)*s1 - (int)*s2;
        s1++;
        s2++;
        n--;
    }

    return diff;
}

void* memchr(void const *s, int c_in, size_t n)
{
    unsigned char c = (unsigned char)c_in;
    unsigned char *char_ptr = (unsigned char*)s;
    for (; n > 0; --n, ++char_ptr) {
        if (*char_ptr == c) {
            return (void*)char_ptr;
        }
    }
    return NULL;
}

size_t strlen(const char *s)
{
    size_t i = 0;

    while (s[i] != 0)
        i++;

    return i;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    size_t i;
    const char *s = (const char *)src;
    char *d = (char *)dst;

#ifdef FAST_MEMCPY
    /* is 32-bit aligned pointer */
    if (((size_t)dst & (sizeof(unsigned long)-1)) == 0 &&
        ((size_t)src & (sizeof(unsigned long)-1)) == 0)
    {
        while (n >= sizeof(unsigned long)) {
            *(unsigned long*)d = *(unsigned long*)s;
            d += sizeof(unsigned long);
            s += sizeof(unsigned long);
            n -= sizeof(unsigned long);
        }
    }
#endif
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return dst;
}
void *memmove(void *dst, const void *src, size_t n)
{
    int i;
    if (dst == src)
        return dst;
    if (src < dst)  {
        const char *s = (const char *)src;
        char *d = (char *)dst;
        for (i = n - 1; i >= 0; i--) {
            d[i] = s[i];
        }
        return dst;
    } else {
        return memcpy(dst, src, n);
    }
}
