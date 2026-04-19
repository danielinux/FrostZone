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
 *      Authors:
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef APP_DF_MODULE
int main(int argc, char *args[])
#else
int icebox_df(int argc, char *args[])
#endif
{
    int fd;
    int r;
    char buf[64];

    fd = open("/sys/df", O_RDONLY);
    if (fd < 0) {
        printf("df: cannot open /sys/df\r\n");
        exit(1);
    }
    do {
        r = read(fd, buf, sizeof(buf));
        if (r > 0)
            write(1, buf, r);
    } while (r > 0);
    close(fd);
    exit(0);
}
