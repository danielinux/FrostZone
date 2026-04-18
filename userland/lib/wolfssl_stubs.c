/*
 * Stub functions for wolfSSL shared library imports.
 * These provide symbols for the linker to resolve against.
 * At runtime, the kernel's GOT resolver replaces these GOT
 * entries with trampolines to the actual shared library.
 */
#include <stdint.h>

/* SHA-256 stubs */
int wc_InitSha256(void *sha) { return -1; }
int wc_Sha256Update(void *sha, const void *data, uint32_t len) { return -1; }
int wc_Sha256Final(void *sha, void *hash) { return -1; }
int wc_Sha256Hash(const void *data, uint32_t len, void *hash) { return -1; }

/* AES stubs */
int wc_AesInit(void *aes, void *heap, int devId) { return -1; }
int wc_AesSetKey(void *aes, const void *key, uint32_t len, const void *iv, int dir) { return -1; }
int wc_AesCbcEncrypt(void *aes, void *out, const void *in, uint32_t sz) { return -1; }
int wc_AesCbcDecrypt(void *aes, void *out, const void *in, uint32_t sz) { return -1; }
int wc_AesFree(void *aes) { return -1; }
int fz_wc_AesInitDefault(void *aes) { return -1; }
int fz_wc_AesSetKeyEnc(void *aes, const void *key, const void *iv) { return -1; }
int fz_wc_AesSetKeyDec(void *aes, const void *key, const void *iv) { return -1; }
int fz_wc_AesSetKeyArgs(const void *args) { return -1; }

/* ECC stubs */
int wc_ecc_init(void *key) { return -1; }
void wc_ecc_free(void *key) { }
int wc_ecc_make_key(void *rng, int keysize, void *key) { return -1; }
int wc_ecc_sign_hash(const void *in, uint32_t inlen, void *out, uint32_t *outlen, void *rng, void *key) { return -1; }
int wc_ecc_verify_hash(const void *sig, uint32_t siglen, const void *hash, uint32_t hashlen, int *res, void *key) { return -1; }
int wc_ecc_import_x963(const void *in, uint32_t inlen, void *key) { return -1; }
int wc_ecc_export_x963(void *key, void *out, uint32_t *outlen) { return -1; }
int wc_ecc_import_private_key_ex(const void *priv, uint32_t privSz, const void *pub, uint32_t pubSz, void *key, int curve_id) { return -1; }
int wc_ecc_export_private_only(void *key, void *out, uint32_t *outlen) { return -1; }
int fz_wc_ecc_sign_p256(const void *args) { return -1; }
int fz_wc_ecc_sign_hash_args(const void *args) { return -1; }
int fz_wc_ecc_sig_to_rs_args(const void *args) { return -1; }
int fz_wc_ecc_sign_der_args(const void *args) { return -1; }
int fz_wc_ecc_ecdh_server_args(const void *args) { return -1; }
int fz_wc_ecc_pub_from_der_args(const void *args) { return -1; }
int fz_wc_ecc_verify_p256(const void *args) { return -1; }

/* RNG stubs */
int wc_InitRng(void *rng) { return -1; }
int wc_InitRng_ex(void *rng, void *heap, int devId) { return -1; }
int wc_RNG_GenerateBlock(void *rng, void *out, uint32_t sz) { return -1; }
int wc_RNG_GenerateByte(void *rng, void *out) { return -1; }
int wc_FreeRng(void *rng) { return -1; }

/* Additional SHA-256 stubs (for wolfSSH) */
int wc_InitSha256_ex(void *sha, void *heap, int devId) { return -1; }
void wc_Sha256Free(void *sha) { }
int wc_Sha256Copy(void *src, void *dst) { return -1; }
int wc_Sha256GetHash(void *sha, void *hash) { return -1; }

/* AES-CTR / GCM / SetIV stubs (for wolfSSH transport encryption) */
int wc_AesSetIV(void *aes, const void *iv) { return -1; }
int wc_AesCtrEncrypt(void *aes, void *out, const void *in, uint32_t sz) { return -1; }
int wc_AesGcmSetKey(void *aes, const void *key, uint32_t len) { return -1; }
int wc_AesGcmEncrypt(void *aes, void *out, const void *in, uint32_t sz,
                     const void *iv, uint32_t ivSz,
                     void *tag, uint32_t tagSz,
                     const void *authIn, uint32_t authInSz) { return -1; }
int wc_AesGcmDecrypt(void *aes, void *out, const void *in, uint32_t sz,
                     const void *iv, uint32_t ivSz,
                     const void *tag, uint32_t tagSz,
                     const void *authIn, uint32_t authInSz) { return -1; }

/* HMAC stubs (for wolfSSH MAC) */
int wc_HmacInit(void *hmac, void *heap, int devId) { return -1; }
int wc_HmacSetKey(void *hmac, int type, const void *key, uint32_t keyLen) { return -1; }
int wc_HmacUpdate(void *hmac, const void *msg, uint32_t len) { return -1; }
int wc_HmacFinal(void *hmac, void *hash) { return -1; }
int wc_HmacSizeByType(int type) { return -1; }
void wc_HmacFree(void *hmac) { }

/* HKDF stubs (for wolfSSH KEX) */
int wc_HKDF(int type, const void *inKey, uint32_t inKeySz,
            const void *salt, uint32_t saltSz,
            const void *info, uint32_t infoSz,
            void *out, uint32_t outSz) { return -1; }
int wc_HKDF_Extract(int type, const void *salt, uint32_t saltSz,
                    const void *inKey, uint32_t inKeySz, void *out) { return -1; }
int wc_HKDF_Expand(int type, const void *inKey, uint32_t inKeySz,
                   const void *info, uint32_t infoSz,
                   void *out, uint32_t outSz) { return -1; }

/* ECC extra stubs */
int wc_ecc_init_ex(void *key, void *heap, int devId) { return -1; }
int wc_ecc_make_key_ex(void *rng, int keysize, void *key, int curve_id) { return -1; }
int wc_ecc_import_x963_ex(const void *in, uint32_t inlen, void *key, int curve_id) { return -1; }
int wc_ecc_shared_secret(void *private_key, void *public_key, void *out, uint32_t *outlen) { return -1; }
int wc_ecc_size(void *key) { return -1; }
int wc_ecc_sig_size(void *key) { return -1; }
int wc_ecc_set_rng(void *key, void *rng) { return -1; }
int wc_ecc_check_key(void *key) { return -1; }
int wc_ecc_get_curve_id(int idx) { return -1; }
int wc_ecc_get_curve_size_from_id(int curve_id) { return -1; }
int wc_ecc_rs_raw_to_sig(const void *r, uint32_t rSz, const void *s, uint32_t sSz,
                         void *out, uint32_t *outlen) { return -1; }
int wc_ecc_sig_to_rs(const void *sig, uint32_t sigLen, void *r, uint32_t *rLen,
                     void *s, uint32_t *sLen) { return -1; }

/* ASN / signature abstraction stubs */
int wc_EccPublicKeyDecode(const void *input, uint32_t *inOutIdx, void *key,
                          uint32_t inSz) { return -1; }
int wc_EccPrivateKeyDecode(const void *input, uint32_t *inOutIdx, void *key,
                           uint32_t inSz) { return -1; }
int wc_SignatureVerify(int hash_type, int sig_type, const void *data, uint32_t dataSz,
                       const void *sig, uint32_t sigSz, const void *key,
                       uint32_t keySz) { return -1; }
int wc_SignatureVerifyHash(int hash_type, int sig_type, const void *hash, uint32_t hashSz,
                           const void *sig, uint32_t sigSz, const void *key,
                           uint32_t keySz) { return -1; }
int wc_Hash(int hash_type, const void *data, uint32_t dataSz, void *hash, uint32_t hashSz) { return -1; }
int fz_wc_HashArgs(const void *args) { return -1; }

/* Global wolfCrypt init/cleanup */
int wolfCrypt_Init(void) { return -1; }
int wolfCrypt_Cleanup(void) { return -1; }

/* Base64 + PEM — wolfSSH uses these during key decoding */
int Base64_Decode(const void *in, uint32_t inLen, void *out, uint32_t *outLen) { return -1; }
int Base64_Encode(const void *in, uint32_t inLen, void *out, uint32_t *outLen) { return -1; }
int wc_KeyPemToDer(const void *pem, int pemSz, void *buff, int buffSz, const char *pass) { return -1; }

/* Hash abstraction stubs */
int wc_HashInit(void *hash, int type) { return -1; }
int wc_HashUpdate(void *hash, int type, const void *data, uint32_t sz) { return -1; }
int wc_HashFinal(void *hash, int type, void *out) { return -1; }
int wc_HashFree(void *hash, int type) { return -1; }
int wc_HashGetDigestSize(int type) { return -1; }
