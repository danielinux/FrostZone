#ifndef _FROSTED_STAT_H
#define _FROSTED_STAT_H

#include <stdint.h>
#include <time.h>

typedef uint32_t mode_t;
typedef short dev_t;
typedef short ino_t;
typedef unsigned short nlink_t;
typedef unsigned short uid_t;
typedef unsigned short gid_t;
typedef int32_t  off_t;
typedef int64_t  time_t;


#ifdef OLD_FROSTED
struct stat {
    dev_t     st_dev;     /* ID of device containing file */
    ino_t     st_ino;     /* inode number */
    mode_t    st_mode;    /* protection */
    nlink_t   st_nlink;   /* number of hard links */
    uid_t     st_uid;     /* user ID of owner */
    gid_t     st_gid;     /* group ID of owner */
    dev_t     st_rdev;    /* device ID (if special file) */
    off_t     st_size;    /* total size, in bytes */
    time_t    st_atime;   /* time of last access */
    time_t    st_mtime;   /* time of last modification */
    time_t    st_ctime;   /* time of last status change */
};
#endif

struct __attribute__((packed)) stat 
{
  dev_t		st_dev;
  ino_t		st_ino;
  mode_t	st_mode;
  nlink_t	st_nlink;
  uid_t		st_uid;
  gid_t		st_gid;
  dev_t		st_rdev;
  off_t		st_size;
  struct timespec st_atim;
  struct timespec st_mtim;
  struct timespec st_ctim;
  uint32_t st_blksize;
  uint32_t	st_blocks;
};

/* File type bits */
#define S_IFMT   0170000 /* type of file */
#define S_IFSOCK 0140000 /* socket */
#define S_IFLNK  0120000 /* symbolic link */
#define S_IFREG  0100000 /* regular */
#define S_IFBLK  0060000 /* block special */
#define S_IFDIR  0040000 /* directory */
#define S_IFCHR  0020000 /* character special */
#define S_IFIFO  0010000 /* FIFO */

/* File mode bits */
#define S_ISUID  0004000 /* set-user-ID bit */
#define S_ISGID  0002000 /* set-group-ID bit */
#define S_ISVTX  0001000 /* sticky bit */
#define S_IRWXU  00700   /* owner: rwx */
#define S_IRUSR  00400   /* owner: read */
#define S_IWUSR  00200   /* owner: write */
#define S_IXUSR  00100   /* owner: execute */
#define S_IRWXG  00070   /* group: rwx */
#define S_IRGRP  00040   /* group: read */
#define S_IWGRP  00020   /* group: write */
#define S_IXGRP  00010   /* group: execute */
#define S_IRWXO  00007   /* others: rwx */
#define S_IROTH  00004   /* others: read */
#define S_IWOTH  00002   /* others: write */
#define S_IXOTH  00001   /* others: execute */

#endif /* _FROSTED_STAT_H */
