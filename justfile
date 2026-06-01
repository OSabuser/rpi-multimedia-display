# =============================================================================
# justfile — Lift Indicator
# Корневой оркестратор. Работает на хосте и внутри devcontainer.
#
# Быстрый старт:
#   ./bootstrap.sh        # первая настройка (хост, один раз)
#   just                  # показать все доступные команды
#   just build::test      # host unit-тесты (devcontainer)
#   just build::pi        # кросс-компиляция под Pi (devcontainer)
#   just pi::deploy       # задеплоить на Pi (хост)
# =============================================================================

set shell  := ["bash", "-euo", "pipefail", "-c"]
set export
set dotenv-load

# === Общие переменные ===

BUILD_DIR := env('BUILD_DIR', justfile_directory() / 'build')
PI_HOST   := env('PI_HOST',   'indicator-01.local')
PI_USER   := env('PI_USER',   'pi')
PI_DIR    := env('PI_DIR',    '/home/pi/indicator')

# === Модули ===

mod build 'just/build.just'   # сборка (devcontainer)
mod pi    'just/pi.just'      # Pi: деплой, SSH, настройка (хост)
mod ci    'just/ci.just'      # CI pipeline

# === Default: дерево всех команд ===

default:
    @just --list --list-submodules

# === Алиасы ===

[doc('Первичная настройка хоста после git clone')]
init:
    @just pi::bootstrap

[doc('Запустить host unit-тесты')]
test:
    @just build::test


[doc('Полный цикл: сборка в Docker → деплой на Pi → проверка ресурсов → рестарт (запускать с ХОСТА)')]
ship:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "  🔨  Building inside indicator-build container..."
    docker run --rm \
        -v "{{justfile_directory()}}:/project" \
        -v "{{justfile_directory()}}/build:/project/build" \
        -w /project \
        indicator-build \
        just build::pi
    echo "  📤  Deploying to Pi..."
    just pi::deploy
    echo "  🔍  Checking resources on Pi..."
    just pi::check-resources
    echo "  🔄  Restarting indicator..."
    just pi::restart
    echo "  ✅  Ship complete"
