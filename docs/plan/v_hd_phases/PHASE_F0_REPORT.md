# PHASE F0 REPORT — Ветки, Toolchain, Сборка

> **Ветка:** `dev-pi2w`
> **Статус:** ЗАКРЫТА ✅
> **Дата:** 2026-06-15

---

## Чеклист (MASTER_PLAN §4.F0)

| # | Пункт | Статус | Примечание |
|---|---|---|---|
| F0.1 | `dev` → `dev-pi`; создать `dev-pi2w` | ✅ | Выполнено на хосте через `git branch -m` |
| F0.2 | Dockerfile: добавить `arm-zig-cc-a53`, проверка | ✅ | Wrapper добавлен, проверка в `RUN echo` |
| F0.3 | Переписать `cmake/Toolchain-RPiZero2W.cmake` под zig | ✅ | Структура скопирована с `Toolchain-RPiZeroW.cmake`, CPU заменён |
| F0.4 | `build.just`: `_configure-pi` и `pi-debug` → `Toolchain-RPiZero2W.cmake` | ✅ | Два вхождения исправлены |
| F0.5 | `just build::pi` — `readelf -A` показывает `Tag_CPU_arch: v8` | ✅ | См. вывод ниже |
| F0.6 | `just build::test` — host-тесты зелёные | ✅ | Все тесты PASS без изменений |
| F0.7 | `.clangd` — путь компилятора не требует изменений | ✅ | `build/pi` читает `compile_commands.json`, wrapper не хардкожен |

---

## Верификация сборки

```
root@51e4361bb551:/project# readelf -A build/pi/indicator | grep -E 'Tag_CPU|Tag_ABI'
  Tag_CPU_name: "cortex-a53"
  Tag_CPU_arch: v8
  Tag_CPU_arch_profile: Application
  Tag_ABI_align_needed: 8-byte
  Tag_ABI_align_preserved: 8-byte, except leaf SP
  Tag_CPU_unaligned_access: v6
```

---

## Изменения файлов

### `build-env/Dockerfile`

Добавлен wrapper `arm-zig-cc-a53` после блока `arm-zig-cc`:

```dockerfile
# ─── Wrapper: arm-zig-cc-a53 (Pi Zero 2W, Cortex-A53) ───────────────────────

RUN printf '#!/bin/sh\nexec zig cc -target arm-linux-gnueabihf -mcpu=cortex_a53 "$@"\n' \
    > /usr/local/bin/arm-zig-cc-a53 && chmod +x /usr/local/bin/arm-zig-cc-a53
```

Добавлена строка проверки в `RUN echo`:

```dockerfile
echo "=== arm-zig-cc-a53 ===" && arm-zig-cc-a53 --version 2>&1 | head -1 && \
```

### `cmake/Toolchain-RPiZero2W.cmake`

Полностью переписан (был legacy gcc-версия времён P-08).
Новая структура идентична `Toolchain-RPiZeroW.cmake`, отличия:

| Параметр | RPiZeroW | RPiZero2W |
|---|---|---|
| `CMAKE_C_COMPILER` | `/usr/local/bin/arm-zig-cc` | `/usr/local/bin/arm-zig-cc-a53` |
| `CMAKE_C_FLAGS_INIT` | `"-marm -mfloat-abi=hard"` | `"-mfloat-abi=hard"` |
| CPU (в wrapper) | `arm1176jzf_s` | `cortex_a53` |

`-marm` убран: Cortex-A53 полноценно исполняет Thumb-2, проблема ARMv6/Thumb-1 неактуальна.

### `just/build.just`

Исправлены два вхождения `Toolchain-RPiZeroW.cmake` → `Toolchain-RPiZero2W.cmake`:
- `_configure-pi`
- `pi-debug`

---

## Железо (начало F1)

Параллельно с F0 поднято устройство Pi Zero 2W на чистом образе
`2023-05-03-raspios-buster-armhf-lite.img`. Закрыты открытые вопросы:

### В2-01 — Ориентация дисплея ✅ ЗАКРЫТ

Панель нативно landscape (CEA 16, 1920×1080 @ 60Hz). Портрет достигается через:

```
display_hdmi_rotate=3   # 270° поворот
disable_overscan=1      # без этого framebuffer 1016×1856 вместо 1080×1920
```

Результат после `disable_overscan=1` и перезагрузки:

```
fbset -s → geometry 1080 1920 1080 1920 32   ✅
```

Следствие для кода: `omxplayer` потребует флага `--orientation 270` в `video_player.c` (F2/F3).

### В2-04 — `gpu_mem` ✅ ЗАКРЫТ

```
gpu_mem=128
```

Значение уже выставлено. Финальная проверка достаточности — в F2 п.8 (`vcgencmd get_mem gpu`, `vcdbg reloc`).

### В2-05 — Hostname ✅ ЗАКРЫТ

Префикс `indicator-hd-`, суффикс из серийника SoC. Изменение в `first_boot.sh`:

```diff
-HOSTNAME="indicator-${SERIAL}"
+HOSTNAME="indicator-hd-${SERIAL}"
```

---

## Решения, принятые в F0

### Sysroot — не перефетчивать

`build-env/pi-sysroot/` (содержит `/opt/vc` и `libpng16.so`/`libz.so`) идентичен
для Pi Zero W и Pi Zero 2W — оба Buster armhf, оба VideoCore IV userland.
Рефетч не требуется.

### Образ ОС — универсальный

`2023-05-03-raspios-buster-armhf-lite.img` содержит все ядра:
- `kernel.img` (ARMv6, Zero W)
- `kernel7.img` (ARMv7, Zero 2W) ✅
- `bcm2710-rpi-zero-2-w.dtb` ✅

Отдельный образ под 2W не нужен.

### Чистый образ вместо Golden Image

Решено использовать чистый Buster Lite, а не `indicator-base-v1.0.0.img.gz`.
Причины: `config.txt` Zero W содержит несовместимые `hdmi_timings` 600×1024;
overlayfs уже включён; бинари ARMv6. Чистый образ → честный factory image
для 2W-линейки в F5.

---

## Открытые вопросы (статус после F0)

| ID | Вопрос | Статус |
|----|--------|--------|
| В2-01 | Ориентация дисплея | ✅ ЗАКРЫТ — `display_hdmi_rotate=3`, `--orientation 270` |
| В2-02 | Font renderer: API, зависимости | ⏳ ОТКРЫТ — F2 |
| В2-03 | PNG-ассеты: кто рендерит, есть ли исходники | ⏳ ОТКРЫТ — F2 |
| В2-04 | `gpu_mem` текущее значение | ✅ ЗАКРЫТ — 128 MB |
| В2-05 | Hostname | ✅ ЗАКРЫТ — `indicator-hd-<serial>` |
| В2-06 | P-29 на A53: закрыть или доделать OPACITY-путь | ⏳ ОТКРЫТ — F2 п.7 |
| В2-07 | Пересборка pi_nku_sync / pi\_nku_menu под ARMv7 | ⏳ ОТКРЫТ — низкий приоритет |

---

## Выход фазы

- ARM-бинари под Cortex-A53 в `build/pi/` ✅
- Host-тесты зелёные ✅
- Pi Zero 2W в сети, framebuffer 1080×1920 ✅
- Docker-образ содержит оба wrapper'а (`arm-zig-cc` + `arm-zig-cc-a53`) ✅

**Следующая фаза: F1 — Bring-up устройства**
