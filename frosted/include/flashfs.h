
#include "frosted.h"

#ifndef FLASHFS_INC
#define FLASHFS_INC


#ifdef CONFIG_FLASHFS

int flashfs_init(void);

#else
#define flashfs_init() ((-ENOENT))

#endif


#endif
