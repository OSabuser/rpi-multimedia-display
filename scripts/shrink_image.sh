#!/usr/bin/env bash
#
# shrink_image.sh — ужимает раздел p3 (/data, ext4, последний раздел) уже
# собранного Gold Master образа и обрезает сам файл образа под новый размер.
#
# Расширяет закрытую процедуру P-36 / Phase 6 Блок A (losetup -Pf + kpartx +
# e2fsck + resize2fs + parted resizepart на macOS через Docker) с "живого"
# устройства на уже готовый .img.gz.
#
# ⚠️  НЕ обкатан на реальном образе автора — это отправная точка в стиле
#     ваших существующих скриптов, а не проверенное решение. Первый прогон
#     делайте пошагово (блок за блоком), сверяя вывод `parted print` с
#     ожиданиями, как у вас принято, прежде чем доверять посчитанным секторам.
#
# ЗАПУСК: внутри привилегированного Docker-контейнера на macOS
# (losetup -P не создаёт partition devices напрямую в Docker Desktop, P-36):
#
#   docker run --rm -it --privileged \
#       -v "$(pwd)":/work -w /work \
#       ubuntu:22.04 bash
#   apt-get update && apt-get install -y kpartx parted e2fsprogs gzip
#   ./shrink_image.sh indicator-base-v1.0.0.img.gz 4096
#
# Работает ТОЛЬКО с копией во временной директории — исходный .img.gz
# не трогается и не перезаписывается.
#
# Предположение (проверьте перед прогоном): таблица разделов msdos/MBR,
# p3 — последний раздел на диске. Так у всех Raspberry Pi OS образов на
# базе Buster в этом проекте.

set -euo pipefail

IMG_IN="${1:?Использование: $0 <image.img|image.img.gz> <NEW_P3_SIZE_MB> [MARGIN_MB]}"
NEW_P3_MB="${2:?Нужен новый размер p3 в МБ, например 4096}"
MARGIN_MB="${3:-256}"
SECTOR_SIZE=512

WORK_DIR="$(mktemp -d)"
WORK_IMG="${WORK_DIR}/work.img"
LOOP_DEV=""

cleanup() {
    set +e
    if [ -n "${LOOP_DEV}" ]; then
        kpartx -dv "${LOOP_DEV}" >/dev/null 2>&1
        losetup -d "${LOOP_DEV}" >/dev/null 2>&1
    fi
    rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

echo "  💾  Копируем образ во временную рабочую область..."
case "${IMG_IN}" in
    *.gz) gunzip -c "${IMG_IN}" > "${WORK_IMG}" ;;
    *)    cp "${IMG_IN}" "${WORK_IMG}" ;;
esac

echo "  🔌  losetup + kpartx (P-36)..."
LOOP_DEV="$(losetup -Pf --show "${WORK_IMG}")"
kpartx -av "${LOOP_DEV}"
sleep 1  # device-mapper нодам нужен момент на появление

MAPPER_P3="/dev/mapper/$(basename "${LOOP_DEV}")p3"
[ -b "${MAPPER_P3}" ] || { echo "  ❌  ${MAPPER_P3} не найден — проверьте раскладку образа"; exit 1; }

echo "  🔎  Текущая таблица разделов (сверьте p3 = последний раздел):"
parted -sm "${LOOP_DEV}" unit s print

P3_START="$(parted -sm "${LOOP_DEV}" unit s print | awk -F: '$1==3{gsub("s","",$2); print $2}')"
[ -n "${P3_START}" ] || { echo "  ❌  Не удалось определить начало p3"; exit 1; }

echo "  🔧  e2fsck -f перед resize (обязателен)..."
e2fsck -f -y "${MAPPER_P3}"

echo "  📏  Минимальный размер p3 по факту занятых данных:"
resize2fs -P "${MAPPER_P3}"
echo "  ℹ️   Если запрошенные ${NEW_P3_MB}M меньше минимума выше — остановитесь и увеличьте NEW_P3_MB."

echo "  📏  resize2fs ${MAPPER_P3} → ${NEW_P3_MB}M..."
resize2fs "${MAPPER_P3}" "${NEW_P3_MB}M"

echo "  🔌  Отключаем kpartx-маппинги перед правкой таблицы разделов..."
kpartx -dv "${LOOP_DEV}"

NEW_P3_SECTORS=$(( NEW_P3_MB * 1024 * 1024 / SECTOR_SIZE ))
NEW_END_SECTOR=$(( P3_START + NEW_P3_SECTORS - 1 ))

echo "  ✂️   parted resizepart 3 → ${NEW_END_SECTOR}s..."
parted ---pretend-input-tty "${LOOP_DEV}" resizepart 3 "${NEW_END_SECTOR}s" Yes

losetup -d "${LOOP_DEV}"
LOOP_DEV=""

NEW_TOTAL_BYTES=$(( (NEW_END_SECTOR + 1) * SECTOR_SIZE + MARGIN_MB * 1024 * 1024 ))
echo "  ✂️   truncate образа до $(( NEW_TOTAL_BYTES / 1024 / 1024 )) MiB (запас ${MARGIN_MB}M)..."
truncate -s "${NEW_TOTAL_BYTES}" "${WORK_IMG}"

BASE="$(basename "${IMG_IN}")"
BASE="${BASE%.gz}"
BASE="${BASE%.img}"
OUT="${BASE}-shrunk.img.gz"

echo "  📦  Пересжимаем → ${OUT}..."
gzip -c "${WORK_IMG}" > "${OUT}"

echo "  ✅  Готово: ${OUT}"
echo "  ℹ️   Дальше: прошить balenaEtcher → just pi::smoke + check-resources,"
echo "      проверить именно на картах, где раньше не влезало, прежде чем"
echo "      считать образ новой рабочей версией."
