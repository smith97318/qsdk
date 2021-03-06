/*
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <lib/spinlock.h>
#include <platform.h>
#include <plat_qti.h>
#include <bl31/bl31.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <qtiseclib_cb_interface.h>
#include <drivers/arm/gicv3.h>
#include <lib/el3_runtime/context_mgmt.h>
#include <context.h>
#include <arch.h>
#include <arch_helpers.h>

extern bool qti_is_overlap_atf_rg(unsigned long long addr, size_t size);
/*Adding it till 64 bit address support will be merged to arm tf*/
uintptr_t qti_page_align(uintptr_t value, unsigned dir)
{
	/* Round up the limit to the next page boundary */
	if (value & (PAGE_SIZE - 1)) {
		value &= ~((uintptr_t)PAGE_SIZE - 1);
		if (dir == UP)
			value += PAGE_SIZE;
	}

	return value;
}

void *qtiseclib_cb_memcpy(void *dst, const void *src, size_t len)
{
	return memcpy(dst, src, len);
}
bool qtiseclib_is_overlap_atf_rg(unsigned long long addr, size_t size)
{
	return qti_is_overlap_atf_rg(addr, size);
}
/* Log Level not used right now. Printing all log from QTISECLIB. */
void qtiseclib_cb_log(unsigned int loglvl, const char *fmt, ...)
{
	va_list argp;
	static spinlock_t qti_log_lock;

	uint32_t tick32_low,tick32_high;
	uint64_t uptime;
	uptime = read_cntpct_el0();

	tick32_low=uptime&0xFFFFFFFF;
	tick32_high=(uptime>>32)&0xFFFFFFFF;

	printf("[%x%08x]", tick32_high,tick32_low);

	va_start(argp, fmt);

	spin_lock(&qti_log_lock);
	printf("QTISECLIB ");
	vprintf(fmt, argp);
	putchar('\n');
	spin_unlock(&qti_log_lock);

	va_end(argp);
}

void qtiseclib_cb_spin_lock(qtiseclib_cb_spinlock_t * lock)
{
	spin_lock((spinlock_t *) lock);
}

void qtiseclib_cb_spin_unlock(qtiseclib_cb_spinlock_t * lock)
{
	spin_unlock((spinlock_t *) lock);
}

unsigned int qtiseclib_cb_plat_my_core_pos(void)
{
	return plat_my_core_pos();
}

int qtiseclib_cb_plat_core_pos_by_mpidr(u_register_t mpidr)
{
	return plat_core_pos_by_mpidr(mpidr);
}

void qtiseclib_cb_set_interrupt_pending(unsigned int irq)
{
	plat_ic_set_interrupt_pending(irq);
}

unsigned int qtiseclib_cb_plat_my_cluster_pos(void)
{
	return plat_qti_my_cluster_pos();
}

void qtiseclib_cb_clear_interrupt_pending(unsigned int id)
{
	plat_ic_clear_interrupt_pending(id);
}

uintptr_t qtiseclib_cb_get_warmboot_entry_addr(void)
{
	return (uintptr_t) bl31_warm_entrypoint;
}

uintptr_t qtiseclib_cb_get_entry_addr(void)
{
    return (uintptr_t) bl31_entrypoint;
}

int qtiseclib_cb_mmap_add_dynamic_region(unsigned long long base_pa,
					 uintptr_t base_va, size_t size,
					 qtiseclib_mmap_attr_t attr)
{
	unsigned int l_attr = 0;

	if (QTISECLIB_MAP_NS_RO_XN_DATA == attr) {
		l_attr = MT_NS | MT_RO | MT_EXECUTE_NEVER;
	} else if (QTISECLIB_MAP_RW_XN_NC_DATA == attr) {
		l_attr = MT_RW | MT_NON_CACHEABLE | MT_EXECUTE_NEVER;
	} else if (QTISECLIB_MAP_RW_XN_DATA == attr) {
		l_attr = MT_RW | MT_EXECUTE_NEVER;
	}
	return qti_mmap_add_dynamic_region(base_pa, base_va,
				       size, l_attr);
}

void qtiseclib_cb_inv_dcache_range(uintptr_t addr, size_t size)
{
	uintptr_t addr_align = qti_page_align(addr, DOWN);
	size_t size_aign = qti_page_align(size, UP);

	inv_dcache_range(addr_align, size_aign);
}

void qtiseclib_cb_flush_dcache_range(uintptr_t addr, size_t size)
{
	uintptr_t addr_align = qti_page_align(addr, DOWN);
	size_t size_aign = qti_page_align(size, UP);

	flush_dcache_range(addr_align, size_aign);
}

void qtiseclib_cb_flush_dcache_all(void)
{
	dcsw_op_all(DCCISW);
}

void qtiseclib_cb_tlbialle3(void)
{
	tlbialle3();
}

int qtiseclib_cb_mmap_remove_dynamic_region(uintptr_t base_va, size_t size)
{
	return qti_mmap_remove_dynamic_region(base_va, size);
}

/* GIC platform functions */
void qtiseclib_cb_gic_pcpu_init(void)
{
	plat_qti_gic_pcpu_init();
}

void qtiseclib_cb_gic_cpuif_enable(void)
{
	plat_qti_gic_cpuif_enable();
}

void qtiseclib_cb_gic_cpuif_disable(void)
{
	plat_qti_gic_cpuif_disable();
}

void qtiseclib_cb_ic_raise_sgi(int sgi_num, u_register_t target)
{
	plat_ic_raise_el3_sgi(sgi_num, target);
}

void qtiseclib_cb_get_ns_ctx(qtiseclib_dbg_a64_ctxt_regs_type *qti_ns_ctx)
{
	void *ctx;
	ctx = cm_get_context(NON_SECURE);

	qti_ns_ctx->spsr_el3 = read_ctx_reg(get_el3state_ctx(ctx), CTX_SPSR_EL3);
	qti_ns_ctx->elr_el3  = read_ctx_reg(get_el3state_ctx(ctx), CTX_ELR_EL3);

	qti_ns_ctx->spsr_el1 = read_ctx_reg(get_sysregs_ctx(ctx), CTX_SPSR_EL1);
	qti_ns_ctx->elr_el1 = read_ctx_reg(get_sysregs_ctx(ctx), CTX_ELR_EL1);
	qti_ns_ctx->sp_el1 = read_ctx_reg(get_sysregs_ctx(ctx), CTX_SP_EL1);

	qti_ns_ctx->x0 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X0);
	qti_ns_ctx->x1 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X1);
	qti_ns_ctx->x2 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X2);
	qti_ns_ctx->x3 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X3);
	qti_ns_ctx->x4 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X4);
	qti_ns_ctx->x5 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X5);
	qti_ns_ctx->x6 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X6);
	qti_ns_ctx->x7 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X7);
	qti_ns_ctx->x8 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X8);
	qti_ns_ctx->x9 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X9);
	qti_ns_ctx->x10 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X10);
	qti_ns_ctx->x11 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X11);
	qti_ns_ctx->x12 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X12);
	qti_ns_ctx->x13 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X13);
	qti_ns_ctx->x14 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X14);
	qti_ns_ctx->x15 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X15);
	qti_ns_ctx->x16 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X16);
	qti_ns_ctx->x17 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X17);
	qti_ns_ctx->x18 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X18);
	qti_ns_ctx->x19 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X19);
	qti_ns_ctx->x20 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X20);
	qti_ns_ctx->x21 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X21);
	qti_ns_ctx->x22 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X22);
	qti_ns_ctx->x23 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X23);
	qti_ns_ctx->x24 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X24);
	qti_ns_ctx->x25 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X25);
	qti_ns_ctx->x26 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X26);
	qti_ns_ctx->x27 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X27);
	qti_ns_ctx->x28 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X28);
	qti_ns_ctx->x29 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_X29);
	qti_ns_ctx->x30 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_LR);
	qti_ns_ctx->sp_el0 = read_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_SP_EL0);
}

void qtiseclib_cb_set_spi_routing(unsigned int id, unsigned int irm, u_register_t target)
{
	assert(QTI_GICV3_IRM_PE == GICV3_IRM_PE);
	assert(QTI_GICV3_IRM_ANY == GICV3_IRM_ANY);
	gic_set_spi_routing(id, irm, target);
}

/* Crash reporting api's wrappers */
int qtiseclib_cb_crash_console_init(void)
{
	return plat_crash_console_init();
}
int qtiseclib_cb_crash_console_flush(void)
{
	return plat_crash_console_flush();
}
