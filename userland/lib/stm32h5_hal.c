/*
 * Frosted STM32H5 HAL shim — userland implementation via /dev/* devices
 *
 * Provides HAL_CRYP_* and HAL_PKA_* using Frosted kernel crypto devices:
 *   /dev/hash — SHA hashing
 *   /dev/aes  — AES encrypt/decrypt
 *   /dev/pka  — ECC/ECDSA operations
 */

#include "stm32h5xx_hal_conf.h"
#include <sys/ioctl.h>
#include <sys/frosted-io.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* ================================================================== */
/* AES (CRYP) implementation via /dev/aes                              */
/* ================================================================== */

static int aes_fd = -1;

int HAL_CRYP_Init(CRYP_HandleTypeDef *hcryp)
{
    struct aes_mode_req mode_req;
    struct aes_key_req  key_req;

    if (!hcryp)
        return HAL_ERROR;

    if (aes_fd < 0) {
        aes_fd = open("/dev/aes", O_RDWR);
        if (aes_fd < 0)
            return HAL_ERROR;
    }

    /* Set mode */
    mode_req.direction = AES_DIR_ENCRYPT; /* will be overridden per call */
    switch (hcryp->Init.Algorithm) {
    case CRYP_AES_CBC:      mode_req.mode = AES_MODE_CBC; break;
    case CRYP_AES_CTR:      mode_req.mode = AES_MODE_CTR; break;
    case CRYP_AES_GCM_GMAC: mode_req.mode = AES_MODE_GCM; break;
    default:                 mode_req.mode = AES_MODE_ECB; break;
    }
    if (ioctl(aes_fd, IOCTL_AES_SET_MODE, &mode_req) < 0)
        return HAL_ERROR;

    /* Set key */
    if (hcryp->Init.pKey) {
        key_req.size = (hcryp->Init.KeySize == CRYP_KEYSIZE_256B) ? 32 : 16;
        memcpy(key_req.key, hcryp->Init.pKey, key_req.size);
        if (ioctl(aes_fd, IOCTL_AES_SET_KEY, &key_req) < 0)
            return HAL_ERROR;
    }

    /* Set IV */
    if (hcryp->Init.Algorithm != CRYP_AES_ECB && hcryp->Init.pInitVect) {
        if (ioctl(aes_fd, IOCTL_AES_SET_IV, hcryp->Init.pInitVect) < 0)
            return HAL_ERROR;
    }

    return HAL_OK;
}

int HAL_CRYP_DeInit(CRYP_HandleTypeDef *hcryp)
{
    if (aes_fd >= 0) {
        close(aes_fd);
        aes_fd = -1;
    }
    return HAL_OK;
}

static int aes_process(CRYP_HandleTypeDef *hcryp, uint32_t *input,
                        uint16_t size, uint32_t *output, uint32_t timeout,
                        int decrypt)
{
    struct aes_mode_req mode_req;
    int ret;

    if (aes_fd < 0)
        return HAL_ERROR;

    /* Update direction */
    mode_req.direction = decrypt ? AES_DIR_DECRYPT : AES_DIR_ENCRYPT;
    switch (hcryp->Init.Algorithm) {
    case CRYP_AES_CBC:      mode_req.mode = AES_MODE_CBC; break;
    case CRYP_AES_CTR:      mode_req.mode = AES_MODE_CTR; break;
    case CRYP_AES_GCM_GMAC: mode_req.mode = AES_MODE_GCM; break;
    default:                 mode_req.mode = AES_MODE_ECB; break;
    }
    ioctl(aes_fd, IOCTL_AES_SET_MODE, &mode_req);

    /* Re-set key for each operation (kernel reconfigures HW each write) */
    if (hcryp->Init.pKey) {
        struct aes_key_req key_req;
        key_req.size = (hcryp->Init.KeySize == CRYP_KEYSIZE_256B) ? 32 : 16;
        memcpy(key_req.key, hcryp->Init.pKey, key_req.size);
        ioctl(aes_fd, IOCTL_AES_SET_KEY, &key_req);
    }
    if (hcryp->Init.Algorithm != CRYP_AES_ECB && hcryp->Init.pInitVect)
        ioctl(aes_fd, IOCTL_AES_SET_IV, hcryp->Init.pInitVect);

    /* Write input data */
    ret = write(aes_fd, input, size);
    if (ret != (int)size)
        return HAL_ERROR;

    /* Read output data */
    ret = read(aes_fd, output, size);
    if (ret != (int)size)
        return HAL_ERROR;

    return HAL_OK;
}

int HAL_CRYPEx_AESGCM_GenerateAuthTAG(CRYP_HandleTypeDef *hcryp,
                                        uint32_t *authTag, uint32_t timeout)
{
    /* TODO: GCM auth tag via /dev/aes ioctl when needed */
    (void)hcryp;
    (void)timeout;
    if (authTag)
        memset(authTag, 0, 16);
    return HAL_OK;
}

int HAL_CRYP_Encrypt(CRYP_HandleTypeDef *hcryp,
                      uint32_t *input, uint16_t size,
                      uint32_t *output, uint32_t timeout)
{
    return aes_process(hcryp, input, size, output, timeout, 0);
}

int HAL_CRYP_Decrypt(CRYP_HandleTypeDef *hcryp,
                      uint32_t *input, uint16_t size,
                      uint32_t *output, uint32_t timeout)
{
    return aes_process(hcryp, input, size, output, timeout, 1);
}


/* ================================================================== */
/* PKA implementation via /dev/pka                                     */
/* ================================================================== */

/* Global PKA handle — wolfSSL references `extern PKA_HandleTypeDef hpka` */
PKA_HandleTypeDef hpka = { .Instance = (PKA_TypeDef *)0x420C2000UL };

static int pka_fd = -1;

static int pka_ensure_open(void)
{
    if (pka_fd < 0) {
        pka_fd = open("/dev/pka", O_RDWR);
        if (pka_fd < 0)
            return -1;
    }
    return 0;
}

int HAL_PKA_ECCMul(PKA_HandleTypeDef *hpka_h,
                    PKA_ECCMulInTypeDef *in, uint32_t timeout)
{
    struct pka_ecc_mul_req req;
    static struct pka_ecc_mul_req last_mul;

    if (!in || pka_ensure_open() < 0)
        return HAL_ERROR;

    memset(&req, 0, sizeof(req));
    req.modulus_size = in->modulusSize;
    req.scalar_size  = in->scalarMulSize;
    req.coef_sign    = in->coefSign;

    memcpy(req.coef_a, in->coefA, in->modulusSize);
    if (in->coefB)
        memcpy(req.coef_b, in->coefB, in->modulusSize);
    memcpy(req.modulus, in->modulus, in->modulusSize);
    memcpy(req.point_x, in->pointX, in->modulusSize);
    memcpy(req.point_y, in->pointY, in->modulusSize);
    memcpy(req.scalar, in->scalarMul, in->scalarMulSize);
    if (in->primeOrder)
        memcpy(req.prime_order, in->primeOrder, in->modulusSize);

    if (ioctl(pka_fd, IOCTL_PKA_ECC_MUL, &req) < 0)
        return HAL_ERROR;

    /* Stash result for GetResult — store in a static (single-threaded) */
    memcpy(&last_mul, &req, sizeof(req));
    hpka_h->Instance = (PKA_TypeDef *)(uintptr_t)&last_mul;

    return HAL_OK;
}

void HAL_PKA_ECCMul_GetResult(PKA_HandleTypeDef *hpka_h,
                               PKA_ECCMulOutTypeDef *out)
{
    struct pka_ecc_mul_req *req;
    if (!hpka_h || !out)
        return;
    req = (struct pka_ecc_mul_req *)(uintptr_t)hpka_h->Instance;
    if (out->ptX)
        memcpy(out->ptX, req->result_x, req->modulus_size);
    if (out->ptY)
        memcpy(out->ptY, req->result_y, req->modulus_size);

    /* Restore Instance pointer */
    hpka_h->Instance = (PKA_TypeDef *)0x420C2000UL;
}


int HAL_PKA_ECDSASign(PKA_HandleTypeDef *hpka_h,
                       PKA_ECDSASignInTypeDef *in, uint32_t timeout)
{
    struct pka_ecdsa_sign_req req;
    static struct pka_ecdsa_sign_req last_sign;

    if (!in || pka_ensure_open() < 0)
        return HAL_ERROR;

    memset(&req, 0, sizeof(req));
    req.order_size   = in->primeOrderSize;
    req.modulus_size = in->modulusSize;
    req.coef_sign    = in->coefSign;

    memcpy(req.coef_a, in->coef, in->modulusSize);
    if (in->coefB)
        memcpy(req.coef_b, in->coefB, in->modulusSize);
    memcpy(req.modulus, in->modulus, in->modulusSize);
    memcpy(req.base_x, in->basePointX, in->modulusSize);
    memcpy(req.base_y, in->basePointY, in->modulusSize);
    memcpy(req.prime_order, in->primeOrder, in->primeOrderSize);
    memcpy(req.hash, in->hash, in->modulusSize);
    memcpy(req.random_k, in->integer, in->modulusSize);
    memcpy(req.private_key, in->privateKey, in->modulusSize);

    if (ioctl(pka_fd, IOCTL_PKA_ECDSA_SIGN, &req) < 0)
        return HAL_ERROR;

    memcpy(&last_sign, &req, sizeof(req));
    hpka_h->Instance = (PKA_TypeDef *)(uintptr_t)&last_sign;

    return HAL_OK;
}

void HAL_PKA_ECDSASign_GetResult(PKA_HandleTypeDef *hpka_h,
                                  PKA_ECDSASignOutTypeDef *out,
                                  uint8_t *error)
{
    struct pka_ecdsa_sign_req *req;
    if (!hpka_h || !out)
        return;
    req = (struct pka_ecdsa_sign_req *)(uintptr_t)hpka_h->Instance;
    if (out->RSign)
        memcpy(out->RSign, req->sig_r, req->order_size);
    if (out->SSign)
        memcpy(out->SSign, req->sig_s, req->order_size);
    if (error)
        *error = req->error;
    hpka_h->Instance = (PKA_TypeDef *)0x420C2000UL;
}


int HAL_PKA_ECDSAVerif(PKA_HandleTypeDef *hpka_h,
                        PKA_ECDSAVerifInTypeDef *in, uint32_t timeout)
{
    struct pka_ecdsa_verify_req req;
    static struct pka_ecdsa_verify_req last_verify;

    if (!in || pka_ensure_open() < 0)
        return HAL_ERROR;

    memset(&req, 0, sizeof(req));
    req.order_size   = in->primeOrderSize;
    req.modulus_size = in->modulusSize;
    req.coef_sign    = in->coefSign;

    memcpy(req.coef_a, in->coef, in->modulusSize);
    memcpy(req.modulus, in->modulus, in->modulusSize);
    memcpy(req.base_x, in->basePointX, in->modulusSize);
    memcpy(req.base_y, in->basePointY, in->modulusSize);
    memcpy(req.prime_order, in->primeOrder, in->primeOrderSize);
    memcpy(req.pub_x, in->pPubKeyCurvePtX, in->modulusSize);
    memcpy(req.pub_y, in->pPubKeyCurvePtY, in->modulusSize);
    memcpy(req.sig_r, in->RSign, in->modulusSize);
    memcpy(req.sig_s, in->SSign, in->modulusSize);
    memcpy(req.hash, in->hash, in->modulusSize);

    if (ioctl(pka_fd, IOCTL_PKA_ECDSA_VERIFY, &req) < 0)
        return HAL_ERROR;

    memcpy(&last_verify, &req, sizeof(req));
    hpka_h->Instance = (PKA_TypeDef *)(uintptr_t)&last_verify;

    return HAL_OK;
}

int HAL_PKA_ECDSAVerif_IsValidSignature(PKA_HandleTypeDef *hpka_h)
{
    struct pka_ecdsa_verify_req *req;
    int valid;
    if (!hpka_h)
        return 0;
    req = (struct pka_ecdsa_verify_req *)(uintptr_t)hpka_h->Instance;
    valid = req->valid ? 1 : 0;
    hpka_h->Instance = (PKA_TypeDef *)0x420C2000UL;
    return valid;
}

void HAL_PKA_RAMReset(PKA_HandleTypeDef *hpka_h)
{
    /* No-op in userland — kernel clears PKA RAM on close */
    (void)hpka_h;
}
