#include <stdint.h>

#include "armv8m_tz.h"
#include "task.h"
#include "limits.h"
#include "mempool.h"
#include <stddef.h>
#include <stdbool.h>

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 256
#endif

static void copy_bytes(void *dst, const void *src, size_t len)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (len--) {
        *d++ = *s++;
    }
}

#if defined(TARGET_RP2350)

#include "pico/stdlib.h"
#include "hardware/flash.h"

#define RAM_BASE 0x20000000
#define RAM_MAX  0x20080000

#define FLASH_PART_OFF (0x1F0000U)
#define FLASH_PART_SIZE (0x10000U)
#define FLASH_PART_BASE_ADDR (0x10000000U + FLASH_PART_OFF)
#define ADDR_IN_RAM(x) (((uint32_t)(uintptr_t)(x)) >= RAM_BASE && ((uint32_t)(uintptr_t)(x)) < RAM_MAX)

#define PARTITION_SIZE (1024 * 64)
#define RP_SECTOR_SIZE (4096U)

static uint8_t sector_cache[RP_SECTOR_SIZE];

__attribute__((cmse_nonsecure_entry))
int secure_flash_write_page(uint32_t off, uint8_t *page)
{
    int i;
    if ((off & (FLASH_PAGE_SIZE - 1U)) != 0U)
        return -1;
    if (off >= PARTITION_SIZE)
        return -1;
    if (!ADDR_IN_RAM(page))
        return -1;

    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        volatile uint32_t *orig = ((uint32_t *)(FLASH_PART_BASE_ADDR + off)) + i;
        volatile uint32_t *new = ((uint32_t *)page) + i;
        if ((*orig & *new) != *new) {
            uint32_t sector_n = off / RP_SECTOR_SIZE;
            uint32_t page_n_in_sector = (off & (RP_SECTOR_SIZE - 1U)) / FLASH_PAGE_SIZE;
            uint32_t j;
            copy_bytes(sector_cache, (void *)((uintptr_t)orig & ~(RP_SECTOR_SIZE - 1U)), RP_SECTOR_SIZE);
            flash_range_erase(FLASH_PART_OFF + off, FLASH_PAGE_SIZE);
            for (j = 0; j < page_n_in_sector; j++) {
                flash_range_program(FLASH_PART_OFF + RP_SECTOR_SIZE * sector_n +
                        j * FLASH_PAGE_SIZE, sector_cache + j * FLASH_PAGE_SIZE,
                        FLASH_PAGE_SIZE);
            }
            flash_range_program(FLASH_PART_OFF + off, page, FLASH_PAGE_SIZE);
            for (j = page_n_in_sector + 1U; j < RP_SECTOR_SIZE / FLASH_PAGE_SIZE; j++) {
                flash_range_program(FLASH_PART_OFF + RP_SECTOR_SIZE * sector_n +
                        j * FLASH_PAGE_SIZE, sector_cache + j * FLASH_PAGE_SIZE,
                        FLASH_PAGE_SIZE);
            }
            return 0;
        }
    }
    flash_range_program(FLASH_PART_OFF + off, page, FLASH_PAGE_SIZE);
    return 0;
}

#elif defined(TARGET_STM32H563)

#include "stm32h563.h"

#define STM32_PARTITION_SIZE       (0x00010000U)
#define STM32_PARTITION_NS_BASE    0x081F0000U
#define STM32_PARTITION_SEC_BASE   (STM32_PARTITION_NS_BASE + FLASH_ALIAS_OFFSET)
#define STM32_FLASH_SECTOR_SIZE    FLASH_PAGE_SIZE_BYTES
#define STM32_FLASH_WRITE_GRANULE  8U
#define NS_RAM_BASE                0x20000000U
#define NS_RAM_MAX                 0x200A0000U

#define ADDR_IN_NS_RAM(x) (((uint32_t)(uintptr_t)(x)) >= NS_RAM_BASE && ((uint32_t)(uintptr_t)(x)) < NS_RAM_MAX)
#define FLASH_REG_BASE_SEC    FLASH_BASE
#define FLASH_SR              (*(volatile uint32_t *)(FLASH_REG_BASE_SEC + 0x24U))
#define FLASH_CR              (*(volatile uint32_t *)(FLASH_REG_BASE_SEC + 0x2CU))
#define FLASH_CCR             (*(volatile uint32_t *)(FLASH_REG_BASE_SEC + 0x34U))
#define FLASH_OPTSR_CUR       (*(volatile uint32_t *)(FLASH_REG_BASE_SEC + 0x50U))

#define FLASH_SR_BSY          (1U << 0)
#define FLASH_SR_WBNE         (1U << 1)
#define FLASH_SR_DBNE         (1U << 3)
#define FLASH_SR_EOP          (1U << 16)
#define FLASH_SR_ERR_MASK     ((1U << 17) | (1U << 18) | (1U << 19) | (1U << 20) | (1U << 21) | (1U << 22) | (1U << 23))

#define FLASH_CCR_CLR_BUSY    (1U << 0)
#define FLASH_CCR_CLR_WBNE    (1U << 1)
#define FLASH_CCR_CLR_DBNE    (1U << 3)
#define FLASH_CCR_CLR_EOP     (1U << 16)
#define FLASH_CCR_CLR_ERR_MASK ((1U << 17) | (1U << 18) | (1U << 19) | (1U << 20) | (1U << 21) | (1U << 22))

#define FLASH_CR_LOCK         (1U << 0)
#define FLASH_CR_PG           (1U << 1)
#define FLASH_CR_SER          (1U << 2)
#define FLASH_CR_BER          (1U << 3)
#define FLASH_CR_STRT         (1U << 5)
#define FLASH_CR_PNB_SHIFT    6U
#define FLASH_CR_PNB_MASK     0x7FU
#define FLASH_CR_MER          (1U << 15)
#define FLASH_CR_BKSEL        (1U << 31)

static uint8_t sector_cache[STM32_FLASH_SECTOR_SIZE];
#define SCB_CCR_DC_BIT             (1U << 16)
#define SCB_CCR                    (*(volatile uint32_t *)0xE000ED14U)
#define SCB_CCSIDR                 (*(volatile uint32_t *)0xE000ED80U)
#define SCB_CSSELR                 (*(volatile uint32_t *)0xE000ED84U)
#define SCB_DCCIMVAC               (*(volatile uint32_t *)0xE000EF70U)

static void stm32_flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    uint32_t wrhighfreq = 1U;

    if ((reg & FLASH_ACR_LATENCY_MASK) < waitstates) {
        reg &= ~(FLASH_ACR_LATENCY_MASK |
                 (FLASH_ACR_WRHIGHFREQ_MASK << FLASH_ACR_WRHIGHFREQ_SHIFT));
        if (waitstates > 3U)
            wrhighfreq = 2U;
        reg |= (waitstates | (wrhighfreq << FLASH_ACR_WRHIGHFREQ_SHIFT));
        FLASH_ACR = reg;
        __asm volatile("isb");
        __asm volatile("dmb");
        while (FLASH_ACR != reg)
            ;
    }
}

static void stm32_dcache_clean_invalidate_range(uintptr_t addr, size_t len)
{
    if ((SCB_CCR & SCB_CCR_DC_BIT) == 0U)
        return;

    SCB_CSSELR = 0U; /* Level 1 data cache */
    __asm volatile ("dsb 0xF" ::: "memory");
    uint32_t ccsidr = SCB_CCSIDR;
    uint32_t line_len = 4U << (ccsidr & 7U);
    uintptr_t start = addr & ~(uintptr_t)(line_len - 1U);
    uintptr_t end = addr + len;

    while (start < end) {
        SCB_DCCIMVAC = start;
        start += line_len;
    }
    __asm volatile ("dsb 0xF" ::: "memory");
    __asm volatile ("isb 0xF" ::: "memory");
}

static void stm32_flush_aliases(uintptr_t sec_addr, size_t len)
{
    stm32_dcache_clean_invalidate_range(sec_addr, len);
    stm32_dcache_clean_invalidate_range(sec_addr - FLASH_ALIAS_OFFSET, len);
}

static inline uint32_t stm32_sec_to_ns(uintptr_t addr);

static void stm32_set_region_security(uintptr_t sec_addr, size_t len, int secure)
{
    uint32_t ns_start = stm32_sec_to_ns(sec_addr);
    uint32_t ns_end = stm32_sec_to_ns(sec_addr + len - 1U);
    ns_start &= ~(FLASH_PAGE_SIZE_BYTES - 1U);
    ns_end = (ns_end & ~(FLASH_PAGE_SIZE_BYTES - 1U)) + FLASH_PAGE_SIZE_BYTES;

    for (uint32_t ns = ns_start; ns < ns_end; ns += FLASH_PAGE_SIZE_BYTES) {
        uint32_t bank = (ns >= (FLASH_NS_BASE + FLASH_BANK_SIZE)) ? 1U : 0U;
        uint32_t base = bank ? (FLASH_NS_BASE + FLASH_BANK_SIZE) : FLASH_NS_BASE;
        uint32_t page_idx = (ns - base) / FLASH_PAGE_SIZE_BYTES;
        uint32_t reg_idx = page_idx / 32U;
        uint32_t bit = 1U << (page_idx % 32U);
        uintptr_t reg_base = bank ? (FLASH_BASE + 0x1A0U) : (FLASH_BASE + 0x0A0U);
        volatile uint32_t *reg = (volatile uint32_t *)(reg_base + reg_idx * 4U);
        if (secure)
            *reg |= bit;
        else
            *reg &= ~bit;
    }
}

static inline uintptr_t stm32_partition_end(void)
{
    return STM32_PARTITION_SEC_BASE + STM32_PARTITION_SIZE;
}

static inline uint32_t stm32_sec_to_ns(uintptr_t addr)
{
    return (uint32_t)(addr - FLASH_ALIAS_OFFSET);
}

static int stm32_flash_unlock(void)
{
    if ((FLASH_CR & FLASH_CR_LOCK) == 0U)
        return 0;
    stm32_flash_set_waitstates(5U);
    FLASH_KEYR = FLASH_KEY1;
    FLASH_KEYR = FLASH_KEY2;
    return (FLASH_CR & FLASH_CR_LOCK) ? -1 : 0;
}

static void stm32_flash_lock(void)
{
    FLASH_CR |= FLASH_CR_LOCK;
}

static void stm32_flash_clear_errors(void)
{
    FLASH_CCR = FLASH_CCR_CLR_ERR_MASK | FLASH_CCR_CLR_EOP | FLASH_CCR_CLR_DBNE | FLASH_CCR_CLR_WBNE | FLASH_CCR_CLR_BUSY;
}

static void stm32_flash_wait_buffer_empty(void)
{
    while (FLASH_SR & (FLASH_SR_WBNE | FLASH_SR_DBNE)) {
    }
}

static int stm32_flash_wait_ready(void)
{
    while (FLASH_SR & FLASH_SR_BSY) {
    }
    if (FLASH_SR & FLASH_SR_ERR_MASK) {
        stm32_flash_clear_errors();
        return -1;
    }
    if (FLASH_SR & FLASH_SR_EOP) {
        FLASH_CCR = FLASH_CCR_CLR_EOP;
    }
    return 0;
}

static int stm32_verify_range(const uint8_t *dst, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (dst[i] != src[i])
            return -1;
    }
    return 0;
}

static int stm32_flash_program_range(uintptr_t dst, const uint8_t *src, size_t len)
{
    size_t offset = 0;
    uint32_t ns_addr = stm32_sec_to_ns(dst);
    (void)ns_addr;

    stm32_flash_clear_errors();
    stm32_flash_wait_buffer_empty();
    while (offset < len) {
        uint32_t lo = 0xffffffffu;
        uint32_t hi = 0xffffffffu;
        size_t remaining = len - offset;
        size_t chunk = remaining < sizeof(lo) ? remaining : sizeof(lo);
        copy_bytes(&lo, src + offset, chunk);
        remaining = (remaining > sizeof(lo)) ? (remaining - sizeof(lo)) : 0U;
        chunk = remaining < sizeof(hi) ? remaining : sizeof(hi);
        if (chunk > 0)
            copy_bytes(&hi, src + offset + sizeof(lo), chunk);
        if (stm32_flash_wait_ready() != 0)
            return -1;
        FLASH_CR |= FLASH_CR_PG;

        *(volatile uint32_t *)(dst + offset) = lo;
        __asm volatile("isb");
        *(volatile uint32_t *)(dst + offset + 4U) = hi;

        if (stm32_flash_wait_ready() != 0) {
            FLASH_CR &= ~FLASH_CR_PG;
            return -1;
        }
        FLASH_CCR = FLASH_CCR_CLR_EOP;
        FLASH_CR &= ~FLASH_CR_PG;
        offset += sizeof(uint64_t);
    }
    return 0;
}

static int stm32_flash_erase_sector(uintptr_t sector_addr)
{
    uint32_t ns_addr = stm32_sec_to_ns(sector_addr);
    uint32_t bank_base = FLASH_NS_BASE;
    uint32_t bank_sel = 0U;
    if (ns_addr >= FLASH_NS_BASE + FLASH_BANK_SIZE) {
        bank_base = FLASH_NS_BASE + FLASH_BANK_SIZE;
        bank_sel = FLASH_CR_BKSEL;
    }
    uint32_t sector = (ns_addr - bank_base) / STM32_FLASH_SECTOR_SIZE;

    if (stm32_flash_wait_ready() != 0)
        return -1;

    stm32_flash_clear_errors();
    stm32_flash_wait_buffer_empty();

    if (FLASH_OPTSR_CUR & (1U << 31))
        bank_sel ^= FLASH_CR_BKSEL;

    uint32_t cr = FLASH_CR & ~(FLASH_CR_SER | FLASH_CR_BER | FLASH_CR_PG | FLASH_CR_MER |
                               (FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) | FLASH_CR_BKSEL);
    cr |= FLASH_CR_SER | (sector << FLASH_CR_PNB_SHIFT) | bank_sel;
    FLASH_CR = cr;
    FLASH_CR = cr | FLASH_CR_STRT;

    if (stm32_flash_wait_ready() != 0) {
        FLASH_CR &= ~FLASH_CR_SER;
        return -1;
    }
    FLASH_CCR = FLASH_CCR_CLR_EOP;
    FLASH_CR &= ~FLASH_CR_SER;
    return 0;
}

static bool stm32_page_needs_erase(uintptr_t dst, const uint8_t *src)
{
    const uint32_t *existing = (const uint32_t *)dst;
    const uint32_t *incoming = (const uint32_t *)src;
    size_t words = FLASH_PAGE_SIZE / sizeof(uint32_t);
    for (size_t i = 0; i < words; i++) {
        if ((existing[i] & incoming[i]) != incoming[i])
            return true;
    }
    return false;
}

__attribute__((cmse_nonsecure_entry))
int secure_flash_write_page(uint32_t off, uint8_t *page)
{
    uintptr_t dest = STM32_PARTITION_SEC_BASE + off;
    uintptr_t claim_base = dest & ~(uintptr_t)(FLASH_PAGE_SIZE - 1U);
    size_t claim_len = FLASH_PAGE_SIZE;
    int claimed = 0;
    int ret = 0;
    if ((off & (FLASH_PAGE_SIZE - 1U)) != 0U)
        return -1;
    if (dest + FLASH_PAGE_SIZE > stm32_partition_end())
        return -1;
    if (!ADDR_IN_NS_RAM(page))
        return -1;
    if (stm32_flash_unlock() != 0)
        return -1;


    if (!stm32_page_needs_erase(dest, page)) {
        stm32_set_region_security(claim_base, claim_len, 1);
        claimed = 1;
        ret = stm32_flash_program_range(dest, page, FLASH_PAGE_SIZE);
        if (ret == 0) {
            stm32_flush_aliases(dest, FLASH_PAGE_SIZE);
        }
    } else {
        uintptr_t sector_base = dest & ~(uintptr_t)(STM32_FLASH_SECTOR_SIZE - 1U);
        claim_base = sector_base;
        claim_len = STM32_FLASH_SECTOR_SIZE;
        stm32_set_region_security(claim_base, claim_len, 1);
        claimed = 1;
        copy_bytes(sector_cache, (const void *)sector_base, STM32_FLASH_SECTOR_SIZE);
        copy_bytes(sector_cache + (dest - sector_base), page, FLASH_PAGE_SIZE);
        if (stm32_flash_erase_sector(sector_base) != 0) {
            ret = -1;
        } else {
            ret = stm32_flash_program_range(sector_base, sector_cache, STM32_FLASH_SECTOR_SIZE);
            if (ret == 0) {
                stm32_flush_aliases(sector_base, STM32_FLASH_SECTOR_SIZE);
            }
        }
    }

    stm32_flash_lock();
    if (claimed)
        stm32_set_region_security(claim_base, claim_len, 0);
    return ret;
}

#else

__attribute__((cmse_nonsecure_entry))
int secure_flash_write_page(uint32_t off, uint8_t *page)
{
    (void)off;
    (void)page;
    return -1;
}

#endif
