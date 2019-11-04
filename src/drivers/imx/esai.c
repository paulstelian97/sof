// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright 2019 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>
// Author: Paul Olaru <paul.olaru@nxp.com>

#include <sof/audio/component.h>
#include <sof/drivers/edma.h>
#include <sof/drivers/esai.h>
#include <sof/lib/alloc.h>
#include <sof/lib/dai.h>
#include <sof/lib/dma.h>
#include <ipc/dai.h>
#include <ipc/topology.h>
#include <errno.h>
#include <stdint.h>

struct esai_pdata {
	struct {
		uint32_t ecr;
		uint32_t tfcr;
		uint32_t rfcr;
		uint32_t saicr;
		uint32_t tcr;
		uint32_t tccr;
		uint32_t rcr;
		uint32_t rccr;
		uint32_t tsma;
		uint32_t tsmb;
		uint32_t rsma;
		uint32_t rsmb;
		uint32_t prrc;
		uint32_t pcrc;
	} regs;
};

struct esai_pdata {
	struct {
		uint32_t ecr;
		uint32_t tfcr;
		uint32_t rfcr;
		uint32_t saicr;
		uint32_t tcr;
		uint32_t tccr;
		uint32_t rcr;
		uint32_t rccr;
		uint32_t tsma;
		uint32_t tsmb;
		uint32_t rsma;
		uint32_t rsmb;
		uint32_t prrc;
		uint32_t pcrc;
	} regs;
	// TODO set the config here, prepare the registers and defer the
	// actual updating of the HW registers to the start() and stop()
	// triggers
};

static void esai_regs_dump(struct dai *dai)
{
	/* Read every readable ESAI register and print it */
	tracev_esai("ESAI_ECR: %08x", dai_read(dai, REG_ESAI_ECR));
	tracev_esai("ESAI_ESR: %08x", dai_read(dai, REG_ESAI_ESR));
	tracev_esai("ESAI_TFCR: %08x", dai_read(dai, REG_ESAI_TFCR));
	tracev_esai("ESAI_TFSR: %08x", dai_read(dai, REG_ESAI_TFSR));
	tracev_esai("ESAI_RFCR: %08x", dai_read(dai, REG_ESAI_RFCR));
	tracev_esai("ESAI_RFSR: %08x", dai_read(dai, REG_ESAI_RFSR));
	tracev_esai("ESAI_SAISR: %08x", dai_read(dai, REG_ESAI_SAISR));
	tracev_esai("ESAI_SAICR: %08x", dai_read(dai, REG_ESAI_SAICR));
	tracev_esai("ESAI_TCR: %08x", dai_read(dai, REG_ESAI_TCR));
	tracev_esai("ESAI_TCCR: %08x", dai_read(dai, REG_ESAI_TCCR));
	tracev_esai("ESAI_RCR: %08x", dai_read(dai, REG_ESAI_RCR));
	tracev_esai("ESAI_RCCR: %08x", dai_read(dai, REG_ESAI_RCCR));
	tracev_esai("ESAI_TSMA: %08x", dai_read(dai, REG_ESAI_TSMA));
	tracev_esai("ESAI_TSMB: %08x", dai_read(dai, REG_ESAI_TSMB));
	tracev_esai("ESAI_RSMA: %08x", dai_read(dai, REG_ESAI_RSMA));
	tracev_esai("ESAI_RSMB: %08x", dai_read(dai, REG_ESAI_RSMB));
	tracev_esai("ESAI_PRRC: %08x", dai_read(dai, REG_ESAI_PRRC));
	tracev_esai("ESAI_PCRC: %08x", dai_read(dai, REG_ESAI_PCRC));
}

static int esai_context_store(struct dai *dai)
{
	struct esai_pdata *pdata = dai_get_drvdata(dai);

	if (!pdata)
		return -EINVAL;

	pdata->regs.ecr = dai_read(dai, REG_ESAI_ECR);
	pdata->regs.tfcr = dai_read(dai, REG_ESAI_TFCR);
	pdata->regs.rfcr = dai_read(dai, REG_ESAI_RFCR);
	pdata->regs.saicr = dai_read(dai, REG_ESAI_SAICR);
	pdata->regs.tcr = dai_read(dai, REG_ESAI_TCR);
	pdata->regs.tccr = dai_read(dai, REG_ESAI_TCCR);
	pdata->regs.rcr = dai_read(dai, REG_ESAI_RCR);
	pdata->regs.rccr = dai_read(dai, REG_ESAI_RCCR);
	pdata->regs.tsma = dai_read(dai, REG_ESAI_TSMA);
	pdata->regs.tsmb = dai_read(dai, REG_ESAI_TSMB);
	pdata->regs.rsma = dai_read(dai, REG_ESAI_RSMA);
	pdata->regs.rsmb = dai_read(dai, REG_ESAI_RSMB);
	pdata->regs.prrc = dai_read(dai, REG_ESAI_PRRC);
	pdata->regs.pcrc = dai_read(dai, REG_ESAI_PCRC);

	return 0;
}

static int esai_context_restore(struct dai *dai)
{
	struct esai_pdata *pdata = dai_get_drvdata(dai);

	if (!pdata)
		return -EINVAL;

	dai_write(dai, REG_ESAI_ECR, ESAI_ECR_ERST);
	dai_write(dai, REG_ESAI_ECR, ESAI_ECR_ESAIEN);
	dai_write(dai, REG_ESAI_TFCR, pdata->regs.tfcr);
	dai_write(dai, REG_ESAI_RFCR, pdata->regs.rfcr);
	dai_write(dai, REG_ESAI_SAICR, pdata->regs.saicr);
	dai_write(dai, REG_ESAI_TCCR, pdata->regs.tccr);
	dai_write(dai, REG_ESAI_RCCR, pdata->regs.rccr);
	dai_write(dai, REG_ESAI_TSMA, pdata->regs.tsma);
	dai_write(dai, REG_ESAI_TSMB, pdata->regs.tsmb);
	dai_write(dai, REG_ESAI_RSMA, pdata->regs.rsma);
	dai_write(dai, REG_ESAI_RSMB, pdata->regs.rsmb);
	dai_write(dai, REG_ESAI_PRRC, pdata->regs.prrc);
	dai_write(dai, REG_ESAI_PCRC, pdata->regs.pcrc);
	dai_write(dai, REG_ESAI_TCR, pdata->regs.tcr);
	dai_write(dai, REG_ESAI_RCR, pdata->regs.rcr);
	dai_write(dai, REG_ESAI_ECR, pdata->regs.ecr);

	return 0;
}

static int esai_probe(struct dai *dai);

static inline int esai_set_config(struct dai *dai,
				 struct sof_ipc_dai_config *config)
{
	uint32_t xcr = 0, xccr = 0, mask;

	tracev_esai("ESAI: set_config format 0x%04x",
		    config->format);
	switch (config->format & SOF_DAI_FMT_FORMAT_MASK) {
	case SOF_DAI_FMT_I2S:
		/* Data on rising edge of bclk, frame low, 1clk before
		 * data
		 */
		xcr |= ESAI_xCR_xFSR;
		xccr |= ESAI_xCCR_xFSP | ESAI_xCCR_xCKP | ESAI_xCCR_xHCKP;
		break;
	case SOF_DAI_FMT_RIGHT_J:
		/* Data on rising edge of bclk, frame high, right
		 * aligned
		 */
		xccr |= ESAI_xCCR_xCKP | ESAI_xCCR_xHCKP;
		xcr  |= ESAI_xCR_xWA;
		break;
	case SOF_DAI_FMT_LEFT_J:
		/* Data on rising edge of bclk, frame high */
		xccr |= ESAI_xCCR_xCKP | ESAI_xCCR_xHCKP;
		break;
	case SOF_DAI_FMT_DSP_A:
		/* Data on rising edge of bclk, frame high, 1clk before
		 * data
		 */
		xcr |= ESAI_xCR_xFSL | ESAI_xCR_xFSR;
		xccr |= ESAI_xCCR_xCKP | ESAI_xCCR_xHCKP;
		break;
	case SOF_DAI_FMT_DSP_B:
		/* Data on rising edge of bclk, frame high */
		xcr |= ESAI_xCR_xFSL;
		xccr |= ESAI_xCCR_xCKP | ESAI_xCCR_xHCKP;
		break;
	case SOF_DAI_FMT_PDM:
		trace_esai_error("ESAI: Unsupported format (PDM)");
		return -EINVAL;
	default:
		trace_esai_error("ESAI: invalid format");
		return -EINVAL;
	}

	switch (config->format & SOF_DAI_FMT_INV_MASK) {
	case SOF_DAI_FMT_NB_NF:
		 /* Nothing to do for both normal cases */
		break;
	case SOF_DAI_FMT_NB_IF:
		/* Invert frame clock */
		xccr ^= ESAI_xCCR_xFSP;
		break;
	case SOF_DAI_FMT_IB_NF:
		/* Invert bit clock */
		xccr ^= ESAI_xCCR_xCKP | ESAI_xCCR_xHCKP;
		break;
	case SOF_DAI_FMT_IB_IF:
		/* Invert both clocks */
		xccr ^= ESAI_xCCR_xCKP | ESAI_xCCR_xHCKP | ESAI_xCCR_xFSP;
		break;
	default:
		trace_esai_error("ESAI: Invalid bit inversion format");
		return -EINVAL;
	}

	switch (config->format & SOF_DAI_FMT_MASTER_MASK) {
	case SOF_DAI_FMT_CBM_CFM:
		/* Nothing to do in the registers */
		break;
	case SOF_DAI_FMT_CBM_CFS:
		xccr |= ESAI_xCCR_xFSD;
		break;
	case SOF_DAI_FMT_CBS_CFM:
		xccr |= ESAI_xCCR_xCKD;
		break;
	case SOF_DAI_FMT_CBS_CFS:
		xccr |= ESAI_xCCR_xFSD | ESAI_xCCR_xCKD;
		break;
	default:
		trace_esai_error("ESAI: Invalid clock master-slave configuration");
		return -EINVAL;
	}

	/* Set networked mode; we only support 2 channels now, not 1 */
	xcr |= ESAI_xCR_xMOD_NETWORK;
	xccr |= ESAI_xCCR_xDC(2);

	/* Codec desires 32-bit samples, while the pipeline works with 24-bit
	 * samples. Pad the least significant bits with zeros.
	 * TODO replace 24 with the sample width requested from the topology.
	 */
	xcr |= ESAI_xCR_xSWS(32, 24) | ESAI_xCR_PADC;

	/* Remove "RESET" flag so we can configure the ESAI */
	dai_update_bits(dai, REG_ESAI_ECR, ESAI_ECR_ERST, 0);

	/* EXTAL transmitter in, we should use external EXTAL pin as MCLK */
	dai_update_bits(dai, REG_ESAI_ECR, ESAI_ECR_ETI, ESAI_ECR_ETI);

	mask = ESAI_xCCR_xCKP | ESAI_xCCR_xHCKP | ESAI_xCCR_xFSP |
		ESAI_xCCR_xFSD | ESAI_xCCR_xCKD | ESAI_xCCR_xHCKD |
		ESAI_xCCR_xDC_MASK;

	xccr |= ESAI_xCCR_xHCKD; /* Set the HCKT pin as an output */

	dai_update_bits(dai, REG_ESAI_TCCR, mask, xccr); /* rx */
	dai_update_bits(dai, REG_ESAI_RCCR, mask, xccr); /* tx */

	mask = ESAI_xCR_xFSL | ESAI_xCR_xFSR | ESAI_xCR_xWA |
		ESAI_xCR_xMOD_MASK | ESAI_xCR_xSWS_MASK | ESAI_xCR_PADC |
		ESAI_xCR_xPR;
	/* Personal reset, suspend the actual TX/RX for now */
	xcr |= ESAI_xCR_xPR;

	dai_update_bits(dai, REG_ESAI_TCR, mask, xcr); /* rx */
	dai_update_bits(dai, REG_ESAI_RCR, mask, xcr); /* tx */

	/* Disable transmission by disabling all slots */
	dai_write(dai, REG_ESAI_TSMA, 0);
	dai_write(dai, REG_ESAI_TSMB, 0);
	dai_write(dai, REG_ESAI_RSMA, 0);
	dai_write(dai, REG_ESAI_RSMB, 0);

	/* Program FIFOs -- rx only reset them
	 * TODO: When we will support recording this needs to be adjusted.
	 */
	dai_update_bits(dai, REG_ESAI_RFCR, ESAI_xFCR_xFR, ESAI_xFCR_xFR);
	dai_update_bits(dai, REG_ESAI_RFCR, ESAI_xFCR_xFR, 0);

	/* Reset transmit FIFO */
	dai_update_bits(dai, REG_ESAI_TFCR,
			ESAI_xFCR_xFR_MASK,
			ESAI_xFCR_xFR);

	/* Set transmit fifo configuration register
	 * xWA(24): 24-bit samples as input. Must agree with xSWS above.
	 *	    TODO get sample width from topology.
	 * xFWM(96): Trigger next DMA transfer when at least 96 (of the 128)
	 *	     slots are empty.
	 * TE(1): Enable 1 transmitter.
	 * TIEN: Transmitter initialization enable. This will pull the initial
	 *       samples from the FIFO in the transmit registers. The
	 *       alternative would have been to manually initialize the
	 *       transmit registers manually, which would have been more
	 *       complex to implement.
	 */
	dai_update_bits(dai, REG_ESAI_TFCR,
			ESAI_xFCR_xFR_MASK | ESAI_xFCR_xWA_MASK
			| ESAI_xFCR_xFWM_MASK | ESAI_xFCR_TE_MASK
			| ESAI_xFCR_TIEN,
			ESAI_xFCR_xWA(24) | ESAI_xFCR_xFWM(96)
			| ESAI_xFCR_TE(1) | ESAI_xFCR_TIEN);

	/* Set the clock divider to divide EXTAL by 16 (DIV8 from PSR,
	 * plus a divide by 2 which is mandatory overall)
	 * This configuration supports hardcoded MCLK at 49152000 Hz and
	 * obtains frame clock of 96000 Hz (2 48000Hz channels) and bit clock
	 * of 3072000 Hz (32-bit samples).
	 * xFP(1): No division from this divider (can do 1-16)
	 * xPSR_DIV8: Divide by 8
	 * There is also an additional divide by 2 which is forced by the
	 * hardware design of the ESAI.
	 *
	 * TODO use ESAI params instead of hardcode to compute the clock divider
	 * settings.
	 */
	dai_update_bits(dai, REG_ESAI_TCCR, ESAI_xCCR_xFP_MASK,
			ESAI_xCCR_xFP(1));
	dai_update_bits(dai, REG_ESAI_TCCR, ESAI_xCCR_xPSR_MASK,
			ESAI_xCCR_xPSR_DIV8);

	/* Remove ESAI personal reset */
	dai_update_bits(dai, REG_ESAI_xCR(0), ESAI_xCR_xPR, 0);
	dai_update_bits(dai, REG_ESAI_xCR(1), ESAI_xCR_xPR, 0);

	/* Configure ESAI pins, enable them all */
	dai_update_bits(dai, REG_ESAI_PRRC, ESAI_PRRC_PDC_MASK,
			ESAI_PRRC_PDC(ESAI_GPIO));
	dai_update_bits(dai, REG_ESAI_PCRC, ESAI_PCRC_PC_MASK,
			ESAI_PCRC_PC(ESAI_GPIO));
	return 0;
}

static void esai_start(struct dai *dai, int direction)
{
	/* FIFO enable */
	dai_update_bits(dai, REG_ESAI_xFCR(direction), ESAI_xFCR_xFEN_MASK,
			ESAI_xFCR_xFEN);

	/* To prevent channel swap issues, write a sample per channel. The ESAI
	 * can begin transmitting these samples while it is requesting for a
	 * DMA transfer, before said DMA transfer actually puts more samples
	 * into the ESAI FIFO. Only needed on playback/transmit.
	 */
	if (!direction) {
		/* TODO adjust this when we have more than 2 channels;
		 * we need to write a word per active channel.
		 */
		dai_write(dai, REG_ESAI_ETDR, 0);
		dai_write(dai, REG_ESAI_ETDR, 0);
	}

	/* Enable one transmitter or receiver */
	dai_update_bits(dai, REG_ESAI_xCR(direction),
			ESAI_xCR_xE_MASK(direction), ESAI_xCR_xE(direction, 1));

	/* Configure time slot registers, enable two time slots for two
	 * channels.
	 *
	 * This actually begins playback/record.
	 *
	 * The order of setting xSMB first and xSMA second is required to
	 * correctly start playback/record; setting them in reverse order may
	 * cause significant channel swap issues when using more than 16
	 * channels.
	 */
	dai_update_bits(dai, REG_ESAI_xSMB(direction), ESAI_xSMB_xS_MASK,
			ESAI_xSMB_xS_CHANS(2));

	dai_update_bits(dai, REG_ESAI_xSMA(direction), ESAI_xSMA_xS_MASK,
			ESAI_xSMA_xS_CHANS(2));
}

static void esai_stop(struct dai *dai, int direction)
{
	/* Disable transmitters/receivers */
	dai_update_bits(dai, REG_ESAI_xCR(direction),
			direction ? ESAI_xCR_TE_MASK : ESAI_xCR_RE_MASK, 0);

	/* Turn off pins */
	dai_update_bits(dai, REG_ESAI_xSMA(direction), ESAI_xSMA_xS_MASK, 0);
	dai_update_bits(dai, REG_ESAI_xSMB(direction), ESAI_xSMB_xS_MASK, 0);

	/* disable and reset FIFO */
	dai_update_bits(dai, REG_ESAI_xFCR(direction),
			ESAI_xFCR_xFR | ESAI_xFCR_xFEN, ESAI_xFCR_xFR);
	dai_update_bits(dai, REG_ESAI_xFCR(direction), ESAI_xFCR_xFR, 0);
}

static int esai_trigger(struct dai *dai, int cmd, int direction)
{
	tracev_esai("ESAI: trigger");

	switch (cmd) {
	case COMP_TRIGGER_START:
	case COMP_TRIGGER_RELEASE:
		esai_start(dai, direction);
		break;
	case COMP_TRIGGER_STOP:
	case COMP_TRIGGER_PAUSE:
		esai_stop(dai, direction);
		break;
	/* Remaining triggers are no-ops */
	case COMP_TRIGGER_SUSPEND:
	case COMP_TRIGGER_RESUME:
		break;
	default:
		trace_esai_error("ESAI: invalid trigger cmd %d", cmd);
		return -EINVAL;
	}
	return 0;
}

static int esai_probe(struct dai *dai)
{
	struct esai_pdata *pdata;

	tracev_esai("ESAI: probe");
	if (dai_get_drvdata(dai)) {
		trace_esai_error("ESAI: Repeated probe, skipping");
		return -EEXIST;
	}
	pdata = rzalloc(RZONE_SYS_RUNTIME | RZONE_FLAG_UNCACHED,
			SOF_MEM_CAPS_RAM, sizeof(*pdata));
	if (!pdata) {
		trace_esai_error("ESAI probe failure, out of memory");
		return -ENOMEM;
	}
	/* ESAI core reset */
	dai_write(dai, REG_ESAI_ECR, ESAI_ECR_ERST | ESAI_ECR_ESAIEN);
	dai_write(dai, REG_ESAI_ECR, ESAI_ECR_ESAIEN);
	/* ESAI personal reset (should be by default but just to be sure) */
	dai_write(dai, REG_ESAI_PRRC, 0);
	dai_write(dai, REG_ESAI_PCRC, 0);
	/* ESAI FIFO reset */
	dai_write(dai, REG_ESAI_TFCR, ESAI_xFCR_xFR);
	dai_write(dai, REG_ESAI_RFCR, ESAI_xFCR_xFR);
	/* Clear TSMA, TSMB (disable all transmitters and receivers) */
	dai_write(dai, REG_ESAI_TSMA, 0);
	dai_write(dai, REG_ESAI_TSMB, 0);
	dai_write(dai, REG_ESAI_RSMA, 0);
	dai_write(dai, REG_ESAI_RSMB, 0);

	return 0;
}

static int esai_get_handshake(struct dai *dai, int direction, int stream_id)
{
	int handshake = dai->plat_data.fifo[direction].handshake;
	int channel = EDMA_HS_GET_CHAN(handshake);
	int irq = irqstr_get_sof_int(EDMA_HS_GET_IRQ(handshake));

	return EDMA_HANDSHAKE(irq, channel);
}

static int esai_get_fifo(struct dai *dai, int direction, int stream_id)
{
	switch (direction) {
	case DAI_DIR_PLAYBACK:
	case DAI_DIR_CAPTURE:
		return dai_fifo(dai, direction); /* stream_id is unused */
	default:
		trace_esai_error("esai_get_fifo(): Invalid direction");
		return -EINVAL;
	}
}

const struct dai_driver esai_driver = {
	.type = SOF_DAI_IMX_ESAI,
	.dma_dev = DMA_DEV_ESAI,
	.ops = {
		.trigger		= esai_trigger,
		.set_config		= esai_set_config,
		.pm_context_store	= esai_context_store,
		.pm_context_restore	= esai_context_restore,
		.probe			= esai_probe,
		.get_handshake		= esai_get_handshake,
		.get_fifo		= esai_get_fifo,
	},
};
