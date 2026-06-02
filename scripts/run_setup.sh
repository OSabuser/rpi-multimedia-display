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

log() { logger -t indicator-setup "$*"; echo "[setup] $*"; }

echo "pending" > "$STATUS_FILE"
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