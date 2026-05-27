# =============================================================================
# just/build.just — сборка и тесты
# Выполняется ТОЛЬКО внутри devcontainer.
#
# Рабочая директория — корень репозитория.
# =============================================================================

BUILD_DIR := env('BUILD_DIR', justfile_directory() / '..' / 'build')

set working-directory := '..'

_default:
    @just --list --justfile just/build.just --unsorted

# =============================================================================
# ГРУППА: test — host unit-тесты (clang, ASan+UBSan)
# Не требуют Pi, не требуют DispmanX.
# =============================================================================

[private]
_configure-host:
    cmake -B "{{BUILD_DIR}}/host" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DINDICATOR_PLATFORM_DISPMANX=OFF \
        -DINDICATOR_BUILD_TESTS=ON \
        -DINDICATOR_ENABLE_ASAN=ON \
        -DINDICATOR_ENABLE_UBSAN=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

[doc('Собрать и запустить host unit-тесты (Debug + ASan/UBSan)')]
[group('test')]
test: _configure-host
    cmake --build "{{BUILD_DIR}}/host" --parallel
    cd "{{BUILD_DIR}}/host" && ctest --output-on-failure --parallel

[doc('Запустить тесты с подробным выводом')]
[group('test')]
test-verbose: _configure-host
    cmake --build "{{BUILD_DIR}}/host" --parallel
    cd "{{BUILD_DIR}}/host" && ctest -V

# =============================================================================
# ГРУППА: pi — кросс-компиляция под Raspberry Pi Zero 2W
# =============================================================================

[private]
_configure-pi:
    cmake -B "{{BUILD_DIR}}/pi" \
        -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-RPiZeroW.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DINDICATOR_PLATFORM_DISPMANX=ON \
        -DINDICATOR_BUILD_TESTS=OFF \
        -DINDICATOR_BUILD_INGEST=ON

[doc('Кросс-компилировать indicator и media_ingest под Pi')]
[group('pi')]
pi: _configure-pi
    cmake --build "{{BUILD_DIR}}/pi" --parallel
    @echo "  ✅  Build complete: {{BUILD_DIR}}/pi/"

[doc('Только indicator (без media_ingest)')]
[group('pi')]
pi-indicator: _configure-pi
    cmake --build "{{BUILD_DIR}}/pi" --target indicator --parallel

[doc('Только media_ingest')]
[group('pi')]
pi-ingest: _configure-pi
    cmake --build "{{BUILD_DIR}}/pi" --target media_ingest --parallel

[doc('Кросс-компиляция для Pi с debug-символами')]
[group('build')]
pi-debug:
    #!/usr/bin/env bash
    set -euo pipefail
    cmake -B "build/pi-debug" \
        -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-RPiZeroW.cmake \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="-g3 -O0 -fno-omit-frame-pointer" \
        -DINDICATOR_PLATFORM_DISPMANX=ON \
        -DINDICATOR_BUILD_TESTS=OFF \
        -DINDICATOR_BUILD_INGEST=OFF
    cmake --build "build/pi-debug" --parallel
    echo "  ✅  Debug build: build/pi-debug/indicator"
# =============================================================================
# ГРУППА: quality — форматирование и статический анализ
# =============================================================================

[doc('Проверить форматирование (без изменений файлов)')]
[group('quality')]
check-format:
    #!/usr/bin/env bash
    set -euo pipefail
    files=$(find src platform -name '*.c' -o -name '*.h' | sort)
    if [ -z "$files" ]; then
        echo "  ⚠️  No C files found"
        exit 0
    fi
    bad=0
    while IFS= read -r f; do
        if ! clang-format --dry-run --Werror "$f" 2>/dev/null; then
            echo "  ❌  $f"
            bad=$((bad+1))
        fi
    done <<< "$files"
    if [ $bad -eq 0 ]; then
        echo "  ✅  All files formatted correctly"
    else
        echo "  ❌  $bad file(s) need formatting — run: just build::format"
        exit 1
    fi

[doc('Применить clang-format ко всем исходникам')]
[group('quality')]
format:
    #!/usr/bin/env bash
    set -euo pipefail
    files=$(find src platform -name '*.c' -o -name '*.h' | sort)
    count=0
    while IFS= read -r f; do
        clang-format -i "$f"
        count=$((count+1))
    done <<< "$files"
    echo "  ✅  Formatted $count files"

# =============================================================================
# ГРУППА: utils
# =============================================================================

[confirm("Удалить директорию build/?")]
[doc('Удалить все артефакты сборки')]
[group('utils')]
clean:
    rm -rf "{{BUILD_DIR}}"
    @echo "  ✅  build/ removed"
