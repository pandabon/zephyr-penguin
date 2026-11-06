/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Included by <zephyr/tc_util.h> when CONFIG_ZTEST_TC_UTIL_USER_OVERRIDE=y
 * (board Kconfig.defconfig turns it on whenever ZTEST is enabled).
 *
 * Stock ztest prints "PROJECT EXECUTION SUCCESSFUL/FAILED" and then idles
 * forever, which leaves a simulator running. Redefine TC_END_REPORT (same
 * body as upstream) so it also ends the simulation, exit code 0 on pass and
 * 1 on fail.
 */

#ifndef CHIPYARD_TC_UTIL_USER_OVERRIDE_H_
#define CHIPYARD_TC_UTIL_USER_OVERRIDE_H_

#include <chipyard/sim_exit.h>

#define TC_END_REPORT(result)                                              \
	do {                                                               \
		PRINT_LINE;                                                \
		TC_PRINT_RUNID;                                            \
		TC_END(result, "PROJECT EXECUTION %s\n",                   \
		       (result) == TC_PASS ? "SUCCESSFUL" : "FAILED");     \
		chipyard_sim_exit((result) == TC_PASS ? 0 : 1);            \
	} while (false)

#endif /* CHIPYARD_TC_UTIL_USER_OVERRIDE_H_ */
