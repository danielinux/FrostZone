/*
 *      Secrets flashfs – secure-only key/value store in the last 16KB of flash.
 *
 *      This is a simplified, standalone flashfs that lives entirely within
 *      the secure supervisor.  It shares the same on-disk bitmap/page layout
 *      as the kernel's flashfs so the format is familiar, but it has no VFS
 *      integration and is invisible to the non-secure world.
 *
 *      Partition: 0x081FC000 – 0x081FFFFF  (16 KiB, 2 × 8 KiB sectors)
 *      Accessed via secure alias: 0x0C1FC000 – 0x0C1FFFFF
 *      Page size: 256 bytes → 64 pages total, bitmap in last page → 63 usable
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "include/flash_ops.h"

#if defined(TARGET_STM32H563)

#include "include/stm32h563.h"

#define SECRETS_NS_BASE      0x081FC000U
#define SECRETS_SEC_BASE     (SECRETS_NS_BASE + FLASH_ALIAS_OFFSET)
#define SECRETS_SIZE         0x4000U          /* 16 KiB */
#define SECRETS_PAGES        (SECRETS_SIZE / FLASH_PAGE_SIZE)  /* 64 */
#define SECRETS_USABLE_PAGES (SECRETS_PAGES - 1)               /* 63 */
#define SECRETS_BMP_PAGE     (SECRETS_PAGES - 1)
#define SECRETS_SECTOR_SIZE  FLASH_PAGE_SIZE_BYTES  /* 8 KiB */

/* Reuse the same file header layout as the kernel flashfs. */
struct secrets_file_hdr {
    uint16_t fname_len;
    uint16_t fsize;
};

static void secrets_copy(void *dst, const void *src, size_t len)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (len--)
        *d++ = *s++;
}

static void secrets_memset(void *dst, uint8_t val, size_t len)
{
    uint8_t *d = dst;
    while (len--)
        *d++ = val;
}

static const uint8_t *secrets_page_ptr(uint16_t page)
{
    return (const uint8_t *)(SECRETS_SEC_BASE + (uint32_t)page * FLASH_PAGE_SIZE);
}

static int secrets_read_page(uint16_t page, uint8_t *buf)
{
    if (page >= SECRETS_PAGES)
        return -1;
    secrets_copy(buf, (const void *)secrets_page_ptr(page), FLASH_PAGE_SIZE);
    return 0;
}

static bool secrets_page_needs_erase(uint16_t page, const uint8_t *src)
{
    const uint32_t *existing = (const uint32_t *)secrets_page_ptr(page);
    const uint32_t *incoming = (const uint32_t *)src;
    for (size_t i = 0; i < FLASH_PAGE_SIZE / sizeof(uint32_t); i++) {
        if ((existing[i] & incoming[i]) != incoming[i])
            return true;
    }
    return false;
}

static uint8_t secrets_sector_cache[SECRETS_SECTOR_SIZE];

static int secrets_write_page(uint16_t page, const uint8_t *buf)
{
    uintptr_t dest = SECRETS_SEC_BASE + (uint32_t)page * FLASH_PAGE_SIZE;
    int ret;

    if (page >= SECRETS_PAGES)
        return -1;
    if (stm32_flash_unlock() != 0)
        return -1;

    if (!secrets_page_needs_erase(page, buf)) {
        ret = stm32_flash_program_range(dest, buf, FLASH_PAGE_SIZE);
    } else {
        uintptr_t sector_base = dest & ~(uintptr_t)(SECRETS_SECTOR_SIZE - 1U);
        secrets_copy(secrets_sector_cache, (const void *)sector_base, SECRETS_SECTOR_SIZE);
        secrets_copy(secrets_sector_cache + (dest - sector_base), buf, FLASH_PAGE_SIZE);
        if (stm32_flash_erase_sector(sector_base) != 0) {
            stm32_flash_lock();
            return -1;
        }
        ret = stm32_flash_program_range(sector_base, secrets_sector_cache, SECRETS_SECTOR_SIZE);
    }

    stm32_flash_lock();
    return ret;
}

/* --- Bitmap helpers (same inverted-bit convention as kernel flashfs) --- */

static int secrets_bmp_test(uint16_t page)
{
    const uint8_t *bmp = secrets_page_ptr(SECRETS_BMP_PAGE);
    return !(bmp[page / 8] & (1 << (page & 7)));
}

static int secrets_bmp_set(uint16_t page)
{
    uint8_t bmp[FLASH_PAGE_SIZE];
    secrets_read_page(SECRETS_BMP_PAGE, bmp);
    bmp[page / 8] &= ~(1 << (page & 7));
    return secrets_write_page(SECRETS_BMP_PAGE, bmp);
}

static int secrets_bmp_clear(uint16_t page)
{
    uint8_t bmp[FLASH_PAGE_SIZE];
    secrets_read_page(SECRETS_BMP_PAGE, bmp);
    bmp[page / 8] |= 1 << (page & 7);
    return secrets_write_page(SECRETS_BMP_PAGE, bmp);
}

static int secrets_bmp_find_free(int pages)
{
    int sz = 0;
    int first = -1;
    for (int i = 0; i < (int)SECRETS_USABLE_PAGES; i++) {
        if (!secrets_bmp_test(i)) {
            if (sz++ == 0)
                first = i;
            if (sz == pages)
                return first;
        } else {
            sz = 0;
        }
    }
    return -1;
}

/* --- Format detection / initialisation --- */

static bool secrets_is_formatted(void)
{
    /* A formatted partition has an all-0xFF bitmap page (all pages free)
     * or a bitmap where at least the bitmap page's own bit is set (free). */
    const uint8_t *bmp = secrets_page_ptr(SECRETS_BMP_PAGE);
    /* If the bitmap page is all-0xFF the partition has been formatted
     * (empty). If data pages are allocated, some bits will be 0.
     * An unformatted (erased) flash region is also all-0xFF, so that
     * counts as "formatted and empty" – which is fine. */

    /* Detect the impossible case: bitmap itself marked as allocated.
     * The bitmap page should never be tracked as used. */
    int bmp_bit_byte = SECRETS_BMP_PAGE / 8;
    int bmp_bit_bit  = SECRETS_BMP_PAGE & 7;
    if (!(bmp[bmp_bit_byte] & (1 << bmp_bit_bit))) {
        /* Bitmap page is marked allocated → corrupt or unformatted */
        return false;
    }

    /* Check for clearly non-flash content (e.g. all zeros). */
    int zeros = 0;
    for (int i = 0; i < FLASH_PAGE_SIZE; i++) {
        if (bmp[i] == 0x00)
            zeros++;
    }
    if (zeros == FLASH_PAGE_SIZE)
        return false;

    return true;
}

static int secrets_format(void)
{
    uint8_t page[FLASH_PAGE_SIZE];

    /* Erase both sectors that make up the secrets partition. */
    if (stm32_flash_unlock() != 0)
        return -1;
    if (stm32_flash_erase_sector(SECRETS_SEC_BASE) != 0) {
        stm32_flash_lock();
        return -1;
    }
    if (stm32_flash_erase_sector(SECRETS_SEC_BASE + SECRETS_SECTOR_SIZE) != 0) {
        stm32_flash_lock();
        return -1;
    }
    stm32_flash_lock();

    /* After erase, flash is 0xFF everywhere – that's already a valid
     * empty bitmap (all bits=1 → all pages free). Nothing more to do. */
    (void)page;
    return 0;
}

void secrets_flashfs_init(void)
{
    if (!secrets_is_formatted()) {
        secrets_format();
    }
}

/* --- Public API (secure-world only) --- */

/**
 * secrets_read - read a secret by name.
 * Returns the number of bytes copied into buf, or -1 if not found.
 */
int secrets_read(const char *name, uint8_t *buf, size_t buflen)
{
    uint8_t page[FLASH_PAGE_SIZE];
    if (!name || !buf || buflen == 0)
        return -1;

    for (int p = 0; p < (int)SECRETS_USABLE_PAGES; p++) {
        if (!secrets_bmp_test(p))
            continue;
        secrets_read_page(p, page);
        struct secrets_file_hdr *hdr = (struct secrets_file_hdr *)page;
        if (hdr->fname_len == 0 || hdr->fname_len == 0xFFFF || hdr->fname_len > 120)
            continue;
        char *fname = (char *)page + sizeof(struct secrets_file_hdr);
        if (fname[hdr->fname_len] != '\0')
            continue;

        /* Compare name */
        int match = 1;
        const char *a = name;
        const char *b = fname;
        while (*a && *b) {
            if (*a++ != *b++) { match = 0; break; }
        }
        if (*a != *b) match = 0;
        if (!match)
            continue;

        /* Found – copy payload */
        size_t payload_off = sizeof(struct secrets_file_hdr) + hdr->fname_len + 1;
        size_t avail = FLASH_PAGE_SIZE - payload_off;
        size_t copylen = hdr->fsize;
        if (copylen > avail)
            copylen = avail;
        if (copylen > buflen)
            copylen = buflen;
        secrets_copy(buf, page + payload_off, copylen);
        return (int)copylen;
    }
    return -1;
}

/**
 * secrets_write - write or overwrite a secret.
 * Each secret occupies a single page, so max payload = 256 - header - name - 1.
 * Returns 0 on success, -1 on error.
 */
int secrets_write(const char *name, const uint8_t *data, size_t len)
{
    uint8_t page[FLASH_PAGE_SIZE];
    if (!name || !data)
        return -1;

    /* Compute name length */
    size_t nlen = 0;
    const char *p = name;
    while (*p++) nlen++;

    size_t payload_off = sizeof(struct secrets_file_hdr) + nlen + 1;
    if (payload_off + len > FLASH_PAGE_SIZE)
        return -1;  /* too large for a single page */

    /* Delete existing entry with same name (if any). */
    for (int pg = 0; pg < (int)SECRETS_USABLE_PAGES; pg++) {
        if (!secrets_bmp_test(pg))
            continue;
        secrets_read_page(pg, page);
        struct secrets_file_hdr *hdr = (struct secrets_file_hdr *)page;
        if (hdr->fname_len != (uint16_t)nlen)
            continue;
        char *fname = (char *)page + sizeof(struct secrets_file_hdr);
        int match = 1;
        for (size_t i = 0; i <= nlen; i++) {
            if (fname[i] != name[i]) { match = 0; break; }
        }
        if (match)
            secrets_bmp_clear(pg);
    }

    /* Find a free page. */
    int slot = secrets_bmp_find_free(1);
    if (slot < 0)
        return -1;

    /* Build page. */
    secrets_memset(page, 0xFF, FLASH_PAGE_SIZE);
    struct secrets_file_hdr *hdr = (struct secrets_file_hdr *)page;
    hdr->fname_len = (uint16_t)nlen;
    hdr->fsize = (uint16_t)len;
    secrets_copy(page + sizeof(struct secrets_file_hdr), name, nlen + 1);
    secrets_copy(page + payload_off, data, len);

    if (secrets_write_page(slot, page) != 0)
        return -1;
    if (secrets_bmp_set(slot) != 0)
        return -1;
    return 0;
}

/**
 * secrets_delete - remove a secret by name.
 * Returns 0 on success, -1 if not found.
 */
int secrets_delete(const char *name)
{
    uint8_t page[FLASH_PAGE_SIZE];
    if (!name)
        return -1;

    size_t nlen = 0;
    const char *p = name;
    while (*p++) nlen++;

    for (int pg = 0; pg < (int)SECRETS_USABLE_PAGES; pg++) {
        if (!secrets_bmp_test(pg))
            continue;
        secrets_read_page(pg, page);
        struct secrets_file_hdr *hdr = (struct secrets_file_hdr *)page;
        if (hdr->fname_len != (uint16_t)nlen)
            continue;
        char *fname = (char *)page + sizeof(struct secrets_file_hdr);
        int match = 1;
        for (size_t i = 0; i <= nlen; i++) {
            if (fname[i] != name[i]) { match = 0; break; }
        }
        if (match) {
            secrets_bmp_clear(pg);
            return 0;
        }
    }
    return -1;
}

#else /* !TARGET_STM32H563 */

void secrets_flashfs_init(void) {}
int secrets_read(const char *name, uint8_t *buf, size_t buflen) { (void)name; (void)buf; (void)buflen; return -1; }
int secrets_write(const char *name, const uint8_t *data, size_t len) { (void)name; (void)data; (void)len; return -1; }
int secrets_delete(const char *name) { (void)name; return -1; }

#endif /* TARGET_STM32H563 */
