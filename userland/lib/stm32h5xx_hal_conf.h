/*
 * Frosted STM32H5 HAL shim for wolfSSL
 *
 * Type definitions and constants compatible with the wolfSSL STM32 port.
 * Actual hardware access goes through /dev/hash, /dev/aes, /dev/pka
 * kernel devices — see stm32h5_hal.c for implementations.
 */
#ifndef __STM32H5XX_HAL_CONF_H
#define __STM32H5XX_HAL_CONF_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Generic HAL return codes                                           */
/* ------------------------------------------------------------------ */
#define HAL_OK        0
#define HAL_ERROR     1
#define HAL_BUSY      2
#define HAL_TIMEOUT   3
#define HAL_MAX_DELAY 0xFFFFFFFFU

/* ------------------------------------------------------------------ */
/* RCC clock macros — no-ops in userland (kernel handles clocks)      */
/* ------------------------------------------------------------------ */
#define __HAL_RCC_HASH_CLK_ENABLE()   /* handled by /dev/hash open */
#define __HAL_RCC_HASH_CLK_DISABLE()
#define __HAL_RCC_AES_CLK_ENABLE()    /* handled by /dev/aes open */
#define __HAL_RCC_AES_CLK_DISABLE()
#define __HAL_RCC_CRYP_CLK_ENABLE     __HAL_RCC_AES_CLK_ENABLE
#define __HAL_RCC_CRYP_CLK_DISABLE    __HAL_RCC_AES_CLK_DISABLE
#define __HAL_RCC_PKA_CLK_ENABLE()    /* handled by /dev/pka open */
#define __HAL_RCC_PKA_CLK_DISABLE()
#define __HAL_RCC_RNG_CLK_ENABLE()

/* ------------------------------------------------------------------ */
/* HASH peripheral types                                               */
/* ------------------------------------------------------------------ */
/* wolfSSL STM32 port accesses HASH registers directly for hashing.
 * We provide the type and pointer so the code compiles, but the
 * actual stm32.c code path does direct register reads — we intercept
 * at a higher level via STM32_HASH_CLOCK_ENABLE/DISABLE overrides
 * and the wc_Stm32_Hash_* functions using /dev/hash.
 */
typedef struct {
    volatile uint32_t CR;
    volatile uint32_t DIN;
    volatile uint32_t STR;
    volatile uint32_t HR[5];
    volatile uint32_t IMR;
    volatile uint32_t SR;
    uint32_t RESERVED0[52];
    volatile uint32_t CSR[103];
    uint32_t RESERVED1[28];
    uint32_t RESERVED2[3];
    volatile uint32_t HRx[16];
} HASH_TypeDef;

typedef struct {
    volatile uint32_t HR[16];
} HASH_DIGEST_TypeDef;

#define HASH_BASE        0x420C0400UL
#define HASH             ((HASH_TypeDef *)HASH_BASE)
#define HASH_DIGEST      ((HASH_DIGEST_TypeDef *)(HASH_BASE + 0x310U))

/* CR register bits */
#define HASH_CR_INIT          (1U << 2)
#define HASH_CR_DATATYPE_Pos  4U
#define HASH_CR_MODE          (1U << 6)
#define HASH_CR_ALGO_Pos      17U
#define HASH_CR_ALGO_0        (1U << 17)
#define HASH_CR_ALGO_1        (1U << 18)

#define HASH_DATATYPE_8B      (0x2U << HASH_CR_DATATYPE_Pos)
#define HASH_BYTE_SWAP        HASH_DATATYPE_8B
#define HASH_ALGOMODE_HASH    0U
#define HASH_ALGOMODE_HMAC    HASH_CR_MODE

#define HASH_AlgoSelection_SHA1     0U
#define HASH_AlgoSelection_MD5      (1U << 7)
#define HASH_AlgoSelection_SHA224   (HASH_CR_ALGO_0)
#define HASH_AlgoSelection_SHA256   (HASH_CR_ALGO_0 | (1U << 7))
#define HASH_ALGOSELECTION_SHA384   (HASH_CR_ALGO_1)
#define HASH_ALGOSELECTION_SHA512   (HASH_CR_ALGO_1 | (1U << 7))

#define HASH_STR_NBLW_Pos    0U
#define HASH_STR_NBLW_Msk    (0x1FU << HASH_STR_NBLW_Pos)
#define HASH_STR_NBW         HASH_STR_NBLW_Msk
#define HASH_STR_DCAL        (1U << 8)

#define HASH_IMR_DINIE       (1U << 0)
#define HASH_IMR_DCIE        (1U << 1)

#define HASH_SR_BUSY         (1U << 3)
#define HASH_SR_DINIS        (1U << 0)
#define HASH_SR_DCIS         (1U << 1)

/* ------------------------------------------------------------------ */
/* AES (CRYP) types                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    volatile uint32_t CR;
    volatile uint32_t SR;
    volatile uint32_t DINR;
    volatile uint32_t DOUTR;
    volatile uint32_t KEYR0;
    volatile uint32_t KEYR1;
    volatile uint32_t KEYR2;
    volatile uint32_t KEYR3;
    volatile uint32_t IVR0;
    volatile uint32_t IVR1;
    volatile uint32_t IVR2;
    volatile uint32_t IVR3;
    volatile uint32_t KEYR4;
    volatile uint32_t KEYR5;
    volatile uint32_t KEYR6;
    volatile uint32_t KEYR7;
    volatile uint32_t SUSP0R;
    volatile uint32_t SUSP1R;
    volatile uint32_t SUSP2R;
    volatile uint32_t SUSP3R;
    volatile uint32_t SUSP4R;
    volatile uint32_t SUSP5R;
    volatile uint32_t SUSP6R;
    volatile uint32_t SUSP7R;
} AES_TypeDef;

#define AES_BASE  0x420C0000UL
#define AES       ((AES_TypeDef *)AES_BASE)
#define CRYP      AES

/* AES CR bits */
#define AES_CR_EN            (1U << 0)
#define AES_CR_DATATYPE_Pos  1U
#define AES_CR_MODE_Pos      3U
#define AES_CR_CHMOD_Pos     5U
#define AES_CR_KEYSIZE       (1U << 18)

#define AES_CR_MODE_ENCRYPT  (0x0U << AES_CR_MODE_Pos)
#define AES_CR_MODE_KEYDERIV (0x1U << AES_CR_MODE_Pos)
#define AES_CR_MODE_DECRYPT  (0x2U << AES_CR_MODE_Pos)

#define AES_CR_CHMOD_ECB     (0x0U << AES_CR_CHMOD_Pos)
#define AES_CR_CHMOD_CBC     (0x1U << AES_CR_CHMOD_Pos)
#define AES_CR_CHMOD_CTR     (0x2U << AES_CR_CHMOD_Pos)
#define AES_CR_CHMOD_GCM     (0x3U << AES_CR_CHMOD_Pos)

#define AES_SR_CCF           (1U << 0)
#define AES_SR_BUSY          (1U << 3)

/* CRYP_HandleTypeDef — HAL-compatible handle for wolfSSL */
typedef struct {
    AES_TypeDef *Instance;
    struct {
        uint32_t DataType;
        uint32_t KeySize;
        uint32_t Algorithm;
        uint32_t DataWidthUnit;
        uint32_t HeaderWidthUnit;
        uint32_t *pKey;
        uint32_t *pInitVect;
        uint32_t KeyIVConfigSkip;
        uint32_t KeySelect;
        uint32_t KeyMode;
        uint32_t *Header;       /* GCM AAD header pointer */
        uint32_t HeaderSize;    /* GCM AAD header size (words) */
    } Init;
} CRYP_HandleTypeDef;

/* GCM auth tag generation — implemented in stm32h5_hal.c */
int HAL_CRYPEx_AESGCM_GenerateAuthTAG(CRYP_HandleTypeDef *hcryp,
         uint32_t *authTag, uint32_t timeout);

/* Algorithm constants */
#define CRYP_AES_ECB          0x00U
#define CRYP_AES_CBC          0x01U
#define CRYP_AES_CTR          0x02U
#define CRYP_AES_GCM_GMAC    0x03U
#define CRYP_AES_GCM          CRYP_AES_GCM_GMAC

/* Key sizes */
#define CRYP_KEYSIZE_128B     0x00U
#define CRYP_KEYSIZE_256B     0x01U

/* Data type / width */
#define CRYP_DATATYPE_8B           0x02U
#define CRYP_DATAWIDTHUNIT_BYTE    0x01U
#define CRYP_HEADERWIDTHUNIT_BYTE  0x00U
#define CRYP_HEADERWIDTHUNIT_WORD  0x01U
#define CRYP_KEYIVCONFIG_ALWAYS    0x00U
#define CRYP_KEYSEL_NORMAL         0x00U
#define CRYP_KEYSEL_HW             0x01U
#define CRYP_KEYMODE_NORMAL        0x00U
#define CRYP_KEYMODE_WRAPPED       0x01U

/* HAL_CRYP API — implemented in stm32h5_hal.c via /dev/aes */
int  HAL_CRYP_Init(CRYP_HandleTypeDef *hcryp);
int  HAL_CRYP_DeInit(CRYP_HandleTypeDef *hcryp);
int  HAL_CRYP_Encrypt(CRYP_HandleTypeDef *hcryp,
         uint32_t *input, uint16_t size, uint32_t *output, uint32_t timeout);
int  HAL_CRYP_Decrypt(CRYP_HandleTypeDef *hcryp,
         uint32_t *input, uint16_t size, uint32_t *output, uint32_t timeout);

/* ------------------------------------------------------------------ */
/* PKA peripheral types                                                */
/* ------------------------------------------------------------------ */
#include "stm32h5xx_hal_pka.h"

#endif /* __STM32H5XX_HAL_CONF_H */
