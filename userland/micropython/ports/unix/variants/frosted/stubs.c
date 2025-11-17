#include <errno.h>
#include <reent.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int mkdir(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    fprintf(stderr, "frosted stub: mkdir(%s) not supported\n", path ? path : "(null)");
    errno = ENOSYS;
    return -1;
}

int fsync(int fd) {
    (void)fd;
    fprintf(stderr, "frosted stub: fsync(%d) not supported\n", fd);
    errno = ENOSYS;
    return -1;
}

int _rename_r(struct _reent *r, const char *old_path, const char *new_path) {
    if (r) {
        r->_errno = ENOSYS;
    }
    errno = ENOSYS;
    fprintf(stderr, "frosted stub: rename(%s -> %s) not supported\n",
        old_path ? old_path : "(null)", new_path ? new_path : "(null)");
    (void)old_path;
    (void)new_path;
    return -1;
}
