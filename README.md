# zephyr-penguin

A Zephyr board support package for the **Penguin SoC** (UC Berkeley EE194 Sp26 tapeout:
Shuttle RV64 core, Saturn RVV vector unit with VLEN 256, Atlas NPU). It is an out-of-tree
Zephyr module (`penguin`) plus the west manifest that pins the
[ucb-bar Zephyr fork](https://github.com/ucb-bar/zephyr) at `852bb170cc5`.

One flow to build, run on Spike, test and debug Zephyr programs for the chip or its FPGA emulation: plain `west build`.
This repo produces the ELFs; loading them onto hardware is done by a separate loader (see "Hardware").

## Targets

| Board target | Runs on |
|---|---|
| `chipyard_penguin/penguin` | the chip (tethered through an FPGA, UART-TSI at 921600 baud) and Spike |
| `chipyard_penguin/penguin/vcu118_ta` | the TA's VCU118 build of the Penguin RTL (50 MHz, UART-TSI at 115200 baud) |
| `chipyard_penguin/penguin/vcu118` | generic Chipyard VCU118 flow (SD-card boot, UART console); cannot run on Spike |
| `chipyard_penguin/penguin/sophialake_4g` | **hypothetical, no such board exists** (SophiaLake has 1 GiB DDR3L): the base board with 4 GiB of DRAM, for Spike only |

## Repository layout

| Folder | Purpose |
|---|---|
| `boards/` | The Penguin board and its variants (`vcu118`, `vcu118_ta`, `sophialake_4g`): `board.cmake`, devicetree overlays, OpenOCD configs |
| `soc/` | The Penguin SoC: ISA selection (Kconfig), timer, HTIF exit |
| `dts/` | Devicetree include `penguin.dtsi` and bindings (`ucb-bar,shuttle`, `ucb-bar,atlas`) |
| `snippets/` | West snippets: `debug` (-Og, frame pointers, asserts) |
| `include/` | Headers shared by apps and the port (`chipyard/sim_exit.h`) |
| `cmake/` | Build helper `spike.cmake` (the board's `run` target) |
| `scripts/` | `run_tests.py` (test runner), the Spike debug runner, `doctor.sh` (machine check), `export_to_fork.sh`, `make_patched_zephyr.sh` |
| `samples/` | Example programs, each a normal Zephyr app: `hello_rvv`, `hw_probe`, `mt_rvv`, `timer` |
| `tests/` | Tests that need only the board: `arch/rvv/ctx_switch_min` (RVV context-switch reproducer) and `debug` (Spike debug chain). Larger workloads and the Atlas/Saturn tests live in a separate application repo |
| `patches/` | Proposed fixes for repos we do not own (not applied) |
| `zephyr/` | Only `module.yml`: tells Zephyr where this module's boards, SoC, DTS, snippets and runner are |
| `west.yml` | The west manifest (pins the Zephyr fork) |

## Workspace setup from scratch

The repo is the **west manifest repository**: clone it into an empty workspace directory and let west fetch
the rest (the Zephyr fork).

```sh
mkdir bringup && cd bringup
git clone https://github.com/pandabon/zephyr-penguin     # adjust the URL if the repo lives elsewhere
west init -l zephyr-penguin
west update                                                   # creates ./zephyr
```

Expected layout afterwards: `bringup/{zephyr-penguin, zephyr}`.

**Writing your own program?** Any Zephyr app folder (`CMakeLists.txt`, `prj.conf`, `src/`) builds with
`west build -b chipyard_penguin/penguin <app>`; the samples here are examples. Apps in a separate repo import this repo
through their own west manifest.

What else you need on your machine (this repo does not install anything):

- **Zephyr SDK 1.0.0-beta1.** It is the only version verified (GCC 14.3 / binutils 2.43; the ISA string in
  `soc/chipyard/penguin/Kconfig` relies on GCC 14.3 accepting `_zvfbfmin`). Download it from the
  [Zephyr SDK releases](https://github.com/zephyrproject-rtos/sdk-ng/releases) and unpack it anywhere.
- **Python 3 with west and the Zephyr requirements:** `pip install west pyyaml` and
  `pip install -r zephyr/scripts/requirements.txt` (a virtualenv is a good idea). `scripts/run_tests.py`
  needs `pyyaml`.
- **Spike** (optional: building never needs it; only running and the test suite's run step do) with the V extension at VLEN 256 plus `zicntr`, `zfh`, `zba/zbb/zbs`, `zvfh`, `zfbfmin`, `zvfbfa`
  (the exact `--isa` string is derived from Kconfig in `cmake/spike.cmake`; the extras Spike needs but the
  compiler does not are in `boards/chipyard/penguin/board.cmake`). Verified with
  [riscv-isa-sim](https://github.com/riscv-software-src/riscv-isa-sim) at commit `609dbe0b` (1.1.1-dev) or
  newer. Put `spike` on `PATH` or set `SPIKE=/path/to/spike`. Without Spike, `run` says "Spike not found" and
  `scripts/run_tests.py` only builds (scenarios are reported `BUILT`).
- **OpenOCD built with `--enable-remote-bitbang`**, only for `west debug` on Spike (the SDK's OpenOCD lacks it).
  Point `SPIKE_OPENOCD` at it.
- A **loader**, only for hardware (see "Hardware"), for example `uart_tsi`, the UART-TSI host tool from the testchipip/Chipyard
  build (in the course it comes from your bringup-chipyard environment).
- Optional, for the Atlas tests in a separate application repo: `TAPEOUT_CHIPYARD` (the course tapeout repo; no default location).

Run `scripts/doctor.sh` at any time: it lists what is found, what is missing and how to fix it.

A minimal `env.sh` (exported variables only; adjust the paths):

```sh
export ZEPHYR_SDK_INSTALL_DIR=$HOME/tools/zephyr-sdk-1.0.0-beta1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_BASE=$HOME/bringup/zephyr
export PATH=$HOME/tools/spike/bin:$ZEPHYR_SDK_INSTALL_DIR/gnu/riscv64-zephyr-elf/bin:$PATH
export SPIKE_OPENOCD=$HOME/tools/openocd-spike/bin/openocd        # only for `west debug`
export TAPEOUT_CHIPYARD=$HOME/tapeout-chipyard                    # optional (Atlas tests in an application repo)
# if Spike's shared libraries are not on the default path: export LD_LIBRARY_PATH=$HOME/tools/spike/lib
```

Activate your Python environment too, so `west` is on `PATH`.

## Quick start

Run from `zephyr-penguin/` with the environment set up.

```sh
west build -p auto -b chipyard_penguin/penguin -d ../build/hello_rvv samples/hello_rvv
west build -d ../build/hello_rvv -t run                   # run on Spike; the exit code is the program's
west debug -d ../build/hello_rvv                          # gdb on Spike through OpenOCD (build with -S debug for -Og)
scripts/run_tests.py -j 8                                 # the whole suite on Spike
```

Other targets: `-b chipyard_penguin/penguin/vcu118_ta` (TA's VCU118) or `.../vcu118`.
`scripts/doctor.sh` checks the tools.

**Environment variables.** Each dependency can be given by a variable and is still checked (`scripts/doctor.sh`):

| Variable | Meaning | Default |
|---|---|---|
| `ZEPHYR_SDK_INSTALL_DIR`, `ZEPHYR_TOOLCHAIN_VARIANT=zephyr` | Zephyr SDK (1.0.0-beta1 verified; `SDK_VERSION` tells doctor which to expect) | none |
| `ZEPHYR_BASE` | the Zephyr checkout | set by west |
| `WS`, `ZEPHYR_PENGUIN` | west workspace and this repo (`scripts/doctor.sh`; `BSP` still works, deprecated) | `west topdir`, this checkout |
| `SPIKE` (or `-DSPIKE`) | the `spike` binary; looked up at configure time, reconfigure after changing it | `spike` on `PATH` |
| `SPIKE_TIMEOUT` | seconds before a hung run is killed (read when `run` executes) | 120 |
| `SPIKE_EXTRA_ARGS` | extra Spike arguments before the ELF (for example a plugin) | none |
| `SPIKE_OPENOCD` | OpenOCD built with remote_bitbang, for `west debug` | `openocd` on `PATH` |
| `TAPEOUT_CHIPYARD` | course tapeout checkout, for the Atlas tests in an application repo | none (tests skipped) |

Underneath, west runs CMake and Ninja: `cmake -B build -GNinja -DBOARD=chipyard_penguin/penguin samples/hello_rvv` then
`ninja -C build` and `ninja -C build run` also works (the `run` target does not rebuild), but debugging needs `west` because it uses its runners.

**Testing.** `scripts/run_tests.py -f rvv` filters by name or tag; `scripts/run_tests.py --list` shows the scenarios.
It builds every sample and test and runs them on Spike. The one expected failure (`arch.riscv.rvv.ctx_switch_min`, see "Known issues")
is documented in `tests/known_failures.yaml` and shows as XFAIL. A separate application repo can hold larger hardware and kernel tests; run its suite with
`scripts/run_tests.py --root <that repo>`. Scenarios for the other board variants are SKIPped unless you pass `--board`. Without Spike,
runnable scenarios are built only and shown as `BUILT` (one notice, summary gets `BUILT=n`); `--build-only` asks for that on purpose.

**Hardware.** This repo builds ELFs and runs them on Spike; it has no loader for the chip. Every build ends with two lines
that say where the ELF is and how to load it, for example:

```
ELF: /path/to/build/hello_rvv/zephyr/zephyr.elf
load on hardware (example): uart_tsi +tty=<tty> +baudrate=921600 /path/to/build/hello_rvv/zephyr/zephyr.elf
```

`uart_tsi` is Chipyard's UART-TSI host tool (testchipip). It loads the ELF through the FPGA and then stays running as the HTIF
console proxy, so "load" means load, run and watch; nothing is persistent. `<tty>` is the serial port that carries
UART-TSI (for example `/dev/ttyUSB1`), not the JTAG (FT-LINK) port. The baud rate must match the bitstream:

| Board target | UART-TSI baud |
|---|---|
| `chipyard_penguin/penguin` (the chip, via its bring-up FPGA) | 921600 |
| `chipyard_penguin/penguin/vcu118_ta` (TA's VCU118) | 115200 |
| `chipyard_penguin/penguin/vcu118` | not UART-TSI: boots from an SD image or JTAG |

Other loaders work the same way on the ELF. Our team's bench tooling (bench bundles, link probes, a `west flash` runner)
is kept outside this repo and is not needed to build or test here.

## Known issues

- **RVV context switch (Zephyr fork bug).** A thread preempted by an interrupt has its `vl`/`vtype` overwritten by the
  vector-state save, which runs twice. Vector code in preemptible threads can therefore run with the wrong element width
  and length (register contents are fine). The fix is `patches/zephyr/0005-riscv-v-make-vstate-save-idempotent.patch`;
  it is **not applied**. The reproducer is `tests/arch/rvv/ctx_switch_min` here, an expected failure until the patch is
  applied. Lazy V switching is not implemented in the fork.
- **PLIC driver.** The Zephyr `sifive,plic-1.0.0` driver treats `riscv,ndev` as a count that includes source 0. The
  Penguin devicetree has the true value `ndev = 1`, so source 1 (the UART) would be rejected as spurious. The console is
  polled/HTIF, so nothing uses UART interrupts today; set `ndev = <2>` (harmless) or fix the driver before you do.
- **Nothing has been run on silicon yet.** The test suite is verified on Spike. On the TA's VCU118 bitstream (FPGA
  emulation of the Penguin RTL), the UART-TSI link works at 115200 baud, Zephyr boots with the HTIF console, and
  `hello_rvv` and the VLEN test pass; the rest of the bench set has not been run there yet. `boards/.../support/openocd-ftlink.cfg`
  (JTAG) is a candidate and untested.
- Spike does not model the 64 KiB scratchpad (`SCRATCHPAD` linker region) or Atlas.

## Patches

`patches/` holds fixes for code in repositories we do not own. They are **proposed, not applied**: `west update` does not
touch them. Apply one yourself, for example `git -C ../zephyr apply patches/zephyr/0005-*.patch`.

- `patches/zephyr/0004-boards-spike-fix-twister-platform-schema.patch`: the fork's Spike/Chipyard board YAML fails
  twister's platform schema, which aborts `west twister` for every board (this is why `scripts/run_tests.py` exists).
- `patches/zephyr/0005-riscv-v-make-vstate-save-idempotent.patch`: the context-switch bug above.

`scripts/make_patched_zephyr.sh` applies `patches/zephyr/*.patch` to a **copy** of `../zephyr` (used only for the
stock-versus-patched ctx_switch comparison for the bench).

## Credits

Builds on the [ucb-bar Zephyr fork](https://github.com/ucb-bar/zephyr) (RVV context switch, HTIF console),
[Chipyard](https://github.com/ucb-bar/chipyard) and testchipip (`uart_tsi`, UART-TSI), the
[Spike](https://github.com/riscv-software-src/riscv-isa-sim) ISA simulator, the course's
[Baremetal-IDE](https://github.com/ucb-ee194-tapeout/Baremetal-IDE) (front-end UX, the `checktsi` idea). The `timer` and `mt_rvv` samples
come from ucb-bar/zephyr-chipyard-sw (Dima Nikiforov). Per-file credits are in the source headers.

## License

Apache-2.0, see [LICENSE](LICENSE). Files derived from other projects keep their credit in the header.
