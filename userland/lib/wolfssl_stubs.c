/*
 * Stub functions for wolfSSL shared library imports.
 * These provide symbols for the linker to resolve against.
 * At runtime, the kernel's GOT resolver replaces these GOT
 * entries with trampolines to the actual shared library.
 */
#include <stdint.h>

/* SHA-256 stubs — ordinals must match wolfssl.exports */
int wc_InitSha256(void *sha) { return -1; }
int wc_Sha256Update(void *sha, const void *data, uint32_t len) { return -1; }
int wc_Sha256Final(void *sha, void *hash) { return -1; }
