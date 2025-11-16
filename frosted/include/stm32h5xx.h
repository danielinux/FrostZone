#ifndef STM32H5XX_H
#define STM32H5XX_H

#include <stdint.h>
#include "nvic.h"

/* --------------------------------------------------------------------------
 * Peripheral base addresses
 * -------------------------------------------------------------------------- */
#define RCC_BASE            0x44020C00UL
#define PWR_BASE            0x44020800UL

#define USB_BASE            0x40016000UL
#define USB_DRD_FS_BASE     USB_BASE
#define USB_DRD_FS_IRQn     (74U)
#define USB_DRD_PMAADDR     0x40016400UL
#define USB_PMAADDR         USB_DRD_PMAADDR

#define CRS_BASE            0x40006000UL

#define GPDMA1_Channel1_IRQn  (28U)

/* --------------------------------------------------------------------------
 * RCC registers and bits
 * -------------------------------------------------------------------------- */
#define RCC_CR              (*(volatile uint32_t *)(RCC_BASE + 0x0000UL))
#define RCC_CR_HSI48ON      (1UL << 12)
#define RCC_CR_HSI48RDY     (1UL << 13)

#define RCC_APB1LENR        (*(volatile uint32_t *)(RCC_BASE + 0x09CUL))
#define RCC_APB1LENR_CRSEN  (1UL << 24)

#define RCC_APB1HRSTR       (*(volatile uint32_t *)(RCC_BASE + 0x078UL))
#define RCC_APB1HRSTR_UCPDRST   (1UL << 23)
#define RCC_APB1HENR        (*(volatile uint32_t *)(RCC_BASE + 0x0A0UL))
#define RCC_APB1HENR_UCPDEN     (1UL << 23)

#define RCC_APB2RSTR        (*(volatile uint32_t *)(RCC_BASE + 0x07CUL))
#define RCC_APB2RSTR_USBFSRST   (1UL << 24)
#define RCC_APB2RSTR_SPI1RST    (1UL << 12)

#define RCC_APB2ENR         (*(volatile uint32_t *)(RCC_BASE + 0x0A4UL))
#define RCC_APB2ENR_USBFSEN     (1UL << 24)
#define RCC_APB2ENR_SPI1EN      (1UL << 12)

#define RCC_CCIPR4          (*(volatile uint32_t *)(RCC_BASE + 0x0E4UL))
#define RCC_CCIPR4_USBFSSEL_Pos 4
#define RCC_CCIPR4_USBFSSEL_Msk (0x3UL << RCC_CCIPR4_USBFSSEL_Pos)
#define RCC_CCIPR4_USBFSSEL_HSI48 (0x3UL << RCC_CCIPR4_USBFSSEL_Pos)

/* --------------------------------------------------------------------------
 * PWR registers and bits
 * -------------------------------------------------------------------------- */
#define PWR_USBSCR          (*(volatile uint32_t *)(PWR_BASE + 0x0038UL))
#define PWR_USBSCR_USB33DEN (1UL << 24)
#define PWR_USBSCR_USB33SV  (1UL << 25)

#define PWR_UCPDR           (*(volatile uint32_t *)(PWR_BASE + 0x002CUL))
#define PWR_UCPDR_DBDIS     (1UL << 0)

#define PWR_VMSR            (*(volatile uint32_t *)(PWR_BASE + 0x003CUL))
#define PWR_VMSR_USB33RDY   (1UL << 24)

/* --------------------------------------------------------------------------
 * CRS registers and bits
 * -------------------------------------------------------------------------- */
#define CRS_CR              (*(volatile uint32_t *)(CRS_BASE + 0x0000UL))
#define CRS_CFGR            (*(volatile uint32_t *)(CRS_BASE + 0x0004UL))
#define CRS_ISR             (*(volatile uint32_t *)(CRS_BASE + 0x0008UL))
#define CRS_ICR             (*(volatile uint32_t *)(CRS_BASE + 0x000CUL))

#define CRS_CR_SYNCOKIE     (1UL << 0)
#define CRS_CR_SYNCWARNIE   (1UL << 1)
#define CRS_CR_ERRIE        (1UL << 2)
#define CRS_CR_ESYNCIE      (1UL << 3)
#define CRS_CR_CEN          (1UL << 5)
#define CRS_CR_AUTOTRIMEN   (1UL << 6)
#define CRS_CR_TRIM_Pos     8U
#define CRS_CR_TRIM_Msk     (0x3FUL << CRS_CR_TRIM_Pos)

#define CRS_CFGR_RELOAD_Pos   0U
#define CRS_CFGR_RELOAD_Msk   (0xFFFFUL << CRS_CFGR_RELOAD_Pos)
#define CRS_CFGR_FELIM_Pos    16U
#define CRS_CFGR_FELIM_Msk    (0xFFUL << CRS_CFGR_FELIM_Pos)
#define CRS_CFGR_SYNCDIV_Pos  24U
#define CRS_CFGR_SYNCDIV_Msk  (0x7UL << CRS_CFGR_SYNCDIV_Pos)
#define CRS_CFGR_SYNCSRC_Pos  28U
#define CRS_CFGR_SYNCSRC_Msk  (0x3UL << CRS_CFGR_SYNCSRC_Pos)
#define CRS_CFGR_SYNCSRC_USB  (0x2UL << CRS_CFGR_SYNCSRC_Pos)
#define CRS_CFGR_SYNCPOL      (1UL << 31)

/* --------------------------------------------------------------------------
 * USB register layout
 * -------------------------------------------------------------------------- */
typedef struct {
    volatile uint32_t CHEP[8];
    volatile uint32_t RESERVED0[8];
    volatile uint32_t CNTR;
    volatile uint32_t ISTR;
    volatile uint32_t FNR;
    volatile uint32_t DADDR;
    volatile uint32_t BTABLE;
    volatile uint32_t LPMCSR;
    volatile uint32_t BCDR;
} stm32_usb_fs_t;

#define USB_DRD_FS          ((stm32_usb_fs_t *)USB_DRD_FS_BASE)

/* Endpoint register helpers (H5 naming) */
#define USB_EP_KIND_MASK        (1UL << 8)
#define USB_EPKIND_MASK         USB_EP_KIND_MASK
#define USB_EP_VTTX             (1UL << 7)
#define USB_EP_VTRX             (1UL << 15)
#define USB_EPTX_STAT           (0x3UL << 4)
#define USB_EP_DTOG_TX          (1UL << 6)
#define USB_EP_DTOG_RX          (1UL << 14)
#define USB_EP_SETUP            (1UL << 11)
#define USB_EP_TYPE_MASK        (0x3UL << 9)
#define USB_EP_CONTROL          (0x0UL << 9)
#define USB_EP_ISOCHRONOUS      (0x1UL << 9)
#define USB_EP_BULK             (0x2UL << 9)
#define USB_EP_INTERRUPT        (0x3UL << 9)

#define USB_CH_RX_VALID         (0x3UL << 12)

#define USB_CHEP_ADDR           (0x0FUL)
#define USB_CHEP_UTYPE          (0x3UL << 9)
#define USB_CHEP_REG_MASK       (USB_EP_VTRX | (1UL << 11) | USB_CHEP_UTYPE | \
                                 USB_EP_KIND_MASK | USB_EP_VTTX | USB_CHEP_ADDR)
#define USB_CHEP_TX_DTOG1       (1UL << 4)
#define USB_CHEP_TX_DTOG2       (1UL << 5)
#define USB_CHEP_RX_DTOG1       (1UL << 12)
#define USB_CHEP_RX_DTOG2       (1UL << 13)
#define USB_CHEP_TX_DTOGMASK    ((0x3UL << 4) | USB_CHEP_REG_MASK)
#define USB_CHEP_RX_DTOGMASK    (USB_CH_RX_VALID | USB_CHEP_REG_MASK)

/* USB control register bits */
#define USB_CNTR_USBRST         (1UL << 0)
#define USB_CNTR_PDWN           (1UL << 1)
#define USB_CNTR_SUSPRDY        (1UL << 2)
#define USB_CNTR_SUSPEN         (1UL << 3)
#define USB_CNTR_L2RES          (1UL << 4)
#define USB_CNTR_L1RES          (1UL << 5)
#define USB_CNTR_L1REQM         (1UL << 7)
#define USB_CNTR_ESOFM          (1UL << 8)
#define USB_CNTR_SOFM           (1UL << 9)
#define USB_CNTR_RESETM         (1UL << 10)
#define USB_CNTR_SUSPM          (1UL << 11)
#define USB_CNTR_WKUPM          (1UL << 12)
#define USB_CNTR_PMAOVRM        (1UL << 14)
#define USB_CNTR_CTRM           (1UL << 15)

#define USB_CNTR_FSUSP          USB_CNTR_SUSPEN
#define USB_CNTR_LPMODE         USB_CNTR_SUSPRDY
#define USB_CNTR_RESUME         USB_CNTR_L2RES

/* USB interrupt status register bits */
#define USB_ISTR_IDN            (0x0FUL)
#define USB_ISTR_EP_ID          USB_ISTR_IDN
#define USB_ISTR_DIR            (1UL << 4)
#define USB_ISTR_L1REQ          (1UL << 7)
#define USB_ISTR_ESOF           (1UL << 8)
#define USB_ISTR_SOF            (1UL << 9)
#define USB_ISTR_RESET          (1UL << 10)
#define USB_ISTR_SUSP           (1UL << 11)
#define USB_ISTR_WKUP           (1UL << 12)
#define USB_ISTR_ERR            (1UL << 13)
#define USB_ISTR_PMAOVR         (1UL << 14)
#define USB_ISTR_CTR            (1UL << 15)
#define USB_FNR_FN              (0x7FFUL)

/* USB battery charging detector register */
#define USB_BCDR_DPPU           (1UL << 15)

/* Device address register */
#define USB_DADDR_EF            (1UL << 7)

/* --------------------------------------------------------------------------
 * UCPD registers and bits
 * -------------------------------------------------------------------------- */
#define UCPD1_BASE          0x4000DC00UL

typedef struct {
    volatile uint32_t CFGR1;
    volatile uint32_t CFGR2;
    volatile uint32_t RESERVED0;
    volatile uint32_t CR;
    volatile uint32_t IMR;
    volatile uint32_t SR;
    volatile uint32_t ICR;
    volatile uint32_t TX_ORDSETR;
    volatile uint32_t TX_PAYSZR;
    volatile uint32_t TXDR;
    volatile uint32_t RX_ORDSETR;
    volatile uint32_t RX_PAYSZR;
    volatile uint32_t RXDR;
} stm32_ucpd_t;

#define UCPD1               ((stm32_ucpd_t *)UCPD1_BASE)

#define UCPD_CFGR1_HBITCLKDIV_Pos 0U
#define UCPD_CFGR1_HBITCLKDIV_Msk (0x3FUL << UCPD_CFGR1_HBITCLKDIV_Pos)
#define UCPD_CFGR1_IFRGAP_Pos     6U
#define UCPD_CFGR1_IFRGAP_Msk     (0x1FUL << UCPD_CFGR1_IFRGAP_Pos)
#define UCPD_CFGR1_TRANSWIN_Pos   11U
#define UCPD_CFGR1_TRANSWIN_Msk   (0x1FUL << UCPD_CFGR1_TRANSWIN_Pos)
#define UCPD_CFGR1_PSC_Pos        17U
#define UCPD_CFGR1_PSC_Msk        (0x7UL << UCPD_CFGR1_PSC_Pos)
#define UCPD_CFGR1_RXORDSETEN_Pos 20U
#define UCPD_CFGR1_RXORDSETEN_Msk (0x1FFUL << UCPD_CFGR1_RXORDSETEN_Pos)
#define UCPD_CFGR1_UCPDEN         (1UL << 31)

#define UCPD_IMR_TYPECEVT1IE      (1UL << 14)
#define UCPD_IMR_TYPECEVT2IE      (1UL << 15)

#define UCPD_CR_PHYRXEN           (1UL << 5)
#define UCPD_CR_PHYCCSEL          (1UL << 6)
#define UCPD_CR_ANASUBMODE_Pos    7U
#define UCPD_CR_ANAMODE           (1UL << 9)
#define UCPD_CR_CCENABLE_Pos      10U
#define UCPD_CR_CCENABLE_0        (1UL << 10)
#define UCPD_CR_CCENABLE_1        (1UL << 11)
#define UCPD_CR_CCENABLE_BOTH     (UCPD_CR_CCENABLE_0 | UCPD_CR_CCENABLE_1)

#define UCPD_SR_TYPECEVT1         (1UL << 14)
#define UCPD_SR_TYPECEVT2         (1UL << 15)
#define UCPD_SR_TYPEC_VSTATE_CC1_Pos 16U
#define UCPD_SR_TYPEC_VSTATE_CC1_Msk (0x3UL << UCPD_SR_TYPEC_VSTATE_CC1_Pos)
#define UCPD_SR_TYPEC_VSTATE_CC2_Pos 18U
#define UCPD_SR_TYPEC_VSTATE_CC2_Msk (0x3UL << UCPD_SR_TYPEC_VSTATE_CC2_Pos)

#define UCPD_ICR_TYPECEVT1CF      (1UL << 14)
#define UCPD_ICR_TYPECEVT2CF      (1UL << 15)

/* --------------------------------------------------------------------------
 * Minimal CMSIS compatibility needed by TinyUSB
 * -------------------------------------------------------------------------- */
typedef int32_t IRQn_Type;

static inline void NVIC_EnableIRQ(IRQn_Type irqn)
{
    nvic_enable_irq((uint32_t)irqn);
}

static inline void NVIC_DisableIRQ(IRQn_Type irqn)
{
    nvic_disable_irq((uint32_t)irqn);
}

static inline void __DSB(void)
{
    __asm volatile ("dsb 0xF" ::: "memory");
}

static inline void __ISB(void)
{
    __asm volatile ("isb 0xF" ::: "memory");
}

#endif /* STM32H5XX_H */
