/*
 * Frosted STM32H5 PKA HAL shim for wolfSSL
 *
 * Register-level PKA types and function declarations compatible
 * with the wolfSSL STM32 PKA port (ECC multiply, ECDSA sign/verify).
 */
#ifndef __STM32H5XX_HAL_PKA_H
#define __STM32H5XX_HAL_PKA_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* PKA register structure                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    volatile uint32_t CR;          /* 0x00  Control */
    volatile uint32_t SR;          /* 0x04  Status */
    volatile uint32_t CLRFR;      /* 0x08  Clear flags */
    uint32_t RESERVED[253];
    volatile uint32_t RAM[894];    /* 0x400 PKA RAM */
} PKA_TypeDef;

#define PKA_BASE  0x420C2000UL
#define PKA       ((PKA_TypeDef *)PKA_BASE)

/* CR bits */
#define PKA_CR_EN        (1U << 0)
#define PKA_CR_START     (1U << 1)
#define PKA_CR_MODE_Pos  8U

/* SR bits */
#define PKA_SR_BUSY      (1U << 16)
#define PKA_SR_PROCENDF  (1U << 17)

/* PKA operation modes */
#define PKA_MODE_ECC_MUL        0x20U
#define PKA_MODE_ECDSA_SIGN     0x24U
#define PKA_MODE_ECDSA_VERIF    0x26U

/* ------------------------------------------------------------------ */
/* PKA handle                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    PKA_TypeDef *Instance;
} PKA_HandleTypeDef;

/* Global handle — wolfSSL references extern hpka */
/* Defined in stm32h5_hal.c */

/* ------------------------------------------------------------------ */
/* ECC scalar multiplication types                                    */
/* ------------------------------------------------------------------ */
/* PKA V2 detection (wolfSSL checks this to enable coefB/primeOrder) */
#define PKA_ECC_SCALAR_MUL_IN_B_COEFF

typedef struct {
    uint32_t  modulusSize;
    uint32_t  coefSign;
    const uint8_t  *coefA;
    const uint8_t  *coefB;         /* PKA V2 */
    const uint8_t  *modulus;
    const uint8_t  *pointX;
    const uint8_t  *pointY;
    uint32_t  scalarMulSize;
    const uint8_t  *scalarMul;
    const uint8_t  *primeOrder;    /* PKA V2 */
} PKA_ECCMulInTypeDef;

typedef struct {
    uint8_t  *ptX;
    uint8_t  *ptY;
} PKA_ECCMulOutTypeDef;

/* ------------------------------------------------------------------ */
/* ECDSA signature types                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t  primeOrderSize;
    uint32_t  modulusSize;
    uint32_t  coefSign;
    const uint8_t  *coef;
    const uint8_t  *coefB;        /* PKA V2 */
    const uint8_t  *modulus;
    const uint8_t  *basePointX;
    const uint8_t  *basePointY;
    const uint8_t  *primeOrder;
    const uint8_t  *hash;
    const uint8_t  *integer;      /* random k */
    const uint8_t  *privateKey;
} PKA_ECDSASignInTypeDef;

typedef struct {
    uint8_t  *RSign;
    uint8_t  *SSign;
} PKA_ECDSASignOutTypeDef;

/* ------------------------------------------------------------------ */
/* ECDSA verification types                                           */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t  primeOrderSize;
    uint32_t  modulusSize;
    uint32_t  coefSign;
    const uint8_t  *coef;
    const uint8_t  *modulus;
    const uint8_t  *basePointX;
    const uint8_t  *basePointY;
    const uint8_t  *primeOrder;
    const uint8_t  *pPubKeyCurvePtX;
    const uint8_t  *pPubKeyCurvePtY;
    const uint8_t  *RSign;
    const uint8_t  *SSign;
    const uint8_t  *hash;
} PKA_ECDSAVerifInTypeDef;

/* ------------------------------------------------------------------ */
/* PKA HAL API — implemented in stm32h5_hal.c                        */
/* ------------------------------------------------------------------ */
int  HAL_PKA_ECCMul(PKA_HandleTypeDef *hpka,
         PKA_ECCMulInTypeDef *in, uint32_t timeout);
void HAL_PKA_ECCMul_GetResult(PKA_HandleTypeDef *hpka,
         PKA_ECCMulOutTypeDef *out);

int  HAL_PKA_ECDSASign(PKA_HandleTypeDef *hpka,
         PKA_ECDSASignInTypeDef *in, uint32_t timeout);
void HAL_PKA_ECDSASign_GetResult(PKA_HandleTypeDef *hpka,
         PKA_ECDSASignOutTypeDef *out, uint8_t *error);

int  HAL_PKA_ECDSAVerif(PKA_HandleTypeDef *hpka,
         PKA_ECDSAVerifInTypeDef *in, uint32_t timeout);
int  HAL_PKA_ECDSAVerif_IsValidSignature(PKA_HandleTypeDef *hpka);

void HAL_PKA_RAMReset(PKA_HandleTypeDef *hpka);

#endif /* __STM32H5XX_HAL_PKA_H */
