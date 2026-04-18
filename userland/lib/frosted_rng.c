/*
 * Custom RNG backend for wolfSSL on Frosted.
 *
 * CUSTOM_RAND_GENERATE_BLOCK is defined in user_settings.h, so wolfSSL
 * calls our wc_GenerateSeed() instead of trying /dev/urandom.
 *
 * We read from Frosted's /dev/random (STM32 TRNG).
 */
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <fcntl.h>
#include <unistd.h>

int frosted_rand_seed(unsigned char *output, unsigned int sz)
{
    int fd, n, total = 0;

    fd = open("/dev/random", O_RDONLY);
    if (fd < 0) {
        /* Fallback: zero-fill for testing (NOT secure) */
        unsigned int i;
        for (i = 0; i < sz; i++)
            output[i] = (unsigned char)(i ^ 0xA5);
        return 0;
    }

    while ((unsigned int)total < sz) {
        n = read(fd, output + total, sz - total);
        if (n <= 0)
            break;
        total += n;
    }
    close(fd);

    return ((unsigned int)total == sz) ? 0 : -1;
}
