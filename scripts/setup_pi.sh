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
deb https://legacy.raspbian.org/raspbian/ buster main contrib non-free rpi
# deb-src https://legacy.raspbian.org/raspbian/ buster main contrib non-free rpi
SOURCES

tee /etc/apt/sources.list.d/raspi.list > /dev/null <<'SOURCES'
deb http://archive.raspberrypi.org/debian/ buster main
SOURCES

tee /etc/apt/apt.conf.d/99archive > /dev/null <<'CONF'
Acquire::Check-Valid-Until "false";
CONF

apt-get --allow-releaseinfo-change update -q
apt-get full-upgrade -y -q
ok "System updated"

# ─── 2. Пакеты ───────────────────────────────────────────────────────────────

step "Установка пакетов"
apt-get install -y -q \
    omxplayer \
    ffmpeg \
    alsa-utils fbi \
    imagemagick \
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

# ─── 4. I2S overlay (googlevoicehat) ─────────────────────────────────────────
#
# MAX98357A подключён через I2S. ALSA-карта появляется только после загрузки
# dtoverlay=googlevoicehat-soundcard.
#
# Примечание по имени: это стандартный overlay для I2S-усилителей Adafruit
# (Speaker Bonnet / MAX98357). Несмотря на название «googlevoicehat» — это не
# Google Voice HAT. Adafruit использует тот же overlay, так как он предоставляет
# нужный I2S-маппинг. Overlay НУЖЕН: без него MAX98357 не виден как ALSA-устройство.
#
# Встроенный audio (bcm2835) отключаем: при включённом built-in card индексы
# могут меняться и MAX98357 окажется не card 0.

step "Настройка I2S overlay для MAX98357 (googlevoicehat-soundcard)"

BOOT_CONFIG="/boot/config.txt"

if grep -q "^dtoverlay=googlevoicehat-soundcard" "$BOOT_CONFIG"; then
    warn "dtoverlay=googlevoicehat-soundcard уже есть — пропускаем"
else
    echo "dtoverlay=googlevoicehat-soundcard" >> "$BOOT_CONFIG"
    ok "dtoverlay=googlevoicehat-soundcard добавлен в $BOOT_CONFIG"
fi

# Отключить встроенный bcm2835 audio — иначе I2S-карта может оказаться не card 0
if grep -q "^dtparam=audio=on" "$BOOT_CONFIG"; then
    sed -i 's|^dtparam=audio=on|#dtparam=audio=on  # disabled by setup_pi.sh (I2S amp)|' "$BOOT_CONFIG"
    ok "dtparam=audio=on отключён"
else
    warn "dtparam=audio=on не найден — пропускаем"
fi

ok "I2S overlay configured"

# ─── 5. ALSA: dmix + softvol для MAX98357 ────────────────────────────────────
#
# Стек (снизу вверх):
#   speakerbonnet  = hw card 0  (I2S amp, видна после googlevoicehat overlay)
#   dmixer         = dmix поверх speakerbonnet — позволяет нескольким процессам
#                    (indicator + i2s-silence.service) одновременно выводить звук
#   softvol        = программный регулятор громкости «PCM» поверх dmixer
#   !default       = plug → softvol (используется aplay/amixer без явного -D)
#
# Параметры slave: 48000 Hz / S16_LE / 2ch (stereo)
# Все WAV-файлы задеплоены в этом же формате.

step "Настройка ALSA (dmix + softvol) для MAX98357"
cat > /etc/asound.conf << 'ALSA'
# /etc/asound.conf — MAX98357A через I2S (Speaker Bonnet / googlevoicehat overlay)
# Сгенерирован setup_pi.sh. Не редактировать вручную.

# ── Уровень 1: аппаратная карта ──────────────────────────────────────────────
pcm.speakerbonnet {
    type hw
    card 0
}

# ── Уровень 2: dmix — разделяемый микшер ─────────────────────────────────────
# Позволяет indicator и i2s-silence.service одновременно выводить звук.
# Держит I2S-тактирование активным → устраняет щелчки при старте/конце трека.
pcm.dmixer {
    type     dmix
    ipc_key  1024
    ipc_perm 0666
    slave {
        pcm         "speakerbonnet"
        period_time 0
        period_size 1024
        buffer_size 8192
        rate        48000
        channels    2
        format      S32_LE
    }
}

ctl.dmixer {
    type hw
    card 0
}

# ── Уровень 3: softvol — программная громкость ───────────────────────────────
# amixer sset 'PCM' N%  →  управляет этим контролом.
# MAX98357 не имеет аппаратного volume control в ALSA.
pcm.softvol {
    type        softvol
    slave.pcm   "dmixer"
    control {
        name "PCM"
        card 0
    }
    min_dB     -51.0
    max_dB     0.0
    resolution 256
}

ctl.softvol {
    type hw
    card 0
}

# ── Уровень 4: default — точка входа для aplay/amixer ────────────────────────
pcm.!default {
    type      plug
    slave.pcm "softvol"
}

ctl.!default {
    type hw
    card 0
}
ALSA
ok "ALSA configured: /etc/asound.conf (48kHz / S16_LE / stereo / dmix+softvol)"

# ─── 6. i2s-silence.service — устранение щелчков ─────────────────────────────
#
# Проблема: MAX98357A при простое отключает I2S-тактирование. При следующем
# воспроизведении PLL заново захватывает частоту → щелчок в начале и конце звука.
#
# Решение: держать dmixer постоянно активным через непрерывное воспроизведение
# тишины из /dev/zero. Нагрузка на CPU минимальна (~0.3% на Zero 2W).
#
# КРИТИЧНО: rate/format/channels должны ТОЧНО совпадать с параметрами slave
# в pcm.dmixer выше. Несовпадение → plug-конвертация → задержка → щелчки снова.

step "Установка i2s-silence.service (I2S clock keepalive)"
cat > /etc/systemd/system/i2s-silence.service << 'UNIT'
[Unit]
Description=I2S clock keepalive (silence playback via dmixer)
After=sound.target
Wants=sound.target
StartLimitBurst=10
StartLimitIntervalSec=30

[Service]
Type=simple
User=pi
ExecStart=/usr/bin/aplay -D dmixer -t raw -r 48000 -c 2 -f S32_LE /dev/zero
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
UNIT

systemctl daemon-reload
systemctl enable i2s-silence.service
ok "i2s-silence.service installed and enabled"



step "Добавление pi в группы video и tty (для fbi)"
usermod -a -G video pi
usermod -a -G tty pi
usermod -a -G dialout pi
ok "pi added to video, dialout groups"

step "Настройка sudoers для fbi (boot splash)"
echo "pi ALL=(root) NOPASSWD: /usr/bin/fbi" > /etc/sudoers.d/indicator-fbi
chmod 0440 /etc/sudoers.d/indicator-fbi
ok "sudoers configured for fbi"

# ─── 7. /data structure (мутабельные данные устройства) ──────────────────────

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

# ─── 8. /home/pi/indicator/ (бинари + точки монтирования) ───────────────────

step "Создание директорий приложения"
mkdir -p \
    "$IND/scripts" \
    "$IND/tools" \
    "$IND/pi_nku_configs" \
    "$IND/resources" \
    "$IND/sounds" \
    "$IND/videos"
chown -R pi:pi "$IND"

mkdir -p /mnt/usb
chown pi:pi /mnt/usb
ok "/mnt/usb created"

ok "Application directories created"

# ─── 9. Bind-монты в /etc/fstab ──────────────────────────────────────────────

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

# ─── 10. IPC FIFO (indicator ↔ media_ingest) ─────────────────────────────────
step "Создание FIFO для IPC"
cat > /etc/tmpfiles.d/indicator.conf << 'TMPFILES'
d /run/indicator 0755 pi pi -
TMPFILES
systemd-tmpfiles --create /etc/tmpfiles.d/indicator.conf
ok "IPC directory configured: /run/indicator/"

# ─── 11. Маскировка getty@tty1 (для TUI при старте) ──────────────────────────

#step "Маскировка getty@tty1"
#systemctl mask getty@tty1.service
#ok "getty@tty1 masked"

# ─── Итог ─────────────────────────────────────────────────────────────────────

step "Итог"
echo ""
echo -e "  ${GREEN}${BOLD}Pi setup complete.${RESET}"
echo ""
echo "  Следующие шаги (с хоста):"
echo "  4. just pi::deploy-full             — бинари + systemd units"
echo "  5. just pi::restart"
echo "  6. just pi::check-resources    — финальная проверка"
echo ""

# FIXME: непонятки с cmdline, маскированием getty@tty1
# TODO: установка подключения к WLAN, включение SSH в raspi-config
# TODO: user в dialout, display, tty!
# TODO: локаль ru_UTF8, console-setup: 
# LANG=ru_RU.UTF-8
# LANGUAGE=
# LC_CTYPE="ru_RU.UTF-8"
# LC_NUMERIC="ru_RU.UTF-8"
# LC_TIME="ru_RU.UTF-8"
# LC_COLLATE="ru_RU.UTF-8"
# LC_MONETARY="ru_RU.UTF-8"
# LC_MESSAGES="ru_RU.UTF-8"
# LC_PAPER="ru_RU.UTF-8"
# LC_NAME="ru_RU.UTF-8"
# LC_ADDRESS="ru_RU.UTF-8"
# LC_TELEPHONE="ru_RU.UTF-8"
# LC_MEASUREMENT="ru_RU.UTF-8"
# LC_IDENTIFICATION="ru_RU.UTF-8"
# LC_ALL=
# pi@indicator-04:~ $ cat /etc/default/console-setup
# CONFIGURATION FILE FOR SETUPCON

# Consult the console-setup(5) manual page.

#ACTIVE_CONSOLES="/dev/tty[1-6]"

#CHARMAP="UTF-8"

#CODESET="CyrSlav"
#FONTFACE="Terminus"
#FONTSIZE="14x28"

#VIDEOMODE=

# The following is an example how to use a braille font
# FONT='lat9w-08.psf.gz brl-8x8.psf'