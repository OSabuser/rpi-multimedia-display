# Lift Indicator — Отчёт Фазы 4

**Дата:** 2026-06-01
**Проект:** `rpi-multimedia-display`
**Устройство:** Raspberry Pi Zero W Rev 1.1 · ARMv6 · Debian Buster · дисплей 600×1024

---

## 1. Фаза 4 — ЗАКРЫТА ✅

**Цель фазы:** DispmanX-рендерер реализован и работает под надзором `indicator`.
Все слои (BACKGROUND, MODE, WEIGHT, DIGIT_LEFT, DIGIT_RIGHT, ARROW) отображаются
корректно поверх видео omxplayer.

### 1.1 Чеклист

| # | Пункт | Статус |
|---|---|---|
| **Код** | | |
| 1 | `src/renderer/renderer.h` — `sprite_slot_t`, API: `create/destroy/show_png/hide/keepalive` | ✅ |
| 2 | `platform/dispmanx/renderer_impl.c` — DispmanX реализация, fast_update, slot management | ✅ |
| 3 | `platform/dispmanx/CMakeLists.txt` — динамическая линковка libpng16 из Pi sysroot | ✅ |
| 4 | `src/config/config.h` — добавлен `renderer_config_t`, `CONFIG_DEFAULT_*` константы | ✅ |
| 5 | `src/config/config.c` — `renderer_config_load()` hand-written parser | ✅ |
| 6 | `configs/device/renderer.toml` — новый конфиг: `resources_dir`, позиции слотов | ✅ |
| 7 | `src/main.c` — `renderer_create/apply/destroy`, `renderer_keepalive` в watchdog | ✅ |
| 8 | `scripts/check_resources.sh` — валидация 68 ресурсов (PNG + WAV) | ✅ |
| 9 | `justfile` — `ship` включает `check-resources` + `restart` | ✅ |
| **On-target** | | |
| 10 | BACKGROUND + WEIGHT отображаются до первого UART фрейма | ✅ |
| 11 | Цифры: fast_update без мигания, ~100 мс latency | ✅ |
| 12 | Двузначные этажи, спецсимволы (П, `-`) | ✅ |
| 13 | MODE показ/скрытие, z-порядок (chars поверх mode) | ✅ |
| 14 | Dispatch CALL/ANSWER, приоритет над mode | ✅ |
| 15 | Стрелка показ/скрытие | ✅ |
| 16 | Latency UART → экран: 88–158 мс (цель < 300 мс) | ✅ |
| 17 | Корректный shutdown за < 1 с | ✅ |
| 18 | `just pi::check-resources` — 68/68 ✅ | ✅ |

**Критерий выполнен.**

---

## 2. Проблемы и решения

### P-27 — SIGSEGV при запуске (libpng16.a, ARMv7 код на ARMv6)

**Симптом:** `indicator` завершался с SIGSEGV до первой строки `main()`.
`EXIT:139`, никаких записей в syslog/journald.

**Диагностика:**
- `journalctl -t indicator` — ни одной строки от Phase 4 процесса
- `strace`: последний syscall `ugetrlimit(RLIMIT_STACK)` → SIGSEGV до вызова `main()`
- `si_addr=0x6e692f2f` (ASCII: `"//in"`) — указатель в строковый литерал

**Причина:** `libpng16.a` из Debian Buster/armhf скомпилирована с `-march=armv7-a`
(Thumb-2 инструкции). При статической линковке `.init_array` конструкторы
выполняются на ARMv6 (Pi Zero W) → недопустимые инструкции → SIGSEGV до `main()`.

**Решение:**
- `just pi::fetch-png-sysroot` копирует `.so` (не `.a`) с Pi
- `platform/dispmanx/CMakeLists.txt`: линковка через `"-lpng16"` строкой
  (обход `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY`)
- libpng16.so загружается Pi-линкером который знает ARMv6 runtime

**Статус:** закрыт ✅

---

### P-28 — Видео останавливается после длительного простоя

**Симптом:** после ~2 суток работы без UART активности видео omxplayer перестало
отображаться на экране. Процесс omxplayer (PID 1861) оставался живым.
После первого dispatch-сигнала (→ `vc_dispmanx_update_submit_sync`) видео
возобновилось немедленно.

**Причина:** VideoCore IV (Pi Zero W) переходит в dormant state при отсутствии
DispmanX-активности. omxplayer продолжает декодировать, кадры не рендерятся.

**Доказательство:** в журнале 3028 строк watchdog с одним и тем же `omxplayer_pid=1861`
без единого SIGCHLD — процесс жив, но видео заморожено. После dispatch:
```
dispatch: state=2 (ANSWER)
renderer: slot 1 created   ← vc_dispmanx_update_submit_sync
```
видео возобновилось.

**Решение:** `renderer_keepalive(r)` — пустая DispmanX транзакция
(`update_start → update_submit_sync`) вызывается из `on_watchdog_tick()` каждые 30 с.
Стоимость: ~1 мс. Эффект: VideoCore не засыпает.

**Статус:** закрыт ✅

---

### P-29 — ARROW использует slow path при каждом появлении (не блокирует)

**Симптом:** при каждом появлении стрелки — `slot 5 created` вместо `slot fast-updated`.
Latency создания ~48 мс (приемлемо).

**Причина:** `renderer_hide()` вызывает `destroyImageLayer` → `initialized=0`.
При следующем `renderer_show_png` fast_update не срабатывает.

**Правильное решение:** скрывать через `ELEMENT_CHANGE_OPACITY=0` вместо
destroy, fast_update при восстановлении. Визуально мигание не замечено.

**Статус:** не блокирует. Фаза 6. ⏳

---

### SSH-агент теряет ключ после перезагрузки macOS

**Симптом:** `Enter passphrase for key '~/.ssh/id_ed25519'` при каждом деплое
после перезагрузки.

**Решение (один раз):**
```bash
ssh-add --apple-use-keychain ~/.ssh/id_ed25519
```
Добавить в `~/.ssh/config`:
```
Host *
    UseKeychain yes
    AddKeysToAgent yes
    IdentityFile ~/.ssh/id_ed25519
```

**Статус:** закрыт ✅ (документировано в памятке)

---

### indicator.target не деплоился

**Симптом:** `just pi::start` → `Unit indicator.target not found`.

**Причина:** `deploy` рецепт копировал только `*.service`, не `*.target`.
`pi::start/stop` использовали `indicator.target`.

**Решение:** `pi::start` и `pi::stop` переведены на `indicator.service`.
`WantedBy=multi-user.target` в `indicator.service`.
`indicator.target` будет создан в Фазе 7 когда `media_ingest` станет реальным сервисом.

**Статус:** закрыт ✅

---

## 3. Z-порядок слоёв (финальный)

```
Z = 1   omxplayer          видео (--layer 1)
Z = 2   SPRITE_BACKGROUND  BACK.png, 600×1024, полупрозрачный
Z = 3   SPRITE_MODE        mode-иконка, 600×1024 (или скрыт)
Z = 4   SPRITE_WEIGHT      load_N.png, 237×59
Z = 4   SPRITE_DIGIT_LEFT  chars/N.png, 202×346  ← fast_update
Z = 4   SPRITE_DIGIT_RIGHT chars/N.png, 202×346  ← fast_update
Z = 4   SPRITE_ARROW       up/down.png, 188×209
Z = 5   SPRITE_NOTIFICATION (Фаза 7)
```

---

## 4. Маппинг mode → PNG (финальный)

| `indicator_mode_t` | Значение | PNG | Действие |
|---|---|---|---|
| `MODE_NORMAL` | 0 | — | скрыть слот |
| `MODE_FIRE_ALARM` | 1 | `modes/firealarm.png` | показать |
| `MODE_MALFUNCTION` | 2 | `modes/malfunction.png` | показать |
| `MODE_LOADING` | 3 | `modes/loading.png` | показать |
| `MODE_OVERLOAD` | 4 | `modes/overload.png` | показать |
| `MODE_SEIS_ALARM` | 5 | `modes/seismo.png` | показать |
| `MODE_FIREMANS` | 6 | `modes/fireman.png` | показать |
| `MODE_SERVICE` | 7 | `modes/inspection.png` | показать |
| `MODE_EVACUATION` | 8 | `modes/evacuation.png` | показать |
| `MODE_UPS_MALFUNCTION` | 9 | `modes/malfunction.png` | показать |
| `MODE_DISPATCH_CALL` | 100 | `modes/calling.png` | через dispatch |
| `MODE_DISPATCH_ANSWER` | 101 | `modes/talking.png` | через dispatch |
| `MODE_CONN_LOST` | 255 | — | скрыть слот |

---

## 5. Latency (on-target, Pi Zero W ARMv6)

| Операция | Замеренное время |
|---|---|
| UART frame → first digit update | 45–63 мс |
| UART frame → both digits updated | 88–158 мс |
| Arrow creation (slow path) | ~48 мс |
| keepalive null update | ~1 мс |

Цель < 300 мс — выполнена с запасом.

---

## 6. Новые и изменённые файлы

| Файл | Действие |
|---|---|
| `src/renderer/renderer.h` | Новый: `sprite_slot_t`, API, `renderer_keepalive` |
| `platform/dispmanx/renderer_impl.c` | Новый: DispmanX реализация |
| `platform/dispmanx/CMakeLists.txt` | Обновлён: динамическая libpng, Pi sysroot |
| `platform/dispmanx/layers/` | Новый: Duncan-код (MIT): image, imageLayer, loadpng |
| `build-env/pi-sysroot/usr/` | Новый: libpng16.so + zlib.so + headers (ARM32) |
| `src/config/config.h` | Обновлён: `renderer_config_t`, `CONFIG_DEFAULT_*` |
| `src/config/config.c` | Обновлён: `renderer_config_load()` |
| `src/main.c` | Обновлён: renderer init/apply/keepalive/destroy |
| `configs/device/renderer.toml` | Новый: позиции слотов, resources_dir |
| `scripts/check_resources.sh` | Новый: валидация 68 ресурсов |
| `just/pi.just` | Обновлён: `check-resources`, `start/stop` → `.service` |
| `justfile` | Обновлён: `ship` включает `check-resources` + `restart` |

---

## 7. Итоговое состояние окружения

```
Raspberry Pi Zero W (indicator-01.local)
├── /home/pi/indicator/
│   ├── indicator            ← ARMv6 Release (phase-4) ✅
│   ├── media_ingest         ← ARMv6 Release (stub) ✅
│   ├── tools/uart_rx_dump   ✅
│   ├── scripts/
│   │   └── check_resources.sh ✅
│   ├── configs/device/
│   │   ├── nku_scheme.toml
│   │   ├── video.toml
│   │   └── renderer.toml    ← новый ✅
│   ├── resources/           ← 68 файлов, все валидны ✅
│   │   ├── BACK.png
│   │   ├── chars/           (38 PNG)
│   │   ├── arrows/          (2 PNG)
│   │   ├── weights/         (16 PNG)
│   │   └── modes/           (10 PNG)
│   ├── sounds/
│   │   ├── s_close.wav      ✅
│   │   └── s_open.wav       ✅
│   └── videos/output.mp4    ← видео играет ✅
└── /etc/systemd/system/
    └── indicator.service    ← WantedBy=multi-user.target ✅
```

---

## 8. Открытые вопросы

| ID | Вопрос | Приоритет | Фаза |
|----|--------|-----------|------|
| P-24 | dbus-daemon не в pgroup omxplayer | низкий | 6 |
| P-29 | ARROW slow path при каждом появлении | низкий | 6 |
| В-05 | Именование WAV-файлов для Phase 5 | средний | 5 |

---

## 9. Контекст для Фазы 5

При начале нового треда передать:

1. `PHASE_4_REPORT.md`
2. `MASTER_PLAN.md`
3. `DEV_ARCH.md`
4. Текущие исходники: `src/main.c`, `src/renderer/renderer.h`,
   `src/config/config.h`, `sound_map.h` (если есть)

**Стартовая фраза:**
> Фазы 0–4 закрыты. Начинаем Фазу 5 — аудио.
> Прикладываю PHASE_4_REPORT и MASTER_PLAN.

---

*Документ сгенерирован по итогам сессии Фазы 4.*