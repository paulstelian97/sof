// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Marcin Maka <marcin.maka@linux.intel.com>

#include <sof/lib/dai.h>
#include <sof/spinlock.h>
#include <sof/trace/trace.h>
#include <user/trace.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#define trace_dai(__e, ...) trace_event(TRACE_CLASS_DAI, __e, ##__VA_ARGS__)

struct dai_info {
	struct dai_type_info *dai_type_array;
	size_t num_dai_types;
};

static struct dai_info lib_dai = {
	.dai_type_array = NULL,
	.num_dai_types = 0
};

void dai_install(struct dai_type_info *dai_type_array, size_t num_dai_types)
{
	lib_dai.dai_type_array = dai_type_array;
	lib_dai.num_dai_types = num_dai_types;
}

static inline struct dai_type_info *dai_find_type(uint32_t type)
{
	struct dai_type_info *dti;

	for (dti = lib_dai.dai_type_array;
	     dti < lib_dai.dai_type_array + lib_dai.num_dai_types; dti++) {
		if (dti->type == type)
			return dti;
	}
	return NULL;
}

struct dai *dai_get(uint32_t type, uint32_t index, uint32_t flags)
{
	int ret = 0;
	struct dai_type_info *dti;
	struct dai *d;

	dti = dai_find_type(type);
	if (!dti)
		return NULL; /* type not found */

	for (d = dti->dai_array; d < dti->dai_array + dti->num_dais; d++) {
		if (d->index != index)
			continue;
		/* device created? */
		spin_lock(d->lock);
		if (d->sref == 0) {
			if (flags & DAI_CREAT)
				ret = dai_probe(d);
			else
				ret = -ENODEV;
		}
		if (!ret)
			d->sref++;

		trace_dai("dai_get(), d = %p, sref = %d",
			  (uintptr_t)d, d->sref);

		spin_unlock(d->lock);

		return !ret ? d : NULL;
	}
	trace_error(TRACE_CLASS_DAI, "dai_get() error: "
		    "type = %d, index = %d not found", type, index);
	return NULL;
}

void dai_put(struct dai *dai)
{
	int ret;

	trace_error(TRACE_CLASS_DAI, "dai_put() sref=%d", dai->sref);
	spin_lock(dai->lock);
	if (--dai->sref == 0) {
		trace_error(TRACE_CLASS_DAI, "dai_put() about to call dai_remove()");
		ret = dai_remove(dai);
		trace_error(TRACE_CLASS_DAI, "dai_put() did call dai_remove()");
		if (ret < 0) {
			trace_error(TRACE_CLASS_DAI,
				    "dai_put() error: "
				    "dai_remove() failed ret = %d", ret);
		}
	}
	trace_event(TRACE_CLASS_DAI, "dai_put(), dai = %p, sref = %d",
		    (uintptr_t)dai, dai->sref);
	spin_unlock(dai->lock);
}
