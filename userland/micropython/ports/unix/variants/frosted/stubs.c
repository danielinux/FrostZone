#include <errno.h>
#include <reent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Frosted's VFS doesn't support lstat/symlink resolution, and newlib's
 * realpath allocates ~2.4 KB on the stack (2x PATH_MAX) which is too
 * much for the 8 KB task stack.  Return a copy of the input path.
 */
char *realpath(const char *path, char *resolved_path) {
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    if (resolved_path) {
        strcpy(resolved_path, path);
        return resolved_path;
    }
    return strdup(path);
}

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
