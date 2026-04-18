#include <stdint.h>

#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/random.h>

#define FZ_ECC_PRIV_KEY_SIZE 32
#define FZ_ECC_PUB_KEY_SIZE  65
#define FZ_ECC_KEY_FILE_SIZE (FZ_ECC_PRIV_KEY_SIZE + FZ_ECC_PUB_KEY_SIZE)

struct fz_ecc_sign_args {
    const unsigned char *keybuf;
    const unsigned char *hash;
    unsigned char *sig;
    word32 *sig_len;
};

struct fz_ecc_verify_args {
    const unsigned char *keybuf;
    uint32_t key_len;
    const unsigned char *sig;
    uint32_t sig_len;
    const unsigned char *hash;
    int *verified;
};

int fz_wc_AesInitDefault(Aes *aes)
{
    return wc_AesInit(aes, NULL, -1);
}

int fz_wc_AesSetKeyEnc(Aes *aes, const void *key, const void *iv)
{
    return wc_AesSetKey(aes, key, 16, iv, AES_ENCRYPTION);
}

int fz_wc_AesSetKeyDec(Aes *aes, const void *key, const void *iv)
{
    return wc_AesSetKey(aes, key, 16, iv, AES_DECRYPTION);
}

int fz_wc_ecc_sign_p256(const struct fz_ecc_sign_args *args)
{
    ecc_key key;
    WC_RNG rng;
    int ret;

    ret = wc_ecc_init(&key);
    if (ret != 0)
        return ret;

    ret = wc_ecc_import_private_key_ex(
        args->keybuf, FZ_ECC_PRIV_KEY_SIZE,
        args->keybuf + FZ_ECC_PRIV_KEY_SIZE, FZ_ECC_PUB_KEY_SIZE,
        &key, ECC_SECP256R1);
    if (ret != 0) {
        wc_ecc_free(&key);
        return ret;
    }

    ret = wc_InitRng(&rng);
    if (ret != 0) {
        wc_ecc_free(&key);
        return ret;
    }

    ret = wc_ecc_sign_hash(args->hash, 32, args->sig, args->sig_len, &rng, &key);
    wc_FreeRng(&rng);
    wc_ecc_free(&key);
    return ret;
}

int fz_wc_ecc_verify_p256(const struct fz_ecc_verify_args *args)
{
    ecc_key key;
    int ret;

    ret = wc_ecc_init(&key);
    if (ret != 0)
        return ret;

    if (args->key_len == FZ_ECC_KEY_FILE_SIZE) {
        ret = wc_ecc_import_x963(args->keybuf + FZ_ECC_PRIV_KEY_SIZE,
                                 FZ_ECC_PUB_KEY_SIZE, &key);
    } else {
        ret = wc_ecc_import_x963(args->keybuf, FZ_ECC_PUB_KEY_SIZE, &key);
    }

    if (ret != 0) {
        wc_ecc_free(&key);
        return ret;
    }

    ret = wc_ecc_verify_hash(args->sig, args->sig_len, args->hash, 32,
                             args->verified, &key);
    wc_ecc_free(&key);
    return ret;
}
