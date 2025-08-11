/* libc/sys/linux/sys/dirent.h - Directory entry as returned by readdir */
/* Written 2000 by Werner Almesberger */
#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H
#include <sys/types.h>
#define _LIBC 1
#define  NOT_IN_libc 1
#include <sys/lock.h>
#undef _LIBC
#define HAVE_NO_D_NAMLEN	/* no struct dirent->d_namlen */
#define HAVE_DD_LOCK  		/* have locking mechanism */
#define MAXNAMLEN 255		/* sizeof(struct dirent.d_name)-1 */
#define __dirfd(dir) (dir)->dd_fd
/* --- redundant --- */
typedef void DIR;

#define MAX_FILE 64
struct dirent {
    uint32_t d_ino;
    char d_name[MAX_FILE];
};

DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int readdir_r(DIR *, struct dirent *ep, struct dirent **result);
void rewinddir(DIR *);
int closedir(DIR *);
/* internal prototype */
void _seekdir(DIR *dir,off_t offset);
DIR *_opendir(const char *);
#ifndef _POSIX_SOURCE
long telldir (DIR *);
void seekdir (DIR *, off_t loc);
int scandir (const char *__dir,
             struct dirent ***__namelist,
             int (*select) (const struct dirent *),
             int (*compar) (const struct dirent **, const struct dirent **));
int alphasort (const struct dirent **__a, const struct dirent **__b);
#endif /* _POSIX_SOURCE */
#endif
