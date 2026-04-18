/*
 * aes128 - AES-128-CBC encrypt/decrypt
 *
 * Usage:
 *   aes128 -e -k keyfile < plaintext > ciphertext
 *   aes128 -d -k keyfile < ciphertext > plaintext
 *
 * Key file must be exactly 16 bytes (raw binary).
 * Reads stdin, writes stdout. Input must be a multiple of 16 bytes.
 * A random 16-byte IV is prepended on encrypt and read on decrypt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/random.h>

int fz_wc_AesInitDefault(Aes *aes);
int fz_wc_AesSetKeyEnc(Aes *aes, const void *key, const void *iv);
int fz_wc_AesSetKeyDec(Aes *aes, const void *key, const void *iv);

static void usage(void)
{
    printf("Usage: aes128 -e|-d -k keyfile\n");
    printf("  -e        encrypt (stdin -> stdout)\n");
    printf("  -d        decrypt (stdin -> stdout)\n");
    printf("  -k file   16-byte binary key file\n");
}

static int read_exact(int fd, unsigned char *buf, int len)
{
    int n, total = 0;
    while (total < len) {
        n = read(fd, buf + total, len - total);
        if (n <= 0)
            return total;
        total += n;
    }
    return total;
}

int main(int argc, char *argv[])
{
    int encrypt = -1;
    const char *keyfile = NULL;
    unsigned char key[16];
    unsigned char iv[16];
    unsigned char inbuf[256];
    unsigned char outbuf[256];
    Aes aes;
    WC_RNG rng;
    int i, fd, n, ret;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0) {
            encrypt = 1;
        } else if (strcmp(argv[i], "-d") == 0) {
            encrypt = 0;
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            keyfile = argv[++i];
        } else {
            usage();
            return 1;
        }
    }

    if (encrypt < 0 || !keyfile) {
        usage();
        return 1;
    }

    /* Read key */
    fd = open(keyfile, O_RDONLY);
    if (fd < 0) {
        printf("aes128: cannot open key file: %s\n", keyfile);
        return 1;
    }
    if (read_exact(fd, key, 16) != 16) {
        printf("aes128: key file must be exactly 16 bytes\n");
        close(fd);
        return 1;
    }
    close(fd);

    ret = fz_wc_AesInitDefault(&aes);
    if (ret != 0) {
        printf("aes128: AES init failed (%d)\n", ret);
        return 1;
    }

    if (encrypt) {
        /* Generate random IV and write it first */
        ret = wc_InitRng(&rng);
        if (ret != 0) {
            printf("aes128: RNG init failed (%d)\n", ret);
            return 1;
        }
        ret = wc_RNG_GenerateBlock(&rng, iv, 16);
        wc_FreeRng(&rng);
        if (ret != 0) {
            printf("aes128: IV generation failed\n");
            return 1;
        }
        write(1, iv, 16);

        ret = fz_wc_AesSetKeyEnc(&aes, key, iv);
        if (ret != 0) {
            printf("aes128: set key failed (%d)\n", ret);
            return 1;
        }

        while ((n = read_exact(0, inbuf, 16)) == 16) {
            ret = wc_AesCbcEncrypt(&aes, outbuf, inbuf, 16);
            if (ret != 0) {
                printf("aes128: encrypt failed (%d)\n", ret);
                return 1;
            }
            write(1, outbuf, 16);
        }
        if (n != 0) {
            printf("aes128: input not a multiple of 16 bytes\n");
            return 1;
        }
    } else {
        /* Read IV from input */
        if (read_exact(0, iv, 16) != 16) {
            printf("aes128: missing IV in input\n");
            return 1;
        }

        ret = fz_wc_AesSetKeyDec(&aes, key, iv);
        if (ret != 0) {
            printf("aes128: set key failed (%d)\n", ret);
            return 1;
        }

        while ((n = read_exact(0, inbuf, 16)) == 16) {
            ret = wc_AesCbcDecrypt(&aes, outbuf, inbuf, 16);
            if (ret != 0) {
                printf("aes128: decrypt failed (%d)\n", ret);
                return 1;
            }
            write(1, outbuf, 16);
        }
    }

    wc_AesFree(&aes);
    return 0;
}
