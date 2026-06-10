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

check_service() {
    local svc="$1"
    if systemctl is-active --quiet "${svc}"; then
        ok "${svc} active"
    else
        local state; state=$(systemctl is-active "${svc}" 2>/dev/null || echo "unknown")
        local enabled; enabled=$(systemctl is-enabled "${svc}" 2>/dev/null || echo "unknown")
        fail "${svc} not active (state=${state}, enabled=${enabled})"
    fi
}

check_service indicator.service
check_service media-ingest.service

# I2S keepalive: поддерживаем оба имени сервиса
# aplay.service     — устанавливается Adafruit i2samp.py (текущие устройства)
# i2s-silence.service — устанавливается setup_pi.sh (новые устройства)
if systemctl is-active --quiet i2s-silence.service 2>/dev/null; then
    ok "i2s-silence.service active (I2S keepalive)"
elif systemctl is-active --quiet aplay.service 2>/dev/null; then
    ok "aplay.service active (I2S keepalive — legacy Adafruit)"
else
    fail "I2S keepalive not running (ни i2s-silence.service, ни aplay.service)"
fi
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
[ -f /data/pi_nku_configs/pi_scheme.toml  ] && ok "pi_scheme.toml"   || fail "pi_scheme.toml missing"

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
#
# Карта создаётся overlay googlevoicehat-soundcard и называется sndrpigooglevoi.
# Проверяем два уровня:
#   1. Карта видна в ALSA (aplay -l)
#   2. Softvol-контрол 'PCM' доступен (amixer) — тест всего стека asound.conf
echo "  Аудио..."

APLAY_OUTPUT=$(aplay -l 2>/dev/null || true)
if echo "$APLAY_OUTPUT" | grep -qi "googlevoice"; then
    ok "I2S card visible (googlevoicehat-soundcard)"
elif echo "$APLAY_OUTPUT" | grep -q "^card 0"; then
    CARD_NAME=$(echo "$APLAY_OUTPUT" | grep "^card 0" | head -1)
    ok "Audio card 0 present: ${CARD_NAME}"
else
    fail "No audio card found (aplay -l empty)"
fi

if amixer scontrols 2>/dev/null | grep -q "'PCM'"; then
    ok "ALSA softvol 'PCM' control present"
else
    fail "ALSA softvol 'PCM' control missing (asound.conf may be wrong)"
fi

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