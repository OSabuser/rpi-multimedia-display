#!/usr/bin/env bash
#
# cleanup_image.sh — превращает снимок настроенной платы (just pi::backup-image)
# в чистый factory image: убирает всё device-specific, чтобы first_boot.sh
# на новом устройстве отработал с нуля (Фаза 6 Блок E, теперь скриптом).
#
# Что делает (только с копией — исходный .img.gz не трогается):
#   p1 /boot : boot=overlay из cmdline.txt, initramfs из config.txt,
#              wpa_supplicant.conf, ssh-флаг
#   p2 rootfs: /etc/hostname + 127.0.1.1, SSH host keys, machine-id,
#              WiFi credentials под bind-mount, журналы, history, apt cache
#   p3 /data : first_boot_done, WiFi credentials
#   + zerofree p2/p3 (если установлен) — образ жмётся в разы лучше
#
# Проверяет: 3 раздела, p3 LABEL=data, fstab с LABEL=data и bind-монтами,
# first_boot.sh с индикатором WiFi seed.
#
# НЕ трогает: /home/pi/.ssh/authorized_keys (нужен для just pi::update-*),
# конфиги /data/pi_nku_configs, ресурсы, звуки, видео.
#
# ЗАПУСК: тот же привилегированный контейнер, что и для shrink_image.sh:
#
#   docker run --rm -it --privileged \
#       -v "$(pwd)":/work -w /work \
#       ubuntu:22.04 bash
#   apt-get update && apt-get install -y kpartx parted e2fsprogs dosfstools zerofree gzip
#   ./scripts/cleanup_image.sh indicator-base-20261021.img.gz
#   ./scripts/inspect_image.sh indicator-hd-base-20261021.img.gz
#   ./scripts/shrink_image.sh  indicator-hd-base-20261021.img.gz 2048   # опц.: только если
#       нужны карты < 16 GB; иначе p3 остаётся 4 GB (запас под замену видео)
#
# Параметры: <image.img|.img.gz> [OUT=indicator-hd-base-YYYYMMDD.img.gz]
# Окружение: TMPDIR — где разворачивать копию (размер карты, ~16 GB).

set -euo pipefail

IMG_IN="${1:?Использование: $0 <image.img|image.img.gz> [OUT]}"
OUT="${2:-indicator-hd-base-$(date +%Y%m%d).img.gz}"
PLACEHOLDER_HOST="indicator-hd-unprovisioned"

for tool in losetup kpartx blkid e2fsck parted gzip; do
    command -v "$tool" >/dev/null || { echo "  ❌  нет $tool — см. apt-get install в шапке"; exit 1; }
done

WORK_DIR="$(mktemp -d)"
WORK_IMG="${WORK_DIR}/work.img"
MNT="${WORK_DIR}/mnt"
LOOP_DEV=""

cleanup() {
    set +e
    for m in "${MNT}/data" "${MNT}/root" "${MNT}/boot"; do
        mountpoint -q "$m" && umount "$m"
    done
    if [ -n "${LOOP_DEV}" ]; then
        kpartx -dv "${LOOP_DEV}" >/dev/null 2>&1
        losetup -d "${LOOP_DEV}" >/dev/null 2>&1
    fi
    rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

fail() { echo "  ❌  $*"; exit 1; }

wpa_template() {
    cat << 'WPA'
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1
country=RU
WPA
}

echo "  💾  Копируем образ во временную рабочую область..."
case "${IMG_IN}" in
    *.gz) gunzip -c "${IMG_IN}" > "${WORK_IMG}" ;;
    *)    cp "${IMG_IN}" "${WORK_IMG}" ;;
esac

echo "  🔌  losetup + kpartx (P-36)..."
LOOP_DEV="$(losetup -f --show "${WORK_IMG}")"
kpartx -av "${LOOP_DEV}"
sleep 1
MAPPER="/dev/mapper/$(basename "${LOOP_DEV}")"
for n in 1 2 3; do
    [ -b "${MAPPER}p${n}" ] || fail "${MAPPER}p${n} не найден — ожидается 3-раздельная SD"
done
[ "$(blkid -o value -s LABEL "${MAPPER}p3")" = "data" ] || fail "p3 без LABEL=data"

echo "  🔧  e2fsck -f p2/p3 перед правкой (снимок мог быть с «грязной» ФС)..."
e2fsck -f -y "${MAPPER}p2" || [ $? -le 1 ] || fail "e2fsck p2"
e2fsck -f -y "${MAPPER}p3" || [ $? -le 1 ] || fail "e2fsck p3"

mkdir -p "${MNT}/boot" "${MNT}/root" "${MNT}/data"
mount "${MAPPER}p1" "${MNT}/boot"
mount "${MAPPER}p2" "${MNT}/root"
mount "${MAPPER}p3" "${MNT}/data"
B="${MNT}/boot"; R="${MNT}/root"; D="${MNT}/data"

echo ""
echo "  ── Проверки golden image ─────────────────────────────────"
grep -qE "^LABEL=data[[:space:]]+/data[[:space:]]" "$R/etc/fstab" || fail "fstab: нет LABEL=data /data (setup_pi.sh не прогнан?)"
grep -qE "^/data/wpa_supplicant.conf[[:space:]]" "$R/etc/fstab"   || fail "fstab: нет bind-mount wpa_supplicant.conf"
DATA_LINE="$(grep -m1 -nE '^LABEL=data[[:space:]]' "$R/etc/fstab" | cut -d: -f1)"
BIND_LINE="$(grep -m1 -nE '^/data/[^[:space:]]+[[:space:]].*bind' "$R/etc/fstab" | cut -d: -f1)"
[ "${DATA_LINE}" -lt "${BIND_LINE}" ] || fail "fstab: LABEL=data должен идти ДО bind-монтов"
FB="$R/home/pi/indicator/scripts/first_boot.sh"
[ -f "$FB" ] || fail "нет $FB (just pi::deploy-scripts)"
grep -q "WPA_SEED" "$FB"        || fail "first_boot.sh на устройстве без WiFi seed — задеплойте актуальный"
grep -q 'indicator-hd-' "$FB"   || fail "first_boot.sh на устройстве без префикса indicator-hd-"
[ -L "$R/etc/systemd/system/multi-user.target.wants/indicator-firstboot.service" ] \
    || [ -L "$R/etc/systemd/system/indicator.target.wants/indicator-firstboot.service" ] \
    || echo "  ⚠️   indicator-firstboot.service не найден в *.wants — проверьте just pi::enable-services"
echo "  ✅  fstab, first_boot.sh — ок"

echo ""
echo "  ── p1 /boot ──────────────────────────────────────────────"
sed -i 's| boot=overlay||' "$B/cmdline.txt"
# initramfs мог склеиться с предыдущей строкой, если не было финального \n
sed -i -E 's/^(.+)initramfs [^ ]+ followkernel$/\1/; /^initramfs /d' "$B/config.txt"
[ -z "$(tail -c 1 "$B/config.txt")" ] || echo >> "$B/config.txt"
rm -f "$B/wpa_supplicant.conf" "$B/ssh" "$B/ssh.txt"
echo "  cmdline.txt: $(cat "$B/cmdline.txt")"
grep -q init_resize "$B/cmdline.txt" && fail "init_resize.sh в cmdline.txt — растянет p2 на новой карте"
echo "  ✅  overlayfs OFF, seed/ssh-флаги убраны"

echo ""
echo "  ── p2 rootfs ─────────────────────────────────────────────"
echo "${PLACEHOLDER_HOST}" > "$R/etc/hostname"
sed -i "/^127\.0\.1\.1[[:space:]]/d" "$R/etc/hosts"
echo "127.0.1.1 ${PLACEHOLDER_HOST}" >> "$R/etc/hosts"
rm -f "$R"/etc/ssh/ssh_host_*
: > "$R/etc/machine-id"
rm -f "$R/var/lib/dbus/machine-id"
# файл под bind-mount: на «голом» p2 лежит исходный conf с bring-up сетью
wpa_template > "$R/etc/wpa_supplicant/wpa_supplicant.conf"
chmod 600 "$R/etc/wpa_supplicant/wpa_supplicant.conf"
rm -rf "$R"/var/log/journal/*
find "$R/var/log" -type f \( -name '*.gz' -o -name '*.[0-9]' -o -name '*.old' \) -delete
find "$R/var/log" -type f -exec truncate -s 0 {} +
rm -f "$R/home/pi/.bash_history" "$R/root/.bash_history" "$R/home/pi/.ssh/known_hosts"
rm -f "$R"/var/cache/apt/archives/*.deb "$R"/var/cache/apt/*.bin
rm -rf "$R"/tmp/* "$R"/var/tmp/*
echo "  ✅  hostname=${PLACEHOLDER_HOST}, SSH keys, machine-id, WiFi, логи, history, apt cache"

if [ -s "$R/home/pi/.ssh/authorized_keys" ]; then
    echo "  ℹ️   authorized_keys оставлен (доступ для just pi::update-*):"
    awk '{print "        " $NF}' "$R/home/pi/.ssh/authorized_keys"
fi

echo ""
echo "  ── p3 /data ──────────────────────────────────────────────"
rm -f "$D/first_boot_done"
wpa_template > "$D/wpa_supplicant.conf"
chmod 600 "$D/wpa_supplicant.conf"
echo "  ✅  first_boot_done снят, WiFi credentials сброшены"
for d in pi_nku_configs resources sounds videos; do
    [ -d "$D/$d" ] || fail "/data/$d отсутствует"
done
[ -f "$D/videos/output.mp4" ] || echo "  ⚠️   /data/videos/output.mp4 отсутствует"
(cd "$D" && du -sh -- *) | sed 's/^/        /'

sync
umount "$D" "$R" "$B"

if command -v zerofree >/dev/null; then
    echo ""
    echo "  🧹  zerofree p2/p3 (улучшает сжатие)..."
    zerofree "${MAPPER}p2"
    zerofree "${MAPPER}p3"
else
    echo "  ⚠️   zerofree не установлен — образ сожмётся хуже"
fi

kpartx -dv "${LOOP_DEV}"
losetup -d "${LOOP_DEV}"
LOOP_DEV=""

# dd снимает карту целиком (16 GB карта ≈ 14.7 GiB), а разделы кончаются на
# ~12.3 GiB. Хвост — нули, но Etcher требует карту не меньше файла образа:
# 16 GB карта другого производителя может оказаться на сотни MB меньше.
LAST_END="$(parted -sm "${WORK_IMG}" unit s print | awk -F: '$1 ~ /^[0-9]+$/ {e=$3} END {gsub("s","",e); print e}')"
[ -n "${LAST_END}" ] || fail "не удалось определить конец последнего раздела"
OLD_BYTES="$(stat -c%s "${WORK_IMG}")"
NEW_BYTES=$(( (LAST_END + 1) * 512 ))
if [ "${NEW_BYTES}" -lt "${OLD_BYTES}" ]; then
    truncate -s "${NEW_BYTES}" "${WORK_IMG}"
    echo "  ✂️   Хвост после p3 обрезан: $(( OLD_BYTES / 1048576 )) → $(( NEW_BYTES / 1048576 )) MiB"
fi

echo ""
echo "  📦  Сжимаем → ${OUT}..."
gzip -c "${WORK_IMG}" > "${OUT}"

echo "  ✅  Готово: ${OUT}"
echo "  ℹ️   Дальше: inspect_image.sh → shrink_image.sh → прошивка на чистую карту"
echo "      → проверка по FLASHING_GUIDE (hostname indicator-hd-*, overlayfs ON, WiFi из /boot)"
