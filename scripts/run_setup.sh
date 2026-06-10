#!/usr/bin/env bash
# =============================================================================
# scripts/run_setup.sh
# Оркестрация MCU-синхронизации при старте:
#   1. pull  — прочитать параметры из MCU
#   2. menu  — дать пользователю изменить их (30s timeout, физический экран)
#   3. push  — записать параметры в MCU и запустить стриминг
#
# Вызывается из indicator-setup.service (TTYPath=/dev/tty1).
# ВСЕГДА завершается с кодом 0 — indicator должен стартовать в любом случае.
# Результат записывается в /data/setup_status.
# =============================================================================
set -uo pipefail

IND="/home/pi/indicator"
STATUS_FILE="/data/setup_status"
BOOT_DELAY=3


log() { logger -t indicator-setup "$*"; echo "[setup] $*"; }

echo "pending" > "$STATUS_FILE"

# ── 0. Boot splash (3 секунды пока MCU инициализируется) ─────────────────────
if [ -f "$IND/splash/splash.jpg" ] && command -v fbi &>/dev/null; then
    sudo fbi -T 2 --noverbose "$IND/splash/splash.jpg" &
    FBI_PID=$!
    sleep 5
    sudo kill "$FBI_PID" 2>/dev/null || true
    wait "$FBI_PID" 2>/dev/null || true
fi

# ── 0.5. Статус устройства (до pull/push) ────────────────────────────────────
CURRENT_HOSTNAME=$(hostname)
FIRMWARE_VERSION=$("$IND/indicator" --version 2>/dev/null || echo "unknown")
if grep -qs ' / overlay ' /proc/mounts; then
    OVERLAY_STATUS="ВКЛЮЧЁН"
else
    OVERLAY_STATUS="ВЫКЛЮЧЕН"
fi
log "hostname: $CURRENT_HOSTNAME | firmware: $FIRMWARE_VERSION | overlayfs: $OVERLAY_STATUS"
chvt 1
printf '\033[2J\033[H' > /dev/tty1
echo "" > /dev/tty1
printf "  Hostname    : %s\n" "$CURRENT_HOSTNAME" > /dev/tty1
printf "  Firmware    : %s\n" "$FIRMWARE_VERSION" > /dev/tty1
printf "  overlayfs   : %s\n" "$OVERLAY_STATUS" > /dev/tty1
echo "" > /dev/tty1
sleep 5

log "MCU sync started (pull → menu → push)"
# ── 1. Pull ───────────────────────────────────────────────────────────────────
log "Step 1/3: pulling parameters from MCU..."
if ! "$IND/pi_nku_sync" -m pull; then
    log "ERROR: pull failed — MCU not responding"
    echo "pull_failed" > "$STATUS_FILE"
    exit 0
fi
log "Pull OK"

# ── 2. TUI-меню (30s timeout, продолжить автоматически) ───────────────────────
log "Step 2/3: config menu (30s, interact on display or wait)..."

# Очистить tty1 от загрузочных сообщений перед запуском TUI
printf '\033[2J\033[H' > /dev/tty1   # ESC[2J = clear screen, ESC[H = cursor home
chvt 1                                # переключить VT на tty1 (на случай если не там)

timeout --foreground 30s "$IND/pi_nku_menu" || true
log "Menu done"

# ── 3. Push ───────────────────────────────────────────────────────────────────
log "Step 3/3: pushing parameters to MCU..."
if ! "$IND/pi_nku_sync" -m push; then
    log "ERROR: push failed — MCU streaming not started"
    echo "push_failed" > "$STATUS_FILE"
    exit 0
fi

echo "ok" > "$STATUS_FILE"
log "MCU sync complete"

exit 0