# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rvv_ctx_switch_min)

target_sources(app PRIVATE src/main.c)
# Keep the compiler from auto-vectorizing the checking code; only the
# intrinsics in vec_loop() are meant to emit vector instructions.
target_compile_options(app PRIVATE -fno-tree-vectorize)
