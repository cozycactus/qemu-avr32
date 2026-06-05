# AVR32 CPU Compare

This directory is a small CPU-only harness for comparing AVR32 code generated
by GCC and LLVM-style toolchains under this QEMU fork.

It intentionally avoids UC3A peripherals, interrupts, libc, and startup files.
The default benchmark writes result words into SRAM and to a QEMU-only test
exit MMIO register at `0xfffff000`.

## Prerequisites

You need:

- this QEMU tree built for `avr32-softmmu`
- an AVR32 GCC toolchain, for example `avr32-gcc` or `avr32-unknown-elf-gcc`
- an LLVM/Clang build with an AVR32 backend, if you want LLVM output
- AVR32 binutils, for example `avr32-objcopy` and `avr32-objdump`

The stock Apple `/usr/bin/clang` does not provide an AVR32 backend.

## Build QEMU

From the repository root:

```sh
PYTHON=/usr/bin/python3 ./configure \
  --target-list=avr32-softmmu \
  --disable-werror \
  --cross-prefix-avr32=/Users/ruslanmigirov/avr32-tools-src/bin/avr32-
ninja -C build qemu-system-avr32
```

That configure command also enables the upstream-style AVR32 TCG smoke test in
`tests/tcg/avr32/`. Run it with:

```sh
make run-tcg-tests-avr32-softmmu
```

## Probe Tools

```sh
experiments/avr32-cpu-compare/scripts/probe-tools.sh
```

Tool locations can be overridden with environment variables:

```sh
AVR32_GCC=/path/to/avr32-gcc \
AVR32_CLANG=/path/to/clang-with-avr32 \
AVR32_LLVM_TRIPLE=avr32-unknown-none \
experiments/avr32-cpu-compare/scripts/probe-tools.sh
```

## Compare

```sh
experiments/avr32-cpu-compare/scripts/compare.sh
```

For this local machine, the known-good tool paths are:

```sh
AVR32_GCC=/Users/ruslanmigirov/avr32-tools-src/bin/avr32-gcc \
AVR32_OBJCOPY=/Users/ruslanmigirov/avr32-tools-src/bin/avr32-objcopy \
AVR32_OBJDUMP=/Users/ruslanmigirov/avr32-tools-src/bin/avr32-objdump \
AVR32_CLANG=/tmp/llvm-avr32-clang-build/bin/clang-23 \
AVR32_LLVM_TRIPLE=avr32-unknown-none \
experiments/avr32-cpu-compare/scripts/compare.sh
```

Outputs are written to `experiments/avr32-cpu-compare/out/`:

- `gcc.elf`, `gcc.bin`, `gcc.disasm`, `gcc.qemu.log`
- `llvm.elf`, `llvm.bin`, `llvm.disasm`, `llvm.qemu.log`
- `disasm.diff`

If LLVM cannot target AVR32, the script still builds the GCC artifact and
reports the LLVM failure clearly.

The script prints a one-line summary for each artifact:

```text
gcc: PASS qemu_status=0 size=...B result=0x... expected=0x... marker=0x...
llvm: PASS qemu_status=0 size=...B result=0x... expected=0x... marker=0x...
```

## Editing The Test

Change `src/bench.c` for the workload. Keep the default shape if you want a
pure CPU/codegen test:

- no libc calls
- no peripheral MMIO
- no initialized global data
- no normal `return` from `_start`

For stack-heavy tests, add startup code that initializes `sp` to `__stack_top`
from `linker.ld`, or use the board's default SRAM-top stack pointer.
