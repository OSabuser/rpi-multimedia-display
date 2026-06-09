#!/usr/bin/env bash
# scripts/gen_notifications.sh
#
# Генерирует PNG-уведомления для SPRITE_NOTIFICATION (DispmanX z=5).
#
# Формат: 600×150 px, RGBA, тёмный полупрозрачный фон, белый текст.
# Размещение на экране 600×1024: y=874 (нижние 150 px).
#
# Зависимости: imagemagick (convert), шрифт DejaVu-Sans-Bold.
# Запуск: bash scripts/gen_notifications.sh [OUTDIR]
#   OUTDIR по умолчанию: deploy/resources/notifications/
#
set -euo pipefail

OUTDIR="${1:-deploy/resources/notifications}"
FONT="DejaVu-Sans-Bold"
SIZE="600x150"
POINTSIZE=36
BG_COLOR="rgba(0,0,0,0.72)"

GREEN="\033[0;32m"; RED="\033[0;31m"; RESET="\033[0m"

if ! command -v convert &>/dev/null; then
    echo -e "${RED}  ❌  imagemagick not found. Install: apt-get install -y imagemagick${RESET}"
    exit 1
fi

if ! convert -list font 2>/dev/null | grep -q "${FONT}"; then
    echo -e "${RED}  ❌  Font '${FONT}' not found. Install: apt-get install -y fonts-dejavu-core${RESET}"
    exit 1
fi

mkdir -p "${OUTDIR}"

gen() {
    local file="$1"
    local text="$2"
    convert \
        -size "${SIZE}" xc:none \
        -fill "${BG_COLOR}" \
        -draw 'rectangle 0,0,599,149' \
        -font "${FONT}" \
        -pointsize "${POINTSIZE}" \
        -fill white \
        -gravity Center \
        -annotate 0 "${text}" \
        "PNG32:${OUTDIR}/${file}"
    echo -e "  ${GREEN}✅${RESET}  ${file}"
}

echo ""
echo "  Generating notification PNGs → ${OUTDIR}/"
echo ""

# USB media ingest notifications
gen notif_found.png       "Найдены видеофайлы"
gen notif_processing.png  "Идёт обработка..."
gen notif_success.png     "Успех!"
gen notif_no_video.png    "Видеофайлы не найдены"
gen notif_eject.png       "Извлеките носитель"
gen notif_error.png       "Ошибка обработки"

# MCU connection notifications
gen notif_no_mcu.png      "Нет связи с MCU"
gen notif_mcu_ok.png      "Связь с MCU установлена"

echo ""
echo "  8 files written to ${OUTDIR}/"
echo ""
