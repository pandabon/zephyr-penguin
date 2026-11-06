/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal reproducer for the Zephyr fork's RVV vl/vtype context-switch bug
 * (patch zephyr-penguin patches/zephyr/0005; manual chapter 07). Smaller sibling of
* (the larger ctx_switch test was removed): one vector thread, one switch, one check.
 *
 * The bug: when an interrupt switches threads, the fork saves the
 * interrupted thread's vector state twice (isr.S on entry, then switch.S).
 * The first save's bulk store runs "vsetvli e8,m8", so the second save
 * records vl=VLMAX, vtype=e8,m8 as the thread's own settings.
 *
 * Each case: thread LO sets its own vl/vtype and waits; thread HI makes it
 * switch away, either by an interrupt (preempt) or voluntarily (control);
 * then LO checks its vl/vtype, or keeps running real vector code.
 *
 *   test_preempt_csr      timer IRQ wakes higher-priority HI -> preempts LO.
 *                         Stock fork: vl=VLMAX(e8m8), vtype=e8m8. Patched: OK.
 *   test_voluntary_csr    LO sleeps (k_msleep). One save only. Passes on both.
 *   test_preempt_compiled same preemption, but LO runs a GCC-intrinsics loop
 *                         whose vsetvli the compiler hoisted out of the loop.
 *                         Stock fork: the next vle32.v under e8,m8 is a
 *                         reserved encoding (EMUL=32) -> illegal instruction.
 *
 * vtype encoding (RISC-V V spec 1.0, section 3.4): vlmul[2:0], vsew[5:3],
 * vta[6], vma[7]. e32,m4,ta,ma = 0xd2; e8,m8,ta,ma = 0xc3.
 */
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>
#include <riscv_vector.h>

#define VON  ".option push\n .option arch, +v\n "
#define VOFF " .option pop\n"

#define AVL          5
#define VTYPE_E32M4  0xd2UL
#define VTYPE_E8M8   0xc3UL
#define STACKSZ      2048
#define N            8

static struct k_thread lo_thread, hi_thread;
static K_THREAD_STACK_DEFINE(lo_stack, STACKSZ);
static K_THREAD_STACK_DEFINE(hi_stack, STACKSZ);

static volatile bool hi_ran;
static volatile bool lo_faulted;
static unsigned long got_vl, got_vtype;
static int32_t a[N], b[N], dst[N];

static inline void set_e32m4(void)
{
	unsigned long vl;

	__asm__ volatile(VON "vsetvli %0, %1, e32, m4, ta, ma\n" VOFF : "=r"(vl) : "r"(AVL));
}

static inline void read_csrs(void)
{
	__asm__ volatile(VON "csrr %0, vl\n csrr %1, vtype\n" VOFF
			 : "=r"(got_vl), "=r"(got_vtype));
}

/* HI: does no vector work; it only exists so a switch happens. */
static void hi_entry(void *p1, void *p2, void *p3)
{
	hi_ran = true;
}

/* LO, CSR cases: set vl/vtype, wait for HI (by spinning or sleeping), read back. */
static void lo_csr_entry(void *voluntary, void *p2, void *p3)
{
	set_e32m4();
	if (voluntary) {
		k_msleep(5);
	} else {
		while (!hi_ran) {
			/* spin: only a timer interrupt can get HI in */
		}
	}
	read_csrs();
}

/* LO, compiled case: a fixed-vl intrinsics loop. GCC 14 emits one
 * "vsetivli zero,5,e32,m4" before the loop and none inside it. */
static void vec_loop(void)
{
	size_t vl = __riscv_vsetvl_e32m4(AVL);

	while (!hi_ran) {
		vint32m4_t x = __riscv_vle32_v_i32m4(a, vl);
		vint32m4_t y = __riscv_vle32_v_i32m4(b, vl);

		__riscv_vse32_v_i32m4(dst, __riscv_vadd_vv_i32m4(x, y, vl), vl);
	}
	/* one more pass after HI has run */
	__riscv_vse32_v_i32m4(dst, __riscv_vadd_vv_i32m4(__riscv_vle32_v_i32m4(a, vl),
							 __riscv_vle32_v_i32m4(b, vl), vl), vl);
}

static void lo_compiled_entry(void *p1, void *p2, void *p3)
{
	/* A fault here is what we test for: let ztest record it (the hook below)
	 * and abort only this thread. Fault validity is per calling thread. */
	ztest_set_fault_valid(true);
	vec_loop();
	ztest_set_fault_valid(false);
}

void ztest_post_fatal_error_hook(unsigned int reason, const struct arch_esf *esf)
{
	lo_faulted = true;
}

/* LO at priority 5 starts now; HI at priority 2 is released 5 ms later by the
 * kernel timer, i.e. from the timer interrupt. */
static void run(k_thread_entry_t lo_entry, void *arg)
{
	hi_ran = false;
	lo_faulted = false;
	got_vl = got_vtype = 0;
	k_thread_create(&lo_thread, lo_stack, STACKSZ, lo_entry, arg, NULL, NULL,
			K_PRIO_PREEMPT(5), K_FP_REGS, K_NO_WAIT);
	k_thread_create(&hi_thread, hi_stack, STACKSZ, hi_entry, NULL, NULL, NULL,
			K_PRIO_PREEMPT(2), K_FP_REGS, K_MSEC(5));
	zassert_ok(k_thread_join(&hi_thread, K_SECONDS(10)), "HI never ran");
	zassert_ok(k_thread_join(&lo_thread, K_SECONDS(10)), "LO hung");
}

ZTEST(rvv_ctx_switch_min, test_preempt_csr)
{
	run(lo_csr_entry, NULL);
	TC_PRINT("after preemption: vl=0x%lx vtype=0x%lx (expected 0x%x 0x%lx)\n",
		 got_vl, got_vtype, AVL, VTYPE_E32M4);
	zassert_equal(got_vtype, VTYPE_E32M4, "vtype clobbered%s",
		      got_vtype == VTYPE_E8M8 ? " to e8,m8 (bug 0005)" : "");
	zassert_equal(got_vl, AVL, "vl clobbered");
}

ZTEST(rvv_ctx_switch_min, test_voluntary_csr)
{
	run(lo_csr_entry, (void *)1);
	TC_PRINT("after k_msleep: vl=0x%lx vtype=0x%lx\n", got_vl, got_vtype);
	zassert_equal(got_vtype, VTYPE_E32M4, "vtype clobbered");
	zassert_equal(got_vl, AVL, "vl clobbered");
}

ZTEST(rvv_ctx_switch_min, test_preempt_compiled)
{
	for (int i = 0; i < N; i++) {
		a[i] = i;
		b[i] = 100 * i;
		dst[i] = -1;
	}
	run(lo_compiled_entry, NULL);
	zassert_false(lo_faulted, "vector loop faulted after preemption (bug 0005)");
	for (int i = 0; i < AVL; i++) {
		zassert_equal(dst[i], 101 * i, "dst[%d] = %d", i, dst[i]);
	}
	zassert_equal(dst[AVL], -1, "wrote past vl: dst[%d] = %d", AVL, dst[AVL]);
}

ZTEST_SUITE(rvv_ctx_switch_min, NULL, NULL, NULL, NULL, NULL);
