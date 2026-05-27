#!/usr/bin/env bash
# scripts/verify_glibc.sh
# Проверяет что бинарь требует glibc <= MAX_GLIBC (Buster = 2.28)

set -euo pipefail

BINARY="${1:-build/pi/indicator}"
MAX_GLIBC="2.28"

if [ ! -f "$BINARY" ]; then
    echo "❌  Binary not found: $BINARY"
    echo "    Run: just build::pi"
    exit 1
fi

echo "=== GLIBC requirements for $BINARY ==="
arm-linux-gnueabihf-objdump -p "$BINARY" | grep GLIBC || echo "(no GLIBC version tags)"

echo ""
echo "=== Checking max required GLIBC version ==="

VERSIONS=$(arm-linux-gnueabihf-objdump -p "$BINARY" \
    | grep -oP 'GLIBC_\K[0-9]+\.[0-9]+' \
    | sort -V | uniq)

if [ -z "$VERSIONS" ]; then
    echo "✅  No GLIBC version requirements (static or no glibc deps)"
    exit 0
fi

echo "$VERSIONS"
MAX_FOUND=$(echo "$VERSIONS" | sort -V | tail -1)

# Правильный порядок: [MAX_FOUND, MAX_GLIBC] должен быть отсортирован,
# то есть MAX_FOUND <= MAX_GLIBC.
# sort -V: "2.4" < "2.28" (4 < 28 числово), поэтому [2.4, 2.28] — отсортировано.
if printf '%s\n%s\n' "$MAX_FOUND" "$MAX_GLIBC" | sort -VC; then
    echo ""
    echo "✅  Max required: GLIBC_${MAX_FOUND} <= ${MAX_GLIBC} — OK for Buster Pi"
else
    echo ""
    echo "❌  Max required: GLIBC_${MAX_FOUND} > ${MAX_GLIBC} — will NOT run on Buster Pi!"
    exit 1
fi