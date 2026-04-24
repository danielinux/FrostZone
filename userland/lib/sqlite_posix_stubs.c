/*
 * sqlite_posix_stubs.c - minimal POSIX shims for SQLite on Frosted
 *
 * SQLite's unix VFS and the interactive shell pull in a handful of
 * POSIX calls that Frosted's newlib/libgloss do not implement. Providing
 * weak no-op stubs here lets libsqlite.so link cleanly; the behavior is
 * "file exists / no-op on success" for the common cases. This also
 * satisfies stat()/lstat() so libgloss's stat.o (which would bring in a
 * second fstat definition and fight libc) is never pulled in.
 *
 * These are weak by design: if a real Frosted syscall gets wired up for
 * any of them later, the real definition transparently takes over.
 */

#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

struct stat;
struct passwd;
struct rusage;

__attribute__((weak))
int stat(const char *path, struct stat *buf)
{
    (void)path; (void)buf;
    errno = ENOSYS;
    return -1;
}

__attribute__((weak))
int lstat(const char *path, struct stat *buf)
{
    (void)path; (void)buf;
    errno = ENOSYS;
    return -1;
}

__attribute__((weak))
int fsync(int fd)
{
    (void)fd;
    return 0;
}

__attribute__((weak))
int fdatasync(int fd)
{
    (void)fd;
    return 0;
}

__attribute__((weak))
int access(const char *path, int mode)
{
    (void)path; (void)mode;
    errno = ENOENT;
    return -1;
}

__attribute__((weak))
int fchmod(int fd, unsigned int mode)
{
    (void)fd; (void)mode;
    return 0;
}

__attribute__((weak))
int fchown(int fd, unsigned int owner, unsigned int group)
{
    (void)fd; (void)owner; (void)group;
    return 0;
}

__attribute__((weak))
unsigned int geteuid(void)
{
    return 0;
}

__attribute__((weak))
unsigned int getuid(void)
{
    return 0;
}

__attribute__((weak))
unsigned int getegid(void)
{
    return 0;
}

__attribute__((weak))
unsigned int getgid(void)
{
    return 0;
}

__attribute__((weak))
struct passwd *getpwuid(unsigned int uid)
{
    (void)uid;
    return NULL;
}

__attribute__((weak))
int getrusage(int who, struct rusage *usage)
{
    (void)who; (void)usage;
    errno = ENOSYS;
    return -1;
}

__attribute__((weak))
void *popen(const char *cmd, const char *mode)
{
    (void)cmd; (void)mode;
    errno = ENOSYS;
    return NULL;
}

__attribute__((weak))
int pclose(void *stream)
{
    (void)stream;
    return -1;
}
