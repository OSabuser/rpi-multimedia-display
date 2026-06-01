#!/usr/bin/env bash
# scripts/check_resources.sh
#
# Валидация ресурсов Lift Indicator на Pi.
# Проверяет наличие, ненулевой размер и корректность PNG всех необходимых файлов.
#
# Использование:
#   На Pi:   bash /home/pi/indicator/scripts/check_resources.sh [--dir /path/to/resources]
#   С хоста: just pi::check-resources
#
# Выход:
#   0 — все ресурсы на месте
#   1 — есть ошибки (список выведен в stderr)

set -euo pipefail

# ─── Аргументы ───────────────────────────────────────────────────────────────

RESOURCES_DIR="/home/pi/indicator/resources"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir) RESOURCES_DIR="$2"; shift 2 ;;
        *)     echo "Unknown arg: $1" >&2; exit 1 ;;
    esac
done

# ─── Счётчики ────────────────────────────────────────────────────────────────

ERRORS=0
CHECKED=0

# ─── Вспомогательные функции ─────────────────────────────────────────────────

ok()   { echo "  ✅  $1"; }
fail() { echo "  ❌  $1" >&2; ERRORS=$((ERRORS + 1)); }
info() { echo "  ℹ️   $1"; }

check_png() {
    local path="$1"
    local label="$2"
    CHECKED=$((CHECKED + 1))

    if [[ ! -f "$path" ]]; then
        fail "MISSING: $label → $path"
        return
    fi

    local size
    size=$(stat -c%s "$path" 2>/dev/null || stat -f%z "$path")
    if [[ "$size" -eq 0 ]]; then
        fail "EMPTY:   $label → $path"
        return
    fi

    # Проверить PNG-сигнатуру (первые 8 байт: 89 50 4E 47 0D 0A 1A 0A)
    local sig
    sig=$(xxd -p -l 8 "$path" 2>/dev/null || hexdump -e '8/1 "%02x"' -n 8 "$path" 2>/dev/null || echo "")
    if [[ "$sig" != "89504e470d0a1a0a" ]]; then
        fail "INVALID PNG: $label → $path (sig: $sig)"
        return
    fi

    ok "$label"
}

# ─── Начало проверки ──────────────────────────────────────────────────────────

echo ""
echo "═══════════════════════════════════════════════════════"
echo "  Resource check: $RESOURCES_DIR"
echo "═══════════════════════════════════════════════════════"
echo ""

if [[ ! -d "$RESOURCES_DIR" ]]; then
    echo "  ❌  Resources directory not found: $RESOURCES_DIR" >&2
    exit 1
fi

# ─── 1. Background ───────────────────────────────────────────────────────────

echo "── Background ──────────────────────────────────────────"
check_png "$RESOURCES_DIR/BACK.png" "BACK.png"
echo ""

# ─── 2. Chars (0–37) ─────────────────────────────────────────────────────────

echo "── Chars (0–37, total 38) ──────────────────────────────"
MISSING_CHARS=""
for i in $(seq 0 37); do
    path="$RESOURCES_DIR/chars/${i}.png"
    CHECKED=$((CHECKED + 1))
    if [[ ! -f "$path" ]]; then
        fail "MISSING: chars/${i}.png"
        MISSING_CHARS="$MISSING_CHARS $i"
    elif [[ $(stat -c%s "$path" 2>/dev/null || stat -f%z "$path") -eq 0 ]]; then
        fail "EMPTY:   chars/${i}.png"
    else
        sig=$(xxd -p -l 8 "$path" 2>/dev/null || echo "")
        if [[ "$sig" != "89504e470d0a1a0a" ]]; then
            fail "INVALID PNG: chars/${i}.png"
        fi
    fi
done
if [[ -z "$MISSING_CHARS" ]]; then
    ok "All 38 char PNGs present and valid"
fi
echo ""

# ─── 3. Arrows ───────────────────────────────────────────────────────────────

echo "── Arrows ──────────────────────────────────────────────"
check_png "$RESOURCES_DIR/arrows/up.png"   "arrows/up.png"
check_png "$RESOURCES_DIR/arrows/down.png" "arrows/down.png"
echo ""

# ─── 4. Weights (load_0 – load_15) ───────────────────────────────────────────

echo "── Weights (load_0–load_15, total 16) ──────────────────"
MISSING_WEIGHTS=""
for i in $(seq 0 15); do
    path="$RESOURCES_DIR/weights/load_${i}.png"
    CHECKED=$((CHECKED + 1))
    if [[ ! -f "$path" ]]; then
        fail "MISSING: weights/load_${i}.png"
        MISSING_WEIGHTS="$MISSING_WEIGHTS $i"
    elif [[ $(stat -c%s "$path" 2>/dev/null || stat -f%z "$path") -eq 0 ]]; then
        fail "EMPTY:   weights/load_${i}.png"
    else
        sig=$(xxd -p -l 8 "$path" 2>/dev/null || echo "")
        if [[ "$sig" != "89504e470d0a1a0a" ]]; then
            fail "INVALID PNG: weights/load_${i}.png"
        fi
    fi
done
if [[ -z "$MISSING_WEIGHTS" ]]; then
    ok "All 16 weight PNGs present and valid"
fi
echo ""

# ─── 5. Modes ────────────────────────────────────────────────────────────────

echo "── Modes ───────────────────────────────────────────────"
# Маппинг: indicator_mode_t → файл (MODE_CONN_LOST и MODE_NORMAL → нет файла)
declare -A MODE_FILES=(
    ["MODE_FIRE_ALARM(1)"]="modes/firealarm.png"
    ["MODE_MALFUNCTION(2)"]="modes/malfunction.png"
    ["MODE_LOADING(3)"]="modes/loading.png"
    ["MODE_OVERLOAD(4)"]="modes/overload.png"
    ["MODE_SEIS_ALARM(5)"]="modes/seismo.png"
    ["MODE_FIREMANS(6)"]="modes/fireman.png"
    ["MODE_SERVICE(7)"]="modes/inspection.png"
    ["MODE_EVACUATION(8)"]="modes/evacuation.png"
    ["MODE_UPS_MALFUNCTION(9)"]="modes/malfunction.png"  # переиспользует malfunction
    ["MODE_DISPATCH_CALL(100)"]="modes/calling.png"
    ["MODE_DISPATCH_ANSWER(101)"]="modes/talking.png"
)

# Уникальные файлы (malfunction.png используется дважды — проверяем один раз)
declare -A SEEN_MODES=()
for label in "${!MODE_FILES[@]}"; do
    file="${MODE_FILES[$label]}"
    if [[ -z "${SEEN_MODES[$file]+_}" ]]; then
        SEEN_MODES["$file"]=1
        check_png "$RESOURCES_DIR/$file" "$label → $file"
    else
        ok "$label → $file (shared, already checked)"
        CHECKED=$((CHECKED + 1))
    fi
done
echo ""

# ─── 6. Sounds (Phase 5 — предупреждение, не ошибка) ─────────────────────────

echo "── Sounds (Phase 5) ────────────────────────────────────"
SOUND_FILES=(
    "s_gong.wav:SOUND_DING(1)"
    "s_up.wav:SOUND_UP(2)"
    "s_down.wav:SOUND_DOWN(3)"
    "s_close.wav:SOUND_CLOSING(4)"
    "s_open.wav:SOUND_OPENING(5)"
    "s_overload.wav:SOUND_OVERLOAD(6)"
    "s_firealarm.wav:SOUND_FIRE_ALARM(7)"
    "s_dont_work.wav:SOUND_DONT_WORK(8)"
    "s_button.wav:SOUND_BUTTON(9)"
)

SOUNDS_DIR="$(dirname "$RESOURCES_DIR")/sounds"
SOUND_WARNINGS=0
for entry in "${SOUND_FILES[@]}"; do
    file="${entry%%:*}"
    label="${entry##*:}"
    path="$SOUNDS_DIR/$file"
    if [[ ! -f "$path" ]]; then
        echo "  ⚠️   MISSING (Phase 5): $label → $path"
        SOUND_WARNINGS=$((SOUND_WARNINGS + 1))
    else
        ok "$label → sounds/$file"
    fi
done
if [[ $SOUND_WARNINGS -gt 0 ]]; then
    info "$SOUND_WARNINGS sound file(s) missing — OK until Phase 5"
fi
echo ""

# ─── Итог ────────────────────────────────────────────────────────────────────

echo "═══════════════════════════════════════════════════════"
echo "  Checked: $CHECKED files"

if [[ $ERRORS -eq 0 ]]; then
    echo "  Result:  ✅  ALL RESOURCES OK"
    echo "═══════════════════════════════════════════════════════"
    echo ""
    exit 0
else
    echo "  Result:  ❌  $ERRORS ERROR(S) FOUND" >&2
    echo "═══════════════════════════════════════════════════════"
    echo ""
    exit 1
fi