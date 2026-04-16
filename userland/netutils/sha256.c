/*
 * sha256 - compute SHA-256 hash of stdin or a file
 *
 * Demonstrates dynamic linking: wc_InitSha256 / wc_Sha256Update /
 * wc_Sha256Final are resolved at load time from libwolfssl.so via
 * the shared library trampoline mechanism.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

/* wolfSSL SHA-256 API */
#include <wolfssl/wolfcrypt/sha256.h>

int main(int argc, char *argv[])
{
    wc_Sha256 sha;
    unsigned char hash[32];
    unsigned char buf[256];
    int fd, n, i;

    if (argc > 1) {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            printf("sha256: cannot open %s\n", argv[1]);
            return 1;
        }
    } else {
        fd = 0;  /* stdin */
    }

    if (wc_InitSha256(&sha) != 0) {
        printf("sha256: init failed\n");
        return 1;
    }

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (wc_Sha256Update(&sha, buf, n) != 0) {
            printf("sha256: update failed\n");
            return 1;
        }
    }

    if (wc_Sha256Final(&sha, hash) != 0) {
        printf("sha256: final failed\n");
        return 1;
    }

    for (i = 0; i < 32; i++)
        printf("%02x", hash[i]);
    printf("\n");

    if (fd > 0)
        close(fd);
    return 0;
}
