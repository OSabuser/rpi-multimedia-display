#!/usr/bin/env bash
# =============================================================================
# scripts/setup_pi.sh — первичная настройка Raspberry Pi Zero 2W
# Запускать: just pi::setup-pi  (через SSH с sudo)
#
# Выполняется ОДИН РАЗ на свежем Raspbian Buster Lite, SD подготовлена
# scripts/prepare_base_image.sh (p3 ext4 LABEL=data уже существует).
# После этого: just pi::deploy-full
# =============================================================================
set -euo pipefail

BOLD="\033[1m"; GREEN="\033[0;32m"; YELLOW="\033[1;33m"; RED="\033[0;31m"; RESET="\033[0m"

step() { echo -e "\n${BOLD}>>> ${*}${RESET}"; }
ok()   { echo -e "  ${GREEN}✅  ${*}${RESET}"; }
warn() { echo -e "  ${YELLOW}⚠️   ${*}${RESET}"; }
die()  { echo -e "  ${RED}❌  ${*}${RESET}"; exit 1; }

IND="/home/pi/indicator"
DATA="/data"

# Скрипт приходит через stdin (ssh … "sudo bash -s" < setup_pi.sh): любой
# интерактивный вопрос apt/debconf прочитал бы остаток скрипта как ответ.
export DEBIAN_FRONTEND=noninteractive
APT_OPTS=(-y -q -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold)

# При активном overlayfs все изменения уйдут в tmpfs и пропадут после reboot
if grep -qs ' / overlay ' /proc/mounts; then
    die "overlayfs активен — сначала just pi::update-start"
fi

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

# Без RTC часы после загрузки — 2023-05-03 до первой NTP-синхронизации;
# с такими часами InRelease «not valid yet» и индексы молча не скачаются
for _ in $(seq 1 30); do
    [ "$(timedatectl show -p NTPSynchronized --value 2>/dev/null)" = "yes" ] && break
    sleep 2
done
[ "$(timedatectl show -p NTPSynchronized --value 2>/dev/null)" = "yes" ] \
    || warn "NTP не синхронизирован за 60 с — дата: $(date)"

apt-get --allow-releaseinfo-change update -q < /dev/null

# apt-get update может «успешно» завершиться без индексов legacy.raspbian.org —
# тогда install ниже падает с невнятным «no installation candidate»
# вывод — в переменную: `apt-cache | grep -q` под pipefail даёт ложный провал
# (grep -q выходит на первом совпадении → SIGPIPE у apt-cache)
for pkg in omxplayer ffmpeg git exfat-fuse; do
    policy="$(apt-cache policy "$pkg")"
    grep -qE "Candidate: [0-9]" <<< "$policy" \
        || die "apt: нет кандидата для $pkg — индексы legacy.raspbian.org не загружены, проверьте 'apt-get update' и дату ($(date))"
done

apt-get full-upgrade "${APT_OPTS[@]}" < /dev/null
ok "System updated"

# ─── 2. Пакеты ───────────────────────────────────────────────────────────────
step "Установка пакетов"
apt-get install "${APT_OPTS[@]}" \
    omxplayer \
    ffmpeg \
    alsa-utils fbi \
    imagemagick \
    libpng16-16 \
    exfat-utils \
    exfat-fuse \
    git < /dev/null
ok "Packages installed"

# ─── 3. Отключение лишних сервисов ───────────────────────────────────────────

step "Отключение лишних сервисов"
systemctl disable --now hciuart bluetooth               2>/dev/null || true
systemctl disable --now serial-getty@ttyAMA0.service    2>/dev/null || true
systemctl disable --now triggerhappy.service            2>/dev/null || true
ok "Unnecessary services disabled"

# ─── 4. I2S overlay (hifiberry-dac) ──────────────────────────────────────────
# Несущая плата 2W: ЦАП PCM5102A по I2S, overlay dtoverlay=hifiberry-dac
# (задан производителем платы, F1). Обычно он уже есть: deploy/boot/config.txt
# кладётся в /boot скриптом prepare_base_image.sh. Здесь — только страховка.
# (Плата Zero W из ветки dev-pi использовала MAX98357 + googlevoicehat-soundcard.)
#
# Встроенный audio (bcm2835) отключаем: при включённом built-in card индексы
# могут меняться и I2S-карта окажется не card 0.

step "Настройка I2S overlay (hifiberry-dac)"

BOOT_CONFIG="/boot/config.txt"

# Проверить наличие любого I2S audio overlay (googlevoicehat, hifiberry-dac, и др.)
if grep -qE "^dtoverlay=(googlevoicehat-soundcard|hifiberry-dac|hifiberry-dacplus|hifiberry-digi|i2s-mmap)" "$BOOT_CONFIG"; then
    warn "I2S overlay уже прописан в $BOOT_CONFIG — пропускаем"
else
    echo "dtoverlay=hifiberry-dac" >> "$BOOT_CONFIG"
    ok "dtoverlay=hifiberry-dac добавлен в $BOOT_CONFIG (применится после reboot)"
fi

# Отключить встроенный bcm2835 audio — иначе I2S-карта может оказаться не card 0
if grep -q "^dtparam=audio=on" "$BOOT_CONFIG"; then
    sed -i 's|^dtparam=audio=on|#dtparam=audio=on  # disabled by setup_pi.sh (I2S amp)|' "$BOOT_CONFIG"
    ok "dtparam=audio=on отключён"
else
    warn "dtparam=audio=on не найден или уже выключен — пропускаем"
fi

ok "I2S overlay configured"

# ─── 5. ALSA: dmix + softvol для I2S-ЦАП ─────────────────────────────────────
#
# Стек (снизу вверх):
#   speakerbonnet  = hw card 0  (I2S-ЦАП; имя PCM историческое, с платы Zero W)
#   dmixer         = dmix поверх speakerbonnet — позволяет нескольким процессам
#                    (indicator + i2s-silence.service) одновременно выводить звук
#   softvol        = программный регулятор громкости «PCM» поверх dmixer
#   !default       = plug → softvol (используется aplay/amixer без явного -D)
#
# Параметры slave: 48000 Hz / S32_LE / 2ch (stereo) — как в i2s-silence.service
# WAV-файлы — 48 kHz / S16_LE / stereo: частота совпадает, S16→S32 делает plug (без ресемплинга).

step "Настройка ALSA (dmix + softvol) для I2S-ЦАП"
cat > /etc/asound.conf << 'ALSA'
# /etc/asound.conf — I2S-ЦАП card 0 (hifiberry-dac / PCM5102A на плате 2W)
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
# У I2S-ЦАП нет аппаратного volume control в ALSA.
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
ok "ALSA configured: /etc/asound.conf (48kHz / S32_LE / stereo / dmix+softvol)"

# P-36: softvol создаёт контрол 'PCM' только при первом открытии PCM-пути,
# поэтому на старте indicator `amixer sset PCM` падал («Unable to find simple
# control»). Открываем softvol один раз и сохраняем state: контрол помечен
# 'user', alsa-restore.service пересоздаёт его на каждой загрузке (до sound.target).
CARDS="$(aplay -l 2>/dev/null || true)"
if grep -q "^card 0" <<< "$CARDS"; then
    aplay -q -D softvol -d 1 -t raw -r 48000 -c 2 -f S32_LE /dev/zero < /dev/null \
        || warn "softvol: не удалось открыть PCM — контрол 'PCM' не создан"
    alsactl store
    if grep -q "name PCM" /var/lib/alsa/asound.state; then
        ok "ALSA state: контрол 'PCM' сохранён (P-36)"
    else
        warn "ALSA state: контрол 'PCM' не найден в /var/lib/alsa/asound.state"
    fi
else
    warn "ALSA: card 0 не видна (overlay из config.txt применится после reboot) — P-36 пропущен, перезапустите setup_pi.sh"
fi

# ─── 6. i2s-silence.service — устранение щелчков ─────────────────────────────
#
# Проблема: при простое I2S-тактирование отключается. При следующем
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

# ─── 6.5. Раздел /data (p3, ext4, LABEL=data) ────────────────────────────────
#
# /data обязан быть отдельным разделом: при overlayfs rootfs read-only, и всё,
# что лежит в /data на p2, будет теряться при каждой перезагрузке.
# Порядок в fstab критичен (Фаза 6): LABEL=data — ДО bind-монтов из /data.

step "Раздел /data (LABEL=data)"

DATA_DEV="$(blkid -L data || true)"
[ -n "$DATA_DEV" ] || die "Раздел с LABEL=data не найден — подготовьте SD через scripts/prepare_base_image.sh"

FSTAB_DATA="LABEL=data  $DATA  ext4  defaults,noatime  0  2"
if grep -qE "^LABEL=data[[:space:]]" /etc/fstab; then
    warn "fstab: LABEL=data уже есть — пропускаем"
elif grep -qF "# indicator /data bind mounts" /etc/fstab; then
    # повторный запуск: bind-монты уже в fstab — вставить строго перед ними
    sed -i "0,\|# indicator /data bind mounts|s||$FSTAB_DATA\n&|" /etc/fstab
    ok "fstab: LABEL=data вставлен перед bind-монтами"
else
    echo "$FSTAB_DATA" >> /etc/fstab
    ok "fstab: LABEL=data → $DATA"
fi

mkdir -p "$DATA"
if ! mountpoint -q "$DATA"; then
    if [ -n "$(ls -A "$DATA" 2>/dev/null)" ]; then
        die "$DATA на rootfs не пуст — перенесите содержимое на $DATA_DEV вручную (cp -a)"
    fi
    mount "$DATA"
fi
ok "$DATA смонтирован с $DATA_DEV"

# ─── 7. /data structure (мутабельные данные устройства) ──────────────────────

step "Добавление pi в группы video и tty (для fbi)"
usermod -a -G video pi
usermod -a -G tty pi
usermod -a -G dialout pi
ok "pi added to video, dialout groups"

step "Настройка sudoers для fbi (boot splash)"
echo "pi ALL=(root) NOPASSWD: /usr/bin/fbi" > /etc/sudoers.d/indicator-fbi
chmod 0440 /etc/sudoers.d/indicator-fbi
ok "sudoers configured for fbi"

step "Создание /data структуры"
mkdir -p \
    "$DATA/pi_nku_configs" \
    "$DATA/resources/arrows" \
    "$DATA/resources/modes" \
    "$DATA/resources/weights" \
    "$DATA/resources/notifications" \
    "$DATA/sounds" \
    "$DATA/videos"
chown -R pi:pi "$DATA"
ok "/data structure created"

# Эталонная плата НЕ должна проходить provisioning: после deploy-full
# indicator-firstboot.service при первой же перезагрузке сменил бы hostname,
# SSH-ключи и включил overlayfs. Флаг снимает cleanup_image.sh при сборке образа.
touch "$DATA/first_boot_done"
ok "$DATA/first_boot_done создан (first_boot.sh на эталонной плате не запустится)"

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

if ! grep -qF "# indicator /data bind mounts" /etc/fstab; then
    echo "" >> /etc/fstab
    echo "# indicator /data bind mounts (setup_pi.sh)" >> /etc/fstab
fi
add_fstab_entry "$DATA/pi_nku_configs" "$IND/pi_nku_configs"
add_fstab_entry "$DATA/resources"      "$IND/resources"
add_fstab_entry "$DATA/sounds"         "$IND/sounds"
add_fstab_entry "$DATA/videos"         "$IND/videos"

# Примонтировать сразу (не ждать reboot); повторный mount --bind наслоил бы второй
for d in pi_nku_configs resources sounds videos; do
    mountpoint -q "$IND/$d" || mount --bind "$DATA/$d" "$IND/$d"
done
ok "Bind mounts active"

# ─── 9.5. WiFi: wpa_supplicant.conf на /data ─────────────────────────────────
#
# С overlayfs запись в /etc/wpa_supplicant/ уходит в tmpfs и теряется.
# Credentials живут на /data (p3) и bind-монтируются в /etc.
# Seed для нового устройства — /boot/wpa_supplicant.conf → first_boot.sh.

step "WiFi: wpa_supplicant.conf на /data"

WPA_ETC="/etc/wpa_supplicant/wpa_supplicant.conf"
WPA_DATA="$DATA/wpa_supplicant.conf"

if [ -f "$WPA_DATA" ]; then
    warn "$WPA_DATA уже есть — пропускаем"
elif [ -f "$WPA_ETC" ]; then
    cat "$WPA_ETC" > "$WPA_DATA"
    ok "$WPA_ETC скопирован → $WPA_DATA"
else
    cat > "$WPA_DATA" << 'WPA'
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1
country=RU
WPA
    warn "$WPA_ETC не найден — создан шаблон без сетей"
fi
chown root:root "$WPA_DATA"
chmod 600 "$WPA_DATA"

add_fstab_entry "$WPA_DATA" "$WPA_ETC"
if ! findmnt -rn --mountpoint "$WPA_ETC" > /dev/null; then
    mount --bind "$WPA_DATA" "$WPA_ETC"
fi
ok "Bind mount: $WPA_DATA → $WPA_ETC"

# P-38: явный старт до dhcpcd вместо D-Bus activation (race при overlayfs initramfs)
systemctl enable wpa_supplicant.service
ok "wpa_supplicant.service enabled"

# raspberrypi-net-mods делает mv /boot/wpa_supplicant.conf → /etc/... — поверх
# bind-mount это ломается. Seed обрабатывает first_boot.sh.
systemctl mask raspberrypi-net-mods.service
ok "raspberrypi-net-mods.service masked (seed → first_boot.sh)"

# ─── 10. IPC FIFO (indicator ↔ media_ingest) ─────────────────────────────────
step "Создание FIFO для IPC"
cat > /etc/tmpfiles.d/indicator.conf << 'TMPFILES'
d /run/indicator 0755 pi pi -
TMPFILES
systemd-tmpfiles --create /etc/tmpfiles.d/indicator.conf
ok "IPC directory configured: /run/indicator/"

# ─── 10.5. Локаль и консольный шрифт с кириллицей ────────────────────────────
#
# run_setup.sh и pi_nku_menu выводят русский текст на tty1. С CODESET="guess"
# при en_GB консоль грузит Lat15-шрифт → вместо кириллицы квадраты.
# Terminus 14x28 на 1080×1920 портрет → ~77×68 символов (как на indicator-04).

step "Локаль ru_RU.UTF-8 и консольный шрифт CyrSlav Terminus"

sed -i 's/^# *\(ru_RU.UTF-8 UTF-8\)/\1/' /etc/locale.gen
grep -q "^ru_RU.UTF-8 UTF-8" /etc/locale.gen || echo "ru_RU.UTF-8 UTF-8" >> /etc/locale.gen
locale-gen
update-locale LANG=ru_RU.UTF-8
ok "Локаль: ru_RU.UTF-8 (применится к новым сессиям)"

cat > /etc/default/console-setup << 'CONSOLE'
# Сгенерирован setup_pi.sh: кириллица на tty (run_setup.sh, pi_nku_menu)
ACTIVE_CONSOLES="/dev/tty[1-6]"
CHARMAP="UTF-8"
CODESET="CyrSlav"
FONTFACE="Terminus"
FONTSIZE="14x28"
VIDEOMODE=
CONSOLE
systemctl restart console-setup.service
ok "Консоль: CyrSlav Terminus 14x28"

# ─── 11. Маскировка getty@tty1 (для TUI при старте) ──────────────────────────

step "Маскировка getty@tty1"
systemctl mask getty@tty1.service
ok "getty@tty1 masked"

# ─── 12. overlayfs ВЫКЛЮЧЕН (golden image) ───────────────────────────────────
# overlayfs через raspi-config (P-37). В golden image он выключен —
# first_boot.sh включит его на новом устройстве после provisioning.

step "overlayfs: выключен"
if grep -q "boot=overlay" /boot/cmdline.txt; then
    raspi-config nonint disable_overlayfs
    ok "overlayfs disabled (first_boot.sh включит при первом старте)"
else
    ok "overlayfs уже выключен (first_boot.sh включит при первом старте)"
fi

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
# (локаль и console-setup — шаг 10.5; ниже — исходная заметка с indicator-04)
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