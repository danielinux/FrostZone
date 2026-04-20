/*
 *      sshd - minimal wolfSSH-based SSH daemon for Frosted.
 *
 *      One client at a time. Negotiates ECDSA-P256 / ECDH-SHA2-NISTP256
 *      / AES128-CTR / HMAC-SHA256 so a stock OpenSSH client connects.
 *      Password auth only: root/toor. On successful handshake + shell
 *      request, spawns /bin/fresh over a socketpair and relays the SSH
 *      channel to the child's stdio (telnetd-style, SSH-wrapped).
 *
 *      wolfCrypt primitives come from libwolfssl.so via a userspace
 *      dlopen()/dlsym() resolver archive; wolfSSH is linked statically.
 *
 *      SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdarg.h>

#include <wolfssh/ssh.h>
#include <wolfssh/internal.h>
#include <wolfssh/log.h>
#include <wolfssl/wolfcrypt/ecc.h>

#include "sshd_hostkey.h"            /* sshd_hostkey_der[] */

#define SSHD_PORT       22
#define SSHD_USER       "root"
#define SSHD_PASS       "toor"

static const char   FRESH_BIN[]       = "/bin/fresh";
static char * const fresh_argv[]      = { "fresh", NULL };

static const char  g_kex_algos[]      = "ecdh-sha2-nistp256";
static const char  g_hostkey_algo[]   = "ecdsa-sha2-nistp256";
static const char  g_cipher_algos[]   = "aes128-ctr,aes256-ctr";
static const char  g_mac_algos[]      = "hmac-sha2-256";
static const char  g_banner[]         = "frosted sshd\r\n";

static void diag_write(const char *msg)
{
    if (msg != NULL)
        (void)write(STDERR_FILENO, msg, strlen(msg));
}

static void dump_sys_mem(void)
{
    int fd;
    char buf[256];
    ssize_t n;

    fd = open("/sys/mem", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "sshd: open(/sys/mem) failed errno=%d\n", errno);
        return;
    }

    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        fprintf(stderr, "sshd: /sys/mem %s", buf);
    }
    close(fd);
}

__attribute__((noreturn))
void _exit(int status)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "sshd: _exit(%d)\n", status);
    diag_write(buf);
    sys_exit((uint32_t)status);
    for (;;)
        ;
}

__attribute__((noreturn))
void exit(int status)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "sshd: exit(%d)\n", status);
    diag_write(buf);
    _exit(status);
}

__attribute__((noreturn))
void abort(void)
{
    diag_write("sshd: abort()\n");
    _exit(134);
}

void __assert_func(const char *file, int line,
                   const char *func, const char *expr)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "sshd: assert %s:%d %s: %s\n",
             file ? file : "?", line, func ? func : "?",
             expr ? expr : "?");
    diag_write(buf);
    _exit(134);
}

/* ------------------------------------------------------------- */
/*                        user auth callback                      */
/* ------------------------------------------------------------- */
static int sshd_user_auth(byte authType,
                          WS_UserAuthData *data,
                          void *ctx)
{
    (void)ctx;

    if (authType != WOLFSSH_USERAUTH_PASSWORD)
        return WOLFSSH_USERAUTH_FAILURE;
    if (data->usernameSz != strlen(SSHD_USER) ||
        memcmp(data->username, SSHD_USER, data->usernameSz) != 0)
        return WOLFSSH_USERAUTH_INVALID_USER;
    if (data->sf.password.passwordSz != strlen(SSHD_PASS) ||
        memcmp(data->sf.password.password, SSHD_PASS,
               data->sf.password.passwordSz) != 0)
        return WOLFSSH_USERAUTH_INVALID_PASSWORD;
    return WOLFSSH_USERAUTH_SUCCESS;
}

/* ------------------------------------------------------------- */
/*                       context setup                            */
/* ------------------------------------------------------------- */
static WOLFSSH_CTX *sshd_make_ctx(void)
{
    int ret;
    WOLFSSH_CTX *ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL);
    if (!ctx) {
        fprintf(stderr, "sshd: wolfSSH_CTX_new failed\n");
        return NULL;
    }
    {
        ecc_key probe;
        word32 idx = 0;
        int ret = wc_ecc_init_ex(&probe, NULL, -2);
        if (ret == 0)
            ret = wc_EccPrivateKeyDecode(sshd_hostkey_der, &idx, &probe,
                                         sshd_hostkey_der_len);
        if (ret == 0)
            fprintf(stderr, "sshd: direct wc_EccPrivateKeyDecode ok idx=%lu curve=%d\n",
                    (unsigned long)idx, wc_ecc_get_curve_id(probe.idx));
        else
            fprintf(stderr, "sshd: direct wc_EccPrivateKeyDecode failed ret=%d idx=%lu\n",
                    ret, (unsigned long)idx);
        wc_ecc_free(&probe);
    }
    {
        byte *out = NULL;
        word32 out_sz = 0;
        const byte *out_type = NULL;
        word32 out_type_sz = 0;

        ret = wolfSSH_ReadKey_buffer(sshd_hostkey_der, sshd_hostkey_der_len,
                                     WOLFSSH_FORMAT_ASN1, &out, &out_sz,
                                     &out_type, &out_type_sz, NULL);
        fprintf(stderr, "sshd: wolfSSH_ReadKey_buffer ret=%d out_sz=%lu type_sz=%lu\n",
                ret, (unsigned long)out_sz, (unsigned long)out_type_sz);
        free(out);
    }
    ret = wolfSSH_CTX_UsePrivateKey_buffer(ctx, sshd_hostkey_der,
                                           sshd_hostkey_der_len,
                                           WOLFSSH_FORMAT_ASN1);
    if (ret != WS_SUCCESS) {
        fprintf(stderr, "sshd: wolfSSH_CTX_UsePrivateKey_buffer failed ret=%d\n",
                ret);
        wolfSSH_CTX_free(ctx);
        return NULL;
    }
    wolfSSH_CTX_SetAlgoListKex   (ctx, g_kex_algos);
    wolfSSH_CTX_SetAlgoListKey   (ctx, g_hostkey_algo);
    wolfSSH_CTX_SetAlgoListCipher(ctx, g_cipher_algos);
    wolfSSH_CTX_SetAlgoListMac   (ctx, g_mac_algos);
    wolfSSH_CTX_SetBanner        (ctx, g_banner);
    wolfSSH_SetUserAuth          (ctx, sshd_user_auth);
    return ctx;
}

/* ------------------------------------------------------------- */
/*                        per-client relay                        */
/* ------------------------------------------------------------- */
static void set_nonblock(int fd)
{
    int f = fcntl(fd, F_GETFL, 0);
    if (f >= 0)
        (void)fcntl(fd, F_SETFL, f | O_NONBLOCK);
}

#define SSHD_RELAY_BUF_SZ  512

static void serve_client(WOLFSSH_CTX *ctx, int client_fd)
{
    WOLFSSH *ssh    = wolfSSH_new(ctx);
    /* Frosted has no socketpair(); use two pipes — "to_child" carries
     * data SSH -> fresh, "to_parent" carries data fresh -> SSH. */
    int      to_child [2] = { -1, -1 };
    int      to_parent[2] = { -1, -1 };
    pid_t    child  = -1;
    /* Keep the relay buffer off the stack — wolfSSH's handshake paths
     * already use most of the task's 8 KB stack, so 512 bytes here is
     * too much. */
    char    *buf    = malloc(SSHD_RELAY_BUF_SZ);
    int      rc;

    fprintf(stderr, "sshd: serve_client fd=%d ctx=%p ssh=%p buf=%p\n",
            client_fd, ctx, ssh, buf);

    if (!ssh || !buf) {
        fprintf(stderr, "sshd: serve_client alloc failure ssh=%p buf=%p\n",
                ssh, buf);
        fprintf(stderr, "sshd: sizeof(WOLFSSH)=%u sizeof(WOLFSSH_CTX)=%u sizeof(HandshakeInfo)=%u\n",
                (unsigned)sizeof(WOLFSSH), (unsigned)sizeof(WOLFSSH_CTX),
                (unsigned)sizeof(HandshakeInfo));
        dump_sys_mem();
        free(buf);
        if (ssh) wolfSSH_free(ssh);
        close(client_fd);
        return;
    }
    /* RFC 8308 ext-info is optional. wolfSSH's current server ext-info packet
     * formatting is still not interoperable here, so suppress it for now. */
    ssh->sendExtInfo = 0;
    ssh->extInfoSent = 1;
    wolfSSH_set_fd(ssh, client_fd);
    fprintf(stderr, "sshd: entering wolfSSH_accept fd=%d\n", client_fd);

    rc = wolfSSH_accept(ssh);
    if (rc != WS_SUCCESS) {
        printf("sshd: handshake failed (rc=%d)\n", rc);
        goto out;
    }
    printf("sshd: handshake OK\n");

    if (pipe(to_child)  < 0) { perror("pipe");  goto out; }
    if (pipe(to_parent) < 0) { perror("pipe");  goto out; }

    child = vfork();
    if (child < 0) {
        perror("vfork");
        goto out;
    }
    if (child == 0) {
        /* Child: to_child[0] -> stdin, to_parent[1] -> stdout/stderr. */
        close(to_child[1]);
        close(to_parent[0]);
        close(client_fd);
        dup2(to_child[0],  STDIN_FILENO);
        dup2(to_parent[1], STDOUT_FILENO);
        dup2(to_parent[1], STDERR_FILENO);
        if (to_child[0]  > STDERR_FILENO) close(to_child[0]);
        if (to_parent[1] > STDERR_FILENO) close(to_parent[1]);
        execv(FRESH_BIN, fresh_argv);
        _exit(127);
    }
    close(to_child[0]);
    close(to_parent[1]);
    set_nonblock(to_parent[0]);

    for (;;) {
        int n;

        n = wolfSSH_stream_read(ssh, (byte *)buf, SSHD_RELAY_BUF_SZ);
        if (n > 0) {
            if (write(to_child[1], buf, n) < 0 && errno != EAGAIN)
                break;
        } else if (n != WS_WANT_READ && n != WS_WANT_WRITE) {
            break;
        }

        n = read(to_parent[0], buf, SSHD_RELAY_BUF_SZ);
        if (n > 0) {
            if (wolfSSH_stream_send(ssh, (byte *)buf, n) < 0)
                break;
        } else if (n == 0) {
            break;                     /* child closed its stdout */
        } else if (errno != EAGAIN) {
            break;
        }
    }

out:
    if (to_child[1]  >= 0) close(to_child[1]);
    if (to_parent[0] >= 0) close(to_parent[0]);
    free(buf);
    wolfSSH_shutdown(ssh);
    wolfSSH_free(ssh);
    close(client_fd);
}

/* ------------------------------------------------------------- */
/*                             main                               */
/* ------------------------------------------------------------- */
#ifndef APP_SSHD_MODULE
int main(int argc, char *argv[])
#else
int icebox_sshd(int argc, char *argv[])
#endif
{
    WOLFSSH_CTX       *ctx;
    struct sockaddr_in sa;
    int                lsd;
    int                one = 1;
    (void)argc; (void)argv;

    /* wolfSSH's debug callback chain currently destabilizes the app's
     * PIC static base on this target. Leave runtime debug logging off by
     * default so normal daemon startup stays on the non-logging path. */

    if (wolfSSH_Init() != WS_SUCCESS) {
        fprintf(stderr, "wolfSSH_Init failed\n");
        return 1;
    }
    ctx = sshd_make_ctx();
    if (!ctx) {
        fprintf(stderr, "wolfSSH context setup failed\n");
        return 1;
    }

    lsd = socket(AF_INET, SOCK_STREAM, 0);
    if (lsd < 0)                                  { perror("socket"); return 1; }
    (void)setsockopt(lsd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(SSHD_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(lsd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(lsd, 2) < 0) { perror("listen"); return 1; }

    printf("sshd: listening on port %d (user=%s pass=%s)\n",
           SSHD_PORT, SSHD_USER, SSHD_PASS);

    for (;;) {
        struct sockaddr_in peer;
        socklen_t          plen = sizeof(peer);
        int cs = accept(lsd, (struct sockaddr *)&peer, &plen);
        if (cs < 0) {
            if (errno == EINTR) continue;
            perror("accept"); break;
        }
        printf("sshd: accepted fd=%d\n", cs);
        serve_client(ctx, cs);
    }

    wolfSSH_CTX_free(ctx);
    wolfSSH_Cleanup();
    close(lsd);
    return 0;
}
