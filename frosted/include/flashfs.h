#ifndef FLASHFS_INC
#define FLASHFS_INC

#include "frosted.h"

#ifdef CONFIG_FLASHFS

#ifdef CONFIG_JEDEC_SPI_FLASH
#include "jedec_spi_flash.h"
int flashfs_register_jedec(struct jedec_spi_flash *jedec_flash);
#else
#define flashfs_register_jedec(jf) (-ENOSYS)
#endif

int flashfs_init(void);

#else

#define flashfs_init() (-ENOENT)
#define flashfs_register_jedec(jf) (-ENOSYS)

#endif

#endif
