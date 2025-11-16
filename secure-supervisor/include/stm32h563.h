#ifndef STM32H563_H

#if defined(TARGET_STM32H563)
#define GTZC1_BASE             (0x50032400U)
#define STM32H563_RCC_BASE     (0x54020C00U)
#define RCC_CR                 (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x00U))
#define RCC_SECCFGR            (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x110U))
#define RCC_PRIVCFGR           (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x114U))
#define RCC_AHB2ENR            (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x08CU))
#define RCC_APB1LRSTR          (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x074U))
#define RCC_APB1LENR           (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x09CU))
#define RCC_APB2ENR            (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x0A4U))
#define RCC_AHB2ENR_GPIOAEN    (1U << 0)
#define RCC_AHB2ENR_GPIOBEN    (1U << 1)
#define RCC_AHB2ENR_GPIOCEN    (1U << 2)
#define RCC_AHB2ENR_GPIODEN    (1U << 3)
#define RCC_AHB2ENR_GPIOEEN    (1U << 4)
#define RCC_AHB2ENR_GPIOFEN    (1U << 5)
#define RCC_AHB2ENR_GPIOGEN    (1U << 6)
#define RCC_AHB2ENR_GPIOHEN    (1U << 7)
#define RCC_AHB2ENR_TRNGEN    (1U << 18)
#define RCC_AHB2ENR_PKAEN     (1U << 19)
#define RCC_AHB2ENR_SAESEN    (1U << 20)
#define RCC_AHB2ENR_SRAM2EN   (1U << 30)
#define RCC_AHB2ENR_SRAM3EN   (1U << 31)


#define RCC_CR_HSI48RDY             (1 << 13)
#define RCC_CR_HSI48ON              (1 << 12)

#define RCC_APB1LENR_USART3EN  (1U << 18)
#define RCC_APB2ENR_SPI1EN     (1U << 12)
#define GTZC1_TZSC             (*(volatile uint32_t *)(GTZC1_BASE + 0x00U))
#define GTZC1_TZSC_SECCFGR1    *(volatile uint32_t *)(GTZC1_BASE + 0x010U)
#define GTZC1_TZSC_SECCFGR2    *(volatile uint32_t *)(GTZC1_BASE + 0x014U)
#define GTZC1_TZSC_SECCFGR3    *(volatile uint32_t *)(GTZC1_BASE + 0x018U)
#define GTZC1_TZSC_PRIVCFGR1   *(volatile uint32_t *)(GTZC1_BASE + 0x020U)
#define GTZC1_TZSC_PRIVCFGR2   *(volatile uint32_t *)(GTZC1_BASE + 0x024U)
#define GTZC1_TZSC_PRIVCFGR3   *(volatile uint32_t *)(GTZC1_BASE + 0x028U)
#define GTZC1_TZIC_IER1        (*(volatile uint32_t *)(GTZC1_BASE + 0x0400U))

#define GTZC1_MPCBB1_SECCFGR   ((volatile uint32_t *)(GTZC1_BASE + 0x0800U + 0x100U))
#define GTZC1_MPCBB2_SECCFGR   ((volatile uint32_t *)(GTZC1_BASE + 0x0C00U + 0x100U))
#define GTZC1_MPCBB3_SECCFGR   ((volatile uint32_t *)(GTZC1_BASE + 0x1000U + 0x100U))
#define GTZC1_MPCBB1_PRIVCFGR  ((volatile uint32_t *)(GTZC1_BASE + 0x0800U + 0x200U))
#define GTZC1_MPCBB2_PRIVCFGR  ((volatile uint32_t *)(GTZC1_BASE + 0x0C00U + 0x200U))
#define GTZC1_MPCBB3_PRIVCFGR  ((volatile uint32_t *)(GTZC1_BASE + 0x1000U + 0x200U))

#define SET_GTZC1_MPCBBx_SECCFGR_VCTR(bank, n, val) \
    (*((volatile uint32_t *)(GTZC1_MPCBB##bank##_SECCFGR) + (n)) = (val))
#define SET_GTZC1_MPCBBx_PRIVCFGR_VCTR(bank, n, val) \
    (*((volatile uint32_t *)(GTZC1_MPCBB##bank##_PRIVCFGR) + (n)) = (val))



#define FLASH_BASE             (0x50022000U)
#define FLASH_NSKEYR           (*(volatile uint32_t *)(FLASH_BASE + 0x004U))
#define FLASH_SECKEYR          (*(volatile uint32_t *)(FLASH_BASE + 0x008U))
#define FLASH_KEYR             FLASH_SECKEYR
#define FLASH_ACR              (*(volatile uint32_t *)(FLASH_BASE + 0x000U))
#define FLASH_NSSR             (*(volatile uint32_t *)(FLASH_BASE + 0x020U))
#define FLASH_SECSR            (*(volatile uint32_t *)(FLASH_BASE + 0x024U))
#define FLASH_NSCR             (*(volatile uint32_t *)(FLASH_BASE + 0x028U))
#define FLASH_SECCR            (*(volatile uint32_t *)(FLASH_BASE + 0x02CU))
#define FLASH_NSCCR            (*(volatile uint32_t *)(FLASH_BASE + 0x030U))
#define FLASH_SECCCR           (*(volatile uint32_t *)(FLASH_BASE + 0x034U))
#define FLASH_SECBB1R1         (*(volatile uint32_t *)(FLASH_BASE + 0x0A0U))
#define FLASH_SECBB1R2         (*(volatile uint32_t *)(FLASH_BASE + 0x0A4U))
#define FLASH_SECBB1R3         (*(volatile uint32_t *)(FLASH_BASE + 0x0A8U))
#define FLASH_SECBB1R4         (*(volatile uint32_t *)(FLASH_BASE + 0x0ACU))
#define FLASH_SECBB2R1         (*(volatile uint32_t *)(FLASH_BASE + 0x1A0U))
#define FLASH_SECBB2R2         (*(volatile uint32_t *)(FLASH_BASE + 0x1A4U))
#define FLASH_SECBB2R3         (*(volatile uint32_t *)(FLASH_BASE + 0x1A8U))
#define FLASH_SECBB2R4         (*(volatile uint32_t *)(FLASH_BASE + 0x1ACU))
#define FLASH_PRIVBB1R1        (*(volatile uint32_t *)(FLASH_BASE + 0x0C0U))
#define FLASH_PRIVBB1R2        (*(volatile uint32_t *)(FLASH_BASE + 0x0C4U))
#define FLASH_PRIVBB1R3        (*(volatile uint32_t *)(FLASH_BASE + 0x0C8U))
#define FLASH_PRIVBB1R4        (*(volatile uint32_t *)(FLASH_BASE + 0x0CCU))
#define FLASH_PRIVBB2R1        (*(volatile uint32_t *)(FLASH_BASE + 0x1C0U))
#define FLASH_PRIVBB2R2        (*(volatile uint32_t *)(FLASH_BASE + 0x1C4U))
#define FLASH_PRIVBB2R3        (*(volatile uint32_t *)(FLASH_BASE + 0x1C8U))
#define FLASH_PRIVBB2R4        (*(volatile uint32_t *)(FLASH_BASE + 0x1CCU))
#define FLASH_PRIVCFGR         (*(volatile uint32_t *)(FLASH_BASE + 0x03CU))
#define FLASH_PRIVCFGR_SPRIV   (1U << 0)
#define FLASH_PRIVCFGR_NSPRIV  (1U << 1)


#define FLASH_NSCR_LOCK        (1U << 0)
#define FLASH_NSCR_PG          (1U << 1)
#define FLASH_NSCR_SER         (1U << 2)
#define FLASH_NSCR_STRT        (1U << 5)
#define FLASH_NSCR_SNB_SHIFT   6U
#define FLASH_NSCR_SNB_MASK    (0x7FU << FLASH_NSCR_SNB_SHIFT)
#define FLASH_NSCR_MER         (1U << 15)
#define FLASH_NSCR_BKSEL       (1U << 31)
#define FLASH_NSSR_BSY         (1U << 0)
#define FLASH_NSSR_EOP         (1U << 16)
#define FLASH_NSSR_WRPERR      (1U << 17)
#define FLASH_NSSR_PGSERR      (1U << 18)
#define FLASH_NSSR_STRBERR     (1U << 19)
#define FLASH_NSSR_INCERR      (1U << 20)
#define FLASH_NSSR_OBKERR      (1U << 21)
#define FLASH_NSSR_OBKWERR     (1U << 22)
#define FLASH_NSSR_OPTCHANGEERR (1U << 23)
#define FLASH_NSCCR_CLR_EOP    (1U << 16)
#define FLASH_NSCCR_CLR_WRPERR (1U << 17)
#define FLASH_NSCCR_CLR_PGSERR (1U << 18)
#define FLASH_NSCCR_CLR_STRBERR (1U << 19)
#define FLASH_NSCCR_CLR_INCERR (1U << 20)
#define FLASH_NSCCR_CLR_OBKERR (1U << 21)
#define FLASH_NSCCR_CLR_OBKWERR (1U << 22)
#define FLASH_NSCCR_CLR_OPTCHANGEERR (1U << 23)
#define FLASH_SECCR_LOCK       (1U << 0)
#define FLASH_SECCR_PG         (1U << 1)
#define FLASH_SECCR_SER        (1U << 2)
#define FLASH_SECCR_STRT       (1U << 5)
#define FLASH_SECCR_SNB_SHIFT  6U
#define FLASH_SECCR_SNB_MASK   (0x7FU << FLASH_SECCR_SNB_SHIFT)
#define FLASH_SECCR_MER        (1U << 15)
#define FLASH_SECCR_BKSEL      (1U << 31)
#define FLASH_SECSR_BSY        (1U << 0)
#define FLASH_SECSR_EOP        (1U << 16)
#define FLASH_SECSR_WRPERR     (1U << 17)
#define FLASH_SECSR_PGSERR     (1U << 18)
#define FLASH_SECSR_STRBERR    (1U << 19)
#define FLASH_SECSR_INCERR     (1U << 20)
#define FLASH_SECSR_OBKERR     (1U << 21)
#define FLASH_SECSR_OBKWERR    (1U << 22)
#define FLASH_SECSR_OPTCHANGEERR (1U << 23)
#define FLASH_SECCCR_CLR_EOP   (1U << 16)
#define FLASH_SECCCR_CLR_WRPERR (1U << 17)
#define FLASH_SECCCR_CLR_PGSERR (1U << 18)
#define FLASH_SECCCR_CLR_STRBERR (1U << 19)
#define FLASH_SECCCR_CLR_INCERR (1U << 20)
#define FLASH_SECCCR_CLR_OBKERR (1U << 21)
#define FLASH_SECCCR_CLR_OBKWERR (1U << 22)
#define FLASH_SECCCR_CLR_OPTCHANGEERR (1U << 23)
#define FLASH_SECCCR_CLR_ERR_MASK   (FLASH_SECCCR_CLR_WRPERR | FLASH_SECCCR_CLR_PGSERR | \
                                     FLASH_SECCCR_CLR_STRBERR | FLASH_SECCCR_CLR_INCERR | \
                                     FLASH_SECCCR_CLR_OBKERR | FLASH_SECCCR_CLR_OBKWERR | \
                                     FLASH_SECCCR_CLR_OPTCHANGEERR)
#define FLASH_NS_BASE          0x08000000U
#define FLASH_SEC_ALIAS_BASE   0x0C000000U
#define FLASH_ALIAS_OFFSET     (FLASH_SEC_ALIAS_BASE - FLASH_NS_BASE)
#define FLASH_BANK_SIZE        0x00100000U
#define FLASH_PAGE_SIZE_BYTES  0x2000U
#define FLASH_ACR_LATENCY_MASK  0xFU
#define FLASH_ACR_WRHIGHFREQ_SHIFT 4U
#define FLASH_ACR_WRHIGHFREQ_MASK  0x3U
#define FLASH_KEY1             0x45670123U
#define FLASH_KEY2             0xCDEF89ABU

#define SRAM_BLOCK_SIZE             512U
#define SRAM_BLOCKS_PER_WORD        32U
#define SRAM_WORD_SPAN_BYTES        (SRAM_BLOCK_SIZE * SRAM_BLOCKS_PER_WORD)

#define SRAM1_SIZE_BYTES            (256U * 1024U)
#define SRAM2_SIZE_BYTES            (64U * 1024U)
#define SRAM3_SIZE_BYTES            (320U * 1024U)

#define SRAM1_SECURE_BYTES          (64U * 1024U)
#define SRAM1_SECURE_WORDS          (SRAM1_SECURE_BYTES / SRAM_WORD_SPAN_BYTES)

#define SRAM1_WORD_COUNT            (SRAM1_SIZE_BYTES / SRAM_WORD_SPAN_BYTES)
#define SRAM2_WORD_COUNT            (SRAM2_SIZE_BYTES / SRAM_WORD_SPAN_BYTES)
#define SRAM3_WORD_COUNT            (SRAM3_SIZE_BYTES / SRAM_WORD_SPAN_BYTES)

#define MPCBB_DESCRIPTOR_WORDS      32U

#define GPIOA_BASE               0x52020000U
#define GPIOB_BASE               0x52020400U
#define GPIOC_BASE               0x52020800U
#define GPIOD_BASE               0x52020C00U
#define GPIOE_BASE               0x52021000U
#define GPIOF_BASE               0x52021400U
#define GPIOG_BASE               0x52021800U
#define GPIOH_BASE               0x52021C00U
#define GPIOI_BASE               0x52022000U


/* GPIO secure configuration registers (offset 0x30 from GPIOx base) */
#define GPIO_SECCFGR_OFFSET         0x30U


#if defined(TARGET_STM32H563) || defined(TARGET_STM32U585)
/* Secure aliases (0x0C... / 0x300...) */
#define SAU_FLASH_SECURE_START   0x0C000000U
#define SAU_FLASH_SECURE_END     0x0C007FFFU
#define SAU_FLASH_NSC_START      0x0C008000U
#define SAU_FLASH_NSC_END        0x0C00FFFFU
#define SAU_RAM_SECURE_START     0x30000000U
#define SAU_RAM_SECURE_END       0x3000FFFFU

/* Non-secure aliases (0x08... / 0x200...) */
#define SAU_FLASH_NS_START       0x08010000U
#define SAU_FLASH_NS_END         0x081FFFFFU
#define SAU_RAM_NS_START         0x20010000U
#define SAU_RAM_NS_END           0x2009FFFFU
#define SAU_PERIPH_START         0x40000000U
#define SAU_PERIPH_END           0x4FFFFFFFU
#define NS_START_ADDR            SAU_FLASH_NS_START
#elif defined(TARGET_RP2350)
#define SAU_FLASH_SECURE_START   0x10000000U
#define SAU_FLASH_SECURE_END     0x10007FFFU
#define SAU_FLASH_NSC_START      0x10008000U
#define SAU_FLASH_NSC_END        0x1000FFFFU
#define SAU_FLASH_NS_START       0x10010000U
#define SAU_FLASH_NS_END         0x1001FFFFU
#define SAU_FLASH_XIP_START      0x10020000U
#define SAU_FLASH_XIP_END        0x101FFFFFU
#define SAU_RAM_SECURE_START     0x20000000U
#define SAU_RAM_SECURE_END       0x20003FFFU
#define SAU_RAM_NS_START         0x20010000U
#define SAU_RAM_NS_END           0x2007FFFFU
#define SAU_USB_RAM_START        0x50100000U
#define SAU_USB_RAM_END          0x5011FFFFU
#define NS_START_ADDR            SAU_FLASH_NS_START
#else
#error Error TARGET not supported
#endif

#define SCB_VTOR_NS       (*(volatile uint32_t *)0xE002ED08)
#define SCB_VTOR          (*(volatile uint32_t *)0xE000ED08)
#define SCB_AIRCR         (*(volatile uint32_t *)0xE000ED0C)
#define AIRCR_BFHFNMINS   (1 << 13)

#define NVIC_ITNS ((volatile uint32_t *)(0xE000E380))

#define NSACR (*(volatile uint32_t *)(0xE000ED8C))
#define CPACR (*(volatile uint32_t *)(0xE000ED88))

static inline void gpio_mark_non_secure(uint32_t gpio_base, uint32_t pin_mask)
{
    volatile uint32_t *seccfgr = (volatile uint32_t *)(gpio_base + GPIO_SECCFGR_OFFSET);
    *seccfgr &= ~pin_mask;
}

static inline void stm32h5_gtzc_setup(void)
{
    uint32_t i;

    /* SRAM1: retain the first 64 KiB for secure use. */
    for (i = 0; i < SRAM1_SECURE_WORDS; i++) {
        SET_GTZC1_MPCBBx_SECCFGR_VCTR(1, i, 0xFFFFFFFFU);
    }
    for (; i < SRAM1_WORD_COUNT; i++) {
        SET_GTZC1_MPCBBx_SECCFGR_VCTR(1, i, 0x00000000U);
    }
    for (; i < MPCBB_DESCRIPTOR_WORDS; i++) {
        SET_GTZC1_MPCBBx_SECCFGR_VCTR(1, i, 0x00000000U);
    }

    /* SRAM1 privilege: keep the secure window privileged, allow unprivileged for the rest. */
    i = 0;
    for (; i < SRAM1_SECURE_WORDS; i++) {
        SET_GTZC1_MPCBBx_PRIVCFGR_VCTR(1, i, 0xFFFFFFFFU);
    }
    for (; i < SRAM1_WORD_COUNT; i++) {
        SET_GTZC1_MPCBBx_PRIVCFGR_VCTR(1, i, 0x00000000U);
    }
    for (; i < MPCBB_DESCRIPTOR_WORDS; i++) {
        SET_GTZC1_MPCBBx_PRIVCFGR_VCTR(1, i, 0x00000000U);
    }

    /* SRAM2: dedicate fully to the non-secure side. */
    for (i = 0; i < SRAM2_WORD_COUNT; i++) {
        SET_GTZC1_MPCBBx_SECCFGR_VCTR(2, i, 0x00000000U);
    }
    for (; i < MPCBB_DESCRIPTOR_WORDS; i++) {
        SET_GTZC1_MPCBBx_SECCFGR_VCTR(2, i, 0x00000000U);
    }

    /* SRAM2 privilege: fully accessible to unprivileged tasks. */
    i = 0;
    for (; i < SRAM2_WORD_COUNT; i++) {
        SET_GTZC1_MPCBBx_PRIVCFGR_VCTR(2, i, 0x00000000U);
    }
    for (; i < MPCBB_DESCRIPTOR_WORDS; i++) {
        SET_GTZC1_MPCBBx_PRIVCFGR_VCTR(2, i, 0x00000000U);
    }

    /* SRAM3: expose entire bank as non-secure (kernel stacks, mempool, etc.). */
    for (i = 0; i < SRAM3_WORD_COUNT; i++) {
        SET_GTZC1_MPCBBx_SECCFGR_VCTR(3, i, 0x00000000U);
    }
    for (; i < MPCBB_DESCRIPTOR_WORDS; i++) {
        SET_GTZC1_MPCBBx_SECCFGR_VCTR(3, i, 0x00000000U);
    }

    /* SRAM3 privilege: allow user stacks/mempool to be unprivileged. */
    i = 0;
    for (; i < SRAM3_WORD_COUNT; i++) {
        SET_GTZC1_MPCBBx_PRIVCFGR_VCTR(3, i, 0x00000000U);
    }
    for (; i < MPCBB_DESCRIPTOR_WORDS; i++) {
        SET_GTZC1_MPCBBx_PRIVCFGR_VCTR(3, i, 0x00000000U);
    }

    /* Program flash block security: lower 64 KiB secure, rest non-secure. */
    FLASH_SECKEYR = FLASH_KEY1;
    FLASH_SECKEYR = FLASH_KEY2;
    //FLASH_SECBB1R1 = 0x000000FFU;
    FLASH_SECBB1R1 = 0x00000000U;
    FLASH_SECBB1R2 = 0x00000000U;
    FLASH_SECBB1R3 = 0x00000000U;
    FLASH_SECBB1R4 = 0x00000000U;
    FLASH_SECBB2R1 = 0x00000000U;
    FLASH_SECBB2R2 = 0x00000000U;
    FLASH_SECBB2R3 = 0x00000000U;
    FLASH_SECBB2R4 = 0x00000000U;

    /* Privileged flash from offset 0x0000 0000 to 0x0002 FFFF */
    FLASH_PRIVBB1R1 = 0x00000000U;
    FLASH_PRIVBB1R2 = 0x00000000U;
    FLASH_PRIVBB1R3 = 0x00000000U;
    FLASH_PRIVBB1R4 = 0x00000000U;
    FLASH_PRIVBB2R1 = 0x00000000U;
    FLASH_PRIVBB2R2 = 0x00000000U;
    FLASH_PRIVBB2R3 = 0x00000000U;
    FLASH_PRIVBB2R4 = 0x00000000U;


    FLASH_SECCR |= FLASH_SECCR_LOCK;

    /* Expose RCC to non-secure world but require privileged access. */
    RCC_SECCFGR = 0x00000000U;
    //RCC_PRIVCFGR = 0xFFFFFFFFU; /* Require privileged access to RCC control paths. */

    /* Pre-enable GPIO and USART3 clocks for the non-secure domain. */
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN | RCC_AHB2ENR_GPIODEN | RCC_AHB2ENR_GPIOFEN | RCC_AHB2ENR_GPIOGEN;
    RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC_APB1LRSTR |= RCC_APB1LENR_USART3EN;
    RCC_APB1LRSTR &= ~RCC_APB1LENR_USART3EN;
    RCC_APB1LENR |= RCC_APB1LENR_USART3EN;
    gpio_mark_non_secure(GPIOD_BASE, (1U << 8) | (1U << 9));


    FLASH_PRIVCFGR &= ~(FLASH_PRIVCFGR_NSPRIV); /* Allow NS access to flash */
    GTZC1_TZSC_SECCFGR1 &= ~(1U << 14); /* Enable non-secure access to USART3 */
    GTZC1_TZSC_SECCFGR2 &= ~(1U << 9);  /* Enable non-secure access to SPI1 */
    GTZC1_TZSC_SECCFGR3 &= ~(1U << 11); /* Enable non-secure access to MAC */

    /* Mark all peripherals as privileged-only. The frosted kernel runs privileged. */
#if 0
    GTZC1_TZSC_PRIVCFGR1 = 0xFFFFFFFFU;
    GTZC1_TZSC_PRIVCFGR2 = 0xFFFFFFFFU;
    GTZC1_TZSC_PRIVCFGR3 = 0xFFFFFFFFU;
#endif


    /* Set all interrupts to non-secure */
    NVIC_ITNS[0] = 0xFFFFFFFF;
    NVIC_ITNS[1] = 0xFFFFFFFF;
    NVIC_ITNS[2] = 0xFFFFFFFF;


}

static inline void stm32h5_configure_gpio_security(void)
{
    /* LED pins: PB0 (green), PG4 (red), PF4 (orange) */
    gpio_mark_non_secure(GPIOB_BASE, (1U << 0));
    gpio_mark_non_secure(GPIOG_BASE, (1U << 4));
    gpio_mark_non_secure(GPIOF_BASE, (1U << 4));

    /* User button on GPIOC: PC13 */
    gpio_mark_non_secure(GPIOC_BASE, (1U << 13));

    /* USB FS DM/DP on PA11, PA12 */
    gpio_mark_non_secure(GPIOA_BASE, (1U << 11) | (1U << 12));

    /* USB Type-C sideband signals: CC1/CC2, fault, dead-battery, and VBUS sense */
    gpio_mark_non_secure(GPIOB_BASE, (1U << 13) | (1U << 14));
    gpio_mark_non_secure(GPIOG_BASE, (1U << 7));
    gpio_mark_non_secure(GPIOA_BASE, (1U << 9) | (1U << 4));

    /* SPI1 / LCD interface */
    gpio_mark_non_secure(GPIOA_BASE, (1U << 5));           /* SCK */
    gpio_mark_non_secure(GPIOB_BASE, (1U << 5) | (1U << 3)); /* MOSI + backlight */
    gpio_mark_non_secure(GPIOD_BASE, (1U << 14) | (1U << 15)); /* CS + RST */
    gpio_mark_non_secure(GPIOF_BASE, (1U << 3));           /* D/C */

    /* USART3 on PD8, PD9 */
    gpio_mark_non_secure(GPIOD_BASE, (1U << 4));
}
#endif // TARGET_STM32H563

#endif // STM32H563_H
