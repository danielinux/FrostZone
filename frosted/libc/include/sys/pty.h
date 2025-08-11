#ifndef SYS_PTY_H
#define SYS_PTY_H

char *ptsname(int fd);
int ptsname_r(int fd, char *buf, size_t buflen);
int grantpt(int fd);
int unlockpt(int fd);
int posix_openpt(int flags);
int openpt(int flags);

#endif
