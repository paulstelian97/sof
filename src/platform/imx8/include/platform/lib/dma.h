/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2019 NXP
 *
 * Author: Daniel Baluta <daniel.baluta@nxp.com>
 */

#ifdef __SOF_LIB_DMA_H__

#ifndef __PLATFORM_LIB_DMA_H__
#define __PLATFORM_LIB_DMA_H__

#define PLATFORM_NUM_DMACS	2

/* max number of supported DMA channels */
#define PLATFORM_MAX_DMA_CHAN	32

#define DMA_ID_EDMA0	0
#define DMA_ID_HOST	1

/* TODO fix IRQ numbers */
#define dma_chan_irq(dma, chan) (dma_irq(dma) + chan)
#define dma_chan_irq_name(dma, chan) dma_irq_name(dma)

int dmac_init(void);

#endif /* __PLATFORM_LIB_DMA_H__ */

#else

#error "This file shouldn't be included from outside of sof/lib/dma.h"

#endif /* __SOF_LIB_DMA_H__ */
