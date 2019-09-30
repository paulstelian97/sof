/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2019 NXP
 *
 * Author: Daniel Baluta <daniel.baluta@nxp.com>
 */

#ifndef __IPC_DAI_IMX_H__
#define __IPC_DAI_IMX_H__

#include <ipc/header.h>
#include <stdint.h>

/* SSP Configuration Request - SOF_IPC_DAI_SSP_CONFIG */
struct sof_ipc_dai_ssp_params {
	struct sof_ipc_hdr hdr;

	/* TDM */
	uint32_t tdm_slots;
	uint32_t rx_slots;
	uint32_t tx_slots;

	/* data */
	uint32_t sample_valid_bits;
	uint16_t tdm_slot_width;
	uint16_t reserved2;	/* alignment */

	/* MCLK */
	uint32_t mclk_direction;
} __attribute__((packed));

/* ESAI Configuration Request */
struct sof_ipc_dai_esai_params {
	struct sof_ipc_hdr hdr;

	/* TDM */
	uint32_t tdm_slots;
	uint32_t rx_slots;
	uint32_t tx_slots;

	/* data */
	uint32_t sample_valid_bits;
	uint16_t tdm_slot_width;
	uint16_t reserved2;	/* alignment */

	/* MCLK */
	uint32_t mclk_direction;
} __attribute__((packed));

#endif /* __IPC_DAI_IMX_H__ */
