// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright 2019 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>
// Author: Paul Olaru <paul.olaru@nxp.com>

#include <sof/audio/component.h>
#include <sof/drivers/edma.h>
#include <sof/lib/alloc.h>
#include <sof/lib/dma.h>
#include <sof/lib/io.h>
#include <sof/platform.h>
#include <stdint.h>

/* Hardware TCD structure. Alignment to 32 bytes is mandated by the hardware */
struct edma_hw_tcd {
	uint32_t saddr;
	uint16_t soff;
	uint16_t attr;
	uint32_t nbytes;
	uint32_t slast;
	uint32_t daddr;
	uint16_t doff;
	uint16_t citer;
	uint32_t dlast_sga;
	uint16_t csr;
	uint16_t biter;
} __aligned(EDMA_TCD_ALIGNMENT);

/* Channel-specific configuration data that isn't stored in the HW registers */
struct edma_ch_data {
	struct edma_hw_tcd tcd_cache;
	struct edma_hw_tcd *tcds;
	void *tcds_alloc;
	int tcds_count;
};

static inline void edma_dump_tcd_offline(struct edma_hw_tcd *tcd)
{
	tracev_edma("EDMA_SADDR: 0x%08x", tcd->saddr);
	tracev_edma("EDMA_SOFF: 0x%04x", tcd->soff);
	tracev_edma("EDMA_ATTR: 0x%04x", tcd->attr);
	tracev_edma("EDMA_NBYTES: 0x%08x", tcd->nbytes);
	tracev_edma("EDMA_SLAST: 0x%08x", tcd->slast);
	tracev_edma("EDMA_DADDR: 0x%08x", tcd->daddr);
	tracev_edma("EDMA_DOFF: 0x%04x", tcd->doff);
	tracev_edma("EDMA_CITER: 0x%04x", tcd->citer);
	tracev_edma("EDMA_DLAST_SGA: 0x%08x", tcd->dlast_sga);
	tracev_edma("EDMA_CSR: 0x%04x", tcd->csr);
	tracev_edma("EDMA_BITER: 0x%04x", tcd->biter);
}

static inline void edma_dump_tcd(struct dma_chan_data *channel)
{
	tracev_edma("EDMA_CH_CSR: 0x%08x",
		    edma_chan_read(channel, EDMA_CH_CSR));
	tracev_edma("EDMA_CH_ES: 0x%08x",
		    edma_chan_read(channel, EDMA_CH_ES));
	tracev_edma("EDMA_CH_INT: 0x%08x",
		    edma_chan_read(channel, EDMA_CH_INT));
	tracev_edma("EDMA_CH_SBR: 0x%08x",
		    edma_chan_read(channel, EDMA_CH_SBR));
	tracev_edma("EDMA_CH_PRI: 0x%08x",
		    edma_chan_read(channel, EDMA_CH_PRI));
	tracev_edma("EDMA_TCD_SADDR: 0x%08x",
		    edma_chan_read(channel, EDMA_TCD_SADDR));
	tracev_edma("EDMA_TCD_SOFF: 0x%04x",
		    edma_chan_read16(channel, EDMA_TCD_SOFF));
	tracev_edma("EDMA_TCD_ATTR: 0x%04x",
		    edma_chan_read16(channel, EDMA_TCD_ATTR));
	tracev_edma("EDMA_TCD_NBYTES: 0x%08x",
		    edma_chan_read(channel, EDMA_TCD_NBYTES));
	tracev_edma("EDMA_TCD_SLAST: 0x%08x",
		    edma_chan_read(channel, EDMA_TCD_SLAST));
	tracev_edma("EDMA_TCD_DADDR: 0x%08x",
		    edma_chan_read(channel, EDMA_TCD_DADDR));
	tracev_edma("EDMA_TCD_DOFF: 0x%04x",
		    edma_chan_read16(channel, EDMA_TCD_DOFF));
	tracev_edma("EDMA_TCD_CITER: 0x%04x",
		    edma_chan_read16(channel, EDMA_TCD_CITER));
	tracev_edma("EDMA_TCD_DLAST_SGA: 0x%08x",
		    edma_chan_read(channel, EDMA_TCD_DLAST_SGA));
	tracev_edma("EDMA_TCD_CSR: 0x%04x",
		    edma_chan_read16(channel, EDMA_TCD_CSR));
	tracev_edma("EDMA_TCD_BITER: 0x%04x",
		    edma_chan_read16(channel, EDMA_TCD_BITER));

	if (dma_chan_reg_read16(channel, EDMA_TCD_CSR) & EDMA_TCD_CSR_ESG) {
		tracev_edma("EDMA: Dumping ESG next value");
		edma_dump_tcd_offline((struct edma_hw_tcd *)
				      dma_chan_reg_read(channel,
							EDMA_TCD_DLAST_SGA));
	}
}

static void edma_channel_load_tcd(struct dma_chan_data *channel)
{
	struct edma_ch_data *ch;

	if (!channel)
		return;
	ch = dma_chan_get_data(channel);
	if (!ch)
		return;
	dma_chan_reg_write16(channel, EDMA_TCD_CSR, 0); /* Stop the channel */

	dma_chan_reg_write(channel, EDMA_TCD_SADDR, ch->tcd_cache.saddr);
	dma_chan_reg_write16(channel, EDMA_TCD_SOFF, ch->tcd_cache.soff);
	dma_chan_reg_write16(channel, EDMA_TCD_ATTR, ch->tcd_cache.attr);
	dma_chan_reg_write(channel, EDMA_TCD_NBYTES, ch->tcd_cache.nbytes);
	dma_chan_reg_write(channel, EDMA_TCD_SLAST, ch->tcd_cache.slast);
	dma_chan_reg_write(channel, EDMA_TCD_DADDR, ch->tcd_cache.daddr);
	dma_chan_reg_write16(channel, EDMA_TCD_DOFF, ch->tcd_cache.doff);
	dma_chan_reg_write16(channel, EDMA_TCD_CITER, ch->tcd_cache.citer);
	dma_chan_reg_write(channel, EDMA_TCD_DLAST_SGA,
			   ch->tcd_cache.dlast_sga);
	dma_chan_reg_write16(channel, EDMA_TCD_BITER, ch->tcd_cache.biter);
	/* Write it the last, this may trigger some action */
	dma_chan_reg_write16(channel, EDMA_TCD_CSR, ch->tcd_cache.csr);
}

static void edma_channel_save_tcd(struct dma_chan_data *channel)
{
	struct edma_ch_data *ch;

	if (!channel)
		return;
	ch = dma_chan_get_data(channel);
	if (!ch)
		return;

	ch->tcd_cache.saddr = dma_chan_reg_read(channel, EDMA_TCD_SADDR);
	ch->tcd_cache.soff = dma_chan_reg_read16(channel, EDMA_TCD_SOFF);
	ch->tcd_cache.attr = dma_chan_reg_read16(channel, EDMA_TCD_ATTR);
	ch->tcd_cache.nbytes = dma_chan_reg_read(channel, EDMA_TCD_NBYTES);
	ch->tcd_cache.slast = dma_chan_reg_read(channel, EDMA_TCD_SLAST);
	ch->tcd_cache.daddr = dma_chan_reg_read(channel, EDMA_TCD_DADDR);
	ch->tcd_cache.doff = dma_chan_reg_read16(channel, EDMA_TCD_DOFF);
	ch->tcd_cache.citer = dma_chan_reg_read16(channel, EDMA_TCD_CITER);
	ch->tcd_cache.dlast_sga = dma_chan_reg_read(channel,
						    EDMA_TCD_DLAST_SGA);
	ch->tcd_cache.csr = dma_chan_reg_read16(channel, EDMA_TCD_CSR);
	ch->tcd_cache.biter = dma_chan_reg_read16(channel, EDMA_TCD_BITER);
}

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
	struct dma_chan_data *chans = dma_get_drvdata(dma);
	struct dma_chan_data *channel;
	struct edma_ch_data *ch;

	tracev_edma("EDMA: channel_get(%d)", req_chan);
	if (req_chan >= dma->plat_data.channels) {
		trace_edma_error("EDMA: Channel %d out of range", req_chan);
		return NULL;
	}

	channel = &chans[req_chan];
	if (channel->status != COMP_STATE_INIT) {
		trace_edma_error("EDMA: Cannot reuse channel %d", req_chan);
		return NULL;
	}

	ch = dma_chan_get_data(channel);
	if (!ch) {
		/* Attempt allocating the channel data */
		ch = rzalloc(RZONE_RUNTIME | RZONE_FLAG_UNCACHED,
			     SOF_MEM_CAPS_RAM, sizeof(*ch));
		if (!ch) {
			trace_edma_error("EDMA: Out of memory allocating channel private data");
			trace_edma_error("EDMA: Channel %d", req_chan);
			return NULL;
		}
		dma_chan_set_data(channel, ch);
	}
	channel->status = COMP_STATE_READY;
	return channel;
}

/* channel must not be running when this is called */
static void edma_channel_put(struct dma_chan_data *channel)
{
	struct edma_ch_data *ch = dma_chan_get_data(channel);

	if (!ch) {
		tracev_edma("EDMA: channel_put(%d) [DUMMY]", channel->index);
		return;
	}
	/* Assuming channel is stopped, we thus don't need hardware to
	 * do anything right now
	 */
	tracev_edma("EDMA: channel_put(%d)", channel->index);
	/* Also release extra memory used for scatter-gather */
	channel->status = COMP_STATE_INIT;
	if (ch->tcds_alloc)
		rfree(ch->tcds_alloc);

	dma_chan_set_data(channel, NULL);
	rfree(ch);
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
	// Reenable HW requests
//	edma_chan_update_bits(channel, EDMA_CH_CSR,
//			      EDMA_CH_CSR_ERQ_EARQ, EDMA_CH_CSR_ERQ_EARQ);
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
	struct edma_ch_data *ch = dma_chan_get_data(channel);
	int rc, i;
	uint32_t sbase, dbase, total_size, elem_count, elem_size;
	struct edma_hw_tcd *tcd;
	struct dma_sg_elem *elem;

	// TODO
	/* Set up the normal TCD and, for SG, also set up the SG TCDs */
	if (!sg) {
		/* Not scatter-gather, just create a regular TCD. Don't
		 * allocate anything
		 */

		/* The only supported non-SG configurations are:
		 * -> 2 buffers
		 * -> The buffers must be of equal size
		 * -> The buffers must be contiguous
		 * -> The first buffer should be of the lower address
		 */

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

		ch->tcd_cache.saddr = sbase;
		ch->tcd_cache.soff = soff;
		rc = edma_compute_attr(src_width, dest_width);
		if (rc < 0)
			return rc;
		ch->tcd_cache.attr = rc;
		ch->tcd_cache.nbytes = elem_size;
		ch->tcd_cache.slast = -total_size * signum(soff);
		ch->tcd_cache.daddr = dbase;
		ch->tcd_cache.doff = doff;
		ch->tcd_cache.citer = elem_count;
		ch->tcd_cache.dlast_sga = -total_size * signum(doff);
		ch->tcd_cache.csr = irqoff ? 0 : EDMA_TCD_CSR_INTMAJOR |
			EDMA_TCD_CSR_INTHALF;
		ch->tcd_cache.biter = elem_count;

		goto load_cached;
		/* The below two lines are unreachable */
		trace_edma_error("Unable to set up non-SG");
		return -EINVAL;
	}
	/* Scatter-gather, we need to allocate additional TCDs */
	// TODO reenable support (currently not supported)
	return -EINVAL;
	ch->tcds_count = sgelems->count;
	/* Since we don't (yet) have aligned allocators, we will do this */
	// HACK
	ch->tcds_alloc = rzalloc(RZONE_RUNTIME | RZONE_FLAG_UNCACHED,
				 SOF_MEM_CAPS_RAM,
				 (sgelems->count + 1) *
				 sizeof(struct edma_hw_tcd));

	if (!ch->tcds_alloc) {
		ch->tcds_count = 0;
		trace_edma_error("Unable to allocate SG TCDs");
		return -ENOMEM;
	}

	ch->tcds = (struct edma_hw_tcd *)
		ALIGN_UP((uintptr_t)ch->tcds_alloc, EDMA_TCD_ALIGNMENT);

	/* Populate each tcd */
	for (i = 0; i < ch->tcds_count; i++) {
		tcd = &ch->tcds[i];
		elem = &sgelems->elems[i];

		tcd->saddr = elem->src;
		tcd->soff = soff;
		rc = edma_compute_attr(src_width, dest_width);
		if (rc < 0)
			return rc;
		tcd->attr = rc;
		tcd->nbytes = elem->size;
		tcd->slast = 0; /* Not used */
		tcd->daddr = elem->dest;
		tcd->doff = doff;
		tcd->citer = 1;
		tcd->dlast_sga = (uint32_t)&ch->tcds[i + 1];
		tcd->csr = EDMA_TCD_CSR_INTMAJOR | EDMA_TCD_CSR_ESG;
		if (irqoff)
			tcd->csr = EDMA_TCD_CSR_ESG;
		tcd->biter = 1;
	}

	if (ch->tcds_count) {
		ch->tcds[ch->tcds_count - 1].dlast_sga =
			(uint32_t)(cyclic ? &ch->tcds[0] : 0);
		if (!cyclic) {
			ch->tcds[ch->tcds_count - 1].csr
				&= ~EDMA_TCD_CSR_ESG;
			ch->tcds[ch->tcds_count - 1].csr
				|= EDMA_TCD_CSR_DREQ;
		}
	}

	/* We will also copy the first TCD into the cache, for later loading */
	ch->tcd_cache = ch->tcds[0];

load_cached:
	/* Load the HW TCD */
	edma_channel_load_tcd(channel);

	channel->status = COMP_STATE_PREPARE;
	return 0;
}

static void edma_chan_irq(struct dma_chan_data *channel)
{
	struct dma_cb_data next = {
		.status = DMA_CB_STATUS_RELOAD,
	};

	if (!channel->cb)
		return;
	if (!(channel->cb_type & DMA_CB_TYPE_IRQ))
		return;

	channel->cb(channel->cb_data, DMA_CB_TYPE_IRQ, &next);

	/* We will ignore status as it is currently
	 * never set
	 */
	/* TODO consider changes in next.status */
	/* By default we let the TCDs just continue
	 * reloading
	 */
}

static void edma_irq(void *arg)
{
	struct dma_chan_data *channel = (struct dma_chan_data *)arg;
	uint32_t err_status;
	int ch_int;

	/* Check the error status for this channel */
	err_status = dma_chan_reg_read(channel, EDMA_CH_ES);
	if (err_status & EDMA_CH_ES_ERR) {
		/* Clear the error status bit */
		dma_chan_reg_update_bits(channel, EDMA_CH_ES, EDMA_CH_ES_ERR,
					 EDMA_CH_ES_ERR);
		/* Print the detected errors */
		trace_edma_error("EDMA: Error detected on channel %d. Printing bits:",
				 channel->index);
		if (err_status & EDMA_CH_ES_SAE)
			trace_edma_error("EDMA: SAE");
		if (err_status & EDMA_CH_ES_SOE)
			trace_edma_error("EDMA: SOE");
		if (err_status & EDMA_CH_ES_DAE)
			trace_edma_error("EDMA: DAE");
		if (err_status & EDMA_CH_ES_DOE)
			trace_edma_error("EDMA: DOE");
		if (err_status & EDMA_CH_ES_NCE)
			trace_edma_error("EDMA: NCE");
		if (err_status & EDMA_CH_ES_SGE)
			trace_edma_error("EDMA: SGE");
		if (err_status & EDMA_CH_ES_SBE)
			trace_edma_error("EDMA: SBE");
		if (err_status & EDMA_CH_ES_DBE)
			trace_edma_error("EDMA: DBE");
	}

	/* Check if the interrupt is real */
	ch_int = dma_chan_reg_read(channel, EDMA_CH_INT);
	if (!ch_int) {
		/* Cannot do below trace, shared interrupts will be able to
		 * cause storms of the below message -- every legitimate
		 * interrupt would get the below message too.
		 */
		/* trace_edma_error("EDMA spurious interrupt"); */
		return;
	}

	/* We have an interrupt, we should handle it... */
	edma_chan_irq(channel);

	/* Clear the interrupt as required by the HW specs */
	dma_chan_reg_write(channel, EDMA_CH_INT, 1);
}

/* set the DMA channel configuration, source/target address, buffer sizes */
static int edma_set_config(struct dma_chan_data *channel,
			   struct dma_sg_config *config)
{
	int handshake, irq, rc, i;
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
	if (config->scatter)
		tracev_edma("EDMA: scatter enabled");
	else
		tracev_edma("EDMA: scatter disabled");
	if (config->irq_disabled) {
		tracev_edma("EDMA: IRQ disabled");
	} else {
		tracev_edma("EDMA: Registering IRQ");

		irq = EDMA_HS_GET_IRQ(handshake);
		trace_edma_error("EDMA registering irq %d", irq);
		rc = interrupt_register(irq, IRQ_AUTO_UNMASK, &edma_irq,
					channel);

		if (rc == -EEXIST) {
			/* Ignore error, it's also our handler */
			rc = 0;
		}

		if (rc < 0) {
			trace_edma_error("Unable to register IRQ, bailing (rc = %d)",
					 rc);
			return rc;
		}

		interrupt_enable(irq, channel);

		/* TODO: Figure out when to disable and perhaps
		 * unregister the interrupts
		 * (note: future upstream PR may make this issue go
		 * away)
		 */
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
	struct dma_chan_data *chans = dma_get_drvdata(dma);
	int channel;

	tracev_edma("EDMA: resuming... We need to restore from the cache");
	for (channel = 0; channel < dma->plat_data.channels; channel++)
		edma_channel_load_tcd(&chans[channel]);
	return 0;
}

/* store DMA context after leaving D3 */
static int edma_pm_context_store(struct dma *dma)
{
	struct dma_chan_data *chans = dma_get_drvdata(dma);
	int channel;

	/* Save all channels' registers */
	for (channel = 0; channel < dma->plat_data.channels; channel++)
		edma_channel_save_tcd(&chans[channel]);

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

	chans = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
			dma->plat_data.channels * sizeof(struct dma_chan_data));
	if (!chans) {
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
	struct dma_chan_data *chans = dma_get_drvdata(dma);
	struct dma_chan_data *chan;
	struct edma_chan_data *ch;
	int channel;

	trace_edma("EDMA: remove (I'd be surprised!)");
	if (!chans) {
		trace_edma_error("EDMA: remove called without probe, it's a no-op");
		return 0;
	}
	for (channel = 0; channel < dma->plat_data.channels; channel++) {
		/* Disable HW requests for this channel */
		dma_chan_reg_write(&chans[channel], EDMA_CH_CSR, 0);
		/* Remove TCD from channel */
		dma_chan_reg_write16(&chans[channel], EDMA_TCD_CSR, 0);
		/* Free up channel private data */
		chan = &chans[channel];
		ch = dma_chan_get_data(chan);
		dma_chan_set_data(&chans[channel], NULL);
		/* No need to NULL-check since rfree does it itself */
		rfree(ch);
	}
	rfree(chans);
	dma_set_drvdata(dma, NULL);
	return 0;
}

static int edma_interrupt(struct dma_chan_data *channel, enum dma_irq_cmd cmd)
{
	/* TODO we need to actually implement interrupt logic here now */
//	trace_edma_error("edma_interrupt(channel=%d)", channel->index);
	switch (cmd) {
	case DMA_IRQ_STATUS_GET:
//		trace_edma("DMA_IRQ_STATUS_GET");
		return dma_chan_reg_read(channel, EDMA_CH_INT);
	case DMA_IRQ_CLEAR:
		dma_chan_reg_write(channel, EDMA_CH_INT, 1);
//		trace_edma("DMA_IRQ_CLEAR (OK)");
		return 0;
	case DMA_IRQ_MASK:
		trace_edma("DMA_IRQ_MASK");
		return 0;
	case DMA_IRQ_UNMASK:
		trace_edma("DMA_IRQ_UNMASK");
		return 0;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
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
	return 0;
}

static int edma_get_data_size(struct dma_chan_data *channel,
			      uint32_t *avail, uint32_t *free)
{
	// TODO get a correct data size
	/* Since we cannot guess how much data actually is in the
	 * hardware FIFOs, we should return how much we can transfer
	 * every interrupt. The copy() function will report to the
	 * registered callback when this amount of data was copied.
	 *
	 * The current hardcoded size is:
	 * 1ms * 2ch * 48 samples/ms/ch * 4 bytes per sample
	 */
	switch (channel->direction) {
	case SOF_IPC_STREAM_PLAYBACK:
		*free = 384;
		break;
	case SOF_IPC_STREAM_CAPTURE:
		*avail = 384;
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
