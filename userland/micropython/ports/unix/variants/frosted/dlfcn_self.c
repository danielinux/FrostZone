/*
 * Self-handle dlopen/dlsym/dlclose for Frosted.
 *
 * MicroPython's FFI module calls dlopen(NULL) to get a handle to
 * the current process, then dlsym() to resolve POSIX symbols.
 * On Frosted all POSIX functions are statically linked via libgloss,
 * so we resolve them from a compile-time table.
 *
 * For non-NULL dlopen paths, we delegate to the kernel syscalls
 * which load bFLT shared libraries from xipfs.
 *
 * Overrides the libgloss weak definitions via --allow-multiple-definition.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

/* Kernel syscall wrappers (from frosted_syscalls.c / libgloss) */
extern int sys_dlopen(uint32_t arg1, uint32_t arg2);
extern int sys_dlsym(uint32_t arg1, uint32_t arg2);
extern int sys_dlclose(uint32_t arg1);

#define SELF_HANDLE ((void *)0xFFFFFFFFu)

/* POSIX functions available in libgloss */
extern int open(const char *, int, ...);
extern int close(int);
extern int read(int, void *, size_t);
extern int write(int, const void *, size_t);
extern int fcntl(int, int, ...);
extern int ioctl(int, unsigned long, ...);
extern int select(int, void *, void *, void *, void *);
extern int poll(void *, unsigned long, int);
extern int socket(int, int, int);
extern int bind(int, const void *, unsigned int);
extern int connect(int, const void *, unsigned int);
extern int listen(int, int);
extern int accept(int, void *, void *);
extern int sendto(int, const void *, size_t, int, const void *, unsigned int);
extern int recvfrom(int, void *, size_t, int, void *, void *);
extern int setsockopt(int, int, int, const void *, unsigned int);
extern int getsockopt(int, int, int, void *, void *);
extern int shutdown(int, int);
extern int getpeername(int, void *, void *);
extern int getsockname(int, void *, void *);
extern int dup(int);
extern int dup2(int, int);
extern int pipe(int *);
extern int stat(const char *, void *);
extern int fstat(int, void *);
extern int mkdir(const char *, unsigned int);
extern int unlink(const char *);
extern int chdir(const char *);
extern char *getcwd(char *, size_t);
extern int opendir(const char *);
extern int readdir(int, void *);
extern int closedir(int);
extern int kill(int, int);
extern int getpid(void);
extern int getppid(void);
extern int waitpid(int, int *, int);
extern int sigaction(int, const void *, void *);
extern int sigprocmask(int, const void *, void *);
extern unsigned int sleep(unsigned int);
extern unsigned int alarm(unsigned int);
extern int clock_gettime(int, void *);

struct sym_entry {
    const char *name;
    void *addr;
};

#define SYM(fn) { #fn, (void *)fn }

static const struct sym_entry self_symbols[] = {
    SYM(open),
    SYM(close),
    SYM(read),
    SYM(write),
    SYM(fcntl),
    SYM(ioctl),
    SYM(select),
    SYM(poll),
    SYM(socket),
    SYM(bind),
    SYM(connect),
    SYM(listen),
    SYM(accept),
    SYM(sendto),
    SYM(recvfrom),
    SYM(setsockopt),
    SYM(getsockopt),
    SYM(shutdown),
    SYM(getpeername),
    SYM(getsockname),
    SYM(dup),
    SYM(dup2),
    SYM(pipe),
    SYM(stat),
    SYM(fstat),
    SYM(mkdir),
    SYM(unlink),
    SYM(chdir),
    SYM(getcwd),
    SYM(opendir),
    SYM(readdir),
    SYM(closedir),
    SYM(kill),
    SYM(getpid),
    SYM(getppid),
    SYM(waitpid),
    SYM(sigaction),
    SYM(sigprocmask),
    SYM(sleep),
    SYM(alarm),
    SYM(clock_gettime),
    { NULL, NULL }
};

static void *self_dlsym(const char *symbol)
{
    const struct sym_entry *e;
    for (e = self_symbols; e->name; e++) {
        if (strcmp(e->name, symbol) == 0)
            return e->addr;
    }
    return NULL;
}

void *dlopen(const char *path, int mode)
{
    int ret;
    if (!path)
        return SELF_HANDLE;
    ret = sys_dlopen((uint32_t)(uintptr_t)path, (uint32_t)mode);
    if (ret < 0) {
        errno = -ret;
        return NULL;
    }
    return (void *)(uintptr_t)ret;
}

void *dlsym(void *handle, const char *symbol)
{
    int ret;
    if (handle == SELF_HANDLE) {
        void *addr = self_dlsym(symbol);
        if (!addr)
            errno = ENOENT;
        return addr;
    }
    ret = sys_dlsym((uint32_t)(uintptr_t)handle, (uint32_t)(uintptr_t)symbol);
    if (ret < 0) {
        errno = -ret;
        return NULL;
    }
    return (void *)(uintptr_t)ret;
}

int dlclose(void *handle)
{
    if (handle == SELF_HANDLE)
        return 0;
    return sys_dlclose((uint32_t)(uintptr_t)handle);
}
