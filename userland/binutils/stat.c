#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

static void show_stat(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0) {
        perror(path);
        return;
    }
    printf("  File: %s\n", path);
    printf("  Size: %lu\tBlocks: %lu\tMode: %o\n",
           (unsigned long)st.st_size, (unsigned long)st.st_blocks,
           (unsigned int)st.st_mode);
    printf("Device: %lu\tInode: %lu\tLinks: %lu\n",
           (unsigned long)st.st_dev, (unsigned long)st.st_ino,
           (unsigned long)st.st_nlink);
    printf("Access: %u\tUid: %u\tGid: %u\n",
           (unsigned int)st.st_mode, (unsigned int)st.st_uid,
           (unsigned int)st.st_gid);
    printf("Access: %lu\nModify: %lu\nChange: %lu\n",
           (unsigned long)st.st_atime, (unsigned long)st.st_mtime,
           (unsigned long)st.st_ctime);
}

#ifndef APP_STAT_MODULE
int main(int argc, char *argv[])
#else
int icebox_stat(int argc, char *argv[])
#endif
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file> [file...]\n", argv[0]);
        return 1;
    }
    for (int i = 1; i < argc; i++)
        show_stat(argv[i]);
    return 0;
}
