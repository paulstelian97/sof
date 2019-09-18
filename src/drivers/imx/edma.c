// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright 2019 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>
// Author: Paul Olaru <paul.olaru@nxp.com>

#include <sof/audio/component.h>
#include <sof/drivers/edma.h>
#include <sof/drivers/timer.h>
#include <sof/lib/alloc.h>
#include <sof/lib/dma.h>
#include <sof/lib/io.h>
#include <sof/platform.h>
#include <stdint.h>

static int edma_compute_attr(int src_width, int dest_width)
{
	int result = 0;

	switch (src_width) {
	case 1:
		result = EDMA_TCD_ATTR_SSIZE_8BIT;
		break;
	case 2:
		result = EDMA_TCD_ATTR_SSIZE_16BIT;
		break;
	case 4:
		result = EDMA_TCD_ATTR_SSIZE_32BIT;
		break;
	case 8:
		result = EDMA_TCD_ATTR_SSIZE_64BIT;
		break;
	case 16:
		result = EDMA_TCD_ATTR_SSIZE_16BYTE;
		break;
	case 32:
		result = EDMA_TCD_ATTR_SSIZE_32BYTE;
		break;
	case 64:
		result = EDMA_TCD_ATTR_SSIZE_64BYTE;
		break;
	default:
		return -EINVAL;
	}

	switch (dest_width) {
	case 1:
		result |= EDMA_TCD_ATTR_DSIZE_8BIT;
		break;
	case 2:
		result |= EDMA_TCD_ATTR_DSIZE_16BIT;
		break;
	case 4:
		result |= EDMA_TCD_ATTR_DSIZE_32BIT;
		break;
	case 8:
		result |= EDMA_TCD_ATTR_DSIZE_64BIT;
		break;
	case 16:
		result |= EDMA_TCD_ATTR_DSIZE_16BYTE;
		break;
	case 32:
		result |= EDMA_TCD_ATTR_DSIZE_32BYTE;
		break;
	case 64:
		result |= EDMA_TCD_ATTR_DSIZE_64BYTE;
		break;
	default:
		return -EINVAL;
	}

	return result;
}

/* acquire the specific DMA channel */
static struct dma_chan_data *edma_channel_get(struct dma *dma,
					      unsigned int req_chan)
{
	struct dma_chan_data *channel;

	tracev_edma("EDMA: channel_get(%d)", req_chan);
	if (req_chan >= dma->plat_data.channels) {
		trace_edma_error("EDMA: Channel %d out of range", req_chan);
		return NULL;
	}

	channel = &dma->chan[req_chan];
	if (channel->status != COMP_STATE_INIT) {
		trace_edma_error("EDMA: Cannot reuse channel %d", req_chan);
		return NULL;
	}

	channel->status = COMP_STATE_READY;
	return channel;
}

/* channel must not be running when this is called */
static void edma_channel_put(struct dma_chan_data *channel)
{
	/* Assuming channel is stopped, we thus don't need hardware to
	 * do anything right now
	 */
	tracev_edma("EDMA: channel_put(%d)", channel->index);
	/* Also release extra memory used for scatter-gather */
	channel->status = COMP_STATE_INIT;

	dma_chan_set_data(channel, NULL);
}

static int edma_start(struct dma_chan_data *channel)
{
	tracev_edma("EDMA: start(%d)", channel->index);
	/* Assuming the channel is already configured, just perform a
	 * manual start. Also update states
	 */
	switch (channel->status) {
	case COMP_STATE_PREPARE: /* Just prepared */
	case COMP_STATE_SUSPEND: /* Must first resume though */
		/* Allow starting */
		break;
	default:
		return -EINVAL; /* Cannot start, wrong state */
	}
	channel->status = COMP_STATE_ACTIVE;
	/* Do the HW start of the DMA */
	dma_chan_reg_update_bits(channel, EDMA_TCD_CSR,
				 EDMA_TCD_CSR_START, EDMA_TCD_CSR_START);
	/* Allow the HW to automatically trigger further transfers */
	dma_chan_reg_update_bits(channel, EDMA_CH_CSR,
				 EDMA_CH_CSR_ERQ_EARQ, EDMA_CH_CSR_ERQ_EARQ);
	return 0;
}

static int edma_release(struct dma_chan_data *channel)
{
	// TODO actually handle pause/release properly?
	tracev_edma("EDMA: release(%d)", channel->index);
	// Validate state
	switch (channel->status) {
	case COMP_STATE_PAUSED:
		break;
	default:
		return -EINVAL;
	}
	channel->status = COMP_STATE_ACTIVE;
	return 0;
}

static int edma_pause(struct dma_chan_data *channel)
{
	// TODO Why is this function here, given that it is never called?
	tracev_edma("EDMA: pause(%d)", channel->index);
	// Validate state
	switch (channel->status) {
	case COMP_STATE_ACTIVE:
		break;
	default:
		return -EINVAL;
	}
	channel->status = COMP_STATE_PAUSED;
	// Disable HW requests
	dma_chan_reg_update_bits(channel, EDMA_CH_CSR,
				 EDMA_CH_CSR_ERQ_EARQ, 0);
	return 0;
}

static int edma_stop(struct dma_chan_data *channel)
{
	tracev_edma("EDMA: stop(%d)", channel->index);
	/* Validate state */
	// TODO: Should we?
	switch (channel->status) {
	case COMP_STATE_READY:
	case COMP_STATE_PREPARE:
		return 1; /* Already stopped, don't propagate request */
	case COMP_STATE_PAUSED:
	case COMP_STATE_ACTIVE:
		break;
	default:
		return -EINVAL;
	}
	channel->status = COMP_STATE_READY;
	/* Disable channel */
	dma_chan_reg_write(channel, EDMA_CH_CSR, 0);
	dma_chan_reg_write(channel, EDMA_TCD_CSR, 0);
	/* The remaining TCD values will still be valid but I don't care
	 * about them anyway
	 */

	return 0;
}

static int edma_copy(struct dma_chan_data *channel, int bytes, uint32_t flags)
{
	struct dma_cb_data next = { .elem.size = bytes };

	tracev_edma("edma_copy() is a nop to me");
	/* We need to call the copy() callback */
	if (channel->cb && channel->cb_type & DMA_CB_TYPE_COPY)
		channel->cb(channel->cb_data, DMA_CB_TYPE_COPY, &next);
	return 0;
}

static int edma_status(struct dma_chan_data *channel,
		       struct dma_chan_status *status, uint8_t direction)
{
	status->state = channel->status;
	status->flags = 0;
	/* Note: these might be slightly inaccurate as they are only
	 * updated at the end of each minor (block) transfer
	 */
	status->r_pos = dma_chan_reg_read(channel, EDMA_TCD_SADDR);
	status->w_pos = dma_chan_reg_read(channel, EDMA_TCD_DADDR);
	status->timestamp = timer_get_system(platform_timer);
	return 0;
}

static inline int signum(int x)
{
	return x < 0 ? -1 : x == 0 ? 0 : 1;
}

static int edma_validate_nonsg_config(struct dma_sg_elem_array *sgelems,
				      uint32_t soff, uint32_t doff)
{
	uint32_t sbase, dbase, size;

	if (!sgelems)
		return -EINVAL;
	if (sgelems->count != 2)
		return -EINVAL; /* Only ping-pong configs supported */

	sbase = sgelems->elems[0].src;
	dbase = sgelems->elems[0].dest;
	size = sgelems->elems[0].size;

	if (sbase + size * signum(soff) != sgelems->elems[1].src)
		return -EINVAL; /**< Not contiguous */
	if (dbase + size * signum(doff) != sgelems->elems[1].dest)
		return -EINVAL; /**< Not contiguous */
	if (size != sgelems->elems[1].size)
		return -EINVAL; /**< Mismatched sizes */

	return 0; /* Ok, we good */
}

/* Some set_config helper functions */
/**
 * \brief Compute and set the TCDs for this channel
 * \param[in] channel DMA channel
 * \param[in] soff Computed SOFF register value
 * \param[in] doff Computed DOFF register value
 * \param[in] cyclic Whether the transfer should be cyclic
 * \param[in] sg Whether we should do scatter-gather
 * \param[in] irqoff Whether the IRQ is disabled
 * \param[in] sgelems The scatter-gather elems, from the config
 * \param[in] src_width Width of transfer on source end
 * \param[in] dest_width Width of transfer on destination end
 * \return 0 on success, negative on error
 *
 * Computes the TCD to be uploaded to the hardware registers. In the
 * scatter-gather situation, it also allocates and computes the
 * additional TCDs, one for each of the input elems.
 */
static int edma_setup_tcd(struct dma_chan_data *channel, uint16_t soff,
			  uint16_t doff, bool cyclic, bool sg, bool irqoff,
			  struct dma_sg_elem_array *sgelems, int src_width,
			  int dest_width)
{
	int rc;
	uint32_t sbase, dbase, total_size, elem_count, elem_size;

	assert(!sg);
	assert(cyclic);
	/* Not scatter-gather, just create a regular TCD. Don't
	 * allocate anything
	 */

	/* The only supported non-SG configurations are:
	 * -> 2 buffers
	 * -> The buffers must be of equal size
	 * -> The buffers must be contiguous
	 * -> The first buffer should be of the lower address
	 */

	/* TODO Support more advanced non-SG configurations */

	rc = edma_validate_nonsg_config(sgelems, soff, doff);
	if (rc < 0)
		return rc;

	/* We should work directly with the TCD cache */
	sbase = sgelems->elems[0].src;
	dbase = sgelems->elems[0].dest;
	total_size = 2 * sgelems->elems[0].size;
	/* TODO more flexible elem_count and elem_size
	 * calculations
	 */
	elem_count = 2;
	elem_size = total_size / elem_count;

	rc = edma_compute_attr(src_width, dest_width);
	if (rc < 0)
		return rc;

	/* Configure the in-hardware TCD */
	dma_chan_reg_write(channel, EDMA_TCD_SADDR, sbase);
	dma_chan_reg_write16(channel, EDMA_TCD_SOFF, soff);
	dma_chan_reg_write16(channel, EDMA_TCD_ATTR, rc);
	dma_chan_reg_write(channel, EDMA_TCD_NBYTES, elem_size);
	dma_chan_reg_write(channel, EDMA_TCD_SLAST, -total_size * signum(soff));
	dma_chan_reg_write(channel, EDMA_TCD_DADDR, dbase);
	dma_chan_reg_write16(channel, EDMA_TCD_DOFF, doff);
	dma_chan_reg_write16(channel, EDMA_TCD_CITER, elem_count);
	dma_chan_reg_write(channel, EDMA_TCD_DLAST_SGA,
			   -total_size * signum(doff));
	dma_chan_reg_write16(channel, EDMA_TCD_BITER, elem_count);
	/* TODO This may be interesting for the interrupt() callback; however
	 * since in that case other bits may be nonzero it may be nontrivial
	 * to update
	 */
	dma_chan_reg_write16(channel, EDMA_TCD_CSR,
			     irqoff ? 0 : EDMA_TCD_CSR_INTMAJOR |
			     EDMA_TCD_CSR_INTHALF);

	channel->status = COMP_STATE_PREPARE;
	return 0;
}

/* set the DMA channel configuration, source/target address, buffer sizes */
static int edma_set_config(struct dma_chan_data *channel,
			   struct dma_sg_config *config)
{
	int handshake, irq, i;
	uint16_t soff = 0;
	uint16_t doff = 0;

	tracev_edma("EDMA: set config");
	tracev_edma("EDMA: source width %d dest width %d burst elems %d",
		    config->src_width, config->dest_width, config->burst_elems);

	switch (config->direction) {
	case DMA_DIR_MEM_TO_DEV:
		soff = config->src_width; doff = 0;
		handshake = config->dest_dev;
		break;
	case DMA_DIR_DEV_TO_MEM:
		soff = 0; doff = config->dest_width;
		handshake = config->src_dev;
		break;
	default:
		trace_edma_error("edma_set_config() unsupported config direction");
		return -EINVAL;
	}

	tracev_edma("EDMA: SOFF = %d DOFF = %d", soff, doff);
	tracev_edma("EDMA: src dev %d dest dev %d", config->src_dev,
		    config->dest_dev);
	tracev_edma("EDMA: cyclic = %d", config->cyclic);
	if (!config->cyclic) {
		trace_edma_error("EDMA: Only cyclic configurations are supported!");
		return -EINVAL;
	}
	if (config->scatter) {
		trace_edma_error("EDMA: scatter enabled, that is not supported for now!");
		return -EINVAL;
	}
	else
		tracev_edma("EDMA: scatter disabled");
	/* TODO Is it still interesting at this point? */
	if (config->irq_disabled) {
		tracev_edma("EDMA: IRQ disabled");
	} else {
		tracev_edma("EDMA: Registering IRQ");

		irq = EDMA_HS_GET_IRQ(handshake);
		/* TODO figure out how to use this IRQ */
		(void)irq;
	}
	tracev_edma("EDMA: %d elements", config->elem_array.count);
	for (i = 0; i < config->elem_array.count; i++)
		tracev_edma("EDMA: elem %d src 0x%08x -> dst 0x%08x size 0x%x bytes",
			    i, config->elem_array.elems[i].src,
			    config->elem_array.elems[i].dest,
			    config->elem_array.elems[i].size);
	return edma_setup_tcd(channel, soff, doff, config->cyclic,
			      config->scatter, config->irq_disabled,
			      &config->elem_array, config->src_width,
			      config->dest_width);
}

/* restore DMA context after leaving D3 */
static int edma_pm_context_restore(struct dma *dma)
{
	/* External to the DSP, won't lose power */
	return 0;
}

/* store DMA context after leaving D3 */
static int edma_pm_context_store(struct dma *dma)
{
	/* External to the DSP, won't lose power */
	return 0;
}

static int edma_set_cb(struct dma_chan_data *channel, int type,
		void (*cb)(void *data, uint32_t type, struct dma_cb_data *next),
		void *data)
{
	channel->cb = cb;
	channel->cb_type = type;
	channel->cb_data = data;
	return 0;
}

static int edma_probe(struct dma *dma)
{
	int channel;

	if (dma->chan) {
		trace_edma_error("EDMA: Repeated probe");
		return -EEXIST;
	}
	trace_edma("EDMA: probe");

	dma->chan = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
			    dma->plat_data.channels *
			    sizeof(struct dma_chan_data));
	if (!dma->chan) {
		trace_edma_error("EDMA: Probe failure, unable to allocate channel descriptors");
		return -ENOMEM;
	}
	for (channel = 0; channel < dma->plat_data.channels; channel++) {
		dma->chan[channel].dma = dma;
		dma->chan[channel].index = channel;
	}
	return 0;
}

static int edma_remove(struct dma *dma)
{
	int channel;

	trace_edma("EDMA: remove (I'd be surprised!)");
	if (!dma->chan) {
		trace_edma_error("EDMA: remove called without probe, it's a no-op");
		return 0;
	}
	for (channel = 0; channel < dma->plat_data.channels; channel++) {
		/* Disable HW requests for this channel */
		dma_chan_reg_write(&dma->chan[channel], EDMA_CH_CSR, 0);
		/* Remove TCD from channel */
		dma_chan_reg_write16(&dma->chan[channel], EDMA_TCD_CSR, 0);
	}
	rfree(dma->chan);
	dma->chan = NULL;

	return 0;
}

static int edma_interrupt(struct dma_chan_data *channel, enum dma_irq_cmd cmd)
{
	/* TODO we need to actually implement interrupt logic here now */
	switch (cmd) {
	case DMA_IRQ_STATUS_GET:
		return dma_chan_reg_read(channel, EDMA_CH_INT);
	case DMA_IRQ_CLEAR:
		dma_chan_reg_write(channel, EDMA_CH_INT, 1);
		return 0;
	case DMA_IRQ_MASK:
		trace_edma("DMA_IRQ_MASK");
		return 0;
	case DMA_IRQ_UNMASK:
		trace_edma("DMA_IRQ_UNMASK");
		return 0;
	default:
		return -EINVAL;
	}
}

static int edma_get_attribute(struct dma *dma, uint32_t type, uint32_t *value)
{
	switch (type) {
	case DMA_ATTR_BUFFER_ALIGNMENT:
	case DMA_ATTR_COPY_ALIGNMENT:
		/* With 4-byte transfers we need to align the buffers to
		 * 4 bytes. Even if it can be programmed in 2-byte or
		 * 1-byte for the transfers, this function cannot
		 * determine that. So return a conservative value.
		 */
		*value = 4;
		return 0;
	default:
		return -ENOENT; /* Attribute not found */
	}
}

static int edma_get_data_size(struct dma_chan_data *channel,
			      uint32_t *avail, uint32_t *free)
{
	switch (channel->direction) {
	case SOF_IPC_STREAM_PLAYBACK:
		*free = dma_chan_reg_read(channel, EDMA_TCD_NBYTES);
		break;
	case SOF_IPC_STREAM_CAPTURE:
		*avail = dma_chan_reg_read(channel, EDMA_TCD_NBYTES);
		break;
	default:
		trace_edma_error("edma_get_data_size() unsupported direction %d",
				 channel->direction);
		return -EINVAL;
	}
	return 0;
}

const struct dma_ops edma_ops = {
	.channel_get	= edma_channel_get,
	.channel_put	= edma_channel_put,
	.start		= edma_start,
	.stop		= edma_stop,
	.pause		= edma_pause,
	.release	= edma_release,
	.copy		= edma_copy,
	.status		= edma_status,
	.set_config	= edma_set_config,
	.set_cb		= edma_set_cb,
	.pm_context_restore	= edma_pm_context_restore,
	.pm_context_store	= edma_pm_context_store,
	.probe		= edma_probe,
	.remove		= edma_remove,
	.interrupt	= edma_interrupt,
	.get_attribute	= edma_get_attribute,
	.get_data_size	= edma_get_data_size,
};
