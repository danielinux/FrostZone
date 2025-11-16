#ifndef INC_TFT
#define INC_TFT

#include <errno.h>

#ifdef CONFIG_ILI9341
int tft_init(void);
#else
#  define tft_init() ((-ENOENT))
#endif

#endif
