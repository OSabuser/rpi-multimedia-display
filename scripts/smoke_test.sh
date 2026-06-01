#!/usr/bin/env bash
# =============================================================================
# scripts/smoke_test.sh — быстрая проверка после деплоя
# Запуск: just pi::smoke
# =============================================================================
set -euo pipefail

GREEN="\033[0;32m"; RED="\033[0;31m"; YELLOW="\033[1;33m"; BOLD="\033[1m"; RESET="\033[0m"
ERRORS=0

ok()   { echo -e "  ${GREEN}✅  ${1}${RESET}"; }
fail() { echo -e "  ${RED}❌  ${1}${RESET}"; ERRORS=$((ERRORS+1)); }
warn() { echo -e "  ${YELLOW}⚠️   ${1}${RESET}"; }

echo ""
echo -e "${BOLD}=== Lift Indicator — Smoke Test ===${RESET}"
echo ""

# ─── 1. Сервисы ───────────────────────────────────────────────────────────────
echo "  Сервисы..."
systemctl is-active --quiet indicator.service    && ok "indicator.service active"    || fail "indicator.service not active"
systemctl is-active --quiet media-ingest.service && ok "media-ingest.service active" || fail "media-ingest.service not active"

# ─── 2. Бинари ────────────────────────────────────────────────────────────────
echo "  Бинари..."
[ -f /home/pi/indicator/indicator    ] && ok "indicator binary"    || fail "indicator binary missing"
[ -f /home/pi/indicator/media_ingest ] && ok "media_ingest binary" || fail "media_ingest binary missing"
echo "  Rust утилиты..."
[ -f /home/pi/indicator/pi_nku_sync ] && ok "pi_nku_sync" || fail "pi_nku_sync missing"
[ -f /home/pi/indicator/pi_nku_menu ] && ok "pi_nku_menu" || fail "pi_nku_menu missing"

# ─── 3. Конфиги (/data/pi_nku_configs) ───────────────────────────────────────
echo "  Конфиги..."
[ -f /data/pi_nku_configs/nku_scheme.toml ] && ok "nku_scheme.toml"  || fail "nku_scheme.toml missing"
[ -f /data/pi_nku_configs/renderer.toml   ] && ok "renderer.toml"    || fail "renderer.toml missing"
[ -f /data/pi_nku_configs/video.toml      ] && ok "video.toml"       || fail "video.toml missing"

# ─── 4. Видеофайл ─────────────────────────────────────────────────────────────
echo "  Видео..."
[ -f /data/videos/output.mp4 ] && ok "output.mp4" || fail "/data/videos/output.mp4 missing"

# ─── 5. Bind-монты активны ────────────────────────────────────────────────────
echo "  Bind-монты..."
mountpoint -q /home/pi/indicator/pi_nku_configs && ok "pi_nku_configs bind mount" || fail "pi_nku_configs NOT mounted"
mountpoint -q /home/pi/indicator/resources      && ok "resources bind mount"      || fail "resources NOT mounted"
mountpoint -q /home/pi/indicator/sounds         && ok "sounds bind mount"         || fail "sounds NOT mounted"
mountpoint -q /home/pi/indicator/videos         && ok "videos bind mount"         || fail "videos NOT mounted"

# ─── 6. UART порт ─────────────────────────────────────────────────────────────
echo "  UART..."
[ -c /dev/ttyAMA0 ] && ok "/dev/ttyAMA0 available" || fail "/dev/ttyAMA0 not available"

# ─── 7. Аудио карта ───────────────────────────────────────────────────────────
echo "  Аудио..."
aplay -l 2>/dev/null | grep -q "sndrpihifiberry" \
    && ok "hifiberry DAC visible" \
    || fail "hifiberry DAC not found"

# ─── 8. Свежие записи в журнале ──────────────────────────────────────────────
echo "  Лог..."
if journalctl -u indicator --since "5 min ago" --no-pager -q 2>/dev/null | grep -q "indicator"; then
    ok "Log has recent entries"
else
    warn "No recent log entries (service may have just started)"
fi

# ─── Итог ─────────────────────────────────────────────────────────────────────
echo ""
if [ $ERRORS -eq 0 ]; then
    echo -e "  ${GREEN}${BOLD}All checks passed.${RESET}"
else
    echo -e "  ${RED}${BOLD}$ERRORS check(s) failed.${RESET}"
    exit 1
fi
echo ""