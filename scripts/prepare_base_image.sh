#!/usr/bin/env bash
#
# prepare_base_image.sh — готовит чистый Raspbian Buster Lite к первой
# загрузке новой платы: 3-раздельная разметка (p1 boot · p2 rootfs ·
# p3 /data LABEL=data) ДО первого старта, чтобы init_resize.sh не растянул
# rootfs на всю карту (иначе p3 пришлось бы выкраивать ужатием p2, Фаза 6 Блок A).
#
# Что делает (только с копией — исходный .img не трогается):
#   1. Увеличивает файл образа до p1 + P2_MB + P3_MB
#   2. parted: растягивает p2 до P2_MB, создаёт p3 (P3_MB)
#   3. e2fsck + resize2fs p2; mkfs.ext4 -L data p3
#   4. /boot: cmdline.txt из deploy/boot/ с PARTUUID образа (без init_resize.sh
#      и без console=serial0 — UART занят STM32), ssh, wpa_supplicant.conf (опц.),
#      config.txt из deploy/boot/ (опц.)
#
# ЗАПУСК: внутри привилегированного Docker-контейнера на macOS (P-36:
# losetup -P не создаёт partition devices в Docker Desktop → kpartx).
# Образ — именно ubuntu:22.04: mke2fs ≥ 1.47 по умолчанию включает
# orphan_file, который не понимает e2fsck 1.44 в Buster (скрипт это гасит,
# но проверено только на 22.04).
#
#   docker run --rm -it --privileged \
#       -v "$(pwd)":/work -w /work \
#       ubuntu:22.04 bash
#   apt-get update && apt-get install -y kpartx parted e2fsprogs dosfstools gzip
#   WPA_CONF=/work/wpa_supplicant.conf \
#       ./scripts/prepare_base_image.sh \
#       build-env/2023-05-03-raspios-buster-armhf-lite.img
#
# Параметры (позиционные): <image.img|.img.gz> [P2_MB=8192] [P3_MB=4096] [OUT]
# Окружение:
#   WPA_CONF     — путь к wpa_supplicant.conf для bring-up (по умолчанию нет)
#   BOOT_CONFIG  — config.txt для /boot (по умолчанию deploy/boot/config.txt;
#                  BOOT_CONFIG= — оставить штатный config.txt образа)
#   BOOT_CMDLINE — шаблон cmdline.txt (по умолчанию deploy/boot/cmdline.txt;
#                  root=PARTUUID берётся из образа; BOOT_CMDLINE= — только
#                  убрать init_resize.sh из штатного)
#   TMPDIR       — где разворачивать рабочую копию (нужно ~P2_MB+P3_MB места,
#                  файл sparse). Например -e TMPDIR=/work/.tmp
#
# Итоговый размер ≈ 256M + 8G + 4G ≈ 12.3 GiB — влезает на любую 16 GB карту.
# p3 позже ужимается shrink_image.sh при снятии factory image.
#
# Предположение: таблица msdos/MBR, 2 раздела (штатный Buster Lite образ).

set -euo pipefail

IMG_IN="${1:?Использование: $0 <image.img|image.img.gz> [P2_MB=8192] [P3_MB=4096] [OUT]}"
P2_MB="${2:-8192}"
P3_MB="${3:-4096}"
OUT="${4:-indicator-hd-prep-$(date +%Y%m%d).img.gz}"
SECTOR_SIZE=512
ALIGN_SECTORS=8192   # 4 MiB — как у p1/p2 в образах Raspberry Pi OS

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BOOT_CONFIG="${BOOT_CONFIG-${SCRIPT_DIR}/../deploy/boot/config.txt}"
BOOT_CMDLINE="${BOOT_CMDLINE-${SCRIPT_DIR}/../deploy/boot/cmdline.txt}"
WPA_CONF="${WPA_CONF:-}"

for tool in losetup kpartx parted e2fsck resize2fs mkfs.ext4 gzip; do
    command -v "$tool" >/dev/null || { echo "  ❌  нет $tool — см. apt-get install в шапке"; exit 1; }
done
[ -z "${WPA_CONF}" ]    || [ -f "${WPA_CONF}" ]    || { echo "  ❌  WPA_CONF=${WPA_CONF} не найден"; exit 1; }

# wpa_supplicant при ошибке разбора молча не стартует — wlan0 без адреса,
# диагностика только с локальной консоли. Ловим типовые ошибки заранее.
# BOM и CR не ошибка — их убираем при копировании в /boot.
validate_wpa_conf() {
    local f="$1" errs=0 bad
    local q_open=$'\xe2\x80\x9c' q_close=$'\xe2\x80\x9d' q_guil_l=$'\xc2\xab' q_guil_r=$'\xc2\xbb'
    if grep -qF -e "${q_open}" -e "${q_close}" -e "${q_guil_l}" -e "${q_guil_r}" "$f"; then
        echo "  ❌  WPA_CONF: типографские кавычки (U+201C/U+201D, ёлочки) — замените на \" (TextEdit/Заметки подставляют их сами)"
        errs=1
    fi
    grep -qE '^[[:space:]]*country=[A-Z]{2}[[:space:]]*$' "$f" \
        || { echo "  ❌  WPA_CONF: нет строки country=XX (без неё WiFi заблокирован rfkill)"; errs=1; }
    grep -qE '^[[:space:]]*network=\{' "$f" \
        || { echo "  ❌  WPA_CONF: нет блока network={...}"; errs=1; }
    bad="$(tr -d '\r' < "$f" | grep -nE '^[[:space:]]*ssid=' | grep -vE ':[[:space:]]*ssid=("[^"]+"|[0-9a-fA-F]+)[[:space:]]*$' || true)"
    [ -z "$bad" ] || { echo "  ❌  WPA_CONF: ssid должен быть в кавычках: ssid=\"ИМЯ\" (строки: $(echo "$bad" | cut -d: -f1 | tr '\n' ' '))"; errs=1; }
    bad="$(tr -d '\r' < "$f" | grep -nE '^[[:space:]]*psk=' | grep -vE ':[[:space:]]*psk=("[^"]{8,63}"|[0-9a-fA-F]{64})[[:space:]]*$' || true)"
    [ -z "$bad" ] || { echo "  ❌  WPA_CONF: psk — \"пароль\" в кавычках (8–63 символа) или 64 hex без кавычек (строки: $(echo "$bad" | cut -d: -f1 | tr '\n' ' '))"; errs=1; }
    return "$errs"
}
if [ -n "${WPA_CONF}" ]; then
    validate_wpa_conf "${WPA_CONF}" || exit 1
fi
[ -z "${BOOT_CONFIG}" ] || [ -f "${BOOT_CONFIG}" ] || { echo "  ❌  BOOT_CONFIG=${BOOT_CONFIG} не найден"; exit 1; }
[ -z "${BOOT_CMDLINE}" ] || [ -f "${BOOT_CMDLINE}" ] || { echo "  ❌  BOOT_CMDLINE=${BOOT_CMDLINE} не найден"; exit 1; }

WORK_DIR="$(mktemp -d)"
WORK_IMG="${WORK_DIR}/work.img"
MNT_BOOT="${WORK_DIR}/boot"
LOOP_DEV=""

cleanup() {
    set +e
    mountpoint -q "${MNT_BOOT}" && umount "${MNT_BOOT}"
    if [ -n "${LOOP_DEV}" ]; then
        kpartx -dv "${LOOP_DEV}" >/dev/null 2>&1
        losetup -d "${LOOP_DEV}" >/dev/null 2>&1
    fi
    rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

attach() {
    LOOP_DEV="$(losetup -f --show "${WORK_IMG}")"
    kpartx -av "${LOOP_DEV}"
    sleep 1  # device-mapper нодам нужен момент на появление
    MAPPER="/dev/mapper/$(basename "${LOOP_DEV}")"
}

detach() {
    kpartx -dv "${LOOP_DEV}"
    losetup -d "${LOOP_DEV}"
    LOOP_DEV=""
}

echo "  💾  Копируем образ во временную рабочую область..."
case "${IMG_IN}" in
    *.gz) gunzip -c "${IMG_IN}" > "${WORK_IMG}" ;;
    *)    cp "${IMG_IN}" "${WORK_IMG}" ;;
esac

echo "  🔎  Исходная таблица разделов:"
parted -s "${WORK_IMG}" unit s print
PARTS="$(parted -sm "${WORK_IMG}" unit s print | grep -cE '^[0-9]+:')"
[ "${PARTS}" -eq 2 ] || { echo "  ❌  Ожидалось 2 раздела, найдено ${PARTS} — образ уже размечен?"; exit 1; }

P2_START="$(parted -sm "${WORK_IMG}" unit s print | awk -F: '$1==2{gsub("s","",$2); print $2}')"
P2_END=$(( P2_START + P2_MB * 1024 * 1024 / SECTOR_SIZE - 1 ))
P3_START=$(( (P2_END / ALIGN_SECTORS + 1) * ALIGN_SECTORS ))
P3_END=$(( P3_START + P3_MB * 1024 * 1024 / SECTOR_SIZE - 1 ))
TOTAL_BYTES=$(( (P3_END + 1) * SECTOR_SIZE ))

echo "  📏  p2: ${P2_START}s…${P2_END}s (${P2_MB}M) · p3: ${P3_START}s…${P3_END}s (${P3_MB}M)"
echo "  📏  Размер образа → $(( TOTAL_BYTES / 1024 / 1024 )) MiB"
truncate -s "${TOTAL_BYTES}" "${WORK_IMG}"

echo "  ✂️   parted: resizepart 2 + mkpart 3..."
parted -s "${WORK_IMG}" unit s resizepart 2 "${P2_END}"
parted -s "${WORK_IMG}" unit s mkpart primary ext4 "${P3_START}" "${P3_END}"
parted -s "${WORK_IMG}" unit s print

echo "  🔌  losetup + kpartx (P-36)..."
attach
[ -b "${MAPPER}p3" ] || { echo "  ❌  ${MAPPER}p3 не найден"; exit 1; }

echo "  🔧  e2fsck -f + resize2fs p2..."
e2fsck -f -y "${MAPPER}p2"
resize2fs "${MAPPER}p2"

echo "  🆕  mkfs.ext4 -L data p3..."
MKFS_OPTS=(-F -L data)
grep -q orphan_file /etc/mke2fs.conf 2>/dev/null && MKFS_OPTS+=(-O ^orphan_file)
mkfs.ext4 "${MKFS_OPTS[@]}" "${MAPPER}p3"

echo "  🥾  /boot: cmdline.txt, ssh, wpa_supplicant.conf, config.txt..."
mkdir -p "${MNT_BOOT}"
mount "${MAPPER}p1" "${MNT_BOOT}"

# init_resize.sh растянул бы p2 на всю карту — разметка уже сделана.
# PARTUUID в deploy/boot/cmdline.txt — от конкретной карты, берём из образа.
ROOT_PARTUUID="$(grep -o 'root=PARTUUID=[^ ]*' "${MNT_BOOT}/cmdline.txt")"
[ -n "${ROOT_PARTUUID}" ] || { echo "  ❌  root=PARTUUID не найден в cmdline.txt образа"; exit 1; }
cp "${MNT_BOOT}/cmdline.txt" "${MNT_BOOT}/cmdline.txt.orig"
if [ -n "${BOOT_CMDLINE}" ]; then
    sed "s|root=PARTUUID=[^ ]*|${ROOT_PARTUUID}|" "${BOOT_CMDLINE}" | tr -d '\n' > "${MNT_BOOT}/cmdline.txt"
    echo >> "${MNT_BOOT}/cmdline.txt"
fi
sed -i 's| init=/usr/lib/raspi-config/init_resize.sh||' "${MNT_BOOT}/cmdline.txt"
if grep -q init_resize "${MNT_BOOT}/cmdline.txt"; then
    echo "  ❌  init_resize.sh остался в cmdline.txt"; exit 1
fi
echo "  cmdline.txt: $(cat "${MNT_BOOT}/cmdline.txt")"

touch "${MNT_BOOT}/ssh"

if [ -n "${WPA_CONF}" ]; then
    sed '1s/^\xEF\xBB\xBF//' "${WPA_CONF}" | tr -d '\r' > "${MNT_BOOT}/wpa_supplicant.conf"
    echo "  ✅  wpa_supplicant.conf положен в /boot (bring-up)"
else
    echo "  ⚠️   WPA_CONF не задан — WiFi при первом старте не поднимется (только Ethernet/USB-gadget)"
fi

if [ -n "${BOOT_CONFIG}" ]; then
    cp "${MNT_BOOT}/config.txt" "${MNT_BOOT}/config.txt.orig"
    cp "${BOOT_CONFIG}" "${MNT_BOOT}/config.txt"
    # raspi-config enable_overlayfs делает echo "initramfs …" >> config.txt —
    # без финального \n строка склеится с последней (dtparam=watchdog=on)
    [ -z "$(tail -c 1 "${MNT_BOOT}/config.txt")" ] || echo >> "${MNT_BOOT}/config.txt"
    echo "  ✅  config.txt ← ${BOOT_CONFIG} (оригинал: config.txt.orig)"
fi

umount "${MNT_BOOT}"
detach

echo "  📦  Сжимаем → ${OUT}..."
gzip -c "${WORK_IMG}" > "${OUT}"

echo "  ✅  Готово: ${OUT}"
echo "  ℹ️   Дальше: balenaEtcher → SD → первая загрузка → just pi::setup-ssh →"
echo "      just pi::setup-pi (подхватит LABEL=data) → just pi::deploy-full"
