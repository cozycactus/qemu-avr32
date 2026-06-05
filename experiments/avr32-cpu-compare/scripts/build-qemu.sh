#!/usr/bin/env bash

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

build_qemu_if_needed
printf 'qemu-system-avr32: %s\n' "$QEMU_AVR32"
