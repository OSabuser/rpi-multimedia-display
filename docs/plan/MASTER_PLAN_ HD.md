# Lift Indicator FullHD — Мастер-план (ветка `dev-pi2w`)

> **Статус:** Фаза F0 — НЕ НАЧАТА. Базовый проект (ветка `dev-pi`, Фазы 0–7 + Deploy + Deploy v2) — ЗАКРЫТ ПОЛНОСТЬЮ.
> **Стек:** C11 · DispmanX · omxplayer · ALSA · systemd · CMake · Docker · just *(без изменений)*
> **Компилятор:** `zig cc` (`arm-zig-cc-a53` wrapper) — ARMv8-A 32-bit, cortex\_a53, hard-float
> **Устройство:** Raspberry Pi Zero 2W · BCM2710A1 · 4× Cortex-A53 · Debian Buster Lite (armhf)
> **Дисплей:** 1080×1920 (портрет) · HDMI · штатный EDID, без кастомных `hdmi_timings`

---

## 0. Природа проекта

Это **порт, а не переписывание**. Zero 2W несёт тот же GPU VideoCore IV (BCM2710 = семейство Pi 3),
поэтому весь display/video/audio-стек переносится без изменений кода. Меняются:

1. Toolchain (`-mcpu=cortex_a53` вместо `arm1176jzf_s`)
2. Геометрия рендера (1080×1920 вместо 600×1024)
3. Рендеринг цифр: **font renderer (бывш. Фаза 8)** вместо PNG-спрайтов `chars/`
4. Boot config (EDID-дисплей без `hdmi_timings`, `gpu_mem` под 1080p)

Протокол UART (универсальный, подтверждено), parser, state machine, sound map, audio,
media-ingest, systemd-юниты, /data layout, overlayfs-процедуры — **идентичны**.

---

## 1. Принятые решения (новые, поверх решений `dev-pi`)

| Тема | Решение | Уверенность | Обоснование |
|---|---|---|---|
| Железо | **Pi Zero 2W** (BCM2710A1, 4× Cortex-A53) | — | omxplayer тянет FullHD без зависаний VideoCore (проверено на железе) |
| ОС | **Buster Lite armhf 32-bit** (тот же образ 2023-05-03) | Высокая | Образ поддерживает 2W (kernel7.img, bcm2710-dtb). arm64-Buster отпадает: нет omxplayer и полноценного DispmanX userland |
| Кросс-компилятор | **zig cc**, новый wrapper `arm-zig-cc-a53`: `zig cc -target arm-linux-gnueabihf -mcpu=cortex_a53` | Высокая | Единый Dockerfile с веткой `dev-pi`; crt-объекты под точный CPU. Debian gcc тоже работал бы (ARMv7 crt легален на A53), но zig сохраняет однородность инфраструктуры |
| Toolchain-файл | **Переписать существующий** `cmake/Toolchain-RPiZero2W.cmake` под zig (сейчас там legacy gcc-версия времён P-08) | Высокая | Блок `/opt/vc`, `CMAKE_FIND_ROOT_PATH_*`, `CMAKE_TRY_COMPILE_TARGET_TYPE` — копируются из Toolchain-RPiZeroW.cmake |
| Флаги | `CMAKE_C_FLAGS_INIT "-mfloat-abi=hard"` — **без `-marm`** | Средняя | Cortex-A53 полноценно поддерживает Thumb-2 + hard-float (проблема Thumb-1+VFP с ARMv6 неактуальна). Если zig выдаст неожиданный код — вернуть `-marm`, нулевой риск |
| Just-рецепты | **Имена не меняются.** В ветке `dev-pi2w` рецепт `build::_configure-pi` указывает на `Toolchain-RPiZero2W.cmake` | Высокая | Ветки разделены — `just build::pi` в каждой ветке собирает под «своё» устройство |
| Docker-образ | **Один образ на обе ветки**: Dockerfile содержит оба wrapper'а (`arm-zig-cc` + `arm-zig-cc-a53`). Изменение бэкпортируется в `dev-pi` | Высокая | Иначе придётся пересобирать образ при переключении веток |
| Sysroot | **Re-fetch с нового устройства** (`just pi::fetch-sysroot`, `fetch-png-sysroot`), но ожидается идентичным | Высокая | `/opt/vc` и libpng16 в Buster armhf одинаковы для всех Pi; fetch — дёшево, снимает риск |
| ARMv6-чистота библиотек | **Неактуальна** на A53; P-09/P-27-класс проблем исчезает | Высокая | ARMv7 Thumb-2 код легален. Динамическую линковку libpng16 НЕ меняем — работает, не трогаем |
| Рендеринг цифр | **Font renderer (Фаза 8 интегрируется сюда, фаза F2)** — PNG `chars/` не пере-рендериваются | — | Решение пользователя; рендерер шрифтов будет передан отдельно. Не плодим 38 одноразовых PNG 1080p |
| Остальные спрайты | PNG пере-рендериваются под новую геометрию (BACK, modes, arrows, weights, notifications) | — | Объём небольшой (~20 файлов); масштабирование DispmanX'ом — запасной интерим-вариант для bring-up |
| Display config | `hdmi_timings` / кастомные `hdmi_*` **удаляются** из config.txt; полагаемся на EDID | Высокая | Новый дисплей со штатным EDID. Ориентация панели (native portrait vs `display_rotate`) — открытый вопрос В2-01 |
| gpu\_mem | Поднять минимум до **128 MB** (проверить текущее значение) | Средняя | 1080p H.264 decode + полноэкранные DispmanX-ресурсы: BACK и MODE по ~8.3 MB ARGB каждый. Точное значение — по результатам F2/F3 |
| Ветки | `dev` → переименовать в **`dev-pi`**; новая ветка — **`dev-pi2w`** | — | Решение пользователя. До F6 фиксы общего кода (parser, audio, media-ingest) cherry-pick'аются в обе стороны — осознанный временный компромисс |
| Стратегия слияния в `main` | **Один `main`, параметризованный по устройству** — реализуется в Фазе F6 (после F5). Отдельные репозитории и два параллельных main-branch не рассматриваются | — | Отдельный репо не устраняет проблему синхронизации кода, а усугубляет её. Параметризация сводится к: (1) `deploy/configs/pi/` и `deploy/configs/pi2w/` — реструктуризация configs; (2) `render_digit()` с двумя реализациями или `#ifdef FONT_RENDERER` в `renderer_impl.c` — единственная реальная дельта в C-коде |
| UART | `/dev/ttyAMA0` @ 115200, как раньше | Средняя | На 2W (как и на Zero W) ttyAMA0 делится с Bluetooth — текущий setup\_pi.sh уже отключает hciuart/bluetooth; верифицировать наличие `dtoverlay=disable-bt` в config.txt при F1 |
| Аудио | Без изменений (та же несущая плата, MAX98357A, googlevoicehat overlay, dmix S32\_LE 48k) | Высокая | Подтверждено: плата та же, меняется только дисплей |
| Протокол | `nku_scheme.toml` и фреймы STM32 — без изменений | Высокая | Подтверждено: протокол универсальный |

---

## 2. Наследуется из `dev-pi` без изменений

Полные описания — в `MASTER_PLAN.md` ветки `dev-pi` (разделы 2, 12). Здесь — только перечень:

- Протокол STM32: SOF/EOF, opcodes 0xDA/0xAA/0xC0, CRC16 little-endian `[LO][HI]` (P-18)
- `floor_decode`, `sound_map_resolve`, DING-логика, приоритеты аудио-очереди
- omxplayer-надзор: `posix_spawn` + `POSIX_SPAWN_SETPGROUP` + `killpg(SIGKILL)` + SIGCHLD watchdog
- `renderer_keepalive()` каждые 30 с (P-28 — VideoCore IV тот же, риск dormant сохраняется)
- media-ingest: inotify /dev, mount(2) + CAP\_SYS\_ADMIN, ffmpeg `-c copy` (P-33: конфликт
  h264\_omx/omxplayer за /dev/vchiq актуален и на 2W — GPU тот же), FIFO IPC, фильтр AppleDouble (P-32)
- systemd-граф: firstboot → setup → i2s-silence → indicator; media-ingest; KillMode=control-group
- `/data/` layout, bind-монты, 3-раздельная SD-карта, overlayfs через `raspi-config nonint`
- Setup TUI: pi\_nku\_sync / pi\_nku\_menu (Rust ARM-бинари — ARMv6-сборка исполняется
  на A53 без пересборки; пересборка под ARMv7 — опциональная оптимизация, не блокер)
- Justfile-структура (mod build / mod pi), Unity-тесты, clang-tidy/format конвенции

---

## 3. Z-порядок DispmanX — целевая геометрия 1080×1920

Масштаб от 600×1024: ширина ×1.80, высота ×1.875. **Аспекты не совпадают**
(0.586 vs 0.563) — чистое масштабирование координат даст рассинхрон до ~36 px по
ширине; нужен один осознанный layout-проход в F2, а не формула.

```
Z = 1   omxplayer            видео 1080×1920 (--layer 1, окно из video.toml)
Z = 2   SPRITE_BACKGROUND    BACK.png 1080×1920 RGBA            (~8.3 MB GPU)
Z = 3   SPRITE_MODE          mode-иконка 1080×1920              (~8.3 MB GPU)
Z = 4   SPRITE_WEIGHT        load_N.png  ~427×106   (масштаб ×1.8, уточнить в F2)
Z = 4   SPRITE_DIGIT_LEFT    font renderer ← НОВОЕ (бывш. chars/N.png)
Z = 4   SPRITE_DIGIT_RIGHT   font renderer ← НОВОЕ
Z = 4   SPRITE_ARROW         arrows/up|down.png ~338×376 (уточнить в F2)
Z = 5   SPRITE_NOTIFICATION  1080×270 px, y ≈ 1638 (нижняя полоса, уточнить в F2)
```

Интеграция font renderer'а в слоты DIGIT решает попутно: один и тот же
`changeSourceImageLayer` fast-path сохраняется, если рендерер пишет в DispmanX-ресурс
фиксированного размера. Детали — после получения кода рендерера (В2-02).

P-29 (ARROW slow path): на 4× A53 destroy+recreate, вероятно, станет визуально
незаметен — замерить в F2 и либо закрыть P-29 «не требуется», либо доделать
`ELEMENT_CHANGE_OPACITY`.

---

## 4. Фазы

### Фаза F0 — Ветки, toolchain, сборка *(devcontainer, без железа)*

| # | Пункт |
|---|---|
| 1 | Переименовать `dev` → `dev-pi`; создать `dev-pi2w` от неё |
| 2 | Dockerfile: добавить wrapper `arm-zig-cc-a53` (`-mcpu=cortex_a53`); бэкпортировать в `dev-pi`; `just pi::build-image` |
| 3 | Переписать `cmake/Toolchain-RPiZero2W.cmake`: zig wrapper, блок `/opt/vc`, `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY`, `CMAKE_C_FLAGS_INIT "-mfloat-abi=hard"` |
| 4 | `just/build.just`: `_configure-pi` → `Toolchain-RPiZero2W.cmake` (имена рецептов не меняются; принудительный `rm -rf build/pi` уже есть — P-12) |
| 5 | `just build::pi` — собираются indicator + media\_ingest + uart\_rx\_dump; `readelf -A` показывает `Tag_CPU_arch: v8` |
| 6 | `just build::test` — host-тесты проходят без изменений (domain-код не трогали) |
| 7 | Обновить `.clangd` профиль `platform/` если путь компилятора изменился |

**Выход:** ARM-бинари под A53 в `build/pi/`, host-тесты зелёные.

### Фаза F1 — Bring-up устройства

| # | Пункт |
|---|---|
| 1 | Прошить Buster Lite 2023-05-03 на SD, загрузить 2W, подтвердить boot (kernel7.img) |
| 2 | config.txt: удалить `hdmi_timings`/кастомные hdmi-параметры; решить В2-01 (native portrait vs `display_rotate`); `gpu_mem=128`; проверить `dtoverlay=disable-bt` |
| 3 | hostname (В2-05), `just pi::setup-ssh`, `.env` под новое устройство |
| 4 | `just pi::fetch-sysroot` + `fetch-png-sysroot` с 2W; diff со старым sysroot (ожидание: идентичен) |
| 5 | `just pi::setup-pi` — пакеты, ALSA, /data, fstab; верифицировать legacy-репозитории Buster всё ещё доступны |
| 6 | `tvservice -s` / EDID: подтвердить режим 1080×1920 (или 1920×1080 + rotate) |
| 7 | UART smoke: `uart_rx_dump` + живой STM32 → фреймы парсятся, CRC OK |
| 8 | Запуск indicator с дефолтной (старой) геометрией — «работает, но мелко/смещено» — это ОК для F1 |

**Выход:** устройство в сети, дисплей в нативном режиме, UART живой, бинари исполняются.

### Фаза F2 — FullHD renderer + font renderer (интеграция бывш. Фазы 8)

| # | Пункт |
|---|---|
| 1 | Получить и изучить font renderer пользователя (В2-02: API, формат вывода, зависимости) |
| 2 | Спроектировать интеграцию: font renderer → DispmanX-ресурс DIGIT-слотов, сохранение fast\_update-пути |
| 3 | Layout-проход: новые координаты всех слотов → `renderer.toml` (новый файл в `deploy/configs/`, парсер `renderer_config_load` не меняется) |
| 4 | Пере-рендер PNG: BACK, modes (×13), arrows (×2), weights, notifications (`gen_notifications.sh`: 600×150 → 1080×270) |
| 5 | `check_resources.sh`: обновить список/размеры ожидаемых ресурсов (chars/ выпадают, размеры меняются) |
| 6 | Splash 1080×1920 (`deploy/boot/splash.jpg`) |
| 7 | Замер fast\_update и ARROW slow path на A53 → решение по P-29 |
| 8 | Контроль GPU-памяти: `vcgencmd get_mem gpu`, `vcdbg reloc` при полном наборе слотов; скорректировать `gpu_mem` |

**Выход:** полный UI в 1080×1920, цифры через font renderer, P-29 решён или закрыт как неактуальный.

### Фаза F3 — FullHD video + media-ingest

| # | Пункт |
|---|---|
| 1 | `video.toml`: окно 0,0,1080×1920; эталонный 1080p H.264 output.mp4 |
| 2 | Длительный прогон omxplayer 1080p loop (≥ 24 ч) + keepalive — подтвердить отсутствие dormant/зависаний на новой нагрузке |
| 3 | media-ingest с 1080p-файлами: тайминг `-c copy` на больших файлах, нотификации новой геометрии, `video_player_replace()` |
| 4 | MEDIA\_GUIDE.md: обновить требования к видео (разрешение, профиль H.264 для 1080p на VC4 — уровень ≤ 4.0/4.1) |
| 5 | Температурный контроль: `vcgencmd measure_temp` под 1080p-нагрузкой (2W греется ощутимо сильнее Zero W; решить, нужен ли радиатор в корпусе) |

**Выход:** фоновое 1080p-видео стабильно, замена с USB работает end-to-end.

### Фаза F4 — Audio + интеграционные испытания

| # | Пункт |
|---|---|
| 1 | Аудио-валидация (та же плата): i2s-silence, dmix, softvol, преемптинг очереди — короткий прогон чеклиста Фазы 5 |
| 2 | Полный интеграционный чеклист (по образцу `indicator_checklist_2.md`, 16 тестов) на 2W |
| 3 | `just pi::smoke` / `smoke_test.sh` — адаптировать ожидания при необходимости |

**Выход:** интеграционные испытания PASS.

### Фаза F5 — Deploy v2 для 2W

| # | Пункт |
|---|---|
| 1 | Повторить процедуру Фазы 6: 3 раздела, p3 `LABEL=data`, fstab-порядок |
| 2 | overlayfs: `raspi-config nonint enable/disable_overlayfs` — верифицировать на 2W (ожидание: идентично, тот же Buster) |
| 3 | WiFi seed `/boot/wpa_supplicant.conf` → `/data/` (механизм Фазы 6 без изменений) |
| 4 | Factory image: cleanup + `indicator2w-base-YYYYMMDD.img.gz`; factory validation |

**Выход:** производственный образ для 2W-линейки.

### Фаза F6 — Trunk-унификация: один `main` для обоих устройств

Выполняется после закрытия F5, когда дельта между ветками точно известна.
До этой фазы черри-пик'ать фиксы общего кода между ветками.

| # | Пункт |
|---|---|
| 1 | Аудит дельты: `git diff dev-pi dev-pi2w` по C-файлам. Ожидание: отличается только `renderer_impl.c` (DIGIT-слоты) и файлы `deploy/configs/` + `deploy/boot/` |
| 2 | Реструктуризация конфигов: `deploy/configs/` → `deploy/configs/pi/` + `deploy/configs/pi2w/`; аналогично `deploy/boot/` |
| 3 | Justfile: переменная `DEVICE` (`pi` / `pi2w`), рецепты `build::pi` и `pi::deploy` принимают её; по умолчанию — из `.env` |
| 4 | `renderer_impl.c`: абстрагировать рендеринг DIGIT-слота через `render_digit()` в `renderer.h`; две реализации — PNG (dev-pi) и font renderer (dev-pi2w) — либо `#ifdef FONT_RENDERER` compile-time флаг в CMakeLists |
| 5 | Смерджить `dev-pi` и `dev-pi2w` в `main`; тег `v1.0-pi` и `v1.0-pi2w` |
| 6 | Убедиться: `just DEVICE=pi build::pi` → ARMv6-бинари; `just DEVICE=pi2w build::pi` → A53-бинари; host-тесты зелёные |

**Выход:** единый trunk `main`, обе платформы собираются из одного репо, cherry-pick больше не нужен.

---

## 5. Риски и trade-offs

| Риск | Оценка | Митигация |
|---|---|---|
| Buster-образ не поддержит какой-то степпинг 2W | Низкий | Образ 2023-05-03 включает bcm2710-zero-2-w dtb; проверяется первым же пунктом F1 |
| EDID нового дисплея кривой / ориентация неожиданная | Средний | В2-01 решается на железе в F1 п.6; fallback — явный `hdmi_mode` + `display_rotate` |
| Font renderer потребует изменений API renderer.h | Средний | Изучение кода до проектирования (F2 п.1–2); renderer.h — наш интерфейс, менять можно, но фиксировать решением |
| gpu\_mem=128 не хватит (1080p decode + 2 полноэкранных ARGB) | Низкий–средний | Замер в F2 п.8; запас до 256 MB есть (RAM 512) — но каждый шаг вверх отнимает у системы |
| Тепло: 2W под 1080p в закрытом корпусе лифта | Средний | F3 п.5; решение по радиатору до производства |
| Расхождение веток dev-pi / dev-pi2w в общем коде | Средний (временный) | Дисциплина cherry-pick до F6; Фаза F6 схлопывает обе ветки в один параметризованный trunk — риск снимается полностью |

---

## 6. Открытые вопросы

| ID | Вопрос | Статус | Влияет на |
|----|--------|--------|-----------|
| В2-01 | Панель native portrait (EDID 1080×1920) или landscape + `display_rotate`? omxplayer `--orientation` нужен? | ⏳ ОТКРЫТ — решается на железе в F1 | config.txt, video\_player.c (аргументы), renderer |
| В2-02 | Font renderer: API, формат вывода (буфер? PNG? прямой DispmanX?), зависимости, лицензия | ⏳ ОТКРЫТ — пользователь передаст код | F2, renderer.h |
| В2-03 | Кто и в чём рендерит новые PNG-ассеты (BACK, modes, arrows, weights)? Исходники (SVG/PSD) есть? | ⏳ ОТКРЫТ | F2 п.4 |
| В2-04 | Текущее значение `gpu_mem` в deploy/boot/config.txt | ⏳ ОТКРЫТ — проверить в F1 | config.txt |
| В2-05 | Hostname нового устройства (`indicator-03.local`?) | ⏳ ОТКРЫТ | .env, F1 |
| В2-06 | P-29 на A53: закрыть как неактуальный или доделать OPACITY-путь | ⏳ ОТКРЫТ — по замеру F2 п.7 | renderer\_impl.c |
| В2-07 | Пересборка pi\_nku\_sync / pi\_nku\_menu под ARMv7 (опциональная оптимизация) | ⏳ ОТКРЫТ — низкий приоритет | third\_party |

---

## 7. Контекст для Фазы F0

При начале нового треда передать:

1. Этот документ (`MASTER_PLAN.md` ветки `dev-pi2w`)
2. `cmake/Toolchain-RPiZeroW.cmake` + текущий `Toolchain-RPiZero2W.cmake` (legacy)
3. `build-env/Dockerfile`, `just/build.just`

**Стартовая фраза:**
> Начинаем Фазу F0 ветки dev-pi2w — toolchain под Cortex-A53.
> Прикладываю MASTER_PLAN (2W), оба toolchain-файла, Dockerfile и build.just.

---

*Документ составлен как дельта к MASTER\_PLAN.md ветки `dev-pi`; разделы протокола, аудио и архитектуры процессов не дублируются.*
