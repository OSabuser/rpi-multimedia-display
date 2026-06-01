# Lift Indicator — План Фазы Deploy v2

**Выполняется после:** Фаза 5 (аудио) + Фаза 6 (systemd lifecycle) + Фаза 7 (media ingest)
**Цель:** производственно-готовый образ, read-only rootfs, воспроизводимый деплой

---

## Блок 1 — Рефакторинг main.c

**Мотивация:** main.c вырастет до ~800+ строк после Фаз 5–7. Логика обработки
фреймов и рендеринга не относится к composition root.

### 1.1 Выделить `src/app/render_dispatch.c/.h`

Перенести из main.c:
- `mode_to_rel_path()`
- `renderer_apply_mode()`
- `renderer_apply_dispatch()`
- `renderer_apply_elevator()`

Зависимости: `app_t`, `renderer.h`, `domain/types.h` — никаких poll/signal.
Тестируется на хосте со stub-renderer.

### 1.2 Выделить `src/app/uart_handler.c/.h`

Перенести из main.c:
- `on_uart_frame()`
- `maybe_clear_mcu_notification()`

Зависимости: `app_t`, `protocol/parser.h`, `domain/state.h`.

**Результат:** main.c ~300 строк — только poll loop + init + cleanup.

---

## Блок 2 — Исправление P-24 (dbus-daemon)

**Симптом:** `indicator.service: Failed with result 'timeout'` при каждой остановке.

### Варианты (выбрать один):

| Вариант | Описание | Сложность |
|---|---|---|
| A | Исследовать `--no-dbus` флаг omxplayer | низкая |
| B | `ExecStopPost=pkill -KILL -f dbus-daemon` в indicator.service | низкая |
| C | D-Bus API для graceful quit omxplayer перед killpg | высокая |

Рекомендация: попробовать A, если не поддерживается — B.

---

## Блок 3 — Исправление P-29 (ARROW slow path)

**Симптом:** при каждом появлении стрелки `slot created` вместо `fast-updated`.

**Причина:** `renderer_hide()` вызывает `destroyImageLayer` → `initialized=0`.

**Решение:** скрывать через `ELEMENT_CHANGE_OPACITY=0` вместо destroy.

```c
// renderer_impl.c — renderer_hide():
// вместо destroyImageLayer:
vc_dispmanx_element_change_attributes(update, slot->element,
    ELEMENT_CHANGE_OPACITY, 0, 0, ...);
// при следующем show: fast_update если PNG тот же размер
```

**Проверка:** в логе должно появиться `slot fast-updated` для стрелки.

---

## Блок 4 — overlayroot (read-only rootfs)

**Мотивация:** Pi в лифте — питание режется рубильником. SD-карта умирает от
записи при внезапном отключении.

### 4.1 Установить overlayroot

```bash
# В setup_pi.sh добавить:
apt-get install -y overlayroot
echo 'overlayroot="tmpfs"' > /etc/overlayroot.conf
```

### 4.2 Проверить bind-монты при overlayroot

С overlayroot rootfs становится read-only, tmpfs overlay — for writes.
Bind-монты из fstab на `/home/pi/indicator/*` должны отрабатывать корректно
(mount происходит после rootfs, /data — отдельный raздел ext4).

**Тест:** `touch /usr/.write_test` → должно упасть с `Read-only file system`.

### 4.3 Writable пути при overlayroot

| Путь | Тип | Запись |
|---|---|---|
| `/data/*` | ext4 раздел | ✅ всегда |
| `/home/pi/indicator/pi_nku_configs/` | bind → /data | ✅ |
| `/home/pi/indicator/resources/` | bind → /data | ✅ |
| `/tmp`, `/run` | tmpfs | ✅ (в RAM) |
| `/home/pi/indicator/indicator` | rootfs overlay | ❌ при reboot |
| `/etc/systemd/system/` | rootfs overlay | ❌ при reboot |

**Следствие:** `just pi::deploy` (rsync бинарей) работает только если rootfs
временно переведён в writable (`overlayroot-chroot`) или overlayroot отключён.

### 4.4 Workflow деплоя при overlayroot

```bash
# Вариант A: деплой через overlayroot-chroot (сложнее)
ssh pi "sudo overlayroot-chroot"
# затем rsync внутри chroot

# Вариант B: отключить overlayroot для деплоя, включить обратно
# В /etc/overlayroot.conf: overlayroot=""
# reboot → deploy → overlayroot="tmpfs" → reboot
```

Рекомендация для разработки: overlayroot включать только для производственного
образа, для dev-Pi — оставить writable.

---

## Блок 5 — Трёхраздельная схема SD-карты

**Текущее состояние:** `/data` — директория на rootfs. Это временно.

### 5.1 Целевая разметка

```
/dev/mmcblk0p1   FAT32   256 MB   /boot       ← firmware, read-only
/dev/mmcblk0p2   ext4    6  GB    /            ← rootfs, overlayroot
/dev/mmcblk0p3   ext4    ~9 GB   /data         ← writable, ext4+journal
```

### 5.2 Создать раздел на существующей Pi

```bash
# На Pi (осторожно — данные с карты не пострадают если всё правильно):
# 1. Проверить текущую разметку
lsblk

# 2. Если место есть после p2 — создать p3 через fdisk
sudo fdisk /dev/mmcblk0
# n → p → 3 → Enter → Enter → w

# 3. Форматировать
sudo mkfs.ext4 -L data /dev/mmcblk0p3

# 4. Примонтировать и перенести данные
sudo mkdir -p /mnt/data_new
sudo mount /dev/mmcblk0p3 /mnt/data_new
sudo rsync -a /data/ /mnt/data_new/
sudo umount /mnt/data_new

# 5. Обновить fstab
# /dev/mmcblk0p3  /data  ext4  defaults,noatime  0  2
# (убрать старые bind mount записи, они уже привязаны к /data)
```

### 5.3 Обновить setup_pi.sh

Вместо `mkdir -p /data` — ожидать что `/data` уже смонтирован как отдельный
раздел (по UUID в fstab). Добавить проверку:
```bash
mountpoint -q /data || { echo "ERROR: /data not mounted"; exit 1; }
```

---

## Блок 6 — build_image.sh (воспроизводимый образ)

**Цель:** образ собирается из кода, а не снимается с живой карты.

### 6.1 Структура скрипта

```bash
scripts/build_image.sh VERSION
```

Шаги внутри:
1. Скачать официальный Buster Lite `.img.xz` (зафиксированный URL + sha256)
2. Расширить образ, создать p3 (`/data`, ext4, ~9 GB)
3. Смонтировать через loop device
4. `chroot` + `qemu-arm-static`:
   - Запустить `setup_pi.sh` (без интерактивных частей)
   - Скопировать все `deploy/*` в нужные места
   - Включить `overlayroot`
   - Прописать fstab с UUID разделов
   - `systemctl enable indicator.service indicator-setup.service indicator-firstboot.service`
   - Создать `/data/first_boot_done = false` (не создавать — пусть first_boot работает)
5. Umount
6. Сжать: `zstd` (быстрее gzip, лучше сжатие)
7. SHA256 рядом

### 6.2 Добавить в just

```just
[doc('Собрать flashable производственный образ')]
[group('release')]
image VERSION=`git describe --tags --always --dirty`:
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p release
    bash scripts/build_image.sh "{{VERSION}}"
    sha256sum "release/indicator-{{VERSION}}.img.zst" \
        > "release/indicator-{{VERSION}}.img.zst.sha256"
    echo "  ✅  release/indicator-{{VERSION}}.img.zst"
```

### 6.3 VERSION файл

Добавить создание `VERSION` при сборке:

```cmake
# В CMakeLists.txt:
execute_process(
    COMMAND git describe --tags --always --dirty
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
file(WRITE ${CMAKE_BINARY_DIR}/VERSION "${GIT_VERSION}\n")
```

Деплоить рядом с бинарём: `just pi::deploy` копирует `build/pi/VERSION` → `PI_DIR/VERSION`.

```just
# В smoke_test.sh добавить:
[ -f /home/pi/indicator/VERSION ] && \
    ok "VERSION: $(cat /home/pi/indicator/VERSION)" || \
    warn "VERSION file missing"
```

---

## Блок 7 — Производственный pipeline

### 7.1 Инструкция для сборщика (финальная)

```
ИНСТРУКЦИЯ: ПРОШИВКА ИНДИКАТОРА ЛИФТА

1. Вставить microSD (≥16 GB) в ридер.
2. Открыть balenaEtcher.
3. Flash from file → indicator-vX.Y.Z.img.zst
4. Select target → SD карта
5. Flash! (ждать зелёного "Flash Complete")
6. Извлечь карту → вставить в Pi → включить питание.
7. Подождать 90 секунд (первый старт: hostname + SSH keys + MCU sync).
8. Готово.
```

### 7.2 Где хранить образы

Корпоративный NAS или S3-совместимое хранилище:
```
indicator/releases/
├── indicator-v1.0.0.img.zst
├── indicator-v1.0.0.img.zst.sha256
└── CHANGELOG.md
```

---

## Блок 8 — Финальная валидация производственного образа

Добавить `just pi::factory-test` — тест для сборщика:

```bash
# factory_test.sh — запускается на только что прошитой Pi
# Результат: PASS/FAIL на экране терминала

check "indicator.service active"
check "setup: status=ok в journald"
check "UART /dev/serial0 открывается"
check "omxplayer процесс жив"
check "/data writable"
check "rootfs read-only"
check "VERSION file exists"
check "bind mounts активны"
# Итог: print hostname + version для протокола
```

---

## Приоритет и зависимости

```
Блок 1 (рефакторинг main.c)    → после Фазы 5+6+7, независимо от остальных
Блок 2 (P-24 dbus)             → в любое время, изолировано
Блок 3 (P-29 arrow)            → в любое время, изолировано
Блок 4 (overlayroot)           → до Блока 5 и 6
Блок 5 (разделы)               → до Блока 6
Блок 6 (build_image.sh)        → требует Блоков 4+5
Блок 7 (pipeline)              → требует Блока 6
Блок 8 (factory-test)          → финальный
```

**Минимально необходимо для производства:** Блоки 4 → 5 → 6 → 7.
Блоки 1–3 — качество, но не блокируют отгрузку.

---

*Документ составлен по итогам сессии Фазы Deploy.*