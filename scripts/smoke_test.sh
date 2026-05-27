#!/usr/bin/env bash
# =============================================================================
# scripts/smoke_test.sh — быстрая проверка после деплоя
# Запуск: just pi::smoke
# =============================================================================
set -euo pipefail

GREEN="\033[0;32m"; RED="\033[0;31m"; BOLD="\033[1m"; RESET="\033[0m"
ERRORS=0

ok()   { echo -e "  ${GREEN}✅ ${1}${RESET}"; }
fail() { echo -e "  ${RED}❌ ${1}${RESET}"; ERRORS=$((ERRORS+1)); }

echo ""
echo -e "${BOLD}=== Lift Indicator — Smoke Test ===${RESET}"
echo ""

# 1. Сервисы запущены
echo "  Проверяем сервисы..."
systemctl is-active indicator.service    &>/dev/null && ok "indicator.service active"    || fail "indicator.service not active"
systemctl is-active media-ingest.service &>/dev/null && ok "media-ingest.service active" || fail "media-ingest.service not active"

# 2. Бинари существуют
echo "  Проверяем бинари..."
[ -f /home/pi/indicator/indicator    ] && ok "indicator binary"    || fail "indicator binary missing"
[ -f /home/pi/indicator/media_ingest ] && ok "media_ingest binary" || fail "media_ingest binary missing"

# 3. Конфиг существует
echo "  Проверяем конфиг..."
[ -f /home/pi/indicator/configs/device/pizero.ini ] && ok "pizero.ini" || fail "pizero.ini missing"

# 4. Видеофайл существует
echo "  Проверяем видео..."
[ -f /home/pi/indicator/videos/output.mp4 ] && ok "output.mp4" || fail "videos/output.mp4 missing"

# 5. UART порт доступен
echo "  Проверяем UART..."
[ -c /dev/ttyAMA0 ] && ok "/dev/ttyAMA0 available" || fail "/dev/ttyAMA0 not available"

# 6. Аудио карта видна
echo "  Проверяем аудио..."
aplay -l 2>/dev/null | grep -q "sndrpihifiberry" && ok "hifiberry DAC visible" || fail "hifiberry DAC not found"

# 7. Последние события в логе
echo "  Проверяем лог..."
if journalctl -u indicator --since "5 min ago" --no-pager -q 2>/dev/null | grep -q "indicator"; then
    ok "Log has recent entries"
else
    warn() { echo -e "  ${YELLOW}⚠️  ${1}${RESET}"; }
    echo "  ⚠️  No recent log entries (service may have just started)"
fi

# ─── Итог ────────────────────────────────────────────────────────────────────

echo ""
if [ $ERRORS -eq 0 ]; then
    echo -e "  ${GREEN}${BOLD}All checks passed.${RESET}"
else
    echo -e "  ${RED}${BOLD}$ERRORS check(s) failed.${RESET}"
    exit 1
fi
echo ""
