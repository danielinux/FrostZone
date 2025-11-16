/*
 *      This file is part of frosted.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *
 *      frosted is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frosted.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors: Daniele Lacamera
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#define ALLFLAG   (1U << 0)
#define HUMANFLAG (1U << 1)
#define LONGFLAG  (1U << 2)

#define PATH_BUF 512

static const char mag[] = " KMGT";
static char normalize_tmp[PATH_BUF];
static char normalize_outbuf[PATH_BUF];
static char *normalize_parts[PATH_BUF / 2];
static char ls_abspath_buf[PATH_BUF];
static char ls_fullpath_buf[PATH_BUF];
static char ls_target_buf[PATH_BUF];

static void format_size(char *buf, size_t len, off_t size, unsigned int flags)
{
    if (flags & HUMANFLAG) {
        unsigned int order = 0;
        double val = (double)size;
        while (val >= 1000.0 && order < (sizeof(mag) - 1)) {
            val /= 1000.0;
            order++;
        }
        snprintf(buf, len, "%.1f%c", val, mag[order]);
    } else {
        snprintf(buf, len, "%ld", (long)size);
    }
}

static void print_entry(const char *name, const char *fullpath,
                        struct stat *st, unsigned int flags)
{
    char size_buf[32] = "";
    char type = '-';
    size_t name_len;
    size_t pad;

    if (!(flags & LONGFLAG)) {
        printf("%s\n", name);
        return;
    }

    if (S_ISDIR(st->st_mode))
        type = 'd';
    else if (S_ISLNK(st->st_mode))
        type = 'l';
    else
        type = 'f';

    if (S_ISLNK(st->st_mode)) {
        ssize_t r = readlink(fullpath, ls_target_buf, sizeof(ls_target_buf) - 1);
        if (r < 0)
            snprintf(size_buf, sizeof(size_buf), "-->BROKEN LNK<--");
        else {
            ls_target_buf[r] = '\0';
            snprintf(size_buf, sizeof(size_buf), "-->%s", ls_target_buf);
        }
    } else {
        format_size(size_buf, sizeof(size_buf), st->st_size, flags);
    }

    printf("%s", name);
    printf("  ");
    name_len = strlen(name);
    if (name_len < 30) {
        pad = 30 - name_len;
        while (pad--)
            putchar(' ');
    }
    printf("%c  %s\n", type, size_buf);
}

static void build_path(char *out, size_t len, const char *dir, const char *entry)
{
    size_t dlen = strlen(dir);
    int need_slash = (dlen > 0 && dir[dlen - 1] != '/');
    if (need_slash)
        snprintf(out, len, "%s/%s", dir, entry);
    else
        snprintf(out, len, "%s%s", dir, entry);
}

static void normalize_path(char *path)
{
    size_t depth = 0;
    int absolute = (path[0] == '/');
    char *token;
    char *saveptr;

    strncpy(normalize_tmp, path, sizeof(normalize_tmp));
    normalize_tmp[sizeof(normalize_tmp) - 1] = '\0';

    token = strtok_r(normalize_tmp, "/", &saveptr);
    while (token) {
        if (token[0] == '\0' || strcmp(token, ".") == 0) {
            /* skip */
        } else if (strcmp(token, "..") == 0) {
            if (depth > 0)
                depth--;
            else if (!absolute)
                normalize_parts[depth++] = token;
        } else {
            normalize_parts[depth++] = token;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    {
        char *dst = normalize_outbuf;
        size_t remaining = sizeof(normalize_outbuf);

        if (absolute) {
            *dst++ = '/';
            remaining--;
        }

        {
            size_t i;
            for (i = 0; i < depth && remaining > 1; i++) {
                size_t seglen = strlen(normalize_parts[i]);
                if (seglen >= remaining)
                    break;
                memcpy(dst, normalize_parts[i], seglen);
                dst += seglen;
                remaining -= seglen;
                if (i + 1 < depth) {
                    if (remaining <= 1)
                        break;
                    *dst++ = '/';
                    remaining--;
                }
            }
        }

        if (dst == normalize_outbuf) {
            if (absolute)
                *dst++ = '/';
            else
                *dst++ = '.';
        }
        *dst = '\0';

        strncpy(path, normalize_outbuf, PATH_BUF);
        path[PATH_BUF - 1] = '\0';
    }
}

static const char *skip_dot_prefix(const char *path)
{
    const char *p = path;

    while (p[0] == '.') {
        if (p[1] == '.' && (p[2] == '/' || p[2] == '\0'))
            break;
        if (p[1] == '/' || p[1] == '\0') {
            if (p[1] == '\0')
                return "";
            p += 2;
            while (*p == '/')
                p++;
            continue;
        }
        break;
    }
    return p;
}

static int make_abspath(const char *path, char *out, size_t len)
{
    const char *effective = path;
    const char *relative;
    const char *pwd;
    size_t need;
    size_t cur;

    if (!effective || !effective[0])
        effective = ".";

    if (effective[0] == '/') {
        need = strlen(effective) + 1;
        if (need > len)
            return -1;
        snprintf(out, len, "%s", effective);
        normalize_path(out);
        return 0;
    }

    relative = skip_dot_prefix(effective);
    pwd = getenv("PWD");

    if (!pwd || strlen(pwd) + 1 > len) {
        if (!getcwd(out, len))
            return -1;
    } else {
        snprintf(out, len, "%s", pwd);
    }

    if (relative[0] == '\0') {
        normalize_path(out);
        return 0;
    }

    cur = strlen(out);
    if (cur + 1 >= len)
        return -1;
    if (out[cur - 1] != '/')
        strncat(out, "/", len - cur - 1);
    strncat(out, relative, len - strlen(out) - 1);
    normalize_path(out);
    return 0;
}

static int list_directory(const char *path, unsigned int flags)
{
    long file_count = 0;
    long dir_count = 0;
    off_t total_bytes = 0;
    DIR *d;
    struct dirent *ent;
    struct stat st;

    if (make_abspath(path, ls_abspath_buf, sizeof(ls_abspath_buf)) != 0) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", path, strerror(errno));
        return -1;
    }
    d = opendir(ls_abspath_buf);
    if (!d) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", path, strerror(errno));
        return -1;
    }

    while ((ent = readdir(d)) != NULL) {
        if (!(flags & ALLFLAG) && ent->d_name[0] == '.')
            continue;
        build_path(ls_fullpath_buf, sizeof(ls_fullpath_buf), ls_abspath_buf, ent->d_name);
        if (lstat(ls_fullpath_buf, &st) != 0) {
            fprintf(stderr, "ls: cannot access '%s': %s\n", ls_fullpath_buf, strerror(errno));
            continue;
        }
        if (S_ISDIR(st.st_mode))
            dir_count++;
        else
            file_count++;
        total_bytes += st.st_size;
        print_entry(ent->d_name, ls_fullpath_buf, &st, flags);
    }
    closedir(d);

    if (flags & LONGFLAG) {
        char total_buf[32];
        format_size(total_buf, sizeof(total_buf), total_bytes, flags);
        putchar('\n');
        printf("%s : %ld files, %ld directories. Total %s%s\n", ls_abspath_buf,
               file_count, dir_count, total_buf, (flags & HUMANFLAG) ? "" : " bytes");
    }
    return 0;
}

static int list_path(const char *path, unsigned int flags)
{
    struct stat st;

    if (make_abspath(path, ls_abspath_buf, sizeof(ls_abspath_buf)) != 0) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (lstat(ls_abspath_buf, &st) != 0) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (S_ISDIR(st.st_mode))
        return list_directory(path, flags);

    print_entry(path, ls_abspath_buf, &st, flags);
    return 0;
}

#ifndef APP_LS_MODULE
int main(int argc, char *argv[])
#else
int icebox_ls(int argc, char *argv[])
#endif
{
    unsigned int flags = 0;
    int c;
    int status = 0;
    int targets;
    int i;
    extern int optind, optopt;

    while ((c = getopt(argc, argv, "ahl")) != -1) {
        switch (c) {
        case 'a':
            flags |= ALLFLAG;
            break;
        case 'h':
            flags |= HUMANFLAG;
            break;
        case 'l':
            flags |= LONGFLAG;
            break;
        default:
            fprintf(stderr, "ls: invalid option -- '%c'\n", optopt);
            return 1;
        }
    }

    targets = argc - optind;
    if (targets <= 0) {
        const char *def = ".";
        targets = 1;
        if (list_path(def, flags) != 0)
            status = 1;
        return status;
    }

    for (i = 0; i < targets; i++) {
        const char *path = argv[optind + i];
        if (targets > 1) {
            printf("%s:\n", path);
        }
        if (list_path(path, flags) != 0)
            status = 1;
        if (targets > 1 && i != targets - 1)
            putchar('\n');
    }
    return status;
}
