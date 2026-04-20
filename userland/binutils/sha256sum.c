/*
 * sha256sum - compute SHA-256 hash of files or stdin
 *
 * Uses wolfSSL shared library (libwolfssl.so) for SHA-256 via
 * a userspace dlopen()/dlsym() resolver shim.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

/* wolfSSL SHA-256 API */
#include <wolfssl/wolfcrypt/sha256.h>

static int sha256_fd(int fd, const char *name)
{
    wc_Sha256 sha;
    unsigned char hash[32];
    unsigned char buf[256];
    int n, i;

    if (wc_InitSha256(&sha) != 0) {
        printf("sha256sum: init failed\n");
        return 1;
    }

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (wc_Sha256Update(&sha, buf, n) != 0) {
            printf("sha256sum: update failed\n");
            return 1;
        }
    }

    if (wc_Sha256Final(&sha, hash) != 0) {
        printf("sha256sum: final failed\n");
        return 1;
    }

    for (i = 0; i < 32; i++)
        printf("%02x", hash[i]);
    printf("  %s\n", name);
    return 0;
}

int main(int argc, char *argv[])
{
    int i, fd, rc = 0;

    if (argc < 2)
        return sha256_fd(0, "-");

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            rc |= sha256_fd(0, "-");
        } else {
            fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                printf("sha256sum: %s: No such file\n", argv[i]);
                rc = 1;
                continue;
            }
            rc |= sha256_fd(fd, argv[i]);
            close(fd);
        }
    }
    return rc;
}
