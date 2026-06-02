#!/usr/bin/env bash
# =============================================================================
# scripts/setup_pi.sh — первичная настройка Raspberry Pi Zero W
# Запускать: just pi::setup-pi  (через SSH с sudo)
#
# Выполняется ОДИН РАЗ на свежем Raspbian Buster Lite.
# После этого: just pi::deploy-resources, deploy-sounds, deploy-configs, deploy
# =============================================================================
set -euo pipefail

BOLD="\033[1m"; GREEN="\033[0;32m"; YELLOW="\033[1;33m"; RESET="\033[0m"

step() { echo -e "\n${BOLD}>>> ${*}${RESET}"; }
ok()   { echo -e "  ${GREEN}✅  ${*}${RESET}"; }
warn() { echo -e "  ${YELLOW}⚠️   ${*}${RESET}"; }

IND="/home/pi/indicator"
DATA="/data"

# ─── 1. APT sources (Buster archived) ────────────────────────────────────────

step "Обновление системы (Buster archived repos)"

tee /etc/apt/sources.list > /dev/null <<'SOURCES'
deb http://archive.debian.org/debian buster main contrib non-free
deb http://archive.debian.org/debian-security buster/updates main contrib non-free
SOURCES

tee /etc/apt/sources.list.d/raspi.list > /dev/null <<'SOURCES'
deb http://archive.raspberrypi.org/debian/ buster main
SOURCES

tee /etc/apt/apt.conf.d/99archive > /dev/null <<'CONF'
Acquire::Check-Valid-Until "false";
CONF

apt-get update -q
apt-get full-upgrade -y -q
ok "System updated"

# ─── 2. Пакеты ───────────────────────────────────────────────────────────────

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

# ─── 3. Отключение лишних сервисов ───────────────────────────────────────────

step "Отключение лишних сервисов"
systemctl disable --now hciuart bluetooth               2>/dev/null || true
systemctl disable --now serial-getty@ttyAMA0.service    2>/dev/null || true
systemctl disable --now triggerhappy.service            2>/dev/null || true
ok "Unnecessary services disabled"

# ─── 4. ALSA softvol для MAX98357 ────────────────────────────────────────────

step "Настройка ALSA softvol для MAX98357"
cat > /etc/asound.conf << 'ALSA'
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
ALSA
ok "ALSA softvol configured: /etc/asound.conf"

# ─── 5. /data structure (мутабельные данные устройства) ──────────────────────

step "Создание /data структуры"
mkdir -p \
    "$DATA/pi_nku_configs" \
    "$DATA/resources/chars" \
    "$DATA/resources/arrows" \
    "$DATA/resources/modes" \
    "$DATA/resources/weights" \
    "$DATA/resources/notifications" \
    "$DATA/sounds" \
    "$DATA/videos"
chown -R pi:pi "$DATA"
ok "/data structure created"

# ─── 6. /home/pi/indicator/ (бинари + точки монтирования) ───────────────────

step "Создание директорий приложения"
mkdir -p \
    "$IND/scripts" \
    "$IND/tools" \
    "$IND/pi_nku_configs" \
    "$IND/resources" \
    "$IND/sounds" \
    "$IND/videos"
chown -R pi:pi "$IND"
ok "Application directories created"

# ─── 7. Bind-монты в /etc/fstab ──────────────────────────────────────────────

step "Настройка bind-монтов (/data → $IND)"

add_fstab_entry() {
    local src="$1" dst="$2"
    if grep -qF "$dst" /etc/fstab; then
        warn "fstab: запись для $dst уже есть — пропускаем"
    else
        echo "$src $dst none bind 0 0" >> /etc/fstab
        ok "fstab: $src → $dst"
    fi
}

echo "" >> /etc/fstab
echo "# indicator /data bind mounts (setup_pi.sh)" >> /etc/fstab
add_fstab_entry "$DATA/pi_nku_configs" "$IND/pi_nku_configs"
add_fstab_entry "$DATA/resources"      "$IND/resources"
add_fstab_entry "$DATA/sounds"         "$IND/sounds"
add_fstab_entry "$DATA/videos"         "$IND/videos"

# Примонтировать сразу (не ждать reboot)
mount --bind "$DATA/pi_nku_configs" "$IND/pi_nku_configs"
mount --bind "$DATA/resources"      "$IND/resources"
mount --bind "$DATA/sounds"         "$IND/sounds"
mount --bind "$DATA/videos"         "$IND/videos"
ok "Bind mounts active"

# ─── 8. IPC FIFO (indicator ↔ media_ingest) ──────────────────────────────────

step "Создание FIFO для IPC"
cat > /etc/tmpfiles.d/indicator.conf << 'TMPFILES'
p /run/indicator-media.fifo 0660 pi pi -
TMPFILES
systemd-tmpfiles --create /etc/tmpfiles.d/indicator.conf
ok "IPC FIFO configured: /run/indicator-media.fifo"

# ─── 9. Маскировка getty@tty1 (для TUI при старте) ───────────────────────────

step "Маскировка getty@tty1"
systemctl mask getty@tty1.service
ok "getty@tty1 masked"


# ─── 10. Boot splash ────────────────────────────────────────────────────────── 
step "Настройка boot splash"
 
# Установить fbi (framebuffer image viewer)
apt-get install -y -q fbi
ok "fbi installed"
 
# Создать директорию для splash
mkdir -p /home/pi/indicator/splash
chown pi:pi /home/pi/indicator/splash
 
# Скопировать splash.jpg с boot-раздела (деплоится туда же что и config.txt)
if [ -f /boot/splash.jpg ]; then
    cp /boot/splash.jpg /home/pi/indicator/splash/splash.jpg
    chown pi:pi /home/pi/indicator/splash/splash.jpg
    ok "splash.jpg copied from /boot/splash.jpg"
else
    warn "splash.jpg not found in /boot/ — положи файл вручную или через just pi::deploy-splash"
    warn "Путь на устройстве: /home/pi/indicator/splash/splash.jpg"
fi
 
ok "Boot splash configured"

step "Настройка sudoers для fbi (boot splash)"
echo "pi ALL=(root) NOPASSWD: /usr/bin/fbi" > /etc/sudoers.d/indicator-fbi
chmod 0440 /etc/sudoers.d/indicator-fbi
ok "sudoers configured for fbi"

# ─── Итог ─────────────────────────────────────────────────────────────────────

step "Итог"
echo ""
echo -e "  ${GREEN}${BOLD}Pi setup complete.${RESET}"
echo ""
echo "  Следующие шаги (с хоста):"
echo "  1. just pi::deploy-resources   — PNG ресурсы → /data/resources/"
echo "  2. just pi::deploy-sounds      — WAV звуки → /data/sounds/"
echo "  3. just pi::deploy-configs     — конфиги → /data/pi_nku_configs/"
echo "  4. just pi::deploy             — бинари + systemd units"
echo "  5. just pi::restart"
echo "  6. just pi::check-resources    — финальная проверка"
echo ""