/*
 * STM32 Crypto driver — /dev/hash, /dev/aes, /dev/pka
 *
 * Provides character-device access to the STM32H5 hardware crypto
 * accelerators: HASH, AES, and PKA (ECC).
 *
 * /dev/hash: open=init, write=update, read=final+digest, ioctl=set algo
 * /dev/aes:  ioctl to set mode/key/iv, write then read for each block
 * /dev/pka:  ioctl-only for ECC mul, ECDSA sign/verify
 */

#include "frosted.h"
#include "device.h"
#include <string.h>
#include <sys/frosted-io.h>

/* ------------------------------------------------------------------ */
/* STM32H5 peripheral register definitions                             */
/* ------------------------------------------------------------------ */

/* RCC AHB2 clock gating */
#define RCC_BASE              0x44020C00UL
#define RCC_AHB2ENR_OFFSET    0x8CU
#define RCC_AHB2ENR     (*(volatile uint32_t *)(RCC_BASE + RCC_AHB2ENR_OFFSET))
#define RCC_AHB2ENR_HASHEN   (1U << 17)
#define RCC_AHB2ENR_AESEN    (1U << 16)
#define RCC_AHB2ENR_PKAEN    (1U << 20)

/* --- HASH peripheral (0x420C_0400) --- */
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
} HASH_Regs;

#define HASH_REGS      ((HASH_Regs *)0x420C0400UL)

#define HASH_CR_INIT          (1U << 2)
#define HASH_CR_DATATYPE_Pos  4U
#define HASH_CR_ALGO_0        (1U << 17)
#define HASH_SR_BUSY          (1U << 3)
#define HASH_SR_DINIS         (1U << 0)
#define HASH_SR_DCIS          (1U << 1)
#define HASH_STR_DCAL         (1U << 8)

/* SHA-256 config: ALGO_0 | datatype=8bit byte swap */
#define HASH_CFG_SHA256  (HASH_CR_ALGO_0 | (1U << 7) | (0x2U << HASH_CR_DATATYPE_Pos))
/* SHA-224 config: ALGO_0 | datatype=8bit byte swap */
#define HASH_CFG_SHA224  (HASH_CR_ALGO_0 | (0x2U << HASH_CR_DATATYPE_Pos))

/* --- AES peripheral (0x420C_0000) --- */
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
} AES_Regs;

#define AES_REGS       ((AES_Regs *)0x420C0000UL)

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

/* --- PKA peripheral (0x420C_2000) --- */
typedef struct {
    volatile uint32_t CR;
    volatile uint32_t SR;
    volatile uint32_t CLRFR;
    uint32_t RESERVED[253];
    volatile uint32_t RAM[894];
} PKA_Regs;

#define PKA_REGS       ((PKA_Regs *)0x420C2000UL)

#define PKA_CR_EN        (1U << 0)
#define PKA_CR_START     (1U << 1)
#define PKA_CR_MODE_Pos  8U
#define PKA_SR_BUSY      (1U << 16)
#define PKA_SR_PROCENDF  (1U << 17)
#define PKA_MODE_ECC_MUL      0x20U
#define PKA_MODE_ECDSA_SIGN   0x24U
#define PKA_MODE_ECDSA_VERIF  0x26U

/* PKA ECC multiply RAM offsets (word index from base) */
#define PKA_ECC_MUL_NB_BITS       0x404U
#define PKA_ECC_MUL_COEF_SIGN     0x408U
#define PKA_ECC_MUL_COEF_A        0x40CU
#define PKA_ECC_MUL_COEF_B        0x520U
#define PKA_ECC_MUL_MODULUS        0x460U
#define PKA_ECC_MUL_POINT_X       0x4B4U
#define PKA_ECC_MUL_POINT_Y       0x508U
#define PKA_ECC_MUL_SCALAR_LEN    0x400U
#define PKA_ECC_MUL_SCALAR        0x55CU
#define PKA_ECC_MUL_PRIME_ORDER   0x574U
#define PKA_ECC_MUL_RES_X         0x578U
#define PKA_ECC_MUL_RES_Y         0x5CCU

/* ECDSA sign RAM offsets */
#define PKA_ECDSA_SIGN_ORDER_LEN   0x400U
#define PKA_ECDSA_SIGN_MOD_LEN     0x404U
#define PKA_ECDSA_SIGN_COEF_SIGN   0x408U
#define PKA_ECDSA_SIGN_COEF_A      0x40CU
#define PKA_ECDSA_SIGN_COEF_B      0x520U
#define PKA_ECDSA_SIGN_MODULUS     0x460U
#define PKA_ECDSA_SIGN_BASE_X      0x4B4U
#define PKA_ECDSA_SIGN_BASE_Y      0x508U
#define PKA_ECDSA_SIGN_ORDER       0x574U
#define PKA_ECDSA_SIGN_HASH        0x5C8U
#define PKA_ECDSA_SIGN_INTEGER_K   0x61CU
#define PKA_ECDSA_SIGN_PRIV_KEY    0x670U
#define PKA_ECDSA_SIGN_RES_R       0x700U
#define PKA_ECDSA_SIGN_RES_S       0x754U

/* ECDSA verify RAM offsets */
#define PKA_ECDSA_VERIF_ORDER_LEN  0x400U
#define PKA_ECDSA_VERIF_MOD_LEN    0x404U
#define PKA_ECDSA_VERIF_COEF_SIGN  0x408U
#define PKA_ECDSA_VERIF_COEF_A     0x40CU
#define PKA_ECDSA_VERIF_MODULUS    0x460U
#define PKA_ECDSA_VERIF_BASE_X     0x4B4U
#define PKA_ECDSA_VERIF_BASE_Y     0x508U
#define PKA_ECDSA_VERIF_ORDER      0x574U
#define PKA_ECDSA_VERIF_PUBKEY_X   0x5C8U
#define PKA_ECDSA_VERIF_PUBKEY_Y   0x61CU
#define PKA_ECDSA_VERIF_R          0x670U
#define PKA_ECDSA_VERIF_S          0x6C4U
#define PKA_ECDSA_VERIF_HASH       0x718U
#define PKA_ECDSA_VERIF_RESULT     0x5B0U


/* ------------------------------------------------------------------ */
/* Per-open state                                                      */
/* ------------------------------------------------------------------ */

struct hash_state {
    uint32_t algo;          /* HASH_ALGO_* */
    int      initialized;   /* HASH_CR_INIT sent? */
};

struct aes_state {
    uint32_t direction;     /* AES_DIR_* */
    uint32_t mode;          /* AES_MODE_* */
    uint32_t key[8];        /* up to 256-bit key */
    uint32_t iv[4];
    uint32_t key_size;      /* 16 or 32 */
    int      key_set;
    int      mode_set;
    /* pending output from last write */
    uint32_t outbuf[256/4]; /* max 256 bytes buffered */
    uint32_t out_bytes;
};

/* ------------------------------------------------------------------ */
/* Module and fnode globals                                            */
/* ------------------------------------------------------------------ */

static struct fnode *fno_hash;
static struct fnode *fno_aes;
static struct fnode *fno_pka;
static struct module mod_crypto;


/* ------------------------------------------------------------------ */
/* PKA helpers                                                         */
/* ------------------------------------------------------------------ */

static void pka_write_bytes(volatile uint32_t *ram, uint32_t word_offset,
                             const uint8_t *data, uint32_t len)
{
    uint32_t nwords = (len + 3) / 4;
    uint32_t i;
    for (i = 0; i < nwords; i++) {
        uint32_t w = 0;
        uint32_t byte_off = len - 1 - (i * 4);
        if (byte_off < len) w |= data[byte_off];
        if (byte_off >= 1 && (byte_off - 1) < len) w |= (uint32_t)data[byte_off - 1] << 8;
        if (byte_off >= 2 && (byte_off - 2) < len) w |= (uint32_t)data[byte_off - 2] << 16;
        if (byte_off >= 3 && (byte_off - 3) < len) w |= (uint32_t)data[byte_off - 3] << 24;
        ram[(word_offset / 4) + i] = w;
    }
}

static void pka_read_bytes(volatile uint32_t *ram, uint32_t word_offset,
                            uint8_t *data, uint32_t len)
{
    uint32_t nwords = (len + 3) / 4;
    uint32_t i;
    for (i = 0; i < nwords; i++) {
        uint32_t w = ram[(word_offset / 4) + i];
        uint32_t byte_off = len - 1 - (i * 4);
        if (byte_off < len) data[byte_off] = (uint8_t)(w & 0xFF);
        if (byte_off >= 1 && (byte_off - 1) < len) data[byte_off - 1] = (uint8_t)((w >> 8) & 0xFF);
        if (byte_off >= 2 && (byte_off - 2) < len) data[byte_off - 2] = (uint8_t)((w >> 16) & 0xFF);
        if (byte_off >= 3 && (byte_off - 3) < len) data[byte_off - 3] = (uint8_t)((w >> 24) & 0xFF);
    }
}

static int pka_wait_complete(void)
{
    volatile uint32_t count = 0;
    while (!(PKA_REGS->SR & PKA_SR_PROCENDF)) {
        if (++count > 0x1000000)
            return -ETIMEDOUT;
    }
    return 0;
}

static void pka_start(uint32_t mode)
{
    PKA_REGS->CR = PKA_CR_EN | (mode << PKA_CR_MODE_Pos);
    PKA_REGS->CR |= PKA_CR_START;
}


/* ------------------------------------------------------------------ */
/* /dev/hash operations                                                */
/* ------------------------------------------------------------------ */

static int hash_open(const char *path, int flags)
{
    struct fnode *f = fno_search(path);
    struct hash_state *hs;
    int fd;
    if (!f)
        return -ENOENT;
    fd = task_filedesc_add(f);
    if (fd < 0)
        return fd;

    hs = kalloc(sizeof(*hs));
    if (!hs)
        return -ENOMEM;
    hs->algo = HASH_ALGO_SHA256;
    hs->initialized = 0;
    f->priv = hs;

    /* Enable HASH clock */
    RCC_AHB2ENR |= RCC_AHB2ENR_HASHEN;
    return fd;
}

static int hash_write(struct fnode *fno, const void *buf, unsigned int len)
{
    struct hash_state *hs = fno->priv;
    const uint8_t *data = buf;
    uint32_t cr_cfg;
    unsigned int i;

    if (!hs)
        return -ENODEV;

    cr_cfg = (hs->algo == HASH_ALGO_SHA224) ? HASH_CFG_SHA224 : HASH_CFG_SHA256;

    if (!hs->initialized) {
        HASH_REGS->CR = cr_cfg | HASH_CR_INIT;
        hs->initialized = 1;
    }

    /* Feed data 32 bits at a time */
    for (i = 0; i + 3 < len; i += 4) {
        /* Wait for DINIS */
        while (!(HASH_REGS->SR & HASH_SR_DINIS))
            ;
        HASH_REGS->DIN = *(const uint32_t *)(data + i);
    }

    /* Handle remaining bytes */
    if (i < len) {
        uint32_t last = 0;
        uint32_t remain = len - i;
        memcpy(&last, data + i, remain);
        while (!(HASH_REGS->SR & HASH_SR_DINIS))
            ;
        HASH_REGS->DIN = last;
        /* Set NBLW (number of valid bits in last word) */
        HASH_REGS->STR = (remain * 8) & 0x1FU;
    }

    return (int)len;
}

static int hash_read(struct fnode *fno, void *buf, unsigned int len)
{
    struct hash_state *hs = fno->priv;
    uint32_t digest_len;
    uint8_t *out = buf;
    unsigned int i;

    if (!hs)
        return -ENODEV;

    digest_len = (hs->algo == HASH_ALGO_SHA224) ? 28 : 32;
    if (len < digest_len)
        return -EINVAL;

    /* Trigger final digest computation */
    HASH_REGS->STR |= HASH_STR_DCAL;

    /* Wait for digest complete */
    while (!(HASH_REGS->SR & HASH_SR_DCIS))
        ;

    /* Read digest from HRx extended registers (big-endian words) */
    for (i = 0; i < digest_len / 4; i++) {
        uint32_t w = HASH_REGS->HRx[i];
        out[i*4 + 0] = (uint8_t)(w >> 24);
        out[i*4 + 1] = (uint8_t)(w >> 16);
        out[i*4 + 2] = (uint8_t)(w >> 8);
        out[i*4 + 3] = (uint8_t)(w);
    }

    /* Reset for next use */
    hs->initialized = 0;
    return (int)digest_len;
}

static int hash_ioctl(struct fnode *fno, const uint32_t cmd, void *arg)
{
    struct hash_state *hs = fno->priv;
    if (!hs)
        return -ENODEV;

    if (cmd == IOCTL_HASH_SET_ALGO) {
        uint32_t algo = (uint32_t)(uintptr_t)arg;
        if (algo > HASH_ALGO_SHA224)
            return -EINVAL;
        hs->algo = algo;
        hs->initialized = 0;
        return 0;
    }
    return -EINVAL;
}

static int hash_close(struct fnode *fno)
{
    if (fno->priv) {
        kfree(fno->priv);
        fno->priv = NULL;
    }
    RCC_AHB2ENR &= ~RCC_AHB2ENR_HASHEN;
    return 0;
}


/* ------------------------------------------------------------------ */
/* /dev/aes operations                                                 */
/* ------------------------------------------------------------------ */

static int aes_wait_ccf(void)
{
    volatile uint32_t count = 0;
    while (!(AES_REGS->SR & AES_SR_CCF)) {
        if (++count > 0x1000000)
            return -ETIMEDOUT;
    }
    return 0;
}

static int aes_open(const char *path, int flags)
{
    struct fnode *f = fno_search(path);
    struct aes_state *as;
    int fd;
    if (!f)
        return -ENOENT;
    fd = task_filedesc_add(f);
    if (fd < 0)
        return fd;

    as = kalloc(sizeof(*as));
    if (!as)
        return -ENOMEM;
    memset(as, 0, sizeof(*as));
    f->priv = as;

    RCC_AHB2ENR |= RCC_AHB2ENR_AESEN;
    return fd;
}

static int aes_ioctl(struct fnode *fno, const uint32_t cmd, void *arg)
{
    struct aes_state *as = fno->priv;
    if (!as)
        return -ENODEV;

    switch (cmd) {
    case IOCTL_AES_SET_MODE: {
        struct aes_mode_req *req = arg;
        if (!req)
            return -EINVAL;
        as->direction = req->direction;
        as->mode = req->mode;
        as->mode_set = 1;
        return 0;
    }
    case IOCTL_AES_SET_KEY: {
        struct aes_key_req *req = arg;
        if (!req || (req->size != 16 && req->size != 32))
            return -EINVAL;
        as->key_size = req->size;
        memcpy(as->key, req->key, req->size);
        as->key_set = 1;
        return 0;
    }
    case IOCTL_AES_SET_IV: {
        if (!arg)
            return -EINVAL;
        memcpy(as->iv, arg, 16);
        return 0;
    }
    }
    return -EINVAL;
}

static int aes_write(struct fnode *fno, const void *buf, unsigned int len)
{
    struct aes_state *as = fno->priv;
    const uint32_t *input;
    uint32_t cr, chmod;
    uint16_t blocks, i;
    int ret, decrypt;

    if (!as || !as->key_set || !as->mode_set)
        return -EINVAL;
    if (len == 0 || (len % 16) != 0)
        return -EINVAL;
    if (len > sizeof(as->outbuf))
        return -EINVAL;

    input = (const uint32_t *)buf;
    blocks = len / 16;
    decrypt = (as->direction == AES_DIR_DECRYPT);

    /* Disable AES, configure */
    AES_REGS->CR = 0;

    /* Build CR */
    cr = (0x2U << AES_CR_DATATYPE_Pos); /* 8-bit byte swap */
    if (as->key_size == 32)
        cr |= AES_CR_KEYSIZE;

    switch (as->mode) {
    case AES_MODE_CBC: chmod = AES_CR_CHMOD_CBC; break;
    case AES_MODE_CTR: chmod = AES_CR_CHMOD_CTR; break;
    case AES_MODE_GCM: chmod = AES_CR_CHMOD_GCM; break;
    default:           chmod = AES_CR_CHMOD_ECB; break;
    }
    cr |= chmod;

    AES_REGS->CR = cr;

    /* Load key */
    AES_REGS->KEYR3 = as->key[0];
    AES_REGS->KEYR2 = as->key[1];
    AES_REGS->KEYR1 = as->key[2];
    AES_REGS->KEYR0 = as->key[3];
    if (as->key_size == 32) {
        AES_REGS->KEYR7 = as->key[4];
        AES_REGS->KEYR6 = as->key[5];
        AES_REGS->KEYR5 = as->key[6];
        AES_REGS->KEYR4 = as->key[7];
    }

    /* Load IV for non-ECB modes */
    if (as->mode != AES_MODE_ECB) {
        AES_REGS->IVR3 = as->iv[0];
        AES_REGS->IVR2 = as->iv[1];
        AES_REGS->IVR1 = as->iv[2];
        AES_REGS->IVR0 = as->iv[3];
    }

    /* Key derivation for ECB/CBC decrypt */
    if (decrypt && (as->mode == AES_MODE_ECB || as->mode == AES_MODE_CBC)) {
        AES_REGS->CR = (cr & ~(0x3U << AES_CR_MODE_Pos)) | AES_CR_MODE_KEYDERIV | AES_CR_EN;
        ret = aes_wait_ccf();
        AES_REGS->CR &= ~AES_CR_EN;
        if (ret != 0)
            return ret;
        cr = AES_REGS->CR & ~(0x3U << AES_CR_MODE_Pos);
        cr |= chmod;
    }

    /* Set mode and enable */
    cr &= ~(0x3U << AES_CR_MODE_Pos);
    cr |= decrypt ? AES_CR_MODE_DECRYPT : AES_CR_MODE_ENCRYPT;
    AES_REGS->CR = cr | AES_CR_EN;

    for (i = 0; i < blocks; i++) {
        AES_REGS->DINR = input[i * 4 + 0];
        AES_REGS->DINR = input[i * 4 + 1];
        AES_REGS->DINR = input[i * 4 + 2];
        AES_REGS->DINR = input[i * 4 + 3];

        ret = aes_wait_ccf();
        if (ret != 0) {
            AES_REGS->CR &= ~AES_CR_EN;
            return ret;
        }

        as->outbuf[i * 4 + 0] = AES_REGS->DOUTR;
        as->outbuf[i * 4 + 1] = AES_REGS->DOUTR;
        as->outbuf[i * 4 + 2] = AES_REGS->DOUTR;
        as->outbuf[i * 4 + 3] = AES_REGS->DOUTR;

        (void)AES_REGS->SR; /* clear CCF */
    }

    AES_REGS->CR &= ~AES_CR_EN;
    as->out_bytes = len;
    return (int)len;
}

static int aes_read(struct fnode *fno, void *buf, unsigned int len)
{
    struct aes_state *as = fno->priv;
    if (!as)
        return -ENODEV;
    if (as->out_bytes == 0)
        return 0;
    if (len > as->out_bytes)
        len = as->out_bytes;
    memcpy(buf, as->outbuf, len);
    as->out_bytes = 0;
    return (int)len;
}

static int aes_close(struct fnode *fno)
{
    if (fno->priv) {
        /* Zero key material */
        memset(fno->priv, 0, sizeof(struct aes_state));
        kfree(fno->priv);
        fno->priv = NULL;
    }
    AES_REGS->CR = 0;
    RCC_AHB2ENR &= ~RCC_AHB2ENR_AESEN;
    return 0;
}


/* ------------------------------------------------------------------ */
/* /dev/pka operations                                                 */
/* ------------------------------------------------------------------ */

static int pka_open(const char *path, int flags)
{
    struct fnode *f = fno_search(path);
    int fd;
    if (!f)
        return -ENOENT;
    fd = task_filedesc_add(f);
    if (fd < 0)
        return fd;
    RCC_AHB2ENR |= RCC_AHB2ENR_PKAEN;
    return fd;
}

static int pka_ioctl(struct fnode *fno, const uint32_t cmd, void *arg)
{
    int ret;

    switch (cmd) {
    case IOCTL_PKA_ECC_MUL: {
        struct pka_ecc_mul_req *req = arg;
        uint32_t nbits, sz;
        if (!req || req->modulus_size > PKA_MAX_KEY_SIZE)
            return -EINVAL;
        sz = req->modulus_size;
        nbits = sz * 8;

        /* Clear PKA RAM */
        {
            uint32_t i;
            for (i = 0; i < 894; i++)
                PKA_REGS->RAM[i] = 0;
        }

        PKA_REGS->RAM[PKA_ECC_MUL_NB_BITS / 4] = nbits;
        PKA_REGS->RAM[PKA_ECC_MUL_SCALAR_LEN / 4] = req->scalar_size * 8;
        PKA_REGS->RAM[PKA_ECC_MUL_COEF_SIGN / 4] = req->coef_sign;

        pka_write_bytes(PKA_REGS->RAM, PKA_ECC_MUL_COEF_A, req->coef_a, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECC_MUL_COEF_B, req->coef_b, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECC_MUL_MODULUS, req->modulus, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECC_MUL_POINT_X, req->point_x, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECC_MUL_POINT_Y, req->point_y, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECC_MUL_SCALAR, req->scalar, req->scalar_size);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECC_MUL_PRIME_ORDER, req->prime_order, sz);

        pka_start(PKA_MODE_ECC_MUL);
        ret = pka_wait_complete();
        if (ret != 0)
            return ret;

        pka_read_bytes(PKA_REGS->RAM, PKA_ECC_MUL_RES_X, req->result_x, sz);
        pka_read_bytes(PKA_REGS->RAM, PKA_ECC_MUL_RES_Y, req->result_y, sz);
        return 0;
    }

    case IOCTL_PKA_ECDSA_SIGN: {
        struct pka_ecdsa_sign_req *req = arg;
        uint32_t sz;
        if (!req || req->modulus_size > PKA_MAX_KEY_SIZE)
            return -EINVAL;
        sz = req->modulus_size;

        {
            uint32_t i;
            for (i = 0; i < 894; i++)
                PKA_REGS->RAM[i] = 0;
        }

        PKA_REGS->RAM[PKA_ECDSA_SIGN_ORDER_LEN / 4] = req->order_size * 8;
        PKA_REGS->RAM[PKA_ECDSA_SIGN_MOD_LEN / 4] = sz * 8;
        PKA_REGS->RAM[PKA_ECDSA_SIGN_COEF_SIGN / 4] = req->coef_sign;

        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_COEF_A, req->coef_a, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_COEF_B, req->coef_b, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_MODULUS, req->modulus, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_BASE_X, req->base_x, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_BASE_Y, req->base_y, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_ORDER, req->prime_order, req->order_size);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_HASH, req->hash, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_INTEGER_K, req->random_k, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_PRIV_KEY, req->private_key, sz);

        pka_start(PKA_MODE_ECDSA_SIGN);
        ret = pka_wait_complete();
        if (ret != 0)
            return ret;

        {
            uint32_t osz = (PKA_REGS->RAM[PKA_ECDSA_SIGN_ORDER_LEN / 4] + 7) / 8;
            pka_read_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_RES_R, req->sig_r, osz);
            pka_read_bytes(PKA_REGS->RAM, PKA_ECDSA_SIGN_RES_S, req->sig_s, osz);
        }
        req->error = 0;
        return 0;
    }

    case IOCTL_PKA_ECDSA_VERIFY: {
        struct pka_ecdsa_verify_req *req = arg;
        uint32_t sz;
        if (!req || req->modulus_size > PKA_MAX_KEY_SIZE)
            return -EINVAL;
        sz = req->modulus_size;

        {
            uint32_t i;
            for (i = 0; i < 894; i++)
                PKA_REGS->RAM[i] = 0;
        }

        PKA_REGS->RAM[PKA_ECDSA_VERIF_ORDER_LEN / 4] = req->order_size * 8;
        PKA_REGS->RAM[PKA_ECDSA_VERIF_MOD_LEN / 4] = sz * 8;
        PKA_REGS->RAM[PKA_ECDSA_VERIF_COEF_SIGN / 4] = req->coef_sign;

        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_VERIF_COEF_A, req->coef_a, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_VERIF_MODULUS, req->modulus, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_VERIF_BASE_X, req->base_x, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_VERIF_BASE_Y, req->base_y, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_VERIF_ORDER, req->prime_order, req->order_size);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_VERIF_PUBKEY_X, req->pub_x, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_VERIF_PUBKEY_Y, req->pub_y, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_VERIF_R, req->sig_r, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_VERIF_S, req->sig_s, sz);
        pka_write_bytes(PKA_REGS->RAM, PKA_ECDSA_VERIF_HASH, req->hash, sz);

        pka_start(PKA_MODE_ECDSA_VERIF);
        ret = pka_wait_complete();
        if (ret != 0)
            return ret;

        req->valid = (PKA_REGS->RAM[PKA_ECDSA_VERIF_RESULT / 4] == 0) ? 1 : 0;
        return 0;
    }
    }
    return -EINVAL;
}

static int pka_close(struct fnode *fno)
{
    /* Clear PKA RAM (security) */
    uint32_t i;
    for (i = 0; i < 894; i++)
        PKA_REGS->RAM[i] = 0;
    PKA_REGS->CR = 0;
    RCC_AHB2ENR &= ~RCC_AHB2ENR_PKAEN;
    return 0;
}


/* ------------------------------------------------------------------ */
/* Unified open dispatcher                                             */
/* ------------------------------------------------------------------ */

static int crypto_open(const char *path, int flags)
{
    struct fnode *f = fno_search(path);
    if (!f)
        return -ENOENT;

    if (f == fno_hash)
        return hash_open(path, flags);
    if (f == fno_aes)
        return aes_open(path, flags);
    if (f == fno_pka)
        return pka_open(path, flags);
    return -ENODEV;
}

static int crypto_read(struct fnode *fno, void *buf, unsigned int len)
{
    if (fno == fno_hash)
        return hash_read(fno, buf, len);
    if (fno == fno_aes)
        return aes_read(fno, buf, len);
    return -EOPNOTSUPP;
}

static int crypto_write(struct fnode *fno, const void *buf, unsigned int len)
{
    if (fno == fno_hash)
        return hash_write(fno, buf, len);
    if (fno == fno_aes)
        return aes_write(fno, buf, len);
    return -EOPNOTSUPP;
}

static int crypto_ioctl(struct fnode *fno, const uint32_t cmd, void *arg)
{
    if (fno == fno_hash)
        return hash_ioctl(fno, cmd, arg);
    if (fno == fno_aes)
        return aes_ioctl(fno, cmd, arg);
    if (fno == fno_pka)
        return pka_ioctl(fno, cmd, arg);
    return -ENODEV;
}

static int crypto_close(struct fnode *fno)
{
    if (fno == fno_hash)
        return hash_close(fno);
    if (fno == fno_aes)
        return aes_close(fno);
    if (fno == fno_pka)
        return pka_close(fno);
    return -ENODEV;
}


/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

static struct module mod_crypto = {
};

void stm32crypto_init(struct fnode *dev)
{
    strncpy(mod_crypto.name, "stm32crypto", sizeof(mod_crypto.name) - 1);
    mod_crypto.family = FAMILY_DEV;
    mod_crypto.ops.open  = crypto_open;
    mod_crypto.ops.read  = crypto_read;
    mod_crypto.ops.write = crypto_write;
    mod_crypto.ops.ioctl = crypto_ioctl;
    mod_crypto.ops.close = crypto_close;

    fno_hash = fno_create(&mod_crypto, "hash", dev);
    fno_aes  = fno_create(&mod_crypto, "aes",  dev);
    fno_pka  = fno_create(&mod_crypto, "pka",  dev);

    register_module(&mod_crypto);
}
