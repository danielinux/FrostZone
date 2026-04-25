#include "stdint.h"
#include "string.h"
#include "stm32h563.h"

#define TRNG_MAX_REQUEST 4096U

#define TRNG_BASE 0x520C0800
#define TRNG_CR *((volatile uint32_t *)(TRNG_BASE + 0x00))
#define TRNG_SR *((volatile uint32_t *)(TRNG_BASE + 0x04))
#define TRNG_DR *((volatile uint32_t *)(TRNG_BASE + 0x08))

#define TRNG_SR_DRDY (1 << 0)
#define TRNG_CR_RNGEN (1 << 2)
#define TRNG_CR_CONFIG3_SHIFT (8)
#define TRNG_CR_CONFIG2_SHIFT (13)
#define TRNG_CR_CLKDIV_SHIFT (16)
#define TRNG_CR_CONFIG1_SHIFT (20)
#define TRNG_CR_CONDRST (1 << 30)

void trng_init(void)
{
    uint32_t trng_cr;
    RCC_CR |= RCC_CR_HSI48ON;
    while ((RCC_CR & RCC_CR_HSI48RDY) == 0)
        ;
    RCC_AHB2ENR |= RCC_AHB2ENR_TRNGEN;

    trng_cr = TRNG_CR;
    trng_cr &= ~(0x1F << TRNG_CR_CONFIG1_SHIFT);
    trng_cr &= ~(0x7 << TRNG_CR_CLKDIV_SHIFT);
    trng_cr &= ~(0x3 << TRNG_CR_CONFIG2_SHIFT);
    trng_cr &= ~(0x7 << TRNG_CR_CONFIG3_SHIFT);
    trng_cr |= 0x0F << TRNG_CR_CONFIG1_SHIFT;
    trng_cr |= 0x0D << TRNG_CR_CONFIG3_SHIFT;

#ifdef TARGET_stm32u5 /* RM0456 40.6.2 */
    trng_cr |= 0x06 << TRNG_CR_CLKDIV_SHIFT;
#endif
    TRNG_CR = TRNG_CR_CONDRST | trng_cr;
    while ((TRNG_CR & TRNG_CR_CONDRST) == 0)
        ;
    TRNG_CR = trng_cr | TRNG_CR_RNGEN;
    while ((TRNG_SR & TRNG_SR_DRDY) == 0)
        ;
}

int trng_getrandom(unsigned char *out, unsigned len)
{
    unsigned i;
    volatile uint32_t rand_seed = 0;
    uint8_t *rand_byte;


    for (i = 0; i < len; i += 4)
    {
        int j;
        while ((TRNG_SR & TRNG_SR_DRDY) == 0)
            ;
        rand_seed = TRNG_DR;
        rand_byte = (uint8_t *)&rand_seed;
        for (j = 0; (j < 4) && ((i + (unsigned)j) < len); j++)
            out[i + j] = rand_byte[j];
    }
    __asm volatile("" ::: "memory");
    rand_seed = 0;
    __asm volatile("" ::: "memory");
    return 0;
}

static void *ns_ram_rw_range_check(const void *ptr, unsigned len)
{
    uintptr_t start;
    uintptr_t end;

    if ((ptr == NULL) || (len == 0u))
        return NULL;

    start = (uintptr_t)ptr;
    if ((start < SAU_RAM_NS_START) || (start > SAU_RAM_NS_END))
        return NULL;
    if (start > (UINTPTR_MAX - ((uintptr_t)len - 1u)))
        return NULL;
    end = start + (uintptr_t)len - 1u;
    if ((end < SAU_RAM_NS_START) || (end > SAU_RAM_NS_END))
        return NULL;

    return (void *)ptr;
}

__attribute__((cmse_nonsecure_entry))
int secure_getrandom(void *buf, int size)
{
    unsigned char *out;

    if ((size <= 0) || ((unsigned)size > TRNG_MAX_REQUEST))
        return -1;

    out = ns_ram_rw_range_check(buf, (unsigned)size);
    if (!out)
        return -1;

    return trng_getrandom(out, (unsigned)size);
}
