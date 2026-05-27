# =============================================================================
# just/pi.just — операции с Raspberry Pi (выполняется на хосте)
#
# Требует: ssh-ключ настроен (just pi::setup-ssh)
# Конфигурация: .env → PI_HOST, PI_USER, PI_DIR
# =============================================================================

BUILD_DIR := env('BUILD_DIR',  justfile_directory() / '..' / 'build')
PI_HOST   := env('PI_HOST',   'indicator-01.local')
PI_USER   := env('PI_USER',   'pi')
PI_DIR    := env('PI_DIR',    '/home/pi/indicator')
_pi       := PI_USER + "@" + PI_HOST

JUST_MIN   := "1.36.0"
UV_MIN     := "0.4.0"
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

    check_tool "just"   "just"   "{{JUST_MIN}}"
    check_tool "docker" "docker" "{{DOCKER_MIN}}"

    if command -v ssh &>/dev/null; then ok "ssh found"
    else fail "ssh not found"; fi

    if command -v rsync &>/dev/null; then ok "rsync found"
    else warn "rsync not found — deploy will use scp fallback"; fi

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
    echo "  📤  Copying key to {{_pi}}..."
    ssh-copy-id -i "${KEY}.pub" "{{_pi}}"
    echo "  ✅  SSH key installed on Pi"

[doc('Скопировать /opt/vc с Pi для Docker sysroot (один раз после setup-pi)')]
[group('setup')]
fetch-sysroot:
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p build-env/pi-sysroot
    echo "  📥  Syncing /opt/vc from {{_pi}}..."
    rsync -avz --progress "{{_pi}}:/opt/vc" build-env/pi-sysroot/
    echo "  ✅  Sysroot ready: build-env/pi-sysroot/vc/"

[doc('Собрать Docker-образ с кросс-компилятором (один раз, или после изменения Dockerfile)')]
[group('setup')]
build-image:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -d "build-env/pi-sysroot/vc" ]; then
        echo "  ❌  Sysroot not found. Run: just pi::fetch-sysroot"
        exit 1
    fi
    echo "  🐳  Building Docker image indicator-build..."
    docker build -t indicator-build build-env/
    echo "  ✅  Docker image ready: indicator-build"

[doc('Первичная настройка Pi: пакеты, ALSA, systemd units')]
[group('setup')]
setup-pi:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  🍓  Running setup_pi.sh on {{_pi}}..."
    ssh "{{_pi}}" "sudo bash -s" < scripts/setup_pi.sh
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

# =============================================================================
# ГРУППА: deploy — деплой на Pi
# =============================================================================

[doc('Задеплоить бинари + ресурсы + systemd units на Pi')]
[group('deploy')]
deploy:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -f "{{BUILD_DIR}}/pi/indicator" ]; then
        echo "  ❌  Binaries not found. Run: just build::pi"
        exit 1
    fi
    echo "  📤  Deploying to {{_pi}}:{{PI_DIR}}..."

    # Бинари
    rsync -avz --progress \
        "{{BUILD_DIR}}/pi/indicator" \
        "{{BUILD_DIR}}/pi/media_ingest" \
        "{{_pi}}:{{PI_DIR}}/"

    # Скрипты
    rsync -avz --progress scripts/smoke_test.sh \
        "{{_pi}}:{{PI_DIR}}/scripts/"

    # systemd units
    rsync -avz --progress deploy/ "{{_pi}}:/tmp/indicator-deploy/"
    ssh "{{_pi}}" "sudo cp /tmp/indicator-deploy/*.service /etc/systemd/system/ && \
                   sudo systemctl daemon-reload"

    echo "  ✅  Deploy complete"

[doc('Задеплоить только бинари (быстро, без ресурсов)')]
[group('deploy')]
deploy-bin:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  📤  Deploying binaries to {{_pi}}:{{PI_DIR}}..."
    rsync -avz --progress \
        "{{BUILD_DIR}}/pi/indicator" \
        "{{BUILD_DIR}}/pi/media_ingest" \
        "{{_pi}}:{{PI_DIR}}/"
    echo "  ✅  Binaries deployed"

[doc('Задеплоить ресурсы (PNG, WAV, видео) на Pi')]
[group('deploy')]
deploy-resources resources_dir:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  📤  Syncing resources from {{resources_dir}}..."
    rsync -avz --progress "{{resources_dir}}/" "{{_pi}}:{{PI_DIR}}/resources/"
    echo "  ✅  Resources deployed"

# =============================================================================
# ГРУППА: control — управление сервисами на Pi
# =============================================================================

[doc('Перезапустить indicator')]
[group('control')]
restart:
    ssh "{{_pi}}" "sudo systemctl restart indicator.service"
    @echo "  ✅  indicator restarted"

[doc('Статус всех сервисов индикатора')]
[group('control')]
status:
    ssh "{{_pi}}" "systemctl status indicator.service media-ingest.service --no-pager"

[doc('Запустить все сервисы')]
[group('control')]
start:
    ssh "{{_pi}}" "sudo systemctl start indicator.target"
    @echo "  ✅  indicator.target started"

[doc('Остановить все сервисы')]
[group('control')]
stop:
    ssh "{{_pi}}" "sudo systemctl stop indicator.target"
    @echo "  ⏹️   indicator.target stopped"

# =============================================================================
# ГРУППА: logs — просмотр логов
# =============================================================================

[doc('Хвост логов indicator в реальном времени')]
[group('logs')]
logs:
    ssh "{{_pi}}" "journalctl -u indicator -f --no-pager"

[doc('Хвост логов media-ingest')]
[group('logs')]
logs-ingest:
    ssh "{{_pi}}" "journalctl -u media-ingest -f --no-pager"

[doc('Показать последние N строк из обоих сервисов (default 50)')]
[group('logs')]
logs-tail n="50":
    ssh "{{_pi}}" "journalctl -u indicator -u media-ingest -n {{n}} --no-pager"

# =============================================================================
# ГРУППА: util — утилиты
# =============================================================================

[doc('SSH на Pi')]
[group('util')]
ssh:
    ssh "{{_pi}}"

[doc('Запустить smoke-тест после деплоя')]
[group('util')]
smoke:
    ssh "{{_pi}}" "bash {{PI_DIR}}/scripts/smoke_test.sh"

[doc('Проверить аудио на Pi (воспроизвести тестовый файл)')]
[group('util')]
test-audio:
    ssh "{{_pi}}" "aplay /usr/share/sounds/alsa/Front_Left.wav"

[doc("Copy test video to Pi and play it")]
test-video:
    @echo "Generating test video..."
    ffmpeg -f lavfi -i testsrc=duration=5:size=600x1024:rate=25 \
           -vcodec libx264 -pix_fmt yuv420p /tmp/indicator_test.mp4 -y 2>/dev/null
    @echo "Copying to Pi..."
    scp /tmp/indicator_test.mp4 ${PI_USER}@${PI_HOST}:/tmp/test.mp4
    @echo "Playing on Pi (Ctrl+C to stop)..."
    ssh ${PI_USER}@${PI_HOST} "omxplayer --no-osd --win 0,0,600,1024 --layer 1 /tmp/test.mp4"

[doc('Показать загрузку CPU/RAM на Pi')]
[group('util')]
top:
    ssh "{{_pi}}" "top -bn1 | head -20"

[doc('Показать свободное место на SD-карте')]
[group('util')]
df:
    ssh "{{_pi}}" "df -h /"

[doc('Скопировать образ SD-карты Pi (указать диск: just pi::backup-image /dev/disk4)')]
[group('util')]
backup-image disk:
    #!/usr/bin/env bash
    set -euo pipefail
    OUT="indicator-base-$(date +%Y%m%d).img.gz"
    echo "  💾  Creating image from {{disk}} → ${OUT}..."
    echo "  ⚠️   Убедитесь что SD-карта вставлена и не примонтирована"
    sudo dd if="{{disk}}" bs=4m status=progress | gzip > "${OUT}"
    echo "  ✅  Image saved: ${OUT}"
