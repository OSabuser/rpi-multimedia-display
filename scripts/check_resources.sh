#!/usr/bin/env bash
# scripts/check_resources.sh
#
# Валидация ресурсов Lift Indicator на Pi.
# Проверяет наличие, ненулевой размер и корректность PNG всех необходимых файлов.
#
# Использование:
#   С хоста: just pi::check-resources
#
# Выход:
#   0 — все ресурсы на месте
#   1 — есть ошибки (список выведен в stderr)

set -euo pipefail

# ─── Аргументы ───────────────────────────────────────────────────────────────

RESOURCES_DIR="/data/resources"
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

check_wav() {
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

    # Проверить WAV/RIFF сигнатуру (первые 4 байта: 52 49 46 46 = "RIFF")
    local sig
    sig=$(xxd -p -l 4 "$path" 2>/dev/null || hexdump -e '4/1 "%02x"' -n 4 "$path" 2>/dev/null || echo "")
    if [[ "$sig" != "52494646" ]]; then
        fail "INVALID WAV: $label → $path (sig: $sig)"
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


# ─── 2. Arrows ───────────────────────────────────────────────────────────────

echo "── Arrows ──────────────────────────────────────────────"
check_png "$RESOURCES_DIR/arrows/up.png"   "arrows/up.png"
check_png "$RESOURCES_DIR/arrows/down.png" "arrows/down.png"
echo ""

# ─── 3. Weights (load_0 – load_15) ───────────────────────────────────────────

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

# ─── 4. Modes ────────────────────────────────────────────────────────────────

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
    ["MODE_UPS_MALFUNCTION(9)"]="modes/ups_malfunction.png"
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

# ─── 6. Sounds ───────────────────────────────────────────────────────────────
#
# Маппинг из src/domain/sound_map.c (Phase 5).
# Структура:
#   6a. Event sounds  (8 файлов) — прямой маппинг sound_t → WAV
#   6b. Floor words   (3 файла)  — «этаж», «подвал», «минус»
#   6c. Numbers       (1–20)     — числовые слова
#   6d. Tens          (5 файлов) — составные десятки для этажей 21–49
#   6e. Fallbacks     (2 файла)  — на случай неизвестного/>49 этажа
#   6f. Music         (7 файлов) — фоновые треки mus1–mus7

echo "── Sounds ──────────────────────────────────────────────"

SOUNDS_DIR="$(dirname "$RESOURCES_DIR")/sounds"

if [[ ! -d "$SOUNDS_DIR" ]]; then
    fail "SOUNDS DIR MISSING: $SOUNDS_DIR"
    echo ""
else

# 6a. Event sounds
echo "  ── 6a. Event sounds ──"
check_wav "$SOUNDS_DIR/up.wav"       "SOUND_UP         → up.wav"
check_wav "$SOUNDS_DIR/down.wav"     "SOUND_DOWN       → down.wav"
check_wav "$SOUNDS_DIR/closing.wav"  "SOUND_CLOSING    → closing.wav"
check_wav "$SOUNDS_DIR/opening.wav"  "SOUND_OPENING    → opening.wav"
check_wav "$SOUNDS_DIR/overload.wav" "SOUND_OVERLOAD   → overload.wav"
check_wav "$SOUNDS_DIR/fire.wav"     "SOUND_FIRE_ALARM → fire.wav"
check_wav "$SOUNDS_DIR/g_double.wav" "SOUND_DONT_WORK  → g_double.wav"
check_wav "$SOUNDS_DIR/button.wav"   "SOUND_BUTTON     → button.wav"

# 6b. Floor announcement words
echo "  ── 6b. Floor words ──"
check_wav "$SOUNDS_DIR/floor.wav"  "floor.wav  (суффикс «этаж»)"
check_wav "$SOUNDS_DIR/podval.wav" "podval.wav (суффикс «подвал»)"
check_wav "$SOUNDS_DIR/minus.wav"  "minus.wav  (префикс «минус»)"

# 6c. Number words 1–20
echo "  ── 6c. Numbers 1–20 ──"
MISSING_NUMS=""
for i in $(seq 1 20); do
    path="$SOUNDS_DIR/${i}.wav"
    CHECKED=$((CHECKED + 1))
    if [[ ! -f "$path" ]]; then
        fail "MISSING: ${i}.wav"
        MISSING_NUMS="$MISSING_NUMS $i"
    elif [[ $(stat -c%s "$path" 2>/dev/null || stat -f%z "$path") -eq 0 ]]; then
        fail "EMPTY:   ${i}.wav"
        MISSING_NUMS="$MISSING_NUMS $i"
    fi
done
if [[ -z "$MISSING_NUMS" ]]; then
    ok "All 20 number WAVs present (1.wav–20.wav)"
fi

# 6d. Composite tens (для этажей 21–49)
echo "  ── 6d. Tens composites (21–49) ──"
check_wav "$SOUNDS_DIR/20-.wav" "20-.wav  (этажи 21–29)"
check_wav "$SOUNDS_DIR/30.wav"  "30.wav   (этаж 30)"
check_wav "$SOUNDS_DIR/30-.wav" "30-.wav  (этажи 31–39)"
check_wav "$SOUNDS_DIR/40.wav"  "40.wav   (этаж 40)"
check_wav "$SOUNDS_DIR/40-.wav" "40-.wav  (этажи 41–49)"

# 6e. Fallbacks
echo "  ── 6e. Fallbacks ──"
check_wav "$SOUNDS_DIR/g_triple.wav" "g_triple.wav (fallback: этаж >49)"
check_wav "$SOUNDS_DIR/g_single.wav" "g_single.wav (fallback: FLOOR_UNKNOWN)"

# 6f. Music tracks
echo "  ── 6f. Music (mus1–mus7) ──"
MISSING_MUS=""
for i in $(seq 1 7); do
    path="$SOUNDS_DIR/mus${i}.wav"
    CHECKED=$((CHECKED + 1))
    if [[ ! -f "$path" ]]; then
        fail "MISSING: mus${i}.wav"
        MISSING_MUS="$MISSING_MUS $i"
    elif [[ $(stat -c%s "$path" 2>/dev/null || stat -f%z "$path") -eq 0 ]]; then
        fail "EMPTY:   mus${i}.wav"
        MISSING_MUS="$MISSING_MUS $i"
    fi
done
if [[ -z "$MISSING_MUS" ]]; then
    ok "All 7 music tracks present (mus1.wav–mus7.wav)"
fi

fi  # end: SOUNDS_DIR exists
echo ""

# ─── 6.5 Rust утилиты ────────────────────────────────────────────────────────

echo "── Rust utilities ──────────────────────────────────────"
IND_DIR="/home/pi/indicator"

for TOOL in pi_nku_sync pi_nku_menu; do
    CHECKED=$((CHECKED + 1))
    path="$IND_DIR/$TOOL"
    if [[ ! -f "$path" ]]; then
        fail "MISSING: $TOOL → $path"
    elif [[ ! -x "$path" ]]; then
        fail "NOT EXECUTABLE: $TOOL → $path"
    else
        ok "$TOOL"
    fi
done
echo ""


# ─── 6.6 Boot splash ─────────────────────────────────────────────────────────

echo "── Boot splash ─────────────────────────────────────────"
SPLASH_PATH="/home/pi/indicator/splash/splash.jpg"
CHECKED=$((CHECKED + 1))

if [[ ! -f "$SPLASH_PATH" ]]; then
    fail "MISSING: splash.jpg → $SPLASH_PATH"
elif [[ ! -s "$SPLASH_PATH" ]]; then
    fail "EMPTY: splash.jpg → $SPLASH_PATH"
else
    # Проверить JPEG-сигнатуру (первые 3 байта: FF D8 FF)
    SIG=$(xxd -p -l 3 "$SPLASH_PATH" 2>/dev/null || \
          hexdump -e '3/1 "%02x"' -n 3 "$SPLASH_PATH" 2>/dev/null || echo "")
    if [[ "$SIG" == "ffd8ff" ]]; then
        SIZE=$(stat -c%s "$SPLASH_PATH" 2>/dev/null || stat -f%z "$SPLASH_PATH")
        ok "splash.jpg (${SIZE} bytes)"
    else
        fail "INVALID JPEG: splash.jpg (sig: $SIG)"
    fi
fi
echo ""

# ─── 7. Configs ──────────────────────────────────────────────────────────────

echo "── Configs ─────────────────────────────────────────────"

DATA_DIR="/data"
NKU_DIR="$DATA_DIR/pi_nku_configs"

check_toml() {
    local path="$1"
    local label="$2"
    local required_section="$3"   # опционально
    local required_key="$4"       # опционально
    CHECKED=$((CHECKED + 1))

    if [[ ! -f "$path" ]]; then
        fail "MISSING: $label → $path"; return
    fi
    if [[ ! -s "$path" ]]; then
        fail "EMPTY: $label → $path"; return
    fi
    if [[ -n "${required_section:-}" ]] && ! grep -q "^\[${required_section}\]" "$path"; then
        fail "MISSING SECTION [$required_section]: $label → $path"; return
    fi
    if [[ -n "${required_key:-}" ]] && ! grep -q "^${required_key}" "$path"; then
        fail "MISSING KEY '${required_key}': $label → $path"; return
    fi
    ok "$label"
}

# nku_scheme.toml — критичный: indicator и утилиты читают его
check_toml "$NKU_DIR/nku_scheme.toml"  "nku_scheme.toml"  "soundvolume"   "current"
check_toml "$NKU_DIR/nku_scheme.toml"  "  → [musicvolume]"  "musicvolume" "current"
check_toml "$NKU_DIR/nku_scheme.toml"  "  → [loadcapacity]" "loadcapacity" "current"

# pi_scheme.toml — UART параметры
check_toml "$NKU_DIR/pi_scheme.toml"   "pi_scheme.toml"   "device"        "current"
check_toml "$NKU_DIR/pi_scheme.toml"   "  → [baudrate]"   "baudrate"      "current"

# menu_style.toml — нужен только rpi_menu, не критичен для indicator
check_toml "$NKU_DIR/menu_style.toml"  "menu_style.toml"  "colors"        ""

# video.toml
check_toml "$NKU_DIR/video.toml"   "video.toml"       "video"         "win_w"

# renderer.toml
check_toml "$NKU_DIR/renderer.toml" "renderer.toml"            "renderer"         "resources_dir"
check_toml "$NKU_DIR/renderer.toml" "  → [slot.digit_left]"    "slot.digit_left"  "x"
check_toml "$NKU_DIR/renderer.toml" "  → [slot.notification]"  "slot.notification" "y"

check_toml "$NKU_DIR/media_ingest.toml" "media_ingest.toml" "paths" "mount_point"
echo ""

# ─── 8. Filesystem ───────────────────────────────────────────────────────────

echo "── Filesystem ──────────────────────────────────────────"

IND="/home/pi/indicator"

check_writable() {
    local path="$1" label="$2"
    CHECKED=$((CHECKED + 1))
    if touch "${path}/.write_test" 2>/dev/null && rm "${path}/.write_test"; then
        ok "${label}: writable"
    else
        fail "${label}: NOT writable or not mounted"
    fi
}

check_bind_mount() {
    local path="$1"
    CHECKED=$((CHECKED + 1))
    if mountpoint -q "${path}" 2>/dev/null; then
        ok "bind mount active: ${path}"
    else
        fail "bind mount NOT active: ${path}"
    fi
}

check_writable "/data"                 "/data"
check_writable "/data/pi_nku_configs"  "/data/pi_nku_configs"
check_writable "/data/resources"       "/data/resources"
check_writable "/data/sounds"          "/data/sounds"
check_writable "/data/videos"          "/data/videos"

check_bind_mount "${IND}/pi_nku_configs"
check_bind_mount "${IND}/resources"
check_bind_mount "${IND}/sounds"
check_bind_mount "${IND}/videos"

echo ""

CHECKED=$((CHECKED + 1))
if [ -f "/data/videos/output.mp4" ]; then
    SIZE=$(stat -c%s /data/videos/output.mp4 2>/dev/null || stat -f%z /data/videos/output.mp4)
    ok "output.mp4 ($(( SIZE / 1024 / 1024 )) MB)"
else
    fail "MISSING: /data/videos/output.mp4"
fi

# ─── 9. Notifications ────────────────────────────────────────────────────────

echo "── Notifications (SPRITE_NOTIFICATION, z=5) ────────────"

NOTIF_DIR="$RESOURCES_DIR/notifications"

if [[ ! -d "$NOTIF_DIR" ]]; then
    fail "NOTIFICATIONS DIR MISSING: $NOTIF_DIR"
else
    check_png "$NOTIF_DIR/notif_found.png"      "notifications/notif_found.png"
    check_png "$NOTIF_DIR/notif_processing.png" "notifications/notif_processing.png"
    check_png "$NOTIF_DIR/notif_success.png"    "notifications/notif_success.png"
    check_png "$NOTIF_DIR/notif_no_video.png"   "notifications/notif_no_video.png"
    check_png "$NOTIF_DIR/notif_eject.png"      "notifications/notif_eject.png"
    check_png "$NOTIF_DIR/notif_error.png"      "notifications/notif_error.png"
    check_png "$NOTIF_DIR/notif_no_mcu.png"     "notifications/notif_no_mcu.png"
    check_png "$NOTIF_DIR/notif_mcu_ok.png"     "notifications/notif_mcu_ok.png"
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
