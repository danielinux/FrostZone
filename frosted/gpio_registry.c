/*
 *      This file is part of frostzone.
 *
 *      This file keeps a lightweight list of GPIO descriptors so
 *      other subsystems (sysfs, diagnostics, etc.) can see which pins
 *      have been registered without pulling in the legacy driver.
 */

#include "frosted.h"
#include "gpio.h"

#include <string.h>

static struct dev_gpio *gpio_head;
static struct dev_gpio *gpio_tail;
static mutex_t *gpio_lock;

static void gpio_registry_init(void)
{
    if (!gpio_lock)
        gpio_lock = mutex_init();
}

void gpio_registry_add_entry(struct module *owner, const struct gpio_config *cfg)
{
    struct dev_gpio *desc;

    if (!cfg || !cfg->base)
        return;

    if (!gpio_lock)
        gpio_registry_init();

    desc = kalloc(sizeof(*desc));
    if (!desc)
        return;

    memset(desc, 0, sizeof(*desc));
    desc->owner = owner;
    desc->mode = cfg->mode;
    desc->af = cfg->af;
    desc->base = cfg->base;
    desc->pin = cfg->pin;
    desc->optype = cfg->optype;
    desc->speed = cfg->speed;
    desc->pullupdown = cfg->pullupdown;
    desc->trigger = (uint8_t)cfg->trigger;

    mutex_lock(gpio_lock);
    desc->next = NULL;
    if (!gpio_head) {
        gpio_head = desc;
        gpio_tail = desc;
    } else {
        gpio_tail->next = desc;
        gpio_tail = desc;
    }
    mutex_unlock(gpio_lock);
}

const struct dev_gpio *gpio_registered_list(void)
{
    return gpio_head;
}

int gpio_list_len(void)
{
    int count = 0;
    const struct dev_gpio *node;

    for (node = gpio_head; node; node = node->next)
        count++;
    return count;
}
