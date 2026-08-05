#!/usr/bin/env bash
#
# inspect_image.sh — read-only проверка разметки и занятого места в
# Gold Master образе. Ничего не меняет: parted print (без resizepart),
# e2fsck -fn (проверка без исправлений), resize2fs -P (print-only).
# Безопасно гонять сколько угодно раз до shrink_image.sh.
#
# ЗАПУСК: тот же привилегированный контейнер, что и для shrink_image.sh.
#
#   ./inspect_image.sh indicator-base-v1.0.0.img.gz

set -euo pipefail

IMG_IN="${1:?Использование: $0 <image.img|image.img.gz>}"
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

echo "  💾  Распаковываем/копируем во временную область..."
case "${IMG_IN}" in
    *.gz) gunzip -c "${IMG_IN}" > "${WORK_IMG}" ;;
    *)    cp "${IMG_IN}" "${WORK_IMG}" ;;
esac

TOTAL_BYTES="$(stat -c%s "${WORK_IMG}")"
TOTAL_SECTORS=$(( TOTAL_BYTES / SECTOR_SIZE ))
TOTAL_GIB="$(awk -v b="${TOTAL_BYTES}" 'BEGIN{printf "%.2f", b/1073741824}')"

echo ""
echo "  ═══════════════════════════════════════════════════════════"
echo "  Файл образа (распакованный .img): ${IMG_IN}"
echo "  Размер: ${TOTAL_BYTES} bytes  (~${TOTAL_GIB} GiB)  ${TOTAL_SECTORS} секторов×512B"
echo "  ═══════════════════════════════════════════════════════════"

echo ""
echo "  🔌  losetup + kpartx..."
LOOP_DEV="$(losetup -Pf --show "${WORK_IMG}")"
kpartx -av "${LOOP_DEV}"
sleep 1

echo ""
echo "  ═══════════════════════════════════════════════════════════"
echo "  Таблица разделов — человекочитаемо (MiB):"
echo "  ═══════════════════════════════════════════════════════════"
parted -s "${LOOP_DEV}" unit MiB print

echo ""
echo "  ═══════════════════════════════════════════════════════════"
echo "  Таблица разделов — точные секторы (для арифметики shrink_image.sh):"
echo "  ═══════════════════════════════════════════════════════════"
parted -sm "${LOOP_DEV}" unit s print

BASE="$(basename "${LOOP_DEV}")"

for N in 1 2 3; do
    MAPPER="/dev/mapper/${BASE}p${N}"
    [ -b "${MAPPER}" ] || continue

    echo ""
    echo "  ───────────────────────────────────────────────────────────"
    echo "  Раздел p${N} (${MAPPER}):"
    echo "  ───────────────────────────────────────────────────────────"

    FSTYPE="$(blkid -o value -s TYPE "${MAPPER}" 2>/dev/null || echo unknown)"
    echo "  Тип ФС: ${FSTYPE}"

    case "${FSTYPE}" in
        ext4|ext3|ext2)
            echo "  → e2fsck -fn (проверка БЕЗ исправлений)..."
            e2fsck -fn "${MAPPER}" || true
            echo ""
            echo "  → dumpe2fs -h (геометрия ФС)..."
            dumpe2fs -h "${MAPPER}" 2>/dev/null | grep -E "Block count|Block size|Free blocks|Inode count|Free inodes"
            echo ""
            echo "  → resize2fs -P (МИНИМАЛЬНЫЙ размер по занятым данным — печатает, не меняет):"
            resize2fs -P "${MAPPER}"
            ;;
        vfat|fat32)
            if command -v fsck.vfat &>/dev/null; then
                echo "  → fsck.vfat -n (проверка БЕЗ исправлений)..."
                fsck.vfat -n "${MAPPER}" 2>/dev/null || true
            else
                echo "  (fsck.vfat не установлен — не критично, p1 не участвует в shrink)"
            fi
            ;;
        *)
            echo "  (неизвестный/неподдерживаемый тип — пропускаем детальную проверку)"
            ;;
    esac
done

echo ""
echo "  ✅  Проверка завершена. Образ НЕ изменён (kpartx -dv + losetup -d при выходе)."
echo "  ℹ️   Для shrink_image.sh: NEW_P3_MB берите с запасом НАД 'минимальным"
echo "      размером' p3 из resize2fs -P выше — не впритык."
