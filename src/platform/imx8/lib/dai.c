// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright 2019 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>

#include <sof/common.h>
#include <sof/drivers/edma.h>
#include <sof/drivers/esai.h>
#include <sof/drivers/sai.h>
#include <sof/lib/dai.h>
#include <sof/lib/memory.h>
#include <sof/spinlock.h>
#include <ipc/dai.h>
#include <ipc/stream.h>

static struct dai esai[] = {
{
	.index = 0,
	.plat_data = {
		.base = ESAI_BASE,
		.fifo[SOF_IPC_STREAM_PLAYBACK] = {
			.offset		= ESAI_BASE + REG_ESAI_ETDR,
			.depth		= 384,  /* in bytes */
			.handshake	= EDMA_HANDSHAKE(EDMA_ESAI_IRQ,
							 EDMA_ESAI_TX_CHAN),
		},
		.fifo[SOF_IPC_STREAM_CAPTURE] = {
			.offset		= ESAI_BASE + REG_ESAI_ERDR,
			.depth		= 384,  /* in bytes */
			.handshake	= EDMA_HANDSHAKE(EDMA_ESAI_IRQ,
							 EDMA_ESAI_RX_CHAN),
		},
	},
	.drv = &esai_driver,
},
};

static struct dai sai[] = {
{
	.index = 1,
	.plat_data = {
		.base = SAI_1_BASE,
		.fifo[SOF_IPC_STREAM_PLAYBACK] = {
			.offset		= SAI_1_BASE + REG_SAI_TDR0,
			/* use depth to model the HW FIFO size:
			 * each channel includes a 64 x 32 bit FIFO
			 * that can be accessed using Transmit or
			 * Receive Data Registers
			 */
			.depth		= 64,  /* in 4 bytes words */
			.handshake	= EDMA_HANDSHAKE(EDMA0_SAI_CHAN_TX_IRQ,
							 EDMA0_SAI_CHAN_TX),
		},
		.fifo[SOF_IPC_STREAM_CAPTURE] = {
			.offset		= SAI_1_BASE + REG_SAI_RDR0,
			.depth		= 64,  /* in 4 bytes words */
			.handshake	= EDMA_HANDSHAKE(EDMA0_SAI_CHAN_RX_IRQ,
							 EDMA0_SAI_CHAN_RX),
		},
	},
	.drv = &sai_driver,
},
};
static struct dai_type_info dti[] = {
	{
		.type = SOF_DAI_IMX_SAI,
		.dai_array = sai,
		.num_dais = ARRAY_SIZE(sai)
	},
	{
		.type = SOF_DAI_IMX_ESAI,
		.dai_array = esai,
		.num_dais = ARRAY_SIZE(esai)
	},
};

int dai_init(void)
{
	int i;

	 /* initialize spin locks early to enable ref counting */
	for (i = 0; i < ARRAY_SIZE(esai); i++)
		spinlock_init(&esai[i].lock);

	for (i = 0; i < ARRAY_SIZE(sai); i++)
		spinlock_init(&sai[i].lock);

	dai_install(dti, ARRAY_SIZE(dti));
	return 0;
}
