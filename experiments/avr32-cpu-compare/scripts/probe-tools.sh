#!/usr/bin/env bash

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

printf 'repo:             %s\n' "$REPO_ROOT"
printf 'experiment:       %s\n' "$EXP_DIR"
printf 'qemu-system-avr32:%s%s\n' "${QEMU_AVR32:+ }" "${QEMU_AVR32:-not found}"
printf 'avr32 gcc:        %s\n' "${AVR32_GCC:-not found}"
printf 'clang:            %s\n' "${AVR32_CLANG:-not found}"
printf 'clang triple:     %s\n' "$AVR32_LLVM_TRIPLE"
printf 'objcopy:          %s\n' "${AVR32_OBJCOPY:-not found}"
printf 'objdump:          %s\n' "${AVR32_OBJDUMP:-not found}"

if [[ -n "$AVR32_CLANG" ]] && clang_supports_avr32; then
    printf 'llvm avr32:       supported\n'
else
    printf 'llvm avr32:       not supported by selected clang\n'
fi

if [[ -x "$QEMU_AVR32" ]]; then
    printf 'qemu build:       present\n'
else
    printf 'qemu build:       missing; run scripts/build-qemu.sh\n'
fi
