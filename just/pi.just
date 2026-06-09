# =============================================================================
# just/pi.just — операции с Raspberry Pi (выполняется на хосте)
#
# Требует: ssh-ключ настроен (just pi::setup-ssh)
# Конфигурация: .env → PI_HOST, PI_USER, PI_DIR
# =============================================================================

BUILD_DIR := env('BUILD_DIR', justfile_directory() / '..' / 'build')
PI_HOST := env('PI_HOST', 'indicator-01.local')
PI_USER := env('PI_USER', 'pi')
PI_DIR := env('PI_DIR', '/home/pi/indicator')
_pi := PI_USER + "@" + PI_HOST
JUST_MIN := "1.36.0"
DOCKER_MIN := "24.0.0"

set working-directory := '..'

_default:
    @just --list --justfile just/pi.just --unsorted

# =============================================================================
# ГРУППА: setup — первичная настройка окружения
# =============================================================================

[doc('Проверить версии зависимостей хоста (just, docker, ssh)')]
[group('setup')]
check-deps:
    #!/usr/bin/env bash
    set -euo pipefail

    BOLD="\033[1m"; GREEN="\033[0;32m"; RED="\033[0;31m"; YELLOW="\033[1;33m"; RESET="\033[0m"
    ERRORS=0; WARNINGS=0

    ok()   { echo -e "  ${GREEN}✅  ${1}${RESET}"; }
    fail() { echo -e "  ${RED}❌  ${1}${RESET}"; ERRORS=$((ERRORS+1)); }
    warn() { echo -e "  ${YELLOW}⚠️   ${1}${RESET}"; WARNINGS=$((WARNINGS+1)); }

    _uname="$(uname -s)"
    case "${_uname}" in
        Linux*)               PLATFORM="linux"   ;;
        Darwin*)              PLATFORM="macos"   ;;
        MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
        *)                    PLATFORM="unknown" ;;
    esac

    semver_ge() {
        local a="$1" b="$2"; local a1 a2 a3 b1 b2 b3
        IFS='.' read -r a1 a2 a3 <<< "${a}"; IFS='.' read -r b1 b2 b3 <<< "${b}"
        a1="${a1//[^0-9]/}"; a2="${a2//[^0-9]/}"; a3="${a3//[^0-9]/}"
        b1="${b1//[^0-9]/}"; b2="${b2//[^0-9]/}"; b3="${b3//[^0-9]/}"
        [[ "${a1:-0}" -gt "${b1:-0}" ]] && return 0; [[ "${a1:-0}" -lt "${b1:-0}" ]] && return 1
        [[ "${a2:-0}" -gt "${b2:-0}" ]] && return 0; [[ "${a2:-0}" -lt "${b2:-0}" ]] && return 1
        [[ "${a3:-0}" -ge "${b3:-0}" ]] && return 0; return 1
    }

    check_tool() {
        local name=$1 cmd=$2 min_ver=$3
        if ! command -v "$cmd" &>/dev/null; then fail "$name not found"; return; fi
        local actual; actual=$("$cmd" --version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
        if semver_ge "${actual:-0.0.0}" "$min_ver"; then ok "$name $actual (>= $min_ver)"
        else fail "$name $actual — need >= $min_ver"; fi
    }

    echo ""
    echo -e "${BOLD}  Platform: ${PLATFORM}${RESET}"
    echo ""
    echo -e "${BOLD}  Checking host dependencies...${RESET}"
    echo ""

    check_tool "just"   "just"   "{{ JUST_MIN }}"
    check_tool "docker" "docker" "{{ DOCKER_MIN }}"

    if command -v ssh &>/dev/null; then ok "ssh found"
    else fail "ssh not found"; fi

    if command -v rsync &>/dev/null; then ok "rsync found"
    else warn "rsync not found — deploy will fail"; fi

    echo ""
    if [[ $ERRORS -gt 0 ]]; then
        echo -e "  ${RED}${BOLD}$ERRORS error(s). Fix before continuing.${RESET}"; exit 1
    else
        echo -e "  ${GREEN}${BOLD}All dependencies satisfied.${RESET}"
    fi
    echo ""

[doc('Настроить SSH-ключ для Pi (один раз)')]
[group('setup')]
setup-ssh:
    #!/usr/bin/env bash
    set -euo pipefail
    KEY="${HOME}/.ssh/id_ed25519"
    if [ ! -f "$KEY" ]; then
        echo "  🔑  Generating SSH key..."
        ssh-keygen -t ed25519 -C "indicator-dev" -f "$KEY" -N ""
    else
        echo "  ✅  SSH key already exists: $KEY"
    fi
    echo "  📤  Copying key to {{ _pi }}..."
    ssh-copy-id -i "${KEY}.pub" "{{ _pi }}"
    echo "  ✅  SSH key installed on Pi"

[doc('Скопировать /opt/vc с Pi для Docker sysroot (один раз после setup-pi)')]
[group('setup')]
fetch-sysroot:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  📦  Fetching /opt/vc from {{ _pi }}..."
    rsync -av --delete \
        "{{ PI_USER }}@{{ PI_HOST }}:/opt/vc/" \
        "build-env/pi-sysroot/opt/vc/"
    echo "  ✅  Sysroot ready: build-env/pi-sysroot/opt/vc/"

[doc('Собрать Docker-образ с кросс-компилятором')]
[group('setup')]
build-image:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -d "build-env/pi-sysroot/opt/vc" ]; then
        echo "  ❌  Sysroot not found. Run: just pi::fetch-sysroot"
        exit 1
    fi
    echo "  🐳  Building Docker image indicator-build (linux/amd64)..."
    docker build -t indicator-build build-env/
    echo "  ✅  Docker image ready: indicator-build"

[doc('Первичная настройка Pi: пакеты, ALSA, /data layout, systemd')]
[group('setup')]
setup-pi:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  🍓  Running setup_pi.sh on {{ _pi }}..."
    ssh "{{ _pi }}" "sudo bash -s" < scripts/setup_pi.sh
    echo "  ✅  Pi setup complete"

[doc('Полная первичная настройка хоста: check-deps → setup-ssh → fetch-sysroot → build-image')]
[group('setup')]
bootstrap:
    #!/usr/bin/env bash
    set -euo pipefail
    echo ""
    echo "  === Bootstrap: step 1/4 — check-deps ==="
    just pi::check-deps
    echo "  === Bootstrap: step 2/4 — setup-ssh ==="
    just pi::setup-ssh
    echo "  === Bootstrap: step 3/4 — fetch-sysroot ==="
    just pi::fetch-sysroot
    echo "  === Bootstrap: step 4/4 — build-image ==="
    just pi::build-image
    echo ""
    echo "  ✅  Bootstrap complete."
    echo "  Next: open project in VSCode → 'Reopen in Container'"
    echo ""

[doc('Скопировать libpng + zlib с Pi для кросс-компиляции (один раз)')]
[group('setup')]
fetch-png-sysroot:
    #!/usr/bin/env bash
    set -euo pipefail

    DEST="build-env/pi-sysroot/usr"
    PI="{{ PI_USER }}@{{ PI_HOST }}"

    echo "  📦  Checking libpng-dev on Pi..."
    ssh "${PI}" "dpkg -s libpng-dev &>/dev/null || sudo apt-get install -y libpng-dev zlib1g-dev"

    echo "  📦  Fetching headers from ${PI}..."
    mkdir -p "${DEST}/include"
    rsync -avzL \
        "${PI}:/usr/include/png.h" \
        "${PI}:/usr/include/pngconf.h" \
        "${PI}:/usr/include/pnglibconf.h" \
        "${PI}:/usr/include/zlib.h" \
        "${PI}:/usr/include/zconf.h" \
        "${DEST}/include/"

    echo "  📦  Fetching shared libs (.so) from ${PI}..."
    mkdir -p "${DEST}/lib/arm-linux-gnueabihf"
    rsync -avzL \
        "${PI}:/usr/lib/arm-linux-gnueabihf/libpng16.so" \
        "${PI}:/usr/lib/arm-linux-gnueabihf/libz.so" \
        "${DEST}/lib/arm-linux-gnueabihf/"

    echo ""
    echo "  ✅  PNG sysroot ready:"
    echo "    headers: $(ls ${DEST}/include/png.h ${DEST}/include/zlib.h)"
    echo "    libs:    $(ls ${DEST}/lib/arm-linux-gnueabihf/libpng16.so ${DEST}/lib/arm-linux-gnueabihf/libz.so)"
    echo ""
    echo "  ℹ️  Используем динамическую линковку (.so)."
    echo "     libpng16.a из Pi содержит ARMv7 код и несовместима с ARMv6 (Pi Zero W)."

[doc('Установить gdbserver на Pi (один раз)')]
[group('setup')]
setup-gdbserver:
    ssh "{{ _pi }}" "sudo apt-get install -y gdbserver && gdbserver --version"
    @echo "  ✅  gdbserver installed on Pi"

[doc('Включить все systemd-сервисы индикатора (один раз после deploy-systemd)')]
[group('setup')]
enable-services:
    #!/usr/bin/env bash
    set -euo pipefail
    ssh "{{_pi}}" "sudo systemctl enable \
        indicator.target \
        indicator.service \
        indicator-setup.service \
        indicator-firstboot.service \
        media-ingest.service"
    echo "  ✅  Services enabled"

# =============================================================================
# ГРУППА: deploy — деплой на Pi
# =============================================================================

[doc('Задеплоить systemd units на Pi')]
[group('deploy')]
deploy-systemd:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  📤  Deploying systemd units to {{ _pi }}..."
    rsync -avz --progress deploy/systemd/ "{{ _pi }}:/tmp/indicator-systemd/"
    ssh "{{ _pi }}" "sudo cp /tmp/indicator-systemd/* /etc/systemd/system/ && \
                   sudo systemctl daemon-reload && \
                   \
                   if sudo systemctl is-enabled aplay.service &>/dev/null; then \
                       sudo systemctl disable --now aplay.service && \
                       echo '  🔄  Migrated: aplay.service → i2s-silence.service'; \
                   fi && \
                   \
                   sudo systemctl enable i2s-silence.service \
                        indicator.target \
                        indicator.service \
                        indicator-setup.service \
                        indicator-firstboot.service \
                        media-ingest.service \
                        2>/dev/null || true"
    echo "  ✅  systemd units deployed and enabled"
    echo "  ℹ️   Перезапустить keepalive: just pi::restart-audio"
    echo "  ℹ️   Перезапустить всё:       just pi::restart"

[doc('Задеплоить скрипты (run_setup.sh, first_boot.sh, smoke_test.sh, check_resources.sh)')]
[group('deploy')]
deploy-scripts:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  📤  Deploying scripts to {{ _pi }}:{{ PI_DIR }}/scripts/..."
    ssh "{{ _pi }}" "mkdir -p {{ PI_DIR }}/scripts"
    rsync -avz --progress \
        scripts/smoke_test.sh \
        scripts/check_resources.sh \
        scripts/run_setup.sh \
        scripts/first_boot.sh \
        "{{ _pi }}:{{ PI_DIR }}/scripts/"
    ssh "{{ _pi }}" "chmod +x {{ PI_DIR }}/scripts/*.sh"
    echo "  ✅  Scripts deployed"

[doc('Задеплоить только основные C-бинари: indicator + media_ingest (быстро)')]
[group('deploy')]
deploy-bin:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -f "{{ BUILD_DIR }}/pi/indicator" ]; then
        echo "  ❌  Binaries not found. Run: just build::pi"
        exit 1
    fi
    echo "  📤  Deploying binaries to {{ _pi }}:{{ PI_DIR }}..."
    rsync -avz --progress \
        "{{ BUILD_DIR }}/pi/indicator" \
        "{{ BUILD_DIR }}/pi/media_ingest" \
        "{{ _pi }}:{{ PI_DIR }}/"
    echo "  ✅  Binaries deployed"

[doc('Задеплоить диагностические утилиты (uart_rx_dump + MUp-rpi0)')]
[group('deploy')]
deploy-tools:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -f "{{ BUILD_DIR }}/pi/uart_rx_dump" ]; then
        echo "  ❌  uart_rx_dump not found. Run: just build::pi-dump"
        exit 1
    fi
    echo "  📤  Deploying tools to {{ _pi }}:{{ PI_DIR }}/tools/..."
    ssh "{{ _pi }}" "mkdir -p {{ PI_DIR }}/tools"
    rsync -avz --progress \
        "{{ BUILD_DIR }}/pi/uart_rx_dump" \
        "{{ _pi }}:{{ PI_DIR }}/tools/"
    rsync -avz --progress \
        deploy/tools/MUp-rpi0 \
        "{{ _pi }}:{{ PI_DIR }}/tools/"
    echo "  ✅  Tools deployed"

[doc('Задеплоить Rust-утилиты (pi_nku_sync, pi_nku_menu) → PI_DIR/')]
[group('deploy')]
deploy-rust:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -f "deploy/bin/pi_nku_sync" ] || [ ! -f "deploy/bin/pi_nku_menu" ]; then
        echo "  ❌  Rust binaries not found in deploy/bin/"
        echo "     Build from third_party/config_toolset/ and copy to deploy/bin/"
        exit 1
    fi
    echo "  📤  Deploying Rust tools to {{ _pi }}:{{ PI_DIR }}/..."
    rsync -avz --progress \
        deploy/bin/pi_nku_sync \
        deploy/bin/pi_nku_menu \
        "{{ _pi }}:{{ PI_DIR }}/"
    echo "  ✅  Rust tools deployed"

[doc('Задеплоить device-конфиги → /data/pi_nku_configs')]
[group('deploy')]
deploy-configs:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  📤  Deploying configs to {{ _pi }}:/data/pi_nku_configs/..."
    rsync -avz --progress deploy/configs/ "{{ _pi }}:/data/pi_nku_configs/"
    echo "  ✅  Configs deployed"

[doc('Задеплоить PNG-ресурсы → /data/resources')]
[group('deploy')]
deploy-resources:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  📤  Syncing resources to {{ _pi }}:/data/resources/..."
    rsync -avz --progress deploy/resources/ "{{ _pi }}:/data/resources/"
    echo "  ✅  Resources deployed"

[doc('Задеплоить WAV-звуки → /data/sounds')]
[group('deploy')]
deploy-sounds:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  📤  Syncing sounds to {{ _pi }}:/data/sounds/..."
    rsync -avz --progress deploy/sounds/ "{{ _pi }}:/data/sounds/"
    echo "  ✅  Sounds deployed"

[doc('Задеплоить boot splash → /boot/splash.png + PI_DIR/splash/')]
[group('deploy')]
deploy-splash:
    #!/usr/bin/env bash
    set -euo pipefail
    SPLASH="deploy/boot/splash.jpg"
    if [ ! -f "$SPLASH" ]; then
        echo "  ❌  $SPLASH not found"
        exit 1
    fi
    echo "  📤  Deploying splash to {{ _pi }}..."
    rsync -avz "$SPLASH" "{{ _pi }}:/tmp/splash_src.jpg"
    ssh "{{ _pi }}" "sudo convert /tmp/splash_src.jpg /boot/splash.png && \
                   rm /tmp/splash_src.jpg"
    ssh "{{ _pi }}" "mkdir -p {{ PI_DIR }}/splash"
    rsync -avz "$SPLASH" "{{ _pi }}:{{ PI_DIR }}/splash/splash.jpg"
    echo "  ✅  splash deployed"

[doc('Задеплоить фоновое видео → /data/videos/output.mp4')]
[group('deploy')]
deploy-video:
    #!/usr/bin/env bash
    set -euo pipefail
    VIDEO="deploy/video/output.mp4"
    if [ ! -f "$VIDEO" ]; then
        echo "  ❌  $VIDEO not found"
        echo "     Положи видеофайл в deploy/video/output.mp4"
        exit 1
    fi
    echo "  📤  Deploying video to {{ _pi }}:/data/videos/..."
    rsync -avz --progress "$VIDEO" "{{ _pi }}:/data/videos/output.mp4"
    echo "  ✅  Video deployed"

[doc('Быстрый деплой: бинари + скрипты + systemd + конфиги + Rust (без ресурсов и видео)')]
[group('deploy')]
deploy:
    #!/usr/bin/env bash
    set -euo pipefail
    echo ""
    echo "  🚀  Deploy: bin + tools + rust + scripts + systemd + configs"
    echo "  ──────────────────────────────────────────────────────────"
    just pi::deploy-bin
    just pi::deploy-tools
    just pi::deploy-rust
    just pi::deploy-scripts
    just pi::deploy-systemd
    just pi::deploy-configs
    echo "  ──────────────────────────────────────────────────────────"
    echo "  ✅  Deploy complete"
    echo "  ℹ️   Ресурсы/звуки/видео: just pi::deploy-full"
    echo ""

[doc('Полный деплой: deploy + ресурсы + звуки + splash + видео (свежая установка)')]
[group('deploy')]
deploy-full:
    #!/usr/bin/env bash
    set -euo pipefail
    echo ""
    echo "  🚀  Full deploy: all assets included"
    echo "  ──────────────────────────────────────────────────────────"
    just pi::deploy
    just pi::deploy-resources
    just pi::deploy-sounds
    just pi::deploy-splash
    just pi::deploy-video
    echo "  ──────────────────────────────────────────────────────────"
    echo "  ✅  Full deploy complete"
    echo ""

[doc('Проверить ресурсы, конфиги и bind-монты на Pi')]
[group('deploy')]
check-resources:
    #!/usr/bin/env bash
    set -euo pipefail
    PI="{{ PI_USER }}@{{ PI_HOST }}"
    echo "  🔍  Checking deployment on ${PI}..."
    echo ""
    scp scripts/check_resources.sh "${PI}:/tmp/check_resources.sh"
    ssh "${PI}" "bash /tmp/check_resources.sh --dir /data/resources"

[doc('Задеплоить debug сборку indicator для отладки на хосте')]
[group('deploy')]
deploy-debug:
    rsync -avz "{{ BUILD_DIR }}/pi-debug/indicator" "{{ _pi }}:{{ PI_DIR }}/indicator-debug"
    @echo "  ✅  Debug binary deployed"

# =============================================================================
# ГРУППА: control — управление сервисами на Pi
# =============================================================================

[doc('Перезапустить сервисы indicator & media ingest')]
[group('control')]
restart:
    ssh "{{_pi}}" "sudo systemctl restart indicator.service media-ingest.service"
    @echo "  ✅  indicator + media-ingest restarted"

[doc('Перезапустить только I2S keepalive (без остановки indicator)')]
[group('control')]
restart-audio:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  🔊  Restarting I2S keepalive..."
    ssh "{{ _pi }}" "sudo systemctl restart i2s-silence.service"
    ssh "{{ _pi }}" "systemctl is-active i2s-silence.service && \
                   echo '  ✅  i2s-silence.service active' || \
                   echo '  ❌  i2s-silence.service failed — check: journalctl -u i2s-silence'"

[doc('Статус всех сервисов индикатора')]
[group('control')]
status:
    ssh "{{ _pi }}" "systemctl status indicator.service media-ingest.service --no-pager"

[doc('Запустить сервисы indicator & media ingest')]
[group('control')]
start:
    ssh "{{_pi}}" "sudo systemctl start indicator.service media-ingest.service"
    @echo "  ✅  indicator + media-ingest started"

[doc('Остановить сервисы indicator & media ingest')]
[group('control')]
stop:
    ssh "{{_pi}}" "sudo systemctl stop indicator.service media-ingest.service"
    @echo "  ⏹️   indicator + media-ingest stopped"

# =============================================================================
# ГРУППА: logs — просмотр логов
# =============================================================================

[doc('Хвост логов indicator в реальном времени')]
[group('logs')]
logs:
    ssh "{{ _pi }}" "journalctl -u indicator -f --no-pager"

[doc('Хвост логов media-ingest')]
[group('logs')]
logs-ingest:
    ssh "{{ _pi }}" "journalctl -u media-ingest -f --no-pager"

[doc('Показать последние N строк из обоих сервисов: just pi::logs-tail 30')]
[group('logs')]
logs-tail n="50":
    ssh "{{_pi}}" "journalctl -u indicator -u media-ingest -n {{n}} --no-pager"

# =============================================================================
# ГРУППА: diag — диагностика UART и железа
# =============================================================================

[doc('Запустить uart_rx_dump: остановить indicator, слушать фреймы, поднять обратно')]
[group('diag')]
uart-dump:
    #!/usr/bin/env bash
    set -euo pipefail
    DUMP="{{ PI_DIR }}/tools/uart_rx_dump"

    ssh "{{ _pi }}" "test -f ${DUMP}" || {
        echo "  ❌  uart_rx_dump not found. Run: just build::pi-dump && just pi::deploy-tools"
        exit 1
    }

    echo "  ⏹️   Stopping indicator.service..."
    ssh "{{ _pi }}" "sudo systemctl stop indicator.service"

    echo "  🔍  Starting uart_rx_dump (Ctrl+C to stop)..."
    echo "  ─────────────────────────────────────────────"

    # trap: поднять indicator обратно при выходе (Ctrl+C или ошибка)
    trap 'echo ""; echo "  🔼  Restarting indicator.service..."; \
          ssh "{{ _pi }}" "sudo systemctl start indicator.service"; \
          echo "  ✅  indicator restarted"' EXIT

    # TODO (Шаг 1): читать port/baudrate из /data/pi_nku_configs/pi_scheme.toml
    ssh -t "{{ _pi }}" "${DUMP} /dev/ttyAMA0 115200"

[doc('Запустить uart_rx_dump без остановки indicator (только для наблюдения)')]
[group('diag')]
uart-dump-passive:
    #!/usr/bin/env bash
    set -euo pipefail
    DUMP="{{ PI_DIR }}/tools/uart_rx_dump"
    ssh "{{ _pi }}" "test -f ${DUMP}" || {
        echo "  ❌  uart_rx_dump not found."
        exit 1
    }
    echo "  ⚠️   indicator.service NOT stopped — порт занят, фреймы не придут"
    echo "  ℹ️   Используй just pi::dump для корректной диагностики"
    # TODO (Шаг 1): читать port/baudrate из /data/pi_nku_configs/pi_scheme.toml
    ssh -t "{{ _pi }}" "${DUMP} /dev/ttyAMA0 115200"

# =============================================================================
# ГРУППА: util — утилиты
# =============================================================================

[doc('SSH на Pi')]
[group('util')]
ssh:
    ssh "{{ _pi }}"

[doc('Запустить smoke-тест после деплоя')]
[group('util')]
test-smoke:
    ssh "{{ _pi }}" "bash {{ PI_DIR }}/scripts/smoke_test.sh"

[doc('Проверить аудио на Pi (воспроизвести тестовый файл)')]
[group('util')]
test-audio:
    ssh "{{ _pi }}" "aplay /usr/share/sounds/alsa/Front_Left.wav"

[doc('Задеплоить и запустить notif_test на Pi')]
[group('util')]
test-notif:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  ▶️   Running notif_test on Pi..."
    ssh "{{ _pi }}" "{{ PI_DIR }}/tools/notif_test"
    echo "  ✅  Done"

[doc('Сгенерировать тестовое видео и воспроизвести на Pi')]
[group('util')]
test-video:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  🎬  Generating test video..."
    ffmpeg -f lavfi -i testsrc=duration=5:size=600x1024:rate=25 \
           -vcodec libx264 -pix_fmt yuv420p /tmp/indicator_test.mp4 -y 2>/dev/null
    echo "  📤  Copying to Pi..."
    scp /tmp/indicator_test.mp4 "{{ PI_USER }}@{{ PI_HOST }}:/tmp/test.mp4"
    echo "  ▶️   Playing on Pi (Ctrl+C to stop)..."
    ssh "{{ PI_USER }}@{{ PI_HOST }}" "omxplayer --no-osd --win 0,0,600,1024 --layer 1 /tmp/test.mp4"

[doc('Показать загрузку CPU/RAM на Pi')]
[group('util')]
top:
    ssh "{{ _pi }}" "top -bn1 | head -20"

[doc('Показать свободное место на SD-карте')]
[group('util')]
df:
    ssh "{{ _pi }}" "df -h /"

[doc('Скопировать образ SD-карты Pi (указать диск: just pi::backup-image /dev/disk4)')]
[group('util')]
backup-image disk:
    #!/usr/bin/env bash
    set -euo pipefail
    OUT="indicator-base-$(date +%Y%m%d).img.gz"
    echo "  💾  Creating image from {{ disk }} → ${OUT}..."
    echo "  ⚠️   Убедитесь что SD-карта вставлена и не примонтирована"
    sudo dd if="{{ disk }}" bs=4m status=progress | gzip > "${OUT}"
    echo "  ✅  Image saved: ${OUT}"

# =============================================================================
# Remote GDB через SSH-туннель
# =============================================================================

[doc('Открыть SSH-туннель и запустить gdbserver на Pi')]
[group('diag')]
gdbserver-start:
    #!/usr/bin/env bash
    set -euo pipefail

    BINARY="{{ PI_DIR }}/indicator-debug"
    LOCAL_PORT=3333
    REMOTE_PORT=3333

    echo "  🔌  Checking indicator-debug on Pi..."
    ssh "{{ _pi }}" "test -f ${BINARY}" || {
        echo "  ❌  indicator-debug not found on Pi. Run: just pi::deploy-debug"
        exit 1
    }

    echo "  🔌  Stopping any existing gdbserver..."
    ssh "{{ _pi }}" "pkill -f gdbserver 2>/dev/null; true" || true
    sleep 1

    echo "  🚀  Starting gdbserver on Pi (port ${REMOTE_PORT})..."
    ssh "{{ _pi }}" "nohup gdbserver :${REMOTE_PORT} ${BINARY} > /tmp/gdbserver.log 2>&1 &"
    sleep 1

    if ! ssh "{{ _pi }}" "pgrep -f gdbserver > /dev/null"; then
        echo "  ❌  gdbserver failed to start. Check: ssh {{ _pi }} cat /tmp/gdbserver.log"
        exit 1
    fi
    echo "  ✅  gdbserver running on Pi:${REMOTE_PORT}"
    echo "  🔗  Tunnel: localhost:${LOCAL_PORT} → {{ _pi }}:${REMOTE_PORT}"
    echo "  ℹ️   Keep this terminal open while debugging. Ctrl+C to stop."
    echo ""
    echo "GDBSERVER READY"

    ssh -N \
        -o ExitOnForwardFailure=yes \
        -o ServerAliveInterval=5 \
        -o ServerAliveCountMax=3 \
        -L "${LOCAL_PORT}:localhost:${REMOTE_PORT}" \
        "{{ _pi }}"

[doc('Остановить gdbserver на Pi')]
[group('diag')]
gdbserver-stop:
    ssh "{{ _pi }}" "pkill -f gdbserver 2>/dev/null; true" || true
    @echo "  ⏹️   gdbserver stopped"

[doc('Смотреть stdout отлаживаемой программы')]
[group('diag')]
debug-output:
    ssh "{{ _pi }}" "tail -f /tmp/gdbserver.log"
