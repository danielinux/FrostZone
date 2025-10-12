#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int copy_file(const char *src, const char *dst)
{
    int sfd;
    int dfd;
    char buf[256];
    ssize_t r;
    sfd = open(src, O_RDONLY);
    if (sfd < 0) {
        perror(src);
        return -1;
    }

    dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dfd < 0) {
        perror(dst);
        close(sfd);
        return -1;
    }

    while ((r = read(sfd, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(dfd, buf + off, r - off);
            if (w < 0) {
                perror("write");
                close(sfd);
                close(dfd);
                return -1;
            }
            off += w;
        }
    }

    if (r < 0) {
        perror("read");
    }

    close(sfd);
    close(dfd);
    return (r < 0) ? -1 : 0;
}

#ifndef APP_CP_MODULE
int main(int argc, char *argv[])
#else
int icebox_cp(int argc, char *argv[])
#endif
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <src> <dst>\n", argv[0]);
        return 1;
    }

    if (copy_file(argv[1], argv[2]) != 0)
        return 1;
    return 0;
}
