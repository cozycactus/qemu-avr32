#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export EXP_DIR="$(cd "$script_dir/.." && pwd)"
export REPO_ROOT="$(cd "$EXP_DIR/../.." && pwd)"
export OUT_DIR="${OUT_DIR:-$EXP_DIR/out}"

first_tool() {
    local name
    for name in "$@"; do
        if command -v "$name" >/dev/null 2>&1; then
            command -v "$name"
            return 0
        fi
    done
    return 1
}

detect_jobs() {
    sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4
}

export QEMU_AVR32="${QEMU_AVR32:-$REPO_ROOT/build/avr32-softmmu/qemu-system-avr32}"
export AVR32_GCC="${AVR32_GCC:-$(first_tool avr32-gcc avr32-unknown-elf-gcc avr32-linux-gcc || true)}"
export AVR32_CLANG="${AVR32_CLANG:-$(first_tool clang || true)}"
export AVR32_LLVM_TRIPLE="${AVR32_LLVM_TRIPLE:-avr32-unknown-none}"
export AVR32_OBJCOPY="${AVR32_OBJCOPY:-$(first_tool avr32-objcopy avr32-unknown-elf-objcopy llvm-objcopy || true)}"
export AVR32_OBJDUMP="${AVR32_OBJDUMP:-$(first_tool avr32-objdump avr32-unknown-elf-objdump llvm-objdump || true)}"

require_executable() {
    local label="$1"
    local path="$2"
    if [[ -z "$path" || ! -x "$path" ]]; then
        printf 'missing %s: %s\n' "$label" "${path:-not found}" >&2
        return 1
    fi
}

clang_supports_avr32() {
    local tmp
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/avr32-clang-probe.XXXXXX")"
    printf 'void f(void) {}\n' > "$tmp/probe.c"
    if "$AVR32_CLANG" --target="$AVR32_LLVM_TRIPLE" -ffreestanding -c "$tmp/probe.c" -o "$tmp/probe.o" >/dev/null 2>&1; then
        rm -rf "$tmp"
        return 0
    fi
    rm -rf "$tmp"
    return 1
}

build_qemu_if_needed() {
    if [[ -x "$QEMU_AVR32" ]]; then
        return 0
    fi

    (cd "$REPO_ROOT" && PYTHON="${PYTHON:-/usr/bin/python3}" ./configure --target-list=avr32-softmmu --disable-werror)
    if command -v ninja >/dev/null 2>&1; then
        (cd "$REPO_ROOT" && ninja -C build qemu-system-avr32)
    else
        (cd "$REPO_ROOT" && make -j"$(detect_jobs)")
    fi
}
