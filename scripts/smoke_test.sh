#!/usr/bin/env bash
# =============================================================================
# scripts/smoke_test.sh — быстрая проверка после деплоя
# Запуск: just pi::test-smoke
#
# Версия: 2.0.0-hd (Pi Zero 2W, 1080×1920, hifiberry-dac)
# =============================================================================
set -euo pipefail

GREEN="\033[0;32m"; RED="\033[0;31m"; YELLOW="\033[1;33m"; BOLD="\033[1m"; RESET="\033[0m"
ERRORS=0

ok()   { echo -e "  ${GREEN}✅  ${1}${RESET}"; }
fail() { echo -e "  ${RED}❌  ${1}${RESET}"; ERRORS=$((ERRORS+1)); }
warn() { echo -e "  ${YELLOW}⚠️   ${1}${RESET}"; }

echo ""
echo -e "${BOLD}=== Lift Indicator HD — Smoke Test (Pi Zero 2W) ===${RESET}"
echo ""

# ─── 1. Сервисы ───────────────────────────────────────────────────────────────
echo "── 1. Сервисы ──────────────────────────────────────────"

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
check_service i2s-silence.service

echo ""

# ─── 2. Бинари ────────────────────────────────────────────────────────────────
echo "── 2. Бинари ───────────────────────────────────────────"
[ -f /home/pi/indicator/indicator    ] && ok "indicator binary"    || fail "indicator binary missing"
[ -f /home/pi/indicator/media_ingest ] && ok "media_ingest binary" || fail "media_ingest binary missing"
[ -f /home/pi/indicator/pi_nku_sync  ] && ok "pi_nku_sync"        || fail "pi_nku_sync missing"
[ -f /home/pi/indicator/pi_nku_menu  ] && ok "pi_nku_menu"        || fail "pi_nku_menu missing"

echo ""

# ─── 3. Конфиги (/data/pi_nku_configs) ───────────────────────────────────────
echo "── 3. Конфиги ──────────────────────────────────────────"
[ -f /data/pi_nku_configs/nku_scheme.toml ] && ok "nku_scheme.toml"  || fail "nku_scheme.toml missing"
[ -f /data/pi_nku_configs/renderer.toml   ] && ok "renderer.toml"    || fail "renderer.toml missing"
[ -f /data/pi_nku_configs/video.toml      ] && ok "video.toml"       || fail "video.toml missing"
[ -f /data/pi_nku_configs/pi_scheme.toml  ] && ok "pi_scheme.toml"   || fail "pi_scheme.toml missing"

echo ""

# ─── 4. Видеофайл ─────────────────────────────────────────────────────────────
echo "── 4. Видео ────────────────────────────────────────────"
if [ -f /data/videos/output.mp4 ]; then
    VIDEO_SIZE=$(stat -c%s /data/videos/output.mp4 2>/dev/null || echo 0)
    if [ "${VIDEO_SIZE}" -gt 0 ]; then
        ok "output.mp4 ($(( VIDEO_SIZE / 1024 / 1024 )) MB)"
    else
        fail "output.mp4 пустой (0 bytes)"
    fi
else
    fail "/data/videos/output.mp4 missing"
fi

echo ""

# ─── 5. Bind-монты активны ────────────────────────────────────────────────────
echo "── 5. Bind-монты ───────────────────────────────────────"
mountpoint -q /home/pi/indicator/pi_nku_configs && ok "pi_nku_configs bind mount" || fail "pi_nku_configs NOT mounted"
mountpoint -q /home/pi/indicator/resources      && ok "resources bind mount"      || fail "resources NOT mounted"
mountpoint -q /home/pi/indicator/sounds         && ok "sounds bind mount"         || fail "sounds NOT mounted"
mountpoint -q /home/pi/indicator/videos         && ok "videos bind mount"         || fail "videos NOT mounted"

echo ""

# ─── 6. UART порт ─────────────────────────────────────────────────────────────
echo "── 6. UART ─────────────────────────────────────────────"
[ -c /dev/ttyAMA0 ] && ok "/dev/ttyAMA0 available" || fail "/dev/ttyAMA0 not available"

echo ""

# ─── 7. Аудио ─────────────────────────────────────────────────────────────────
#
# Pi Zero 2W: carrier board использует dtoverlay=hifiberry-dac.
# Карта называется snd_rpi_hifiberry_dac (не sndrpigooglevoi).
# Проверяем три уровня:
#   1. Карта видна в ALSA (card 0 присутствует)
#   2. Softvol-контрол 'PCM' доступен
#   3. i2s-silence.service удерживает dmix открытым
echo "── 7. Аудио (hifiberry-dac) ────────────────────────────"

APLAY_OUTPUT=$(aplay -l 2>/dev/null || true)

# Проверка карты — поддерживаем оба overlay (hifiberry-dac и googlevoicehat)
if echo "$APLAY_OUTPUT" | grep -qi "hifiberry"; then
    CARD_NAME=$(echo "$APLAY_OUTPUT" | grep -i "hifiberry" | head -1 | sed 's/^[[:space:]]*//')
    ok "I2S card visible (hifiberry-dac): ${CARD_NAME}"
elif echo "$APLAY_OUTPUT" | grep -qi "googlevoice"; then
    ok "I2S card visible (googlevoicehat-soundcard)"
elif echo "$APLAY_OUTPUT" | grep -q "^card 0"; then
    CARD_NAME=$(echo "$APLAY_OUTPUT" | grep "^card 0" | head -1)
    warn "Audio card 0 present (unknown overlay): ${CARD_NAME}"
else
    fail "No audio card found (aplay -l empty)"
fi

# Проверка softvol PCM контрола
if amixer scontrols 2>/dev/null | grep -q "'PCM'"; then
    ok "ALSA 'PCM' control present"
else
    fail "ALSA 'PCM' control missing — amixer scontrols пустой"
fi

# Проверка что i2s-silence держит dmix открытым
if systemctl is-active --quiet i2s-silence.service 2>/dev/null; then
    ok "i2s-silence.service active (I2S clock keepalive)"
else
    fail "i2s-silence.service not active — возможны щелчки при воспроизведении"
fi

echo ""

# ─── 8. FIFO ─────────────────────────────────────────────────────────────────
echo "── 8. IPC ──────────────────────────────────────────────"
FIFO_PATH="/run/indicator/media_status.fifo"
if [ -p "${FIFO_PATH}" ]; then
    ok "media_status.fifo (FIFO) exists"
else
    fail "media_status.fifo missing — media-ingest IPC не работает"
fi

echo ""

# ─── 9. Лог indicator ────────────────────────────────────────────────────────
echo "── 9. Лог ──────────────────────────────────────────────"

# Проверить наличие записей за последние 5 минут (любые строки — не grep по тексту)
LOG_LINES=$(journalctl -u indicator --since "5 min ago" --no-pager -q 2>/dev/null | wc -l)
if [ "${LOG_LINES}" -gt 0 ]; then
    ok "Лог indicator: ${LOG_LINES} строк за 5 мин"
else
    warn "Нет свежих записей в логе за 5 мин (сервис только что запущен?)"
fi

# Критические ошибки за последние 5 минут
CRIT_COUNT=$(journalctl -u indicator --since "5 min ago" --no-pager -q 2>/dev/null \
    | grep -cE "\bCRIT\b|\bEMERG\b" || true)
if [ "${CRIT_COUNT}" -eq 0 ]; then
    ok "Нет CRIT/EMERG в логе за 5 мин"
else
    fail "CRIT/EMERG в логе: ${CRIT_COUNT} строк — проверить journalctl -u indicator"
fi

# Проверка omxplayer через watchdog-строку (появляется каждые 30 с)
if journalctl -u indicator --since "35 sec ago" --no-pager -q 2>/dev/null \
    | grep -q "watchdog:"; then
    ok "omxplayer watchdog: строка свежая (≤35 с)"
elif journalctl -u indicator --since "5 min ago" --no-pager -q 2>/dev/null \
    | grep -q "watchdog:"; then
    ok "omxplayer watchdog: строка есть (последние 5 мин)"
else
    warn "Нет watchdog-строки за 5 мин — нормально сразу после старта"
fi

echo ""

# ─── 10. Размер GPU-памяти ──────────────────────────────────────────────────
echo "── 10. Ресурсы ─────────────────────────────────────────"
if command -v vcgencmd &>/dev/null; then
    GPU_MEM=$(vcgencmd get_mem gpu 2>/dev/null | grep -oP '\d+' || echo "?")
    if [ "${GPU_MEM}" -ge 128 ] 2>/dev/null; then
        ok "gpu_mem=${GPU_MEM}MB (≥128 требуется для 1080p)"
    else
        fail "gpu_mem=${GPU_MEM}MB — требуется ≥128 для 1080p DispmanX"
    fi

    TEMP=$(vcgencmd measure_temp 2>/dev/null | grep -oP '[\d.]+' || echo "?")
    if echo "${TEMP}" | grep -qP '^\d'; then
        if (( $(echo "${TEMP} < 80" | bc -l 2>/dev/null || echo 1) )); then
            ok "CPU temp=${TEMP}°C (< 80°C — норма)"
        else
            warn "CPU temp=${TEMP}°C — высокая температура"
        fi
    fi
else
    warn "vcgencmd недоступен — пропуск проверки GPU/temp"
fi

echo ""

# ─── Итог ─────────────────────────────────────────────────────────────────────
if [ $ERRORS -eq 0 ]; then
    echo -e "  ${GREEN}${BOLD}✅  All checks passed.${RESET}"
else
    echo -e "  ${RED}${BOLD}❌  ${ERRORS} check(s) failed.${RESET}"
    exit 1
fi
echo ""