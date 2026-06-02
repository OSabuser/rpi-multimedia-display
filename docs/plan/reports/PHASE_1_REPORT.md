# Lift Indicator — Отчёт Фазы 1 и Вопросы Фазы 2

**Дата:** 2026-05-28
**Проект:** `rpi-multimedia-display`
**Устройство:** Raspberry Pi Zero W Rev 1.1 · ARM1176JZF-S · Debian Buster · дисплей 600×1024

---

## 1. Фаза 1 — ЗАКРЫТА ✅

**Цель фазы:** бизнес-логика написана, покрыта тестами, ASan+UBSan чисто. Ни строчки DispmanX.

### 1.1 Чеклист

| # | Пункт | Статус |
|---|---|---|
| **Domain-слой** | | |
| 1 | `src/protocol/types.h` — enum'ы синхронизированы с STM32 (`s_code_t`, `el_mode_t`, `arrows_state_t`) | ✅ |
| 2 | `src/protocol/parser.c/.h` — `protocol_parse_frame()` + `protocol_parse_payload()`, без malloc | ✅ |
| 3 | `src/domain/floor.c/.h` — `floor_decode()`, полная таблица декодирования | ✅ |
| 4 | `src/domain/sound_map.c/.h` — `sound_map_resolve()`, `sound_map_volume_percent()` | ✅ |
| 5 | `src/domain/state.c/.h` — `state_apply_frame()` → `state_update_result_t` | ✅ |
| 6 | `src/config/config.c/.h` — `config_load()` из `nku_scheme.toml`, дефолты, валидация | ✅ |
| **Тесты** | | |
| 7 | `tests/test_parser.c` — CRC-16, parse_frame, parse_payload, mode_is_valid | ✅ |
| 8 | `tests/test_floor.c` — все типы этажей, граничные случаи | ✅ |
| 9 | `tests/test_sound_map.c` — объявления 1–40, П, П1–П9, -1..-9, UP/DOWN/OVERLOAD | ✅ |
| 10 | `tests/test_state.c` — первый фрейм, diff, edge-triggered sound | ✅ |
| 11 | `tests/test_config.c` — корректный TOML, fallback, файл не найден, комментарии | ✅ |
| **Качество** | | |
| 12 | `just build::test` → 5/5 passed, 0 failed | ✅ |
| 13 | ASan + UBSan — без ошибок | ✅ |
| 14 | `just build::check-format` — все файлы отформатированы | ✅ |
| 15 | Ни одного include `bcm_host.h` / `vc_dispmanx.h` в `src/` | ✅ |
| **Сборка и деплой** | | |
| 16 | `just build::pi` → ARM32, hard-float ABI, без ошибок компилятора | ✅ |
| 17 | `./indicator` на Pi → `indicator stub` без segfault | ✅ |
| **Рабочее окружение** | | |
| 18 | clangd: автодополнение, переход по F12, два профиля (host/pi) | ✅ |
| 19 | clang-format: форматирование при сохранении в VSCode | ✅ |
| 20 | Remote GDB: breakpoint в `main()` срабатывает через F5 | ✅ |

**Критерий выполнен.**

---

### 1.2 Проблемы, возникшие в процессе, и их решения

#### P-08 — Бинарь требовал GLIBC_2.34 на Pi Buster (GLIBC 2.28)

**Симптом:** `./indicator: /lib/arm-linux-gnueabihf/libc.so.6: version 'GLIBC_2.34' not found`

**Причина:** `arm-linux-gnueabihf-gcc` из Debian Bookworm имеет `crt*.o` объекты, скомпилированные против glibc 2.34+. `CMAKE_SYSROOT` перекрывает поиск заголовков, но не меняет pre-compiled crt-файлы — их пути зашиты в GCC specs.

**Решение:** заменить кросс-компилятор на `zig cc -target arm-linux-gnueabihf -mcpu=arm1176jzf_s`. Zig компилирует собственные `crt*.o` точно под указанный target — это тот же механизм, который использует Rust/cross+zig. Никаких `CMAKE_SYSROOT` не требуется.

Wrapper `/usr/local/bin/arm-zig-cc`:
```sh
exec zig cc -target arm-linux-gnueabihf -mcpu=arm1176jzf_s "$@"
```

---

#### P-09 — Segfault до `main()` на Pi Zero W (ARMv6)

**Симптом:** `./indicator` → Segmentation fault. gdb показывает crash в `_start` до вызова `main()`.

**Причина:** устройство оказалось Pi Zero W (ARM1176JZF-S, ARMv6), а не Pi Zero 2W (Cortex-A53, ARMv8) как предполагалось. Бинарь содержал инструкции ARMv7 Thumb-2 (из `crt1.o` toolchain'а) — ARMv6 их не поддерживает.

`readelf -A ./indicator` показал:
```
Tag_CPU_arch: v7
Tag_THUMB_ISA_use: Thumb-2   ← ARMv6 не поддерживает
```

**Решение:** правильный target в zig — `arm1176jzf_s`. Дополнительные флаги в toolchain:
```cmake
set(CMAKE_C_FLAGS_INIT "-marm -mfloat-abi=hard")
```
`-marm` обязателен: Thumb-1 + hard-float VFP — неподдерживаемая комбинация в GCC (ошибка `sorry, unimplemented: Thumb-1 hard-float VFP ABI`).

---

#### P-10 — Docker-образ не собирался на Apple Silicon (aarch64)

**Симптом серии:**
- `FROM debian:buster-slim` + cmake/just как x86_64-бинари → `rosetta error: failed to open elf at /lib64/ld-linux-x86-64.so.2`
- `FROM --platform=linux/amd64` → lint warning `FromPlatformFlagConstDisallowed`
- `clangd` не находился в Buster apt (пакет называется иначе в разных версиях)

**Решение:** переход на Ubuntu 22.04 с LLVM official repo:
```dockerfile
FROM ubuntu:22.04
# LLVM apt repo → clangd-17, clang-format-17, clang-tidy-17
# cmake и just скачиваются как arch-aware бинари (x86_64 / aarch64)
# zig распаковывается в /opt/zig/, симлинк в PATH
```

Zig и cmake/just теперь выбирают архитектуру автоматически через `uname -m`.

---

#### P-11 — ASan runtime отсутствовал на aarch64 в контейнере

**Симптом:** `ld: cannot find /usr/lib/llvm-17/lib/clang/17/lib/linux/libclang_rt.asan-aarch64.a`

**Причина:** пакет `clang-17` не включает ASan runtime — он в отдельном пакете.

**Решение:** добавить `libclang-rt-17-dev` в Dockerfile (по образцу референсного проекта).

---

#### P-12 — CMake не применял toolchain (старый CMakeCache.txt)

**Симптом:** `CMake Warning: Manually-specified variables were not used by the project: CMAKE_TOOLCHAIN_FILE`. Бинарь собирался под x86_64.

**Причина:** при смене toolchain'а старый `build/pi/CMakeCache.txt` содержит закэшированный путь к компилятору и игнорирует новый `CMAKE_TOOLCHAIN_FILE`.

**Решение:** добавить в `just build::_configure-pi`:
```bash
rm -rf "{{BUILD_DIR}}/pi"
cmake -B "{{BUILD_DIR}}/pi" ...
```
Принудительная очистка при каждой конфигурации. Также добавлена проверка архитектуры после сборки: `file build/pi/indicator` должен показывать ARM.

---

#### P-13 — clangd ошибки `bits/libc-header-start.h file not found` в `src/`

**Симптом:** clangd подчёркивает все системные заголовки в `src/` красным. `bits/libc-header-start.h` не найден.

**Причина:** `.clangd` направлял `src/` на `build/pi/compile_commands.json` где компилятор `arm-zig-cc`. clangd не знает где zig хранит системные заголовки (`/opt/zig/lib/libc/include/`).

**Решение:** `src/` domain-код платформонезависим — использовать `build/host/compile_commands.json`. Только `platform/dispmanx/` остаётся на `build/pi`.

```yaml
# .clangd
If:
  PathMatch: "src/.*\\.(c|h)"
CompileFlags:
  CompilationDatabase: build/host
```

---

#### P-14 — Remote GDB: Connection timed out на `localhost:3333`

**Симптом:** gdb-multiarch в devcontainer не подключается к `localhost:3333`.

**Причина:** SSH-туннель открыт на **хосте** (macOS). `localhost:3333` в контейнере — это сам контейнер, не хост.

**Решение:** использовать `host.docker.internal:3333`. Добавить в `devcontainer.json`:
```json
"runArgs": ["--add-host=host.docker.internal:host-gateway"]
```
В `launch.json`: `"miDebuggerServerAddress": "host.docker.internal:3333"`.

---

#### P-15 — SSH: `Bad configuration option: usekeychain` в контейнере

**Симптом:** `just pi::gdbserver-start` падает при первом SSH-вызове.

**Причина:** `~/.ssh/config` с хоста macOS содержит `UseKeychain yes` — опция специфична для macOS и не поддерживается Linux SSH client.

**Решение:** убрать `readonly` с SSH mount и добавить в `postCreateCommand`:
```bash
sed -i '/UseKeychain/d; /AddKeysToAgent/d' /root/.ssh/config 2>/dev/null || true
```

---

#### P-16 — `launch.json`: тип `cppdbg` требует `ms-vscode.cpptools`

**Симптом:** все поля в `launch.json` подчёркнуты: `type cppdbg is not recognized`, `miDebuggerPath is not allowed` и т.д.

**Причина:** первоначально в devcontainer был установлен `marus25.cortex-debug` (для MCU bare-metal), а не `ms-vscode.cpptools` (для Linux remote gdbserver).

**Решение:** заменить в extensions:
```diff
- "marus25.cortex-debug"
+ "ms-vscode.cpptools"
```
`cortex-debug` — для JTAG/SWD (J-Link, OpenOCD). Для Linux gdbserver нужен `cppdbg`.

---

#### P-17 — multiple definition of `main` при линковке тестов

**Симптом:** линковщик падает с `multiple definition of 'main'` при сборке `indicator_tests`.

**Причина:** каждый `test_*.c` содержит собственный `main()` + `setUp()` + `tearDown()`. Линковать их в один бинарь нельзя — это стандартная архитектура Unity.

**Решение:** заменить один executable на `foreach` в CMakeLists.txt:
```cmake
foreach(TEST_NAME test_parser test_floor test_sound_map test_state test_config)
    add_executable(${TEST_NAME} tests/${TEST_NAME}.c)
    target_link_libraries(${TEST_NAME} PRIVATE indicator_domain unity)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()
```

---

### 1.3 Уточнения протокола (получены в процессе)

В ходе фазы получены реальные enum'ы STM32. Ключевые изменения vs исходный MASTER_PLAN:

**Бинарный фрейм подтверждён:**
```
[0xAA][size][opcode][data...][CRC16_H][CRC16_L][0xBB]
```
`#STM:L%d:R%d:A%d:S%d:M%d:E#\r\n` — это `data` внутри фрейма при `opcode=0xDA`.

**`sound_t` расширен до 9 значений:**
```c
SOUND_NONE=0, SOUND_DING=1, SOUND_UP=2, SOUND_DOWN=3,
SOUND_CLOSING=4, SOUND_OPENING=5, SOUND_OVERLOAD=6,
SOUND_FIRE_ALARM=7, SOUND_DONT_WORK=8, SOUND_BUTTON=9
```

**`mode_t` — разрывный диапазон (0–9, 100, 101, 255):**
```c
MODE_NORMAL=0 .. MODE_UPS_MALFUNCTION=9,
MODE_DISPATCH_CALL=100, MODE_DISPATCH_ANSWER=101, MODE_CONN_LOST=255
```
Проверка через `mode_is_valid()`, не `<= MAX`.

**Конфиг:** только `nku_scheme.toml` (TOML), `pizero.ini` — legacy, не используется.

**STM32 отправляет только при изменении состояния.** Звук — edge-triggered.

---

### 1.4 Итоговое состояние

```
Хост (macOS)
├── just, docker, ssh, rsync
├── build-env/pi-sysroot/opt/vc/ — DispmanX headers с Pi
├── Docker-образ indicator-build (Ubuntu 22.04 + LLVM 17 + zig 0.13.0)
├── build/ — bind mount (виден на хосте и в контейнере)
└── .env — PI_HOST=indicator-01.local

Devcontainer (indicator-build)
├── arm-zig-cc           — кросс-компилятор (ARMv6, arm1176jzf_s)
├── clang-17             — host unit-тесты
├── clangd-17            — LSP (три профиля: tests/src/platform)
├── clang-format-17      — форматирование
├── gdb-multiarch        — remote debug
├── cmake 3.28 / just 1.36
└── /opt/vc              — DispmanX sysroot

Raspberry Pi Zero W (indicator-01.local)
├── ARM1176JZF-S, ARMv6, Debian Buster
├── /home/pi/indicator/
│   ├── indicator            ← ARMv6 Release-бинарь ✅
│   ├── media_ingest         ← ARMv6 Release-бинарь ✅
│   ├── indicator-debug      ← ARMv6 Debug-бинарь (-g3) ✅
│   ├── configs/device/nku_scheme.toml
│   ├── resources/, sounds/, videos/
│   └── scripts/smoke_test.sh
├── gdbserver — установлен
└── /etc/systemd/system/
    ├── indicator.service
    ├── media-ingest.service
    └── indicator.target
```

---

## 2. Открытые вопросы

Будут закрыты в Фазе 2 при интеграции UART-транспорта и реальном тестировании с STM32.

---

#### В-01 — WAV-файлы для новых звуковых событий

В `sound_map.c` три события пока имеют заглушки:

```c
SOUND_FIRE_ALARM = 7  → "g_triple.wav"  // TODO: уточнить
SOUND_DONT_WORK  = 8  → "g_double.wav"  // TODO: уточнить
SOUND_BUTTON     = 9  → "g_single.wav"  // TODO: уточнить
```

**Вопрос:** какие WAV-файлы используются для этих событий? Есть ли они в `sounds/`?

---

#### В-02 — WAV-файлы для `SOUND_CLOSING` и `SOUND_OPENING`

```c
SOUND_CLOSING = 4  → "closing.wav"  // TODO: подтвердить наличие
SOUND_OPENING = 5  → "opening.wav"  // TODO: подтвердить наличие
```

**Вопрос:** есть ли `closing.wav` и `opening.wav` в `sounds/` на устройстве?

---

#### В-03 — Логика выбора `g_triple` / `g_double` / `g_single`

В `sound_map.c` для этажей > 40 и неизвестных используется `g_triple.wav`.

**Вопрос:** когда используется `g_double.wav` и `g_single.wav`? Зависит ли выбор от контекста (тип события, этаж, режим)?

---

#### В-04 — Актуализация UART-протокола

MASTER_PLAN §12 описывает устаревшую версию протокола (текстовый формат без бинарного фрейма, старые значения mode).

В Фазе 2 при написании `src/transport/uart.c` необходимо:
- Обновить MASTER_PLAN §12 по реальным enum'ам STM32
- Подтвердить UART параметры: 115200 baud, even parity, 8N1
- Проверить корректность парсера на реальном трафике с STM32

---

## 3. Контекст для Фазы 2

При начале нового треда передать:

1. Этот документ (`PHASE_1_REPORT.md`)
2. `MASTER_PLAN.md`
3. `DEV_ARCH.md` (актуальная версия)
4. Текущие файлы: `src/protocol/types.h`, `src/protocol/parser.h/.c`, `src/transport/uart.h/.c`

**Стартовая фраза:**
> Фазы 0 и 1 закрыты. Начинаем Фазу 2 — UART transport + event loop.
> Прикладываю PHASE_1_REPORT, MASTER_PLAN, DEV_ARCH и текущие исходники.

---

*Документ сгенерирован по итогам сессии Фазы 1.*
