#!/usr/bin/env bash

set -euo pipefail

avr32_clang_was_set=${AVR32_CLANG+x}

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

mkdir -p "$OUT_DIR"

src="$EXP_DIR/src/bench.c"
ldscript="$EXP_DIR/linker.ld"

cflags=(
    -std=gnu99
    -O2
    -ffreestanding
    -fno-builtin
    -fno-stack-protector
    -fomit-frame-pointer
    -Wall
    -Wextra
    -Werror
    -c
    "$src"
)

linkflags=(
    -nostdlib
    -nostartfiles
    -Wl,-T,"$ldscript"
    -Wl,--build-id=none
)

split_words() {
    local value="$1"
    if [[ -n "$value" ]]; then
        # shellcheck disable=SC2206
        printf '%s\n' ${value}
    fi
}

build_with_gcc() {
    require_executable avr32-gcc "$AVR32_GCC"
    "$AVR32_GCC" "${cflags[@]}" $(split_words "${AVR32_GCC_CFLAGS:-}") -o "$OUT_DIR/gcc.o"
    "$AVR32_GCC" "${linkflags[@]}" -Wl,-Map,"$OUT_DIR/gcc.map" "$OUT_DIR/gcc.o" -o "$OUT_DIR/gcc.elf"
}

build_with_llvm() {
    require_executable clang "$AVR32_CLANG"
    if ! clang_supports_avr32; then
        printf 'selected clang does not support --target=%s\n' "$AVR32_LLVM_TRIPLE" >&2
        return 1
    fi

    "$AVR32_CLANG" \
        --target="$AVR32_LLVM_TRIPLE" \
        "${cflags[@]}" \
        $(split_words "${AVR32_CLANG_CFLAGS:-}") \
        -o "$OUT_DIR/llvm.o"

    "$AVR32_GCC" "${linkflags[@]}" -Wl,-Map,"$OUT_DIR/llvm.map" "$OUT_DIR/llvm.o" -o "$OUT_DIR/llvm.elf"
}

make_binary_and_disasm() {
    local name="$1"
    require_executable objcopy "$AVR32_OBJCOPY"
    "$AVR32_OBJCOPY" -O binary "$OUT_DIR/$name.elf" "$OUT_DIR/$name.bin"

    if [[ -n "$AVR32_OBJDUMP" && -x "$AVR32_OBJDUMP" ]]; then
        "$AVR32_OBJDUMP" -dr "$OUT_DIR/$name.elf" > "$OUT_DIR/$name.disasm" || true
    fi
}

report_result() {
    local name="$1"
    local bin="$OUT_DIR/$name.bin"
    local status_file="$OUT_DIR/$name.qemu.log.status"
    local stderr_file="$OUT_DIR/$name.qemu.log.stderr"

    python3 - "$name" "$bin" "$status_file" "$stderr_file" <<'PY'
import os
import re
import sys

name, bin_path, status_path, stderr_path = sys.argv[1:]

status = "missing"
if os.path.exists(status_path):
    with open(status_path, encoding="utf-8") as f:
        status = f.read().strip() or "missing"

stderr = ""
if os.path.exists(stderr_path):
    with open(stderr_path, encoding="utf-8", errors="replace") as f:
        stderr = f.read()

match = re.search(
    r"avr32exp-test-exit: status=(0x[0-9a-fA-F]+) result=(0x[0-9a-fA-F]+) expected=(0x[0-9a-fA-F]+)",
    stderr,
)

size = os.path.getsize(bin_path) if os.path.exists(bin_path) else 0

if not match:
    print(f"{name}: qemu_status={status} size={size}B result=unknown")
    sys.exit(0)

marker, result, expected = match.groups()
verdict = "PASS" if status == "0" and result.lower() == expected.lower() else "FAIL"
print(
    f"{name}: {verdict} qemu_status={status} size={size}B "
    f"result={result.lower()} expected={expected.lower()} marker={marker.lower()}"
)
PY
}

printf 'building GCC artifact...\n'
build_with_gcc
make_binary_and_disasm gcc

build_qemu_if_needed

llvm_ok=0
printf 'building LLVM artifact...\n'
if build_with_llvm; then
    llvm_ok=1
    make_binary_and_disasm llvm
else
    printf 'LLVM artifact was not built; see prerequisites and AVR32_CLANG settings.\n' >&2
    if [[ -n "$avr32_clang_was_set" ]]; then
        exit 1
    fi
fi

if [[ -f "$OUT_DIR/gcc.bin" ]]; then
    "$EXP_DIR/scripts/run-qemu.sh" "$OUT_DIR/gcc.bin" "$OUT_DIR/gcc.qemu.log"
    report_result gcc
fi

if [[ "$llvm_ok" -eq 1 && -f "$OUT_DIR/llvm.bin" ]]; then
    "$EXP_DIR/scripts/run-qemu.sh" "$OUT_DIR/llvm.bin" "$OUT_DIR/llvm.qemu.log"
    report_result llvm
fi

if [[ "$llvm_ok" -eq 1 && -f "$OUT_DIR/gcc.disasm" && -f "$OUT_DIR/llvm.disasm" ]]; then
    diff -u "$OUT_DIR/gcc.disasm" "$OUT_DIR/llvm.disasm" > "$OUT_DIR/disasm.diff" || true
    printf 'disassembly diff: %s\n' "$OUT_DIR/disasm.diff"
fi

printf 'outputs: %s\n' "$OUT_DIR"
