#include "stdint.h"
#include "string.h"
#include "stm32h563.h"

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
        for (j = 0; j < 4; j++) {
            out[i + j] = rand_byte[j];
            if (i + j >= len)
                break;
        }
    }
    return 0;
}


__attribute__((cmse_nonsecure_entry))
int secure_getrandom(void *buf, int size)
{
    return trng_getrandom(buf, size);
}
