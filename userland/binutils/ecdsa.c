/*
 * ecdsa - ECDSA P-256 sign/verify
 *
 * Usage:
 *   ecdsa -g -k keyfile                 Generate keypair, write to keyfile
 *   ecdsa -s -k keyfile < data > sig    Sign stdin hash with private key
 *   ecdsa -v -k keyfile < sig           Verify signature (data on stdin after sig)
 *
 * Key format:
 *   Private key file: 32 bytes raw private scalar + 65 bytes X9.63 public key
 *   Public key file:  65 bytes X9.63 public key (04 || X || Y)
 *
 * Signature is DER-encoded ECDSA signature written to stdout (-s)
 * or read from a file argument (-v).
 *
 * The tool hashes the input data with SHA-256 before signing/verifying.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/asn_public.h>

#define PRIV_KEY_SIZE  32
#define PUB_KEY_SIZE   65  /* uncompressed X9.63: 04 || X || Y */
#define KEY_FILE_SIZE  (PRIV_KEY_SIZE + PUB_KEY_SIZE)  /* 97 */
#define MAX_SIG_SIZE   72  /* DER ECDSA P-256 max */
#define ECC_KEY_CURVE  ECC_SECP256R1

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
    word32 sig_len;
    const unsigned char *hash;
    int *verified;
};

int fz_wc_ecc_sign_p256(const struct fz_ecc_sign_args *args);
int fz_wc_ecc_verify_p256(const struct fz_ecc_verify_args *args);

static void usage(void)
{
    printf("Usage: ecdsa -g|-s|-v -k keyfile [sigfile]\n");
    printf("  -g          generate P-256 keypair -> keyfile (97 bytes)\n");
    printf("  -s          sign: hash stdin, write DER sig to stdout\n");
    printf("  -v sigfile  verify: read sig from sigfile, hash stdin\n");
    printf("  -k file     key file (97 bytes for -g/-s, 65 for -v pub only)\n");
}

static int hash_stdin(unsigned char hash[32])
{
    wc_Sha256 sha;
    unsigned char buf[256];
    int n;

    if (wc_InitSha256(&sha) != 0)
        return -1;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        if (wc_Sha256Update(&sha, buf, n) != 0)
            return -1;
    }
    if (wc_Sha256Final(&sha, hash) != 0)
        return -1;
    return 0;
}

static int do_generate(const char *keyfile)
{
    ecc_key key;
    WC_RNG rng;
    unsigned char priv[PRIV_KEY_SIZE];
    unsigned char pub[PUB_KEY_SIZE];
    word32 priv_len = PRIV_KEY_SIZE;
    word32 pub_len = PUB_KEY_SIZE;
    int fd, ret;

    ret = wc_InitRng(&rng);
    if (ret != 0) {
        printf("ecdsa: RNG init failed (%d)\n", ret);
        return 1;
    }

    ret = wc_ecc_init(&key);
    if (ret != 0) {
        printf("ecdsa: key init failed (%d)\n", ret);
        wc_FreeRng(&rng);
        return 1;
    }

    ret = wc_ecc_make_key(&rng, 32, &key);
    if (ret != 0) {
        printf("ecdsa: keygen failed (%d)\n", ret);
        goto err;
    }

    ret = wc_ecc_export_private_only(&key, priv, &priv_len);
    if (ret != 0) {
        printf("ecdsa: export private failed (%d)\n", ret);
        goto err;
    }

    ret = wc_ecc_export_x963(&key, pub, &pub_len);
    if (ret != 0) {
        printf("ecdsa: export public failed (%d)\n", ret);
        goto err;
    }

    fd = open(keyfile, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("ecdsa: cannot create %s\n", keyfile);
        goto err;
    }
    write(fd, priv, PRIV_KEY_SIZE);
    write(fd, pub, PUB_KEY_SIZE);
    close(fd);

    printf("ecdsa: P-256 keypair written to %s (%d bytes)\n",
           keyfile, KEY_FILE_SIZE);

    wc_ecc_free(&key);
    wc_FreeRng(&rng);
    return 0;

err:
    wc_ecc_free(&key);
    wc_FreeRng(&rng);
    return 1;
}

static int do_sign(const char *keyfile)
{
    unsigned char keybuf[KEY_FILE_SIZE];
    unsigned char hash[32];
    unsigned char sig[MAX_SIG_SIZE];
    word32 sig_len = MAX_SIG_SIZE;
    struct fz_ecc_sign_args args;
    int fd, n, ret;

    /* Read key file */
    fd = open(keyfile, O_RDONLY);
    if (fd < 0) {
        printf("ecdsa: cannot open %s\n", keyfile);
        return 1;
    }
    n = read(fd, keybuf, KEY_FILE_SIZE);
    close(fd);
    if (n != KEY_FILE_SIZE) {
        printf("ecdsa: key file must be %d bytes\n", KEY_FILE_SIZE);
        return 1;
    }

    /* Hash stdin */
    if (hash_stdin(hash) != 0) {
        printf("ecdsa: hash failed\n");
        return 1;
    }

    args.keybuf = keybuf;
    args.hash = hash;
    args.sig = sig;
    args.sig_len = &sig_len;
    ret = fz_wc_ecc_sign_p256(&args);
    if (ret != 0) {
        printf("ecdsa: sign failed (%d)\n", ret);
        return 1;
    }

    /* Write DER signature to stdout */
    write(1, sig, sig_len);
    return 0;
}

static int do_verify(const char *keyfile, const char *sigfile)
{
    unsigned char keybuf[KEY_FILE_SIZE];
    unsigned char hash[32];
    unsigned char sig[MAX_SIG_SIZE];
    struct fz_ecc_verify_args args;
    int fd, n, key_len, verified = 0, ret;

    /* Read key file (can be 97 bytes full or 65 bytes pub-only) */
    fd = open(keyfile, O_RDONLY);
    if (fd < 0) {
        printf("ecdsa: cannot open %s\n", keyfile);
        return 1;
    }
    key_len = read(fd, keybuf, KEY_FILE_SIZE);
    close(fd);
    if (key_len != KEY_FILE_SIZE && key_len != PUB_KEY_SIZE) {
        printf("ecdsa: key file must be %d or %d bytes\n",
               KEY_FILE_SIZE, PUB_KEY_SIZE);
        return 1;
    }

    /* Read signature from file */
    fd = open(sigfile, O_RDONLY);
    if (fd < 0) {
        printf("ecdsa: cannot open signature file: %s\n", sigfile);
        return 1;
    }
    n = read(fd, sig, MAX_SIG_SIZE);
    close(fd);
    if (n <= 0) {
        printf("ecdsa: empty signature\n");
        return 1;
    }

    /* Hash stdin */
    if (hash_stdin(hash) != 0) {
        printf("ecdsa: hash failed\n");
        return 1;
    }

    args.keybuf = keybuf;
    args.key_len = key_len;
    args.sig = sig;
    args.sig_len = n;
    args.hash = hash;
    args.verified = &verified;
    ret = fz_wc_ecc_verify_p256(&args);
    if (ret != 0) {
        printf("ecdsa: verify failed (%d)\n", ret);
        return 1;
    }

    if (verified)
        printf("ecdsa: signature OK\n");
    else
        printf("ecdsa: signature INVALID\n");
    return verified ? 0 : 1;
}

int main(int argc, char *argv[])
{
    int mode = 0;   /* 'g', 's', 'v' */
    const char *keyfile = NULL;
    const char *sigfile = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-g") == 0) {
            mode = 'g';
        } else if (strcmp(argv[i], "-s") == 0) {
            mode = 's';
        } else if (strcmp(argv[i], "-v") == 0) {
            mode = 'v';
            if (i + 1 < argc)
                sigfile = argv[++i];
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            keyfile = argv[++i];
        } else {
            usage();
            return 1;
        }
    }

    if (!mode || !keyfile) {
        usage();
        return 1;
    }

    switch (mode) {
    case 'g': return do_generate(keyfile);
    case 's': return do_sign(keyfile);
    case 'v':
        if (!sigfile) {
            printf("ecdsa: -v requires a signature file argument\n");
            usage();
            return 1;
        }
        return do_verify(keyfile, sigfile);
    }

    return 1;
}
