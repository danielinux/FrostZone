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

#include "frosted.h"
#include "device.h"
#include "pool.h"
#include "locks.h"

POOL_DEFINE(device_pool, struct device, CONFIG_MAX_DEVICES);

void device_pool_init(void)
{
    pool_init(&device_pool);
}

int device_open(const char *path, int flags)
{
    struct fnode *f = fno_search(path);
    if (!f)
        return -1;
    return task_filedesc_add(f);
}

struct device *  device_fno_init(struct module * mod, const char * name, struct fnode *node, uint32_t flags, void * priv)
{
    struct device * device = pool_alloc(&device_pool);
    if (!device)
        return NULL;
    device->fno = NULL;
    /* Only create a device node if there is a name */
    if(name)
    {
        device->fno =  fno_create(mod, name, node);
        if (!device->fno) {
            pool_free(&device_pool, device);
            return NULL;
        }
        device->fno->priv = priv;
        device->fno->flags |= flags;
    }
    device->task = NULL;
    device->mutex = mutex_init();
    return device;
}
