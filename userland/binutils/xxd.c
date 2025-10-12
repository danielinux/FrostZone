#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int dump_fd(int fd)
{
    unsigned char buf[16];
    unsigned long offset = 0;
    ssize_t r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        printf("%08lx:", offset);
        for (int i = 0; i < r; i++) {
            if (i % 2 == 0) printf(" ");
            printf("%02x", buf[i]);
        }
        printf("  ");
        for (int i = 0; i < r; i++)
            printf("%c", isprint(buf[i]) ? buf[i] : '.');
        printf("\n");
        offset += r;
    }
    return (r < 0) ? -1 : 0;
}

static int dump_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }
    {
        int rc = dump_fd(fd);
        close(fd);
        return rc;
    }
}

#ifndef APP_XXD_MODULE
int main(int argc, char *argv[])
#else
int icebox_xxd(int argc, char *argv[])
#endif
{
    int rc = 0;
    int i;
    if (argc == 1)
        return dump_fd(STDIN_FILENO) == 0 ? 0 : 1;
    for (i = 1; i < argc; i++)
        if (dump_file(argv[i]) != 0)
            rc = 1;
    return rc;
}
