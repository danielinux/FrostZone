#include <stdint.h>
#include <string.h>

#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
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

struct fz_aes_setkey_args {
    Aes *aes;
    const void *key;
    uint32_t len;
    const void *iv;
    int dir;
};

struct fz_ecc_sign_hash_args {
    const unsigned char *hash;
    uint32_t hash_len;
    unsigned char *sig;
    word32 *sig_len;
    WC_RNG *rng;
    ecc_key *key;
};

struct fz_ecc_sig_to_rs_args {
    const unsigned char *sig;
    uint32_t sig_len;
    unsigned char *r;
    word32 *r_len;
    unsigned char *s;
    word32 *s_len;
};

struct fz_hash_args {
    int hash_type;
    const void *data;
    uint32_t data_sz;
    void *hash;
    uint32_t hash_sz;
};

struct fz_ecc_sign_der_args {
    const unsigned char *key_der;
    uint32_t key_der_len;
    const unsigned char *hash;
    uint32_t hash_len;
    unsigned char *sig;
    word32 *sig_len;
};

struct fz_ecc_ecdh_server_args {
    int curve_id;
    const unsigned char *peer_pub;
    uint32_t peer_pub_len;
    unsigned char *server_pub;
    word32 *server_pub_len;
    unsigned char *shared_secret;
    word32 *shared_secret_len;
};

struct fz_ecc_pub_from_der_args {
    const unsigned char *key_der;
    uint32_t key_der_len;
    unsigned char *pub;
    word32 *pub_len;
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

int fz_wc_AesSetKeyArgs(const struct fz_aes_setkey_args *args)
{
    return wc_AesSetKey(args->aes, args->key, args->len, args->iv, args->dir);
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

int fz_wc_ecc_sign_hash_args(const struct fz_ecc_sign_hash_args *args)
{
    return wc_ecc_sign_hash(args->hash, args->hash_len, args->sig,
                            args->sig_len, args->rng, args->key);
}

int fz_wc_ecc_sig_to_rs_args(const struct fz_ecc_sig_to_rs_args *args)
{
    return wc_ecc_sig_to_rs(args->sig, args->sig_len, args->r, args->r_len,
                            args->s, args->s_len);
}

int fz_wc_HashArgs(const struct fz_hash_args *args)
{
    return wc_Hash(args->hash_type, args->data, args->data_sz,
                   args->hash, args->hash_sz);
}

int fz_wc_ecc_sign_der_args(const struct fz_ecc_sign_der_args *args)
{
    ecc_key key;
    WC_RNG rng;
    word32 idx = 0;
    int ret;

    ret = wc_ecc_init(&key);
    if (ret != 0)
        return ret;

    ret = wc_EccPrivateKeyDecode(args->key_der, &idx, &key, args->key_der_len);
    if (ret != 0) {
        wc_ecc_free(&key);
        return ret;
    }

    ret = wc_InitRng(&rng);
    if (ret != 0) {
        wc_ecc_free(&key);
        return ret;
    }

    ret = wc_ecc_sign_hash(args->hash, args->hash_len, args->sig,
                           args->sig_len, &rng, &key);
    wc_FreeRng(&rng);
    wc_ecc_free(&key);
    return ret;
}

int fz_wc_ecc_ecdh_server_args(const struct fz_ecc_ecdh_server_args *args)
{
    ecc_key pubKey;
    ecc_key privKey;
    WC_RNG rng;
    int ret;

    ret = wc_ecc_init(&pubKey);
    if (ret != 0)
        return ret;

    ret = wc_ecc_init(&privKey);
    if (ret != 0) {
        wc_ecc_free(&pubKey);
        return ret;
    }

    ret = wc_ecc_import_x963_ex(args->peer_pub, args->peer_pub_len, &pubKey,
                                args->curve_id);
    if (ret != 0)
        goto out;

    ret = wc_InitRng(&rng);
    if (ret != 0)
        goto out;

    ret = wc_ecc_make_key_ex(&rng,
                             wc_ecc_get_curve_size_from_id(args->curve_id),
                             &privKey, args->curve_id);
    if (ret != 0) {
        wc_FreeRng(&rng);
        goto out;
    }

    ret = wc_ecc_export_x963(&privKey, args->server_pub, args->server_pub_len);
    if (ret == 0) {
        ret = wc_ecc_shared_secret(&privKey, &pubKey, args->shared_secret,
                                   args->shared_secret_len);
    }

    wc_FreeRng(&rng);

out:
    wc_ecc_free(&privKey);
    wc_ecc_free(&pubKey);
    return ret;
}

int fz_wc_ecc_pub_from_der_args(const struct fz_ecc_pub_from_der_args *args)
{
    const unsigned char *p = args->key_der;
    uint32_t len = args->key_der_len;
    uint32_t i;

    if (p == NULL || args->pub == NULL || args->pub_len == NULL)
        return BAD_FUNC_ARG;

    /* SEC1 ECPrivateKey: ... [1] BIT STRING publicKey ... */
    for (i = 0; i + 4 < len; i++) {
        if (p[i] != 0xa1)
            continue;
        if (i + 2 >= len)
            break;
        if (p[i + 2] != 0x03)
            continue;
        if (i + 4 >= len)
            break;

        /* Expect a short-form BIT STRING containing 0 unused bits. */
        {
            uint32_t bitstr_len = p[i + 3];
            if (bitstr_len < 2)
                continue;
            if (i + 4 + bitstr_len > len)
                continue;
            if (p[i + 4] != 0x00)
                continue;

            bitstr_len -= 1;
            if (*args->pub_len < bitstr_len)
                return BUFFER_E;

            memcpy(args->pub, p + i + 5, bitstr_len);
            *args->pub_len = bitstr_len;
            return 0;
        }
    }

    return ASN_PARSE_E;
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
