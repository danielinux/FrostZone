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

/* RNG stubs */
int wc_InitRng(void *rng) { return -1; }
int wc_RNG_GenerateBlock(void *rng, void *out, uint32_t sz) { return -1; }
int wc_FreeRng(void *rng) { return -1; }
