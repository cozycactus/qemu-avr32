#!/usr/bin/env bash

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

image="${1:-}"
log="${2:-$OUT_DIR/qemu.log}"
seconds="${QEMU_TRACE_SECONDS:-2}"

if [[ -z "$image" ]]; then
    printf 'usage: %s path/to/image.bin [qemu.log]\n' "$0" >&2
    exit 2
fi

require_executable qemu-system-avr32 "$QEMU_AVR32"
mkdir -p "$(dirname "$log")"

python3 - "$seconds" "$log" "$QEMU_AVR32" \
    -M avr32example-board \
    -bios "$image" \
    -display none \
    -monitor none \
    -serial none \
    -no-reboot \
    -singlestep \
    -d in_asm,cpu,exec \
    -D "$log" <<'PY'
import subprocess
import sys

timeout = float(sys.argv[1])
log = sys.argv[2]
cmd = sys.argv[3:]

try:
    completed = subprocess.run(
        cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )
    status = str(completed.returncode)
    stderr = completed.stderr
except subprocess.TimeoutExpired as exc:
    status = "timeout"
    stderr = exc.stderr or b""

with open(log + ".status", "w", encoding="utf-8") as f:
    f.write(status + "\n")

with open(log + ".stderr", "wb") as f:
    f.write(stderr)
PY

printf 'qemu trace: %s\n' "$log"
