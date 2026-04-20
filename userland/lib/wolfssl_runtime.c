#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t word32;
typedef unsigned char byte;

#define WOLFSSL_SO_PATH "/bin/libwolfssl.so"

static void *wolfssl_handle;

int wolfssl_runtime_init(void)
{
    if (wolfssl_handle == NULL)
        wolfssl_handle = dlopen(WOLFSSL_SO_PATH, RTLD_NOW | RTLD_LOCAL);

    return (wolfssl_handle != NULL) ? 0 : -1;
}

static void *wolfssl_resolve(const char *symbol)
{
    void *fn;

    if (wolfssl_runtime_init() != 0)
        return NULL;

    fn = dlsym(wolfssl_handle, symbol);
    if (fn == NULL && errno == 0)
        errno = ENOENT;
    return fn;
}

__attribute__((constructor))
static void wolfssl_runtime_autoload(void)
{
    (void)wolfssl_runtime_init();
}

#define DEFINE_WOLFSSL_INT(name, decl, call)         \
    int name decl                                    \
    {                                                \
        typedef int (*fn_t) decl;                    \
        static fn_t fn;                              \
                                                     \
        if (fn == NULL)                              \
            fn = (fn_t)wolfssl_resolve(#name);       \
        if (fn == NULL)                              \
            return -1;                               \
        return fn call;                              \
    }

#define DEFINE_WOLFSSL_VOID(name, decl, call)        \
    void name decl                                   \
    {                                                \
        typedef void (*fn_t) decl;                   \
        static fn_t fn;                              \
                                                     \
        if (fn == NULL)                              \
            fn = (fn_t)wolfssl_resolve(#name);       \
        if (fn == NULL)                              \
            return;                                  \
        fn call;                                     \
    }

#define WOLFSSL_INT_APIS(X) \
    X(wc_InitSha256, (void *sha), (sha)) \
    X(wc_Sha256Update, (void *sha, const void *data, uint32_t len), (sha, data, len)) \
    X(wc_Sha256Final, (void *sha, void *hash), (sha, hash)) \
    X(wc_Sha256Hash, (const void *data, uint32_t len, void *hash), (data, len, hash)) \
    X(wc_AesInit, (void *aes, void *heap, int devId), (aes, heap, devId)) \
    X(wc_AesSetKey, (void *aes, const void *key, uint32_t len, const void *iv, int dir), (aes, key, len, iv, dir)) \
    X(wc_AesCbcEncrypt, (void *aes, void *out, const void *in, uint32_t sz), (aes, out, in, sz)) \
    X(wc_AesCbcDecrypt, (void *aes, void *out, const void *in, uint32_t sz), (aes, out, in, sz)) \
    X(wc_AesFree, (void *aes), (aes)) \
    X(fz_wc_AesInitDefault, (void *aes), (aes)) \
    X(fz_wc_AesSetKeyEnc, (void *aes, const void *key, const void *iv), (aes, key, iv)) \
    X(fz_wc_AesSetKeyDec, (void *aes, const void *key, const void *iv), (aes, key, iv)) \
    X(fz_wc_AesSetKeyArgs, (const void *args), (args)) \
    X(wc_ecc_init, (void *key), (key)) \
    X(wc_ecc_make_key, (void *rng, int keysize, void *key), (rng, keysize, key)) \
    X(wc_ecc_sign_hash, (const void *in, uint32_t inlen, void *out, uint32_t *outlen, void *rng, void *key), (in, inlen, out, outlen, rng, key)) \
    X(wc_ecc_verify_hash, (const void *sig, uint32_t siglen, const void *hash, uint32_t hashlen, int *res, void *key), (sig, siglen, hash, hashlen, res, key)) \
    X(wc_ecc_import_x963, (const void *in, uint32_t inlen, void *key), (in, inlen, key)) \
    X(wc_ecc_export_x963, (void *key, void *out, uint32_t *outlen), (key, out, outlen)) \
    X(wc_ecc_import_private_key_ex, (const void *priv, uint32_t privSz, const void *pub, uint32_t pubSz, void *key, int curve_id), (priv, privSz, pub, pubSz, key, curve_id)) \
    X(wc_ecc_export_private_only, (void *key, void *out, uint32_t *outlen), (key, out, outlen)) \
    X(fz_wc_ecc_sign_p256, (const void *args), (args)) \
    X(fz_wc_ecc_sign_hash_args, (const void *args), (args)) \
    X(fz_wc_ecc_sig_to_rs_args, (const void *args), (args)) \
    X(fz_wc_ecc_sign_der_args, (const void *args), (args)) \
    X(fz_wc_ecc_ecdh_server_args, (const void *args), (args)) \
    X(fz_wc_ecc_pub_from_der_args, (const void *args), (args)) \
    X(fz_wc_ecc_verify_p256, (const void *args), (args)) \
    X(wc_InitRng, (void *rng), (rng)) \
    X(wc_InitRng_ex, (void *rng, void *heap, int devId), (rng, heap, devId)) \
    X(wc_RNG_GenerateBlock, (void *rng, void *out, uint32_t sz), (rng, out, sz)) \
    X(wc_RNG_GenerateByte, (void *rng, void *out), (rng, out)) \
    X(wc_FreeRng, (void *rng), (rng)) \
    X(wc_InitSha256_ex, (void *sha, void *heap, int devId), (sha, heap, devId)) \
    X(wc_Sha256Copy, (void *src, void *dst), (src, dst)) \
    X(wc_Sha256GetHash, (void *sha, void *hash), (sha, hash)) \
    X(wc_AesSetIV, (void *aes, const void *iv), (aes, iv)) \
    X(wc_AesCtrEncrypt, (void *aes, void *out, const void *in, uint32_t sz), (aes, out, in, sz)) \
    X(wc_AesGcmSetKey, (void *aes, const void *key, uint32_t len), (aes, key, len)) \
    X(wc_AesGcmEncrypt, (void *aes, void *out, const void *in, uint32_t sz, const void *iv, uint32_t ivSz, void *tag, uint32_t tagSz, const void *authIn, uint32_t authInSz), (aes, out, in, sz, iv, ivSz, tag, tagSz, authIn, authInSz)) \
    X(wc_AesGcmDecrypt, (void *aes, void *out, const void *in, uint32_t sz, const void *iv, uint32_t ivSz, const void *tag, uint32_t tagSz, const void *authIn, uint32_t authInSz), (aes, out, in, sz, iv, ivSz, tag, tagSz, authIn, authInSz)) \
    X(wc_HmacInit, (void *hmac, void *heap, int devId), (hmac, heap, devId)) \
    X(wc_HmacSetKey, (void *hmac, int type, const void *key, uint32_t keyLen), (hmac, type, key, keyLen)) \
    X(wc_HmacUpdate, (void *hmac, const void *msg, uint32_t len), (hmac, msg, len)) \
    X(wc_HmacFinal, (void *hmac, void *hash), (hmac, hash)) \
    X(wc_HmacSizeByType, (int type), (type)) \
    X(wc_HKDF, (int type, const void *inKey, uint32_t inKeySz, const void *salt, uint32_t saltSz, const void *info, uint32_t infoSz, void *out, uint32_t outSz), (type, inKey, inKeySz, salt, saltSz, info, infoSz, out, outSz)) \
    X(wc_HKDF_Extract, (int type, const void *salt, uint32_t saltSz, const void *inKey, uint32_t inKeySz, void *out), (type, salt, saltSz, inKey, inKeySz, out)) \
    X(wc_HKDF_Expand, (int type, const void *inKey, uint32_t inKeySz, const void *info, uint32_t infoSz, void *out, uint32_t outSz), (type, inKey, inKeySz, info, infoSz, out, outSz)) \
    X(wc_ecc_init_ex, (void *key, void *heap, int devId), (key, heap, devId)) \
    X(wc_ecc_make_key_ex, (void *rng, int keysize, void *key, int curve_id), (rng, keysize, key, curve_id)) \
    X(wc_ecc_import_x963_ex, (const void *in, uint32_t inlen, void *key, int curve_id), (in, inlen, key, curve_id)) \
    X(wc_ecc_shared_secret, (void *private_key, void *public_key, void *out, uint32_t *outlen), (private_key, public_key, out, outlen)) \
    X(wc_ecc_size, (void *key), (key)) \
    X(wc_ecc_sig_size, (void *key), (key)) \
    X(wc_ecc_set_rng, (void *key, void *rng), (key, rng)) \
    X(wc_ecc_check_key, (void *key), (key)) \
    X(wc_ecc_get_curve_id, (int idx), (idx)) \
    X(wc_ecc_get_curve_size_from_id, (int curve_id), (curve_id)) \
    X(wc_ecc_rs_raw_to_sig, (const void *r, uint32_t rSz, const void *s, uint32_t sSz, void *out, uint32_t *outlen), (r, rSz, s, sSz, out, outlen)) \
    X(wc_ecc_sig_to_rs, (const void *sig, uint32_t sigLen, void *r, uint32_t *rLen, void *s, uint32_t *sLen), (sig, sigLen, r, rLen, s, sLen)) \
    X(wc_EccPublicKeyDecode, (const void *input, uint32_t *inOutIdx, void *key, uint32_t inSz), (input, inOutIdx, key, inSz)) \
    X(wc_EccPrivateKeyDecode, (const void *input, uint32_t *inOutIdx, void *key, uint32_t inSz), (input, inOutIdx, key, inSz)) \
    X(wc_SignatureVerify, (int hash_type, int sig_type, const void *data, uint32_t dataSz, const void *sig, uint32_t sigSz, const void *key, uint32_t keySz), (hash_type, sig_type, data, dataSz, sig, sigSz, key, keySz)) \
    X(wc_SignatureVerifyHash, (int hash_type, int sig_type, const void *hash, uint32_t hashSz, const void *sig, uint32_t sigSz, const void *key, uint32_t keySz), (hash_type, sig_type, hash, hashSz, sig, sigSz, key, keySz)) \
    X(wc_Hash, (int hash_type, const void *data, uint32_t dataSz, void *hash, uint32_t hashSz), (hash_type, data, dataSz, hash, hashSz)) \
    X(fz_wc_HashArgs, (const void *args), (args)) \
    X(wolfCrypt_Init, (void), ()) \
    X(wolfCrypt_Cleanup, (void), ()) \
    X(Base64_Decode, (const void *in, uint32_t inLen, void *out, uint32_t *outLen), (in, inLen, out, outLen)) \
    X(Base64_Encode, (const void *in, uint32_t inLen, void *out, uint32_t *outLen), (in, inLen, out, outLen)) \
    X(wc_KeyPemToDer, (const void *pem, int pemSz, void *buff, int buffSz, const char *pass), (pem, pemSz, buff, buffSz, pass)) \
    X(wc_HashInit, (void *hash, int type), (hash, type)) \
    X(wc_HashUpdate, (void *hash, int type, const void *data, uint32_t sz), (hash, type, data, sz)) \
    X(wc_HashFinal, (void *hash, int type, void *out), (hash, type, out)) \
    X(wc_HashFree, (void *hash, int type), (hash, type)) \
    X(wc_HashGetDigestSize, (int type), (type))

#define WOLFSSL_VOID_APIS(X) \
    X(wc_ecc_free, (void *key), (key)) \
    X(wc_Sha256Free, (void *sha), (sha)) \
    X(wc_HmacFree, (void *hmac), (hmac))

WOLFSSL_INT_APIS(DEFINE_WOLFSSL_INT)
WOLFSSL_VOID_APIS(DEFINE_WOLFSSL_VOID)
