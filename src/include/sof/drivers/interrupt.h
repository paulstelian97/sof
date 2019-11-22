/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Author: Janusz Jankowski <janusz.jankowski@linux.intel.com>
 */

#ifndef __SOF_DRIVERS_INTERRUPT_H__
#define __SOF_DRIVERS_INTERRUPT_H__

#include <arch/drivers/interrupt.h>
#include <platform/drivers/interrupt.h>
#include <sof/lib/cpu.h>
#include <sof/list.h>
#include <sof/trace/trace.h>
#include <user/trace.h>
#include <stdbool.h>
#include <stdint.h>

/* For spinlock_t we cannot directly include <sof/spinlock.h> anymore. But
 * we only need this type which is defined in <arch/spinlock.h>. But in
 * order to get to it we need to pretend we are including
 * <sof/spinlock.h> to avoid the compile error.
 *
 * Also, if that file is already included we don't need further
 * includes.
 *
 * Solves a circular dependency.
 */

#ifndef __SOF_SPINLOCK_H__
#define __SOF_SPINLOCK_H__
#include <arch/spinlock.h>
#undef __SOF_SPINLOCK_H__
#endif

#define trace_irq(__e)	trace_event(TRACE_CLASS_IRQ, __e)
#define trace_irq_error(__e, ...) \
	trace_error(TRACE_CLASS_IRQ,  __e, ##__VA_ARGS__)

/**
 * \brief child IRQ descriptor for cascading IRQ controllers.
 */
struct irq_child {
	int enable_count[PLATFORM_CORE_COUNT];	/**< IRQ enable counter */
	struct list_item list;			/**< head for IRQ descriptors,
						  * sharing this interrupt
						  */
};

/**
 * \brief interrupt client descriptor
 */
struct irq_desc {
	int irq;			/**< virtual IRQ number */
	void (*handler)(void *arg);	/**< interrupt handler function */
	void *handler_arg;		/**< interrupt handler argument */
	uint32_t cpu_mask;		/**< a mask of CPUs on which this
					  * interrupt is enabled
					  */
	struct list_item irq_list;	/**< to link to other irq_desc */
};

/**
 * \brief cascading IRQ controller operations.
 */
struct irq_cascade_ops {
	void (*mask)(struct irq_desc *desc, uint32_t irq,
		     unsigned int cpu);				/**< mask */
	void (*unmask)(struct irq_desc *desc, uint32_t irq,
		       unsigned int cpu);			/**< unmask */
};

/**
 * \brief cascading interrupt controller descriptor.
 */
struct irq_cascade_desc {
	const char *name;				/**< name of the
							  * controller
							  */
	int irq_base;					/**< first virtual IRQ
							  * number, assigned to
							  * this controller
							  */
	const struct irq_cascade_ops *ops;		/**< cascading interrupt
							  * controller driver
							  * operations
							  */
	struct irq_desc desc;				/**< the interrupt, that
							  * this controller is
							  * generating
							  */
	struct irq_cascade_desc *next;			/**< link to the global
							  * list of interrupt
							  * controllers
							  */
	bool global_mask;				/**< the controller
							  * cannot mask input
							  * interrupts per core
							  */
	spinlock_t *lock;				/**< protect child
							  * lists, enable and
							  * child counters
							  */
	int enable_count[PLATFORM_CORE_COUNT];		/**< enabled child
							  * interrupt counter
							  */
	unsigned int num_children[PLATFORM_CORE_COUNT];	/**< number of children
							  */
	struct irq_child child[PLATFORM_IRQ_CHILDREN];	/**< array of child
							  * lists - one per
							  * multiplexed IRQ
							  */
};

/* A descriptor for cascading interrupt controller template */
struct irq_cascade_tmpl {
	const char *name;
	const struct irq_cascade_ops *ops;
	int irq;
	void (*handler)(void *arg);
	bool global_mask;
};

int interrupt_register(uint32_t irq, void(*handler)(void *arg), void *arg);
void interrupt_unregister(uint32_t irq, const void *arg);
uint32_t interrupt_enable(uint32_t irq, void *arg);
uint32_t interrupt_disable(uint32_t irq, void *arg);

void platform_interrupt_init(void);

void platform_interrupt_set(uint32_t irq);
void platform_interrupt_clear(uint32_t irq, uint32_t mask);
uint32_t platform_interrupt_get_enabled(void);
void interrupt_mask(uint32_t irq, unsigned int cpu);
void interrupt_unmask(uint32_t irq, unsigned int cpu);

/*
 * On platforms, supporting cascading interrupts cascaded interrupt numbers
 * are greater than or equal to PLATFORM_IRQ_HW_NUM
 */
#define interrupt_is_dsp_direct(irq) (!PLATFORM_IRQ_CHILDREN || \
					irq < PLATFORM_IRQ_HW_NUM)

void interrupt_init(void);
int interrupt_cascade_register(const struct irq_cascade_tmpl *tmpl);
struct irq_cascade_desc *interrupt_get_parent(uint32_t irq);
int interrupt_get_irq(unsigned int irq, const char *cascade);

static inline void interrupt_set(int irq)
{
	platform_interrupt_set(irq);
}

static inline void interrupt_clear_mask(int irq, uint32_t mask)
{
	platform_interrupt_clear(irq, mask);
}

static inline void interrupt_clear(int irq)
{
	interrupt_clear_mask(irq, 1);
}

static inline uint32_t interrupt_global_disable(void)
{
	return arch_interrupt_global_disable();
}

static inline void interrupt_global_enable(uint32_t flags)
{
	arch_interrupt_global_enable(flags);
}

/* disables all IRQ sources on current core */
#define irq_local_disable(flags) \
	(flags = interrupt_global_disable())

/* re-enables IRQ sources on current core */
#define irq_local_enable(flags) \
	interrupt_global_enable(flags)

#endif /* __SOF_DRIVERS_INTERRUPT_H__ */
