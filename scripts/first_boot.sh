#!/usr/bin/env bash
# =============================================================================
# scripts/first_boot.sh
# Запускается ОДИН РАЗ при первом старте нового устройства.
# Повторный запуск предотвращён через ConditionPathExists=!/data/first_boot_done.
#
# НЕ запускать на уже настроенной Pi — изменит hostname и SSH-ключи.
# На существующей Pi создать флаг вручную: touch /data/first_boot_done
# =============================================================================
set -euo pipefail

log() { logger -t indicator-firstboot "$*"; echo "[firstboot] $*"; }

log "First-boot provisioning started"

# ── 1. Уникальный hostname по серийнику SoC ───────────────────────────────────
# Серийник SoC уникален для каждого чипа; префикс -hd- — линейка Pi Zero 2W (В2-05)
SERIAL=$(grep Serial /proc/cpuinfo | awk '{print $3}' | tail -c 7)
HOSTNAME="indicator-hd-${SERIAL}"
hostnamectl set-hostname "$HOSTNAME"
# старое имя golden image из /etc/hosts убираем — иначе sudo резолвит не то имя
sed -i '/^127\.0\.1\.1[[:space:]]/d' /etc/hosts
echo "127.0.1.1 $HOSTNAME" >> /etc/hosts
log "Hostname: $HOSTNAME"

# ── 2. Свежие SSH host keys ───────────────────────────────────────────────────
rm -f /etc/ssh/ssh_host_*
dpkg-reconfigure -f noninteractive openssh-server
log "SSH host keys regenerated"

# ── 3. Уникальный machine-id ─────────────────────────────────────────────────
rm -f /etc/machine-id /var/lib/dbus/machine-id
systemd-machine-id-setup
[ -e /var/lib/dbus/machine-id ] || \
    ln -sf /etc/machine-id /var/lib/dbus/machine-id
log "machine-id regenerated"

# ── 3.5. WiFi seed: /boot/wpa_supplicant.conf → /data ─────────────────────────
# /data/wpa_supplicant.conf bind-монтирован в /etc/wpa_supplicant/ (setup_pi.sh),
# поэтому пишем через перенаправление — inode сохраняется, bind-mount видит
# новое содержимое. CR убираем: файл обычно готовят в Windows.
# raspberrypi-net-mods.service замаскирован setup_pi.sh — seed обрабатываем здесь.
WPA_SEED=/boot/wpa_supplicant.conf
WPA_DATA=/data/wpa_supplicant.conf
if [ -f "$WPA_SEED" ]; then
    tr -d '\r' < "$WPA_SEED" > "$WPA_DATA"
    chmod 600 "$WPA_DATA"
    rm -f "$WPA_SEED"
    WIFI_STATUS="seed из /boot применён"
else
    WIFI_STATUS="seed не найден — оставлен $WPA_DATA"
fi
log "WiFi: $WIFI_STATUS"

# ── 4. Включить overlayfs (rootfs станет read-only после перезагрузки) ────────
raspi-config nonint enable_overlayfs
log "overlayfs: enabled via raspi-config"

# ── 5. Флаг — больше не запускаться ──────────────────────────────────────────
touch /data/first_boot_done

# ── 6. Сводка ─────────────────────────────────────────────────────────────────
echo ""
echo "======================================================="
echo "  INDICATOR — первый старт завершён"
echo "======================================================="
printf "  Hostname    : %s\n" "$HOSTNAME"
echo "  SSH keys    : пересозданы"
echo "  machine-id  : пересоздан"
printf "  WiFi        : %s\n" "$WIFI_STATUS"
echo "  overlayfs   : ВКЛЮЧЁН (активируется после перезагрузки)"
echo ""
echo "  Перезагрузка через 5 секунд..."
echo "======================================================="
echo ""

log "First-boot provisioning complete: $HOSTNAME — rebooting to activate overlayfs"
sleep 5
systemctl reboot