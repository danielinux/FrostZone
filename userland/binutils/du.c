#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned long du_path(const char *path)
{
    struct stat st;
    unsigned long total;
    DIR *dir;
    struct dirent *ent;
    char child[256];
    if (lstat(path, &st) < 0) {
        perror(path);
        return 0;
    }
    total = st.st_size;
    if (S_ISDIR(st.st_mode)) {
        dir = opendir(path);
        if (!dir)
            return total;
        while ((ent = readdir(dir)) != NULL) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            total += du_path(child);
        }
        closedir(dir);
    }
    return total;
}

#ifndef APP_DU_MODULE
int main(int argc, char *argv[])
#else
int icebox_du(int argc, char *argv[])
#endif
{
    int i;
    if (argc < 2)
        argv[argc++] = ".";
    for (i = 1; i < argc; i++) {
        unsigned long total = du_path(argv[i]);
        printf("%lu\t%s\n", total, argv[i]);
    }
    return 0;
}
