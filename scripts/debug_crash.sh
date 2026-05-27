#!/usr/bin/env bash
# scripts/debug_crash.sh
# Запускать на Pi: bash scripts/debug_crash.sh
# Даёт максимум информации о segfault без gdb.

set -euo pipefail
BIN="${1:-./indicator}"
echo "=== Binary: $BIN ==="

echo ""
echo "--- Linked libraries ---"
ldd "$BIN" 2>&1 || true

echo ""
echo "--- /dev/vchiq accessible (DispmanX требует) ---"
ls -la /dev/vchiq 2>&1 || echo "MISSING: /dev/vchiq"

echo ""
echo "--- VideoCore firmware ---"
vcgencmd version 2>&1 || echo "vcgencmd not available"

echo ""
echo "--- /dev/snd (ALSA) ---"
ls /dev/snd/ 2>&1 || echo "MISSING: /dev/snd"

echo ""
echo "--- strace (первые 60 syscall до crash) ---"
if command -v strace &>/dev/null; then
    strace -e trace=open,openat,mmap,mprotect,brk,access "$BIN" 2>&1 | head -80 || true
else
    echo "strace not installed. Run: sudo apt-get install -y strace"
fi

echo ""
echo "--- ulimit core ---"
ulimit -c unlimited
echo "core dumps enabled"
"$BIN" 2>&1 || true
if [ -f core ]; then
    echo "core dump written: ./core"
fi