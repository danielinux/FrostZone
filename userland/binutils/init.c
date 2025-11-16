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
 *      Authors: Daniele Lacamera, Maxime Vincent
 *
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ioctl.h"
#include "syscalls.h"
#define IDLE()                                                                 \
    while (1) {                                                                \
        do {                                                                   \
        } while (0);                                                           \
    }
#define GREETING "Welcome to frosted!\n"
extern void *_init;

char idling_txt[10] = "idling";
char fresh_txt[10] = "fresh";
char serial_dev[12] = "/dev/ttyS0";

const char fresh_path[30] = "/bin/fresh";
const char *shebang;

static char initsh[] = "/bin/init.sh";

static char *sh_args[4] = {fresh_txt, "-t", serial_dev, NULL};
static char *idling_args[2] = {idling_txt, NULL};

int main(void *arg)
{
    volatile int i = (int)arg;
    volatile int pid;
    int status;
    int sh_fd = open(initsh, O_RDONLY);
    char line[80];
    char *pline;

    if (sh_fd >= 0) {
        int r;
        sh_args[1] = initsh;
        sh_args[2] = NULL;
        r = read(sh_fd, line, 80);
        close(sh_fd);
        if ((r > 2) && strncmp(line, "#!", 2) == 0) {
            pline = line + 2;
            shebang = pline;
            while (*pline != ' ' && *pline != '\t' && *pline != '\r' && *pline != '\n')
                pline++;
            *pline = (char)0;
        } else {
            shebang = fresh_path;
        }
        if (vfork() == 0) {
            execve(shebang, sh_args, NULL);
            exit(1);
        }
    } else {
        int fd = open("/dev/ttyS0", O_RDWR);
        int stdo, stde;
        if (fd >= 0) {
            stdo = dup(fd);
            stde = dup(fd);
            fprintf(stderr, "WARNING: /bin/init.sh not found. Starting "
                            "emergency shell.\r\n");
            close(fd);
            close(stdo);
            close(stde);
        }
        if (vfork() == 0) {
            execve(fresh_path, sh_args, NULL);
            exit(1);
        }
    }

    while (1) {
        pid = waitpid(-1, &status, 0);
    }
    return 0;
}
