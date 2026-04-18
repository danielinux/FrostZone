/* wolfSSH build configuration for Frosted's sshd app.
 *
 * Aimed at letting OpenSSH clients connect using:
 *   KEX         : ecdh-sha2-nistp256
 *   Host key    : ecdsa-sha2-nistp256
 *   Cipher      : aes128-ctr (aes256-ctr also compiled in)
 *   MAC         : hmac-sha2-256
 *   User auth   : password (root/toor, see sshd.c)
 *
 * Only one host-key and KEX algorithm is compiled in; everything else is
 * disabled to keep the app small. wolfCrypt primitives come from
 * libwolfssl.so via the stub archive + import map.
 */

#ifndef _FZ_SSHD_WOLFSSH_SETTINGS_H_
#define _FZ_SSHD_WOLFSSH_SETTINGS_H_

#define WOLFSSH_NO_SERVER              0   /* we are the server */
#define WOLFSSH_NO_CLIENT              1
#define WOLFSSH_NO_SFTP                1
#define WOLFSSH_NO_SCP                 1
#define WOLFSSH_NO_AGENT               1
#define WOLFSSH_NO_TERMINAL            0
#define WOLFSSH_TERM                   1
#define WOLFSSH_SHELL                  1
#define WOLFSSH_NO_FWD                 1
#define WOLFSSH_NO_CERTS               1

/* Host-key / public-key algorithms — only ECDSA P-256. */
#define WOLFSSH_NO_RSA                 1
#define WOLFSSH_NO_RSA_SHA2_256        1
#define WOLFSSH_NO_RSA_SHA2_512        1
#define WOLFSSH_NO_DH_GROUP1_SHA1      1
#define WOLFSSH_NO_DH_GROUP14_SHA1     1
#define WOLFSSH_NO_DH_GROUP14_SHA256   1
#define WOLFSSH_NO_DH_GEX_SHA256       1
#define WOLFSSH_NO_ED25519             1
#define WOLFSSH_NO_ECDSA_SHA2_NISTP384 1
#define WOLFSSH_NO_ECDSA_SHA2_NISTP521 1
#define WOLFSSH_NO_ECDH_SHA2_NISTP384  1
#define WOLFSSH_NO_ECDH_SHA2_NISTP521  1
#define WOLFSSH_NO_CURVE25519_SHA256   1
#define WOLFSSH_NO_NISTP256_MLKEM768_SHA256 1
/* This sshd only negotiates ecdh-sha2-nistp256, so the default wolfSSH
 * KEX scratch sizing for ML-KEM / large DH is excessive. Keep the inline
 * session buffers small enough to fit Frosted's allocator constraints. */
#define MAX_KEX_KEY_SZ                 256

/* Ciphers — keep AES-CTR only, drop AES-GCM/CBC/etc. */
#define WOLFSSH_NO_AES_CBC             1
#define WOLFSSH_NO_AES_GCM             1

/* MACs — HMAC-SHA-256 only. */
#define WOLFSSH_NO_HMAC_SHA1           1
#define WOLFSSH_NO_HMAC_SHA1_96        1
#define WOLFSSH_NO_HMAC_SHA2_512       1

/* Sub-protocols. */
#define WOLFSSH_IGNORE_UNIMPLEMENTED   1
#define DEFAULT_WINDOW_SZ              (16 * 1024)
#define DEFAULT_MAX_PACKET_SZ          (16 * 1024)

/* wolfSSL's own user_settings.h is pulled in through the -DWOLFSSL_USER_SETTINGS
 * flag applied at the compiler level (see netutils/Makefile). */

#endif /* _FZ_SSHD_WOLFSSH_SETTINGS_H_ */
