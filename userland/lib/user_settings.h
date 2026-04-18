/* wolfSSL user settings for Frosted shared library build */
#ifndef _USER_SETTINGS_H_
#define _USER_SETTINGS_H_

#define WOLFCRYPT_ONLY
#define NO_FILESYSTEM
#define NO_WRITEV
#define SINGLE_THREADED
#define NO_WOLFSSL_MEMORY
#define NO_ERROR_STRINGS

/* ---- STM32H5 hardware crypto ---- */
#define WOLFSSL_STM32H5
#define WOLFSSL_STM32_CUBEMX
#define STM32_HAL_V2
/* HASH: use software SHA-256 — the STM32_HASH path does direct register
 * access which faults from unprivileged userland.  /dev/hash is available
 * for future wc_Stm32_Hash_* shim but software is fast enough on M33. */
#define NO_STM32_HASH
/* AES: hardware via HAL_CRYP_* (our shim routes through /dev/aes) */
/* (do NOT define NO_STM32_CRYPTO — let settings.h enable STM32_CRYPTO) */
/* PKA: hardware ECC via HAL_PKA_* */
#define WOLFSSL_STM32_PKA

/* ---- Algorithm selection ---- */
#define WOLFSSL_SHA256

/* AES */
#define HAVE_AESGCM
#define HAVE_AES_CBC
#define WOLFSSL_AES_DIRECT

/* ECC */
#define HAVE_ECC
#define HAVE_ECC_SIGN
#define HAVE_ECC_VERIFY
#define HAVE_ECC_DHE
#define HAVE_ECC_KEY_IMPORT
#define HAVE_ECC_KEY_EXPORT
#define ECC_TIMING_RESISTANT
#define WOLFSSL_HAVE_SP_ECC

/* Needed for ECC key handling */
#define WOLFSSL_ASN_TEMPLATE

/* ---- Disabled algorithms ---- */
#define NO_RSA
#define NO_DH
#define NO_DSA
#define NO_DES3
#define NO_RC4
#define NO_MD5
#define NO_SHA       /* no SHA-1 */
#define NO_HMAC
#define NO_PWDBASED
#define NO_CERTS
#define NO_OLD_TLS
#define NO_PSK

/* We need coding (base64) and ASN for ECC key import/export */
/* #define NO_CODING */
/* #define NO_ASN */

/* ---- Math: SP C implementation ---- */
/* NOTE: Cortex-M ASM (sp_cortexm.c) conflicts with PIC/fPIC in shared
 * libraries, so we use the portable C version (sp_c32.c / sp_int.c).
 * Hardware crypto (HASH/AES/PKA) handles the heavy lifting anyway. */
#define WOLFSSL_SP
#define WOLFSSL_SP_MATH
#define WOLFSSL_SP_SMALL
#define SP_WORD_SIZE 32

/* RNG — use Frosted /dev/random via our wc_GenerateSeed() */
#define NO_STM32_RNG        /* don't use ST HAL_RNG_* — we have /dev/random */
#define NO_DEV_RANDOM
#define NO_DEV_URANDOM
int frosted_rand_seed(unsigned char *output, unsigned int sz);
#define CUSTOM_RAND_GENERATE_SEED  frosted_rand_seed
#define SIZEOF_LONG_LONG 8

#endif
