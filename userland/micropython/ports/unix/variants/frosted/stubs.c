#include <errno.h>
#include <reent.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

int mkdir(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

int fsync(int fd) {
    (void)fd;
    errno = ENOSYS;
    return -1;
}

char *realpath(const char *restrict path, char *restrict resolved_path) {
    (void)path;
    (void)resolved_path;
    errno = ENOSYS;
    return NULL;
}

int _rename_r(struct _reent *r, const char *old_path, const char *new_path) {
    if (r) {
        r->_errno = ENOSYS;
    }
    errno = ENOSYS;
    (void)old_path;
    (void)new_path;
    return -1;
}
