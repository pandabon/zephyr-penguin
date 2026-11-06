/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/printk.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/arch/riscv/csr.h>
#if defined(CONFIG_CHIPYARD_SIM_EXIT_HTIF)
#include <zephyr/drivers/htif.h>
#endif
#include <chipyard/sim_exit.h>

#define CSR_VLENB 0xc22

/*
 * The mtime rate is stated twice: Kconfig (SYS_CLOCK_HW_CYCLES_PER_SEC, soc
 * Kconfig.defconfig, which the timer driver and tick math use) and the DT
 * /cpus timebase-frequency (which is what the real Chipyard DTS reports and
 * what the DT-driven code sees). Nothing else ties them together, so a board
 * that edits one and forgets the other would silently skew every timeout.
 */
BUILD_ASSERT(DT_PROP(DT_PATH(cpus), timebase_frequency) ==
	     CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC,
	     "DT /cpus timebase-frequency != CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC");

/*
 * thread.h sizes each thread's vector save area from
 * CONFIG_RISCV_VECTOR_MAX_LEN; if the hardware VLEN is larger, context
 * switches silently overrun it. Catch that at boot instead.
 */
static int penguin_check_vlen(void)
{
	unsigned long vs = csr_read(mstatus) & MSTATUS_VS;
	unsigned long vlenb;

	csr_set(mstatus, MSTATUS_VS_INIT);
	vlenb = csr_read(CSR_VLENB);
	csr_clear(mstatus, MSTATUS_VS);
	csr_set(mstatus, vs);

	if (vlenb * 8 > CONFIG_RISCV_VECTOR_MAX_LEN) {
		printk("penguin: VLEN %lu > CONFIG_RISCV_VECTOR_MAX_LEN %d\n",
		       vlenb * 8, CONFIG_RISCV_VECTOR_MAX_LEN);
		k_panic();
	}

	return 0;
}

SYS_INIT(penguin_check_vlen, PRE_KERNEL_2, 0);

/*
 * HTIF exit: writing (code << 1) | 1 to tohost makes the host side (Spike's
 * FESVR, or the Chipyard testharness) terminate with exit status `code`.
 *
 * No mutex here: this also runs from fatal-error context (possibly an ISR),
 * where k_mutex_lock() is not allowed. Locking interrupts is enough on one
 * hart, and each tohost write is a complete HTIF command.
 */
FUNC_NORETURN void chipyard_sim_exit(int code)
{
	(void)arch_irq_lock();
#if defined(CONFIG_CHIPYARD_SIM_EXIT_HTIF)
	while (tohost != 0) {
		if (fromhost != 0) {
			fromhost = 0;
		}
	}
	tohost = ((uint64_t)code << 1) | 1;
	while (1) {
	}
#else
	/* No HTIF host (FPGA): tell the human on the UART, then park the hart. */
	printk("\n*** EXIT %d ***\n", code);
	while (1) {
		__asm__ volatile("wfi");
	}
#endif
}

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	chipyard_sim_exit(0);
}

/*
 * Called by the kernel after a fatal error (k_sys_fatal_error_handler() has
 * returned). The default spins forever, which hangs the simulator; exit
 * non-zero instead so `west build -t run` and twister see the failure.
 */
FUNC_NORETURN void arch_system_halt(unsigned int reason)
{
	ARG_UNUSED(reason);

	chipyard_sim_exit(1);
}
