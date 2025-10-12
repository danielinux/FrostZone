#ifndef FROSTED_FONTS_H
#define FROSTED_FONTS_H

#include <stdint.h>

#define FONT_WIDTH 8
#define FONT_HEIGHT 8

extern const unsigned char fb_font[256][FONT_HEIGHT];
extern const uint32_t xterm_cmap[256];

#endif /* FROSTED_FONTS_H */

