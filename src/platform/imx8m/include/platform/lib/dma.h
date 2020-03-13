/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2020 NXP
 *
 * Author: Daniel Baluta <daniel.baluta@nxp.com>
 */

#ifdef __SOF_LIB_DMA_H__

#ifndef __PLATFORM_LIB_DMA_H__
#define __PLATFORM_LIB_DMA_H__

#define PLATFORM_NUM_DMACS	2

/* max number of supported DMA channels */
#define PLATFORM_MAX_DMA_CHAN	32

#define DMA_ID_SDMA2	0
#define DMA_ID_HOST	1

#define dma_chan_irq(dma, chan) dma_irq(dma)
#define dma_chan_irq_name(dma, chan) dma_irq_name(dma)

/* SDMA2 specific data */

/* Interrupts must be set up interestingly -- shift them all by 32 like
 * on the other platforms.
 */

#define SDMA2_IRQ	7 /* TODO What? */
#define SDMA2_IRQ_NAME	"irqstr2" /* TODO find the correct one */

#define SDMA_CORE_RATIO 0/* Enable ACR bit as it's needed for this platform */

#endif /* __PLATFORM_LIB_DMA_H__ */

#else

#error "This file shouldn't be included from outside of sof/lib/dma.h"

#endif /* __SOF_LIB_DMA_H__ */
