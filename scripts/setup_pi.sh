#!/usr/bin/env bash
# =============================================================================
# scripts/setup_pi.sh — первичная настройка Raspberry Pi Zero 2W
# Запускать: just pi::setup-pi (через SSH с sudo)
#
# Выполняется ОДИН РАЗ на свежем Raspbian Buster Lite.
# =============================================================================
set -euo pipefail

BOLD="\033[1m"; GREEN="\033[0;32m"; YELLOW="\033[1;33m"; RESET="\033[0m"

step() { echo -e "\n${BOLD}>>> ${*}${RESET}"; }
ok()   { echo -e "  ${GREEN}✅ ${*}${RESET}"; }
warn() { echo -e "  ${YELLOW}⚠️  ${*}${RESET}"; }

step "Обновление системы"
fix_buster_apt_sources() {
    echo "[setup] Fixing Raspbian Buster archived repositories..."

    # Основной Debian Buster → архив
    sudo tee /etc/apt/sources.list > /dev/null <<'EOF'
deb http://archive.debian.org/debian buster main contrib non-free
deb http://archive.debian.org/debian-security buster/updates main contrib non-free
EOF

    # Raspbian → архив
    sudo tee /etc/apt/sources.list.d/raspi.list > /dev/null <<'EOF'
deb http://archive.raspberrypi.org/debian/ buster main
EOF

    # Отключить проверку дат (архив — старые Release-файлы)
    sudo tee /etc/apt/apt.conf.d/99archive > /dev/null <<'EOF'
Acquire::Check-Valid-Until "false";
EOF

    echo "[setup] Sources fixed."
}

fix_buster_apt_sources

apt-get update -q
apt-get full-upgrade -y -q
ok "System updated"

step "Установка пакетов"
apt-get install -y -q \
    omxplayer \
    ffmpeg \
    alsa-utils \
    libpng16-16 \
    exfat-utils \
    exfat-fuse \
    git
ok "Packages installed"

step "Отключение лишних сервисов"
systemctl disable --now hciuart bluetooth               2>/dev/null || true
systemctl disable --now serial-getty@ttyAMA0.service    2>/dev/null || true
systemctl disable --now triggerhappy.service            2>/dev/null || true
ok "Unnecessary services disabled"

step "Настройка ALSA softvol для MAX98357"
cat > /etc/asound.conf << 'EOF'
# Программный регулятор громкости для MAX98357 (hifiberry-dac)
# MAX98357 не имеет аппаратного volume control в ALSA
pcm.!default {
    type            softvol
    slave.pcm       "plughw:0,0"
    control.name    "PCM"
    control.card    0
    min_dB          -51.0
    max_dB          0.0
    resolution      256
}
ctl.!default {
    type    hw
    card    0
}
EOF
ok "ALSA softvol configured: /etc/asound.conf"

step "Создание директорий приложения"
mkdir -p /home/pi/indicator/{configs/device,videos,scripts}
mkdir -p /home/pi/indicator/resources/{chars,arrows,modes,weights,notifications}
mkdir -p /home/pi/indicator/sounds
chown -R pi:pi /home/pi/indicator
ok "Application directories created"

step "Создание FIFO для IPC (indicator ↔ media_ingest)"
cat > /etc/tmpfiles.d/indicator.conf << 'EOF'
p /run/indicator-media.fifo 0660 pi pi -
EOF
systemd-tmpfiles --create /etc/tmpfiles.d/indicator.conf
ok "IPC FIFO configured"

step "Итог"
echo ""
echo -e "  ${GREEN}${BOLD}Pi setup complete.${RESET}"
echo ""
echo "  Следующие шаги:"
echo "  1. Проверить аудио:  aplay /usr/share/sounds/alsa/Front_Left.wav"
echo "  2. Проверить видео:  omxplayer --layer 1 <test.mp4>"
echo "  3. Создать образ:    just pi::backup-image /dev/rdisk<N>"
echo ""
