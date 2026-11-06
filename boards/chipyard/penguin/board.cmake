# SPDX-License-Identifier: Apache-2.0
#
# `west build -t run` / twister run the ELF on Spike. The generic logic
# (ISA string from Kconfig, SPIKE override) lives in the module's cmake/.
#
# This board only PRODUCES binaries (and runs them on Spike); it has no west runner
# that loads them onto hardware. Loading lives in a separate loader, e.g. Chipyard's
# `uart_tsi` host tool. So that nobody has to
# hunt for the file, every build ends by printing the ELF path and an example load
# line. This is a CMake target in the default `all` build, so `west build`,
# `west build` shows it, with no code in west.
# The baud rate is the only per-board fact: the UART-TSI link of the chip's bring-up FPGA
# runs at 921600, the TA's VCU118 bitstream at 115200 (testchipip tsi/Configs.scala:8,
# stock WithUARTTSIClient). It is a plain CMake variable here, in the one place that already
# distinguishes the variants, rather than a Kconfig symbol, because nothing in the firmware
# uses it.
set(PENGUIN_ELF ${PROJECT_BINARY_DIR}/${KERNEL_ELF_NAME})

# Variant "sophialake_4g" is HYPOTHETICAL: no board with 4 GiB of DRAM exists (the real
# SophiaLake board has 1 GiB of DDR3L). Say so on every build, so that nobody loads
# the image onto today's hardware without noticing.
if(BOARD_QUALIFIERS STREQUAL "/penguin/sophialake_4g")
  set(PENGUIN_HYPOTHETICAL_4G TRUE)
  message(WARNING
    "chipyard_penguin/penguin/sophialake_4g is a HYPOTHETICAL board: it declares 4 GiB of "
    "DRAM, but no such board exists. The real SophiaLake board has 1 GiB of DDR3L. Use "
    "this variant on Spike only. On today's hardware, code or data above 0x8000_0000 + "
    "1 GiB (0xC000_0000) would alias or fault.")
endif()
if(CONFIG_CHIPYARD_FPGA_TA)
  set(PENGUIN_LOAD_BAUD 115200)
else()
  set(PENGUIN_LOAD_BAUD 921600)
endif()
if(PENGUIN_HYPOTHETICAL_4G)
  set(PENGUIN_LOAD_HINT "load: NONE. This is the HYPOTHETICAL 4 GiB variant (no such board exists; SophiaLake has 1 GiB). Run it on Spike (west build -t run)")
elseif(CONFIG_CHIPYARD_FPGA_EMULATION)
  set(PENGUIN_LOAD_HINT "load: this variant boots from an SD image or JTAG (not UART-TSI); see your loader's docs")
else()
  set(PENGUIN_LOAD_HINT "load on hardware (example): uart_tsi +tty=<tty> +baudrate=${PENGUIN_LOAD_BAUD} ${PENGUIN_ELF}")
endif()
add_custom_target(elf_load_hint ALL
  COMMAND ${CMAKE_COMMAND} -E echo "ELF: ${PENGUIN_ELF}"
  COMMAND ${CMAKE_COMMAND} -E echo "${PENGUIN_LOAD_HINT}"
  COMMENT "ELF location and hardware load hint"
  VERBATIM
  )
# Order it after the final link. That needs a target-level dependency (DEPENDS on a target
# name would only order files), and the ELF target does not exist yet while board.cmake is
# read, so add it at the end of the top-level CMake run.
cmake_language(DEFER CALL add_dependencies elf_load_hint ${logical_target_for_zephyr_elf})

if(CONFIG_CHIPYARD_FPGA_EMULATION)
  # Variant "vcu118": whole SoC on an FPGA. Spike has no SiFive UART model at
  # 0x6400_0000, so it cannot run this image; `run` explains instead of
  # silently printing nothing. Load via SD card or JTAG.
  set(SUPPORTED_EMU_PLATFORMS custom)
  add_custom_target(run_custom
    COMMAND ${CMAKE_COMMAND} -E echo "ERROR: board variant chipyard_penguin/penguin/vcu118 cannot run on Spike (no sifive,uart0 model at 0x64000000; console is the VCU118 UART). Build the base target chipyard_penguin/penguin to simulate, or load this image on the FPGA (SD card or JTAG; the loader is not part of this repo)."
    COMMAND ${CMAKE_COMMAND} -E false
    DEPENDS ${logical_target_for_zephyr_elf}
    USES_TERMINAL
    VERBATIM
    )
  return()
endif()

# Zicntr is NOT in the chip's riscv,isa (rdtime is not implemented in rocket-chip,
# see penguin.dtsi) and is kept out of Kconfig/-march (see soc Kconfig), but
# Spike needs it for apps that read the cycle counter, so tell Spike directly;
# otherwise apps that read the cycle counter trap as illegal on Spike.
#
# BF16: Zvfbfmin comes from Kconfig (soc/chipyard/penguin/Kconfig, it is
# accepted by GCC 14.3). Zfbfmin and Zvfbfa are in the chip's riscv,isa but GCC
# 14.3 rejects them in -march, so they are only told to Spike here. Zvfbfa
# gives vtype.altfmt (BF16 vector arithmetic); Spike needs Zfbfmin for it.
# Zvfofp8min (OFP8 conversions) is likewise compiler-less but in Penguin's riscv,isa
# (generator + the FPGA's generated DTS), so Spike gets it too.
set(SPIKE_ISA_EXTRA zicntr zfbfmin zvfbfa zvfofp8min)

if(PENGUIN_HYPOTHETICAL_4G)
  # Spike's default memory is 2 GiB at 0x8000_0000, smaller than this variant's ram0.
  # Give Spike exactly the declared RAM (-m<base>:<size>), taken from the devicetree
  # so the two cannot disagree. Spike allocates it lazily, so 4 GiB costs host memory
  # only for the pages the program touches. Appended to any SPIKE_EXTRA_ARGS you pass
  # (do not pass your own -m for this variant).
  dt_chosen(_sram_path PROPERTY "zephyr,sram")
  dt_reg_addr(_ram_base PATH ${_sram_path})
  dt_reg_size(_ram_size PATH ${_sram_path})
  math(EXPR _ram_base_hex "${_ram_base}" OUTPUT_FORMAT HEXADECIMAL)
  math(EXPR _ram_size_hex "${_ram_size}" OUTPUT_FORMAT HEXADECIMAL)
  string(APPEND SPIKE_EXTRA_ARGS " -m${_ram_base_hex}:${_ram_size_hex}")

  # Keep the linker off the reserved Atlas DMA area (node atlas_dram). ram0 contains it,
  # so the linker would happily place a big image or static array across it. Fail the
  # link instead. A workload whose image must exceed this limit (e.g. hundreds of MiB of
  # weights) cannot also use the fixed-address Atlas buffers; it opts out with
  # -DPENGUIN_ATLAS_GUARD=OFF on the west build command line.
  if(NOT DEFINED PENGUIN_ATLAS_GUARD)
    set(PENGUIN_ATLAS_GUARD ON)
  endif()
  dt_nodelabel(_atlas_path NODELABEL "atlas_dram")
  dt_reg_addr(_atlas_base PATH ${_atlas_path})
  math(EXPR _atlas_base_hex "${_atlas_base}" OUTPUT_FORMAT HEXADECIMAL)
  file(WRITE ${PROJECT_BINARY_DIR}/atlas_dram_guard.ld
    "ASSERT(_end <= ${_atlas_base_hex}, \"sophialake_4g: the image (RAM sections, incl. bss and the malloc arena) overlaps the reserved Atlas DMA area at ${_atlas_base_hex}; shrink it (e.g. CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE), or build with -DPENGUIN_ATLAS_GUARD=OFF if the app does not use the Atlas buffers\")\n")
  if(PENGUIN_ATLAS_GUARD)
    zephyr_linker_sources(SECTIONS ${PROJECT_BINARY_DIR}/atlas_dram_guard.ld)
  endif()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/spike.cmake)

