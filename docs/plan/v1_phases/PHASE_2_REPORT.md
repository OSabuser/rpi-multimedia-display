# Lift Indicator — Отчёт Фазы 2

**Дата:** 2026-05-29
**Проект:** `rpi-multimedia-display`
**Устройство:** Raspberry Pi Zero W Rev 1.1 · ARM1176JZF-S · Debian Buster · дисплей 600×1024

---

## 1. Фаза 2 — ЗАКРЫТА ✅

**Цель фазы:** демон читает бинарные фреймы от STM32 через UART, парсит payload,
логирует все события в journald. Стабильный lifecycle под systemd.

### 1.1 Чеклист

| # | Пункт | Статус |
|---|---|---|
| **Код** | | |
| 1 | `src/transport/uart.h/.c` — полная замена стаба: callback-модель, кольцевой буфер, `protocol_parse_frame()` | ✅ |
| 2 | `src/main.c` — poll-цикл: uart_fd + signalfd + timerfd, `--config` аргумент | ✅ |
| 3 | `tools/uart_rx_dump.c` — verbose диагностика: raw bytes + все parse events | ✅ |
| 4 | clang-tidy clean: naming conventions, enum/struct теги, pointer prefixes | ✅ |
| **Тесты** | | |
| 5 | `tests/test_uart.c` — 5 тестов через pipe(): valid frame, junk, bad CRC, partial, two frames | ✅ |
| 6 | `just build::test` → 6/6 passed, ASan+UBSan чисто | ✅ |
| **Systemd** | | |
| 7 | `indicator.service` обновлён: `SyslogIdentifier`, `TimeoutStopSec`, `--config` путь | ✅ |
| 8 | `systemctl start indicator` → active, event loop started | ✅ |
| 9 | `systemctl stop indicator` → clean shutdown (SIGTERM → signal 15 → indicator stopped) | ✅ |
| 10 | `kill -SIGSEGV` → systemd перезапускает за 2 с | ✅ |
| 11 | Watchdog тики каждые 30 с в journald | ✅ |
| **Железо** | | |
| 12 | `uart_rx_dump` — 0 ошибок CRC на реальном трафике STM32 | ✅ |
| 13 | Каждый фрейм логируется с правильными полями floor/arrow/sound/mode | ✅ |
| 14 | edge-triggered sound подтверждён: STM32 шлёт ненулевой sound ровно 1 фрейм | ✅ |
| 15 | Dispatch (opcode=0xAA): CALL/ANSWER/OFF — все три варианта получены и залогированы | ✅ |

**Критерий выполнен.**

---

## 2. Проблемы и решения

### P-18 — CRC byte order

**Симптом:** `uart_rx_dump` не показывал фреймов. `indicator` показывал `frames_ok=0` при активном STM32.

**Причина:** `parser.c` читал CRC в big-endian порядке `[HI][LO]`, а STM32 (`MU_tx_frame_create`) записывает little-endian `[LO][HI]`. Тесты проходили потому что `make_frame()` в `test_parser.c` тоже использовал big-endian — оба конца были согласованы между собой, но не со STM32.

**Обнаружено:** сравнением `MU_tx_frame_create` и `MU_rx_frame_is_valid` из STM32-прошивки.

**Решение:** исправить одну строку в `parser.c`:
```c
// БЫЛО:
uint16_t crc_recv =
    ((uint16_t)buf[start + 3u + data_size] << 8u) | (uint16_t)buf[start + 4u + data_size];
// СТАЛО:
uint16_t crc_recv =
    ((uint16_t)buf[start + 4u + data_size] << 8u) | (uint16_t)buf[start + 3u + data_size];
```
Исправить `make_frame()` в тестах аналогично. Добавить `test_parse_frame_crc_byte_order` — регрессионный тест.

---

### P-19 — Утечка памяти в test_uart (LeakSanitizer)

**Симптом:** `test_uart` завершался с ошибкой — 4 утечки по 544 байт каждая.

**Причина:** Unity использует `longjmp` при провале `TEST_ASSERT`. `uart_close()` в конце тела теста не вызывался. 4 из 5 тестов текли — именно те, где `TEST_ASSERT_EQUAL_INT(1, cap.call_count)` падал из-за P-18. Тест 3 (`bad_crc`) ожидал `call_count == 0` — проходил и не тёк.

**Решение:** перенести `uart_close()` и `close(wr)` в `tearDown()` через статические переменные модуля. Unity гарантированно вызывает `tearDown()` после каждого теста включая упавшие.

---

### P-20 — Опечатка `inndicator_mode_t` (двойная n)

**Симптом:** несоответствие между `types.h` (`inndicator_mode_t`) и `parser.c` (`indicator_mode_t`).

**Решение:** исправить везде на `indicator_mode_t` (одна n). Добавить enum-теги по clang-tidy: `typedef enum indicator_mode_e { } indicator_mode_t`.

---

### P-21 — `cfmakeraw()` и `O_CLOEXEC` недоступны без `_GNU_SOURCE`

**Симптом:** ошибки компиляции в `uart.c` при zig cc (строгий C11).

**Причина:** `cfmakeraw()` объявлена под `__USE_MISC`; `O_CLOEXEC` определён как `__O_CLOEXEC` и раскрывается только с `_GNU_SOURCE`.

**Решение:**
- `cfmakeraw()` → явная установка флагов termios в `termios_set_raw()` (5 строк, эквивалентно)
- `O_CLOEXEC` → `fcntl(fd, F_SETFD, FD_CLOEXEC)` — чистый POSIX.1-2001
- `uart.c` не требует `_GNU_SOURCE`; `main.c` требует (signalfd, timerfd, CLOCK_MONOTONIC)

---

### P-22 — sigemptyset / CLOCK_MONOTONIC не видны clangd без `_GNU_SOURCE`

**Симптом:** clangd подчёркивал символы красным в `main.c`, хотя сборка проходила (`CMAKE_C_EXTENSIONS=ON` неявно определяет `_GNU_SOURCE`).

**Решение:** явный `#define _GNU_SOURCE` первой строкой `main.c` до любых `#include`.

---

### P-23 — `build/pi/CMakeCache.txt` создан в devcontainer, запуск с хоста

**Симптом:** `just build::pi-dump` с хоста — ошибка пути CMakeCache.

**Причина:** кэш содержит абсолютный путь `/project/...` (devcontainer), а хост видит `/Users/...`.

**Решение:** `rm -rf build/pi` перед cmake. Добавить `rm -rf "{{BUILD_DIR}}/pi"` в начало `_configure-pi` в `build.just` — как зафиксировано в P-12 (Фаза 1), но не применено.

---

## 3. Уточнения протокола, полученные в фазе

### 3.1 Новый тип фреймов: opcode=0xAA (диспетчерская связь)

Обнаружен при тестировании `uart_rx_dump`. Приходит однократно при изменении сигнальных входов.

```
Payload: "DISPATCH CALL\r\n"   → DISPATCH_CALL
         "DISPATCH ANSWER\r\n" → DISPATCH_ANSWER
         "DISPATCH OFF\r\n"    → DISPATCH_OFF
```

Приоритет выше любого `mode_t` из opcode=0xDA. При активном dispatch иконка режима лифта не затирается. При `DISPATCH_OFF` — возврат к mode из 0xDA.

Примечание: значение опкода `0xAA` совпадает с `MU_SYNC1`, но занимает другую позицию в кадре (byte[2] = OPCODE, byte[0] = SYNC1) — парсер работает корректно.

### 3.2 Служебный опкод opcode=0xC0

Наблюдается в трафике. Пустой payload (len=0). Консольный/служебный фрейм STM32. В логике индикатора не используется, обрабатывается веткой `unknown_opcodes` с `LOG_DEBUG`.

### 3.3 CHAR_pi_cyr (код 19) — подвальный этаж

STM32 присылает как заглавную `CHAR_PI_CYR=17`, так и строчную `CHAR_pi_cyr=19` для обозначения подвальных уровней. Оба кода теперь обрабатываются `is_pi_char()` в `floor_decode()`. Подтверждено на реальном трафике: L=19, R=5 → П5.

### 3.4 UNKNOWN-этажи на реальном трафике

| Комбинация | Пример | Обработка |
|---|---|---|
| `L=MINUS, R=0` (-0) | фрейм #16 в дампе | `FLOOR_TYPE_UNKNOWN` → g_triple |
| `L=PI, R=0` (П0) | теоретически | `FLOOR_TYPE_UNKNOWN` → g_triple |
| `L=22, R=0` | реальный трафик | `FLOOR_TYPE_UNKNOWN` → g_triple |

### 3.5 Edge-triggered sound — подтверждено

STM32 выставляет ненулевой `sound` ровно на один фрейм, затем сбрасывает в 0. Дедупликация в `state_apply_frame()` не нужна. Подтверждено на живом трафике для SOUND_UP, SOUND_DING, SOUND_CLOSING.

### 3.6 Null-байт в конце data

Все payload содержат `\r\n\0` в конце — null-терминатор включён в `data_len`. `data_len` на 1 больше видимой строки. На парсинг не влияет.

---

## 4. Новые и изменённые файлы

| Файл | Действие |
|---|---|
| `src/transport/uart.h` | Новый (замена стаба): `baud_rate_t`, `uart_parity_t`, `frame_ready_cb_t`, `uart_wrap_fd` |
| `src/transport/uart.c` | Новый (замена стаба): termios, ring buffer, `protocol_parse_frame()` интеграция |
| `src/main.c` | Новый (замена стаба): poll-цикл, signalfd, timerfd, `--config`, dispatch handling |
| `src/protocol/types.h` | Обновлён: `dispatch_state_t`, исправлена опечатка, enum-теги |
| `src/protocol/parser.h` | Обновлён: `MU_OPCODE_DISPATCH`, `protocol_parse_dispatch()` |
| `src/protocol/parser.c` | Обновлён: CRC byte order fix, `protocol_parse_dispatch()` |
| `src/domain/floor.c` | Обновлён: `is_pi_char()`, ветки для `CHAR_pi_cyr` |
| `src/domain/state.h` | Обновлён: `active_dispatch`, `dispatch_changed`, `state_apply_dispatch()` |
| `src/domain/state.c` | Обновлён: `state_apply_dispatch()` |
| `deploy/indicator.service` | Обновлён: `SyslogIdentifier`, `TimeoutStopSec=5`, `--config` путь к TOML |
| `tests/test_uart.c` | Новый: 5 тестов через pipe(), cleanup через tearDown() |
| `tests/test_parser.c` | Обновлён: little-endian CRC в make_frame(), регрессионный тест byte order, dispatch тесты |
| `tests/test_floor.c` | Обновлён: CHAR_pi_cyr, П0, -0, реальный фрейм L=19 R=5 |
| `tests/test_state.c` | Обновлён: state_apply_dispatch тесты |
| `tools/uart_rx_dump.c` | Новый: verbose диагностика UART с цветным выводом |
| `tools/CMakeLists.txt` | Новый: цель `uart_rx_dump` |
| `just/build.just` | Обновлён: `pi-dump`, `tools/` в format/check-format |
| `just/pi.just` | Обновлён: `deploy-tools`, `deploy-sounds`, `dump`, `dump-passive` |

---

## 5. Итоговое состояние окружения

```
Raspberry Pi Zero W (indicator-01.local)
├── ARM1176JZF-S, ARMv6, Debian Buster
├── /home/pi/indicator/
│   ├── indicator            ← ARMv6 Release (phase-2) ✅
│   ├── media_ingest         ← ARMv6 Release (stub) ✅
│   ├── tools/
│   │   └── uart_rx_dump     ← диагностическая утилита ✅
│   ├── configs/device/nku_scheme.toml
│   ├── sounds/              ← WAV файлы задеплоены
│   ├── resources/           ← пусто (Фаза 4)
│   └── videos/              ← пусто (Фаза 3)
└── /etc/systemd/system/
    ├── indicator.service    ← обновлён ✅
    ├── media-ingest.service
    └── indicator.target
```

---

## 6. Открытые вопросы

| ID | Вопрос | Приоритет |
|----|--------|-----------|
| В-05 | `s_close.wav`, `s_open.wav` — не используются (резерв). Удалить из деплоя или оставить? | низкий |
| В-06 | `g_single.wav`, `g_double.wav` — не используются (резерв). Аналогично. | низкий |
| В-07 | `0.wav` — про запас, не используется в текущем sound_map. Подтверждено. | закрыт |

---

## 7. Контекст для Фазы 3

При начале нового треда передать:

1. `PHASE_2_REPORT.md`
2. `MASTER_PLAN.md` (актуализированный)
3. `DEV_ARCH.md`
4. Текущие исходники: `src/main.c`, `src/player/video_player.h/.c`, `deploy/indicator.service`

**Стартовая фраза:**
> Фазы 0, 1, 2 закрыты. Начинаем Фазу 3 — Video Player (omxplayer + SIGCHLD watchdog).
> Прикладываю PHASE_2_REPORT и MASTER_PLAN.

---

*Документ сгенерирован по итогам сессии Фазы 2.*