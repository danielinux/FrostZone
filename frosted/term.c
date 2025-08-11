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
#include "frosted.h"
#include <string.h>
#include <sys/termios.h>

static struct module *get_term_mod(int td)
{
    struct fnode *f = task_filedesc_get(td);
    if (f)
        return f->owner;
    return NULL;
}

int sys_tcgetattr_hdlr(int fd, struct termios *t)
{
    struct module *m;
    if (!t || task_ptr_valid(t))
        return -EACCES;
    m = get_term_mod(fd);
    if (m && m->ops.tcgetattr)
        return m->ops.tcgetattr(fd, t);
    else
        return -EOPNOTSUPP;
}

int sys_tcsetattr_hdlr(int fd, int optional_actions, struct termios *t)
{
    struct module *m;
    if (!t || task_ptr_valid(t))
        return -EACCES;
    m = get_term_mod(fd);
    if (m && m->ops.tcsetattr)
        return m->ops.tcsetattr(fd, optional_actions, t);
    else
        return -EOPNOTSUPP;
}


int sys_tcsendbreak_hdlr(int arg1, int arg2)
{
    /* TODO: send SIGINT to self. */
    return -EOPNOTSUPP;
}
