// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright 2019 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>
// Author: Paul Olaru <paul.olaru@nxp.com>

/* Dummy DMA driver (software-based DMA controller)
 *
 * This driver is usable on all platforms where the DSP can directly access
 * all of the host physical memory (or at least the host buffers).
 *
 * The way this driver works is that it simply performs the copies
 * synchronously within the dma_start() and dma_copy() calls.
 *
 * One of the drawbacks of this driver is that it doesn't actually have a true
 * IRQ context, as the copy is done synchronously and the IRQ callbacks are
 * called in process context.
 *
 * An actual hardware DMA driver may be preferable because of the above
 * drawback which comes from a software implementation. But if there isn't any
 * hardware DMA controller dedicated for the host this driver can be used.
 *
 * This driver requires physical addresses in the elems. This assumption only
 * holds if you have CONFIG_HOST_PTABLE enabled, at least currently.
 */

#include <sof/atomic.h>
#include <sof/lib/dma.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sof/platform.h>
#include <sof/string.h>
#include <config.h>
#include <sof/audio/component.h>

#define trace_dummydma(__e, ...) \
	trace_event(TRACE_CLASS_DMA, __e, ##__VA_ARGS__)
#define tracev_dummydma(__e, ...) \
	tracev_event(TRACE_CLASS_DMA, __e, ##__VA_ARGS__)
#define trace_dummydma_error(__e, ...) \
	trace_error(TRACE_CLASS_DMA, __e, ##__VA_ARGS__)

struct dma_chan_pdata {
	struct dma_sg_elem_array *elems;
	int sg_elem_curr_idx;
	uintptr_t r_pos;
	uintptr_t w_pos;
};

/**
 * \brief Allocate next free DMA channel
 * \param[in] dma: DMA controller
 * \param[in] req_chan: Ignored, would have been a preference for a particular
 *			channel
 * \return A structure to be used with the other callbacks in this driver,
 * or NULL in case no channel could be allocated.
 *
 * This function allocates a DMA channel for actual usage by any SOF client
 * code.
 */
static struct dma_chan_data *dummy_dma_channel_get(struct dma *dma,
						   unsigned int req_chan)
{
	struct dma_chan_data *chans = dma_get_drvdata(dma);
	uint32_t flags;
	int i;

	spin_lock_irq(dma->lock, flags);
	for (i = 0; i < dma->plat_data.channels; i++) {
		/* use channel if it's free */
		if (chans[i].status == COMP_STATE_INIT) {
			chans[i].status = COMP_STATE_READY;

			atomic_add(&dma->num_channels_busy, 1);

			/* return channel */
			spin_unlock_irq(dma->lock, flags);
			return &chans[i];
		}
	}
	spin_unlock_irq(dma->lock, flags);
	trace_dummydma_error("dummy-dmac: %d no free channel",
			     dma->plat_data.id);
	return NULL;
}

static void dummy_dma_channel_put_unlocked(struct dma_chan_data *channel)
{
	struct dma_chan_pdata *ch = dma_chan_get_data(channel);

	/* Reset channel state */
	channel->cb = NULL;
	channel->cb_type = 0;
	channel->cb_data = NULL;

	ch->elems = NULL;
	channel->desc_count = 0;
	ch->sg_elem_curr_idx = 0;

	ch->r_pos = 0;
	ch->w_pos = 0;

	channel->status = COMP_STATE_INIT;
	atomic_sub(&channel->dma->num_channels_busy, 1);
}

/**
 * \brief Free a DMA channel
 * \param[in] channel: DMA channel
 *
 * Once a DMA channel is no longer needed it should be freed by calling this
 * function.
 */
static void dummy_dma_channel_put(struct dma_chan_data *channel)
{
	uint32_t flags;

	spin_lock_irq(channel->dma->lock, flags);
	dummy_dma_channel_put_unlocked(channel);
	spin_unlock_irq(channel->dma->lock, flags);
}

static bool do_onecopy(struct dma_chan_data *ch, struct dma_cb_data *next,
		       size_t *size);

/* Trigger start on this DMA channel */
static int dummy_dma_start(struct dma_chan_data *channel)
{
	struct dma_cb_data next = { .status = DMA_CB_STATUS_SPLIT };
	size_t ignored_size;

	/* On start we need to copy one elem, otherwise the calling code may
	 * end up waiting forever for a copy that never completes.
	 *
	 * Since this is done strictly on the CPU, we can only do it
	 * synchronously. So let's do it here.
	 */
	do_onecopy(channel, &next, &ignored_size);
	return 0;
}

/* Since copies are synchronous, release trigger is a no-op */
static int dummy_dma_release(struct dma_chan_data *channel)
{
	return 0;
}

/* Since copies are synchronous, pause trigger is a no-op */
static int dummy_dma_pause(struct dma_chan_data *channel)
{
	return 0;
}

/* Since copies are synchronous, stop trigger is a no-op */
static int dummy_dma_stop(struct dma_chan_data *channel)
{
	return 0;
}

/* fill in "status" with current DMA channel state and position */
static int dummy_dma_status(struct dma_chan_data *channel,
			    struct dma_chan_status *status,
			    uint8_t direction)
{
	struct dma_chan_pdata *ch = dma_chan_get_data(channel);

	status->state = channel->status;
	status->flags = 0; /* TODO What flags should be put here? */
	status->r_pos = ch->r_pos;
	status->w_pos = ch->w_pos;

	status->timestamp = timer_get_system(platform_timer);
	return 0;
}

/**
 * \brief Set channel configuration
 * \param[in] channel: The channel to configure
 * \param[in] config: Configuration data
 * \return 0 on success, -EINVAL if the config is invalid or unsupported.
 *
 * Sets the channel configuration. For this particular driver the config means
 * the direction and the actual SG elems for copying.
 */
static int dummy_dma_set_config(struct dma_chan_data *channel,
				struct dma_sg_config *config)
{
	struct dma_chan_pdata *ch = dma_chan_get_data(channel);
	uint32_t flags;
	int ret = 0;

	spin_lock_irq(channel->dma->lock, flags);

	if (!config->elem_array.count) {
		trace_dummydma_error("dummy-dmac: %d channel %d no DMA descriptors",
				     channel->dma->plat_data.id,
				     channel->index);

		ret = -EINVAL;
		goto out;
	}

	channel->direction = config->direction;

	if (config->direction != DMA_DIR_HMEM_TO_LMEM &&
	    config->direction != DMA_DIR_LMEM_TO_HMEM) {
		/* Shouldn't even happen though */
		trace_dummydma_error("dummy-dmac: %d channel %d invalid direction %d",
				     channel->dma->plat_data.id, channel->index,
				     config->direction);
		ret = -EINVAL;
		goto out;
	}
	channel->desc_count = config->elem_array.count;
	ch->elems = &config->elem_array;
	ch->sg_elem_curr_idx = 0;

	channel->status = COMP_STATE_PREPARE;
out:
	spin_unlock_irq(channel->dma->lock, flags);
	return ret;
}

/* restore DMA context after leaving D3 */
static int dummy_dma_pm_context_restore(struct dma *dma)
{
	/* Virtual device, no hardware registers */
	return 0;
}

/* store DMA context after leaving D3 */
static int dummy_dma_pm_context_store(struct dma *dma)
{
	/* Virtual device, no hardware registers */
	return 0;
}

static int dummy_dma_set_cb(struct dma_chan_data *channel, int type,
		void (*cb)(void *data, uint32_t type, struct dma_cb_data *next),
		void *data)
{
	channel->cb = cb;
	channel->cb_data = data;
	channel->cb_type = type;
	return 0;
}

/**
 * \brief Get the next SG elem from the active configuration
 * \param[in] channel: DMA channel to get the config from
 * \param[out] next: Next element
 * Return: true if @next was modified, false if no next element available.
 *
 * Get the next element from the config, so that the copy flow can continue.
 */
static bool get_next_elem(struct dma_chan_data *channel,
			  struct dma_cb_data *next)
{
	struct dma_chan_pdata *ch = dma_chan_get_data(channel);
	int curr_idx = ch->sg_elem_curr_idx;

	if (curr_idx >= channel->desc_count)
		return false;

	ch->sg_elem_curr_idx++;

	next->elem = ch->elems->elems[curr_idx];
	return true;
}

/**
 * \brief Perform one individual copy
 * \param[in] ch: DMA channel to do the copy for
 * \param[in, out] next: Next element
 * \param[out] size: Size in bytes of the last completed transfer
 * \return: true if the transfer should continue, false if it should stop
 *	    immediately
 *
 * Does a copy of one elem. The elem itself is either from the config or set
 * by a IRQ callback using DMA_CB_STATUS_SPLIT.
 *
 * Updates @next within the callback. If the callback set next.status as
 * DMA_CB_STATUS_RELOAD, @next is additionally updated to get the next elem
 * from the config.
 */
static bool do_onecopy(struct dma_chan_data *ch, struct dma_cb_data *next,
		       size_t *size)
{
	struct dma_chan_pdata *pdata = dma_chan_get_data(ch);
	void *dest = (void *)next->elem.dest;
	void *src = (void *)next->elem.src;
	size_t xfer_size = next->elem.size;

	/* Do the copy */
	/* Note that we don't know the real buffer size,
	 * we assume `size` is correct for memcpy_s
	 */
	memcpy_s(dest, xfer_size, src, xfer_size);

	*size = xfer_size;

	/* For status, point to immediately after the last copy */
	pdata->r_pos = next->elem.src + xfer_size;
	pdata->w_pos = next->elem.dest + xfer_size;

	if (!(ch->cb_type & DMA_CB_TYPE_IRQ))
		return false; /* Callback not applicable, end for now */

	/* Call the registered DMA IRQ callback to finish processing of this
	 * elem. Default to loading the next descriptor.
	 */
	next->status = DMA_CB_STATUS_RELOAD;

	ch->cb(ch->cb_data, DMA_CB_TYPE_IRQ, next);

	/* Interpret what the callback set in next */
	switch (next->status) {
	case DMA_CB_STATUS_RELOAD:
		/* Load next elem from the config */
		return get_next_elem(ch, next);
	case DMA_CB_STATUS_SPLIT:
		// TODO upstream change will remove this case
		/* We got new elem in next; we may just return */
		return true;
	case DMA_CB_STATUS_END:
	case DMA_CB_STATUS_IGNORE:
		/* This copy should end */
		return false;
	default:
		trace_dummydma_error("dummy_dma docopy: switch default case");
		trace_dummydma_error("dummy_dma docopy: status is %d",
				     next->status);
		/* Stop now */
		return false;
	}
}

/**
 * \brief Perform the actual copying.
 * \param[in] ch: Channel to perform the copying on
 * \param[in] bytes: Minimum number of bytes to be copied
 * \return The number of bytes actually copied
 *
 * Copy @bytes bytes on channel @ch.
 * The copying may stop short if a callback returned DMA_CB_TYPE_IGNORE,
 * DMA_CB_TYPE_END or if the elems have been exhausted from the config.
 */
static int do_copy(struct dma_chan_data *ch, int bytes)
{
	/* Allocate a static "next" variable */
	/* TODO Maybe save it in the channel data??
	 * No longer needed once split transfers go away
	 */
	// TODO update this after upstream changes to DMA
	struct dma_cb_data next;
	int xfer_bytes = 0;
	size_t size;
	bool should_continue;

	/* Just don't do any copy if there is no way to configure it */
	if (!get_next_elem(ch, &next))
		return 0;

	do {
		should_continue = do_onecopy(ch, &next, &size);
		xfer_bytes += size;
	} while (should_continue && xfer_bytes < bytes);

	return xfer_bytes;
}

/**
 * \brief Perform the DMA copy itself
 * \param[in] channel The channel to do the copying
 * \param[in] bytes How many bytes are requested to be copied
 * \param[in] flags Flags which may alter the copying (this driver ignores them)
 * \return 0 on success (this driver always succeeds)
 *
 * The copying must be done synchronously within this function, then SOF (the
 * host component) is notified via the callback that this number of bytes is
 * available.
 */
static int dummy_dma_copy(struct dma_chan_data *channel, int bytes,
			  uint32_t flags)
{
	struct dma_cb_data next;

	next.elem.size = do_copy(channel, bytes);

	/* Let the user of the driver know how much we copied */
	if (channel->cb_type & DMA_CB_TYPE_COPY)
		channel->cb(channel->cb_data, DMA_CB_TYPE_COPY, &next);

	return 0;
}

/**
 * \brief Initialize the driver
 * \param[in] The preallocated DMA controller structure
 * \return 0 on success, a negative value on error
 *
 * This function must be called before any other will work. Calling functions
 * such as dma_channel_get() without a successful dma_probe() is undefined
 * behavior.
 */
static int dummy_dma_probe(struct dma *dma)
{
	struct dma_chan_data *dma_chans;
	struct dma_chan_pdata *chanp;
	int i;

	if (dma_get_drvdata(dma)) {
		trace_dummydma_error("dummy-dmac %d already created!",
				     dma->plat_data.id);
		return -EEXIST; /* already created */
	}

	dma_chans = rzalloc(RZONE_SYS_RUNTIME | RZONE_FLAG_UNCACHED,
			    SOF_MEM_CAPS_RAM,
			    dma->plat_data.channels * sizeof(dma_chans[0]));
	if (!dma_chans) {
		trace_dummydma_error("dummy-dmac %d: Out of memory!",
				     dma->plat_data.id);
		return -ENOMEM;
	}

	chanp = rzalloc(RZONE_SYS_RUNTIME | RZONE_FLAG_UNCACHED,
			SOF_MEM_CAPS_RAM,
			dma->plat_data.channels * sizeof(chanp[0]));
	if (!chanp) {
		rfree(dma_chans);
		trace_dummydma_error("dummy-dmac %d: Out of memory!",
				     dma->plat_data.id);
		return -ENOMEM;
	}

	dma_set_drvdata(dma, dma_chans);

	for (i = 0; i < dma->plat_data.channels; i++) {
		dma_chans[i].dma = dma;
		dma_chans[i].index = i;
		dma_chans[i].status = COMP_STATE_INIT;
		dma_chans[i].private = &chanp[i];
	}

	atomic_init(&dma->num_channels_busy, 0);

	return 0;
}

/**
 * \brief Free up all memory and resources used by this driver
 * \param[in] dma The DMA controller structure belonging to this driver
 *
 * This function undoes everything that probe() did. All channels that were
 * returned via dma_channel_get() become invalid and further usage of them is
 * undefined behavior. dma_channel_put() is automatically called on all
 * channels.
 *
 * This function is idempotent, and safe to call multiple times in a row.
 */
static int dummy_dma_remove(struct dma *dma)
{
	struct dma_chan_data *chans = dma_get_drvdata(dma);

	tracev_dummydma("dummy_dma %d -> remove", dma->plat_data.id);
	if (!chans)
		return 0;

	rfree(dma_chan_get_data(&chans[0]));
	rfree(chans);
	dma_set_drvdata(dma, NULL);
	return 0;
}

/**
 * \brief Get DMA copy data sizes
 * \param[in] channel DMA channel on which we're interested of the sizes
 * \param[out] avail How much data the channel can deliver if copy() is called
 *		     now
 * \param[out] free How much data can be copied to the host via this channel
 *		    without going over the buffer size
 * \return 0 on success, -EINVAL if a configuration error is detected
 */
static int dummy_dma_get_data_size(struct dma_chan_data *channel,
				   uint32_t *avail, uint32_t *free)
{
	/* TODO remove hardcode, if possible */
	/* Currently hardcoded as
	 * 1ms * 2 channels * 48 samples * 4 bytes per sample
	 */
	switch (channel->direction) {
	case DMA_DIR_HMEM_TO_LMEM:
		*avail = 384;
		break;
	case DMA_DIR_LMEM_TO_HMEM:
		*free = 384;
		break;
	default:
		trace_dummydma_error("get_data_size direction: %d",
				     channel->direction);
		return -EINVAL;
	}
	return 0;
}

static int dummy_dma_get_attribute(struct dma *dma, uint32_t type,
				   uint32_t *value)
{
	switch (type) {
	case DMA_ATTR_BUFFER_ALIGNMENT:
	case DMA_ATTR_COPY_ALIGNMENT:
		*value = 4; /* Align to uint32_t; not sure, maybe 1 works? */
		return 0;
	default:
		return -ENOENT; /* Attribute not found */
	}
	return 0;
}

const struct dma_ops dummy_dma_ops = {
	.channel_get	= dummy_dma_channel_get,
	.channel_put	= dummy_dma_channel_put,
	.start		= dummy_dma_start,
	.stop		= dummy_dma_stop,
	.pause		= dummy_dma_pause,
	.release	= dummy_dma_release,
	.copy		= dummy_dma_copy,
	.status		= dummy_dma_status,
	.set_config	= dummy_dma_set_config,
	.set_cb		= dummy_dma_set_cb,
	.pm_context_restore		= dummy_dma_pm_context_restore,
	.pm_context_store		= dummy_dma_pm_context_store,
	.probe		= dummy_dma_probe,
	.remove		= dummy_dma_remove,
	.get_data_size	= dummy_dma_get_data_size,
	.get_attribute	= dummy_dma_get_attribute,
};
