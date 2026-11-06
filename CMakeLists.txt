# SPDX-License-Identifier: Apache-2.0
#
# Module hook: Zephyr runs this file because zephyr/module.yml says `cmake: .`.
# It is intentionally almost empty; there are no out-of-tree drivers or libraries yet.
# If you add one, add it here, e.g. add_subdirectory_ifdef(CONFIG_FOO drivers/foo).

# Headers shared by apps and the port: <chipyard/sim_exit.h> and tc_util_user_override.h
# (Zephyr's test framework includes the latter by name).
zephyr_include_directories(include)
