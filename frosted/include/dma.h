#ifndef FROSTED_DMA_H
#define FROSTED_DMA_H

#include <stdint.h>

struct dma_config {
    uint32_t base;
    uint32_t stream;
    uint32_t channel;
    uint32_t irq;
};

#endif /* FROSTED_DMA_H */

