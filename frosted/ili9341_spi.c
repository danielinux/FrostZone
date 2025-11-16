#include "frosted.h"
#include "device.h"
#include "framebuffer.h"
#include "gpio.h"
#include "spi.h"
#include "tft.h"
#include "stm32h5xx.h"
#define STM32_RCC_BASE RCC_BASE
#include "stm32x5_board_common.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if !CONFIG_ILI9341
int tft_init(void)
{
    return -ENODEV;
}
#else

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define ILI9341_WIDTH   320U
#define ILI9341_HEIGHT  240U
#define ILI9341_PIXELS  (ILI9341_WIDTH * ILI9341_HEIGHT)

#define ILI9341_CMD_SWRESET   0x01U
#define ILI9341_CMD_SLPOUT    0x11U
#define ILI9341_CMD_DISPON    0x29U
#define ILI9341_CMD_CASET     0x2AU
#define ILI9341_CMD_PASET     0x2BU
#define ILI9341_CMD_RAMWR     0x2CU
#define ILI9341_CMD_MADCTL    0x36U
#define ILI9341_CMD_COLMOD    0x3AU

#define ILI9341_SPI_BUS       0
struct ili9341_gpio {
    uint32_t base;
    uint8_t pin;
};

struct ili9341_panel {
    struct fb_info fb;
    struct spi_slave spi;
    struct ili9341_gpio cs;
    struct ili9341_gpio dc;
    struct ili9341_gpio rst;
    struct ili9341_gpio bl;
    uint16_t palette[256];
    uint8_t *frontbuffer;
};

static struct ili9341_panel ili9341;
static int ili9341_fb_update(struct fb_info *info, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

static inline void ili9341_gpio_init_output(struct ili9341_gpio *gpio)
{
    stm32x5_rcc_enable_gpio(gpio->base);
    stm32x5_gpio_write_mode(gpio->base, gpio->pin, GPIO_MODE_OUTPUT);
    stm32x5_gpio_write_otype(gpio->base, gpio->pin, GPIO_OTYPE_PP);
    stm32x5_gpio_write_speed(gpio->base, gpio->pin, GPIO_SPEED_HIGH);
    stm32x5_gpio_write_pull(gpio->base, gpio->pin, IOCTL_GPIO_PUPD_NONE);
    stm32x5_gpio_set(gpio->base, gpio->pin);
}

static inline void ili9341_pin_set(const struct ili9341_gpio *gpio, bool level)
{
    if (level)
        stm32x5_gpio_set(gpio->base, gpio->pin);
    else
        stm32x5_gpio_reset(gpio->base, gpio->pin);
}

static void ili9341_delay_ms(uint32_t ms)
{
    kthread_sleep_ms(ms);
}

static int ili9341_spi_write(const uint8_t *data, size_t len)
{
    if (!data || len == 0U)
        return 0;
    return devspi_xfer(&ili9341.spi, (const char *)data, NULL, (unsigned int)len);
}

static int ili9341_write_command(uint8_t cmd, const uint8_t *payload, size_t len)
{
    int ret;

    ili9341_pin_set(&ili9341.cs, false);
    ili9341_pin_set(&ili9341.dc, false);
    ret = ili9341_spi_write(&cmd, 1U);
    if (ret < 0)
        goto out;
    if (len && payload) {
        ili9341_pin_set(&ili9341.dc, true);
        ret = ili9341_spi_write(payload, len);
    }
out:
    ili9341_pin_set(&ili9341.cs, true);
    return ret;
}

static void ili9341_reset_panel(void)
{
    ili9341_pin_set(&ili9341.cs, true);
    ili9341_pin_set(&ili9341.dc, true);
    ili9341_pin_set(&ili9341.rst, true);
    ili9341_delay_ms(5);
    ili9341_pin_set(&ili9341.rst, false);
    ili9341_delay_ms(20);
    ili9341_pin_set(&ili9341.rst, true);
    ili9341_delay_ms(150);
}

static void ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];

    buf[0] = (uint8_t)(x0 >> 8);
    buf[1] = (uint8_t)(x0 & 0xFFU);
    buf[2] = (uint8_t)(x1 >> 8);
    buf[3] = (uint8_t)(x1 & 0xFFU);
    ili9341_write_command(ILI9341_CMD_CASET, buf, sizeof(buf));

    buf[0] = (uint8_t)(y0 >> 8);
    buf[1] = (uint8_t)(y0 & 0xFFU);
    buf[2] = (uint8_t)(y1 >> 8);
    buf[3] = (uint8_t)(y1 & 0xFFU);
    ili9341_write_command(ILI9341_CMD_PASET, buf, sizeof(buf));
}

static int ili9341_init_sequence(void)
{
    uint8_t data;

    ili9341_reset_panel();
    if (ili9341_write_command(ILI9341_CMD_SWRESET, NULL, 0) < 0)
        return -EIO;
    ili9341_delay_ms(150);

    if (ili9341_write_command(ILI9341_CMD_SLPOUT, NULL, 0) < 0)
        return -EIO;
    ili9341_delay_ms(120);

    data = 0x55; /* 16-bit pixel */
    if (ili9341_write_command(ILI9341_CMD_COLMOD, &data, 1U) < 0)
        return -EIO;

    data = 0xA8; /* Landscape, BGR */
    if (ili9341_write_command(ILI9341_CMD_MADCTL, &data, 1U) < 0)
        return -EIO;

    ili9341_set_window(0, 0, ILI9341_WIDTH - 1U, ILI9341_HEIGHT - 1U);

    if (ili9341_write_command(ILI9341_CMD_DISPON, NULL, 0) < 0)
        return -EIO;
    ili9341_delay_ms(20);
    ili9341_pin_set(&ili9341.bl, true);
    return 0;
}

static uint16_t ili9341_rgb565(uint32_t rgb888)
{
    uint16_t r = (uint16_t)((rgb888 >> 19) & 0x1FU);
    uint16_t g = (uint16_t)((rgb888 >> 10) & 0x3FU);
    uint16_t b = (uint16_t)((rgb888 >> 3) & 0x1FU);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void ili9341_demo_gradient(void)
{
    uint8_t cmd = ILI9341_CMD_RAMWR;
    uint16_t linebuf[ILI9341_WIDTH];
    uint32_t x, y;

    ili9341_set_window(0, 0, ILI9341_WIDTH - 1U, ILI9341_HEIGHT - 1U);
    ili9341_pin_set(&ili9341.cs, false);
    ili9341_pin_set(&ili9341.dc, false);
    if (ili9341_spi_write(&cmd, 1U) < 0)
        goto out;
    ili9341_pin_set(&ili9341.dc, true);
    for (y = 0; y < ILI9341_HEIGHT; y++) {
        uint8_t g = (uint8_t)((y * 255U) / (ILI9341_HEIGHT - 1U));
        for (x = 0; x < ILI9341_WIDTH; x++) {
            uint8_t r = (uint8_t)((x * 255U) / (ILI9341_WIDTH - 1U));
            uint8_t b = (uint8_t)(((x + y) * 255U) / (ILI9341_WIDTH + ILI9341_HEIGHT - 2U));
            uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            linebuf[x] = ili9341_rgb565(rgb);
        }
        if (ili9341_spi_write((const uint8_t *)linebuf, sizeof(linebuf)) < 0)
            break;
    }
out:
    ili9341_pin_set(&ili9341.cs, true);
    ili9341_pin_set(&ili9341.dc, true);
}

static int ili9341_fb_setcmap(const uint32_t *cmap, struct fb_info *info)
{
    size_t i;
    struct ili9341_panel *panel = info ? info->priv : NULL;

    if (!panel || !cmap)
        return -EINVAL;

    for (i = 0; i < ARRAY_SIZE(panel->palette); i++)
        panel->palette[i] = ili9341_rgb565(cmap[i]);
    return 0;
}

static int ili9341_fb_blank(struct fb_info *info)
{
    struct ili9341_panel *panel = info ? info->priv : NULL;

    if (!panel || !panel->frontbuffer)
        return -ENODEV;

    memset(panel->frontbuffer, 0, ILI9341_PIXELS);
    return ili9341_fb_update(info, 0, 0, ILI9341_WIDTH, ILI9341_HEIGHT);
}

static int ili9341_fb_update(struct fb_info *info, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct ili9341_panel *panel = info ? info->priv : NULL;
    uint32_t row, col;
    uint16_t linebuf[ILI9341_WIDTH];
    uint8_t cmd = ILI9341_CMD_RAMWR;

    if (!panel || !panel->frontbuffer)
        return -ENODEV;

    if (x >= ILI9341_WIDTH || y >= ILI9341_HEIGHT)
        return -EINVAL;

    if (x + w > ILI9341_WIDTH)
        w = ILI9341_WIDTH - x;
    if (y + h > ILI9341_HEIGHT)
        h = ILI9341_HEIGHT - y;

    ili9341_set_window((uint16_t)x, (uint16_t)y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    ili9341_pin_set(&ili9341.cs, false);
    ili9341_pin_set(&ili9341.dc, false);
    if (ili9341_spi_write(&cmd, 1U) < 0) {
        ili9341_pin_set(&ili9341.cs, true);
        return -EIO;
    }
    ili9341_pin_set(&ili9341.dc, true);
    for (row = 0; row < h; row++) {
        uint32_t offset = (y + row) * ILI9341_WIDTH + x;
        for (col = 0; col < w; col++) {
            uint8_t idx = panel->frontbuffer[offset + col];
            linebuf[col] = panel->palette[idx];
        }
        if (ili9341_spi_write((const uint8_t *)linebuf, w * sizeof(uint16_t)) < 0) {
            ili9341_pin_set(&ili9341.cs, true);
            return -EIO;
        }
    }
    ili9341_pin_set(&ili9341.cs, true);

    return 0;
}

static struct fb_ops ili9341_ops = {
    .fb_open = NULL,
    .fb_release = NULL,
    .fb_check_var = NULL,
    .fb_set_par = NULL,
    .fb_setcmap = ili9341_fb_setcmap,
    .fb_blank = ili9341_fb_blank,
    .fb_ioctl = NULL,
    .fb_destroy = NULL,
    .fb_update = ili9341_fb_update,
};

static void ili9341_configure_fb(struct fb_info *fb, uint8_t *buffer)
{
    memset(fb, 0, sizeof(*fb));
    fb->var.type = FB_TYPE_PIXELMAP;
    fb->var.xres = ILI9341_WIDTH;
    fb->var.yres = ILI9341_HEIGHT;
    fb->var.bits_per_pixel = 8;
    fb->var.pixel_format = FB_PF_CMAP256;
    fb->fbops = &ili9341_ops;
    fb->screen_buffer = buffer;
    fb->var.smem_start = buffer;
    fb->var.smem_len = ILI9341_PIXELS;
    fb->priv = &ili9341;
}

int tft_init(void)
{
    int ret;

    memset(&ili9341, 0, sizeof(ili9341));

    ili9341.frontbuffer = kalloc(ILI9341_PIXELS);
    if (!ili9341.frontbuffer)
        return -ENOMEM;
    memset(ili9341.frontbuffer, 0, ILI9341_PIXELS);

    ret = spi_bus_init();
    if (ret < 0) {
        kfree(ili9341.frontbuffer);
        ili9341.frontbuffer = NULL;
        return ret;
    }

    ili9341.spi.bus = ILI9341_SPI_BUS;
    ili9341.spi.priv = &ili9341;

    ili9341.cs = (struct ili9341_gpio){ GPIOD_BASE, 14 };
    ili9341.rst = (struct ili9341_gpio){ GPIOD_BASE, 15 };
    ili9341.dc = (struct ili9341_gpio){ GPIOF_BASE, 3 };
    ili9341.bl = (struct ili9341_gpio){ GPIOB_BASE, 3 };

    ili9341_gpio_init_output(&ili9341.cs);
    ili9341_gpio_init_output(&ili9341.rst);
    ili9341_gpio_init_output(&ili9341.dc);
    ili9341_gpio_init_output(&ili9341.bl);
    ili9341_pin_set(&ili9341.bl, false);

    ili9341_configure_fb(&ili9341.fb, ili9341.frontbuffer);
    ret = register_framebuffer(&ili9341.fb);
    if (ret < 0) {
        kfree(ili9341.frontbuffer);
        ili9341.frontbuffer = NULL;
        return ret;
    }

    ret = ili9341_init_sequence();
    if (ret < 0)
        return ret;

    ili9341_demo_gradient();

    return 0;
}

#endif /* CONFIG_ILI9341 */
