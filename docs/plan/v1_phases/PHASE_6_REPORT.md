# Lift Indicator — Отчёт Фазы 6 (Deploy v2)

**Дата:** 2026-06-10
**Проект:** `rpi-multimedia-display`
**Устройство:** Raspberry Pi Zero W Rev 1.1 · ARMv6 · Debian Buster · дисплей 600×1024

---

## 1. Фаза 6 — ЗАКРЫТА ✅

**Цель фазы:** Надёжность производственного устройства: read-only rootfs (overlayfs),
3-раздельная схема SD-карты, WiFi-архитектура при overlayfs, factory image для
прошивки новых устройств, процедура обновления ПО.

### 1.1 Чеклист

| # | Пункт | Статус |
|---|---|---|
| **Блок A — 3-раздельная SD-карта** | | |
| 1 | Процедура работы с образами на macOS через Docker (privileged + kpartx) задокументирована | ✅ |
| 2 | p2 уменьшен с 14.3 GB до 8 GB (`e2fsck` + `resize2fs` + `parted resizepart`) | ✅ |
| 3 | p3 создан: ext4, 6.3 GB, `LABEL=data` | ✅ |
| 4 | Содержимое `/data` скопировано на p3 (`cp -a`) | ✅ |
| 5 | `/etc/fstab`: `LABEL=data /data ext4` добавлен **до** bind-монтов (порядок критичен) | ✅ |
| 6 | Устройство загружено с 3 разделами, `/data` смонтирован с p3 | ✅ |
| 7 | Все bind-монты активны после миграции | ✅ |
| **Блок B — overlayfs** | | |
| 8 | `overlayroot` (Ubuntu-пакет) недоступен в Raspbian Buster → используем `raspi-config` (P-37) | ✅ |
| 9 | `raspi-config nonint disable_overlayfs` — overlayfs ВЫКЛЮЧЕН в factory image | ✅ |
| 10 | `raspi-config nonint enable_overlayfs` — включается `first_boot.sh` при первом старте | ✅ |
| 11 | Проверка: `grep ' / overlay ' /proc/mounts` — используется везде как детектор статуса | ✅ |
| **Блок C — Код** | | |
| 12 | `scripts/first_boot.sh`: шаг 3.5 — WiFi seed из `/boot/wpa_supplicant.conf` | ✅ |
| 13 | `scripts/first_boot.sh`: шаг 4 — `raspi-config nonint enable_overlayfs` | ✅ |
| 14 | `scripts/first_boot.sh`: шаг 6 — сводка на консоль + `sleep 5` + `systemctl reboot` | ✅ |
| 15 | `scripts/run_setup.sh`: шаг 0.5 — hostname + overlayfs статус до pull/push | ✅ |
| 16 | `scripts/setup_pi.sh`: шаг overlayfs — `raspi-config nonint disable_overlayfs` | ✅ |
| 17 | `just/pi.just`: группа `update` — `update-start`, `update-finish`, `reboot`, `overlay-status` | ✅ |
| **Блок D — WiFi-архитектура при overlayfs** | | |
| 18 | `wpa_supplicant.conf` перенесён на `/data/wpa_supplicant.conf` (ext4, writable, переживает overlay) | ✅ |
| 19 | `/etc/fstab`: bind-mount `/data/wpa_supplicant.conf → /etc/wpa_supplicant/wpa_supplicant.conf` | ✅ |
| 20 | Seed-механизм: `/boot/wpa_supplicant.conf` (FAT32) → `first_boot.sh` → `/data/wpa_supplicant.conf` | ✅ |
| 21 | P-39: race condition dhcpcd/wpa_supplicant → `systemctl enable wpa_supplicant.service` | ✅ |
| **Блок E — Factory image** | | |
| 22 | Cleanup-процедура: `cmdline.txt` без `boot=overlay`, hostname, SSH keys, machine-id, журналы | ✅ |
| 23 | Cleanup в Docker через loop device + kpartx (все 3 раздела) | ✅ |
| 24 | `indicator-base-20260610.img.gz` создан (`dd \| gzip`, 16 GB карта) | ✅ |
| 25 | Образ прошит через balenaEtcher, WiFi seed положен в `/boot/` с macOS | ✅ |
| 26 | Полная factory validation пройдена (см. раздел 5) | ✅ |

**Деferred (не блокируют закрытие фазы):**

| # | Пункт | Причина отложения |
|---|---|---|
| — | Рефакторинг `main.c` (render_dispatch + uart_handler) | Функциональность не затронута; откладываем осознанно |
| P-24 | dbus-daemon cosmetic timeout при stop | Низкий приоритет; не влияет на работу |
| P-29 | ARROW slow path | Визуально приемлемо |
| — | `just pi::factory-test` (полный чеклист) | Частично реализован через `just pi::update-*`; отдельная задача |
| — | `build_image.sh` (Блок 6 из плана) | Заменён Gold Master подходом (см. арх. решения) |

---

## 2. Проблемы и решения

### P-36 — Docker Desktop macOS: `losetup -P` не создаёт partition devices

**Симптом:** `e2fsck: No such file or directory` при обращении к `/dev/loop0p2`
после `losetup -Pf image.img`.

**Причина:** Docker Desktop на macOS прокидывает loop-устройство через виртуальную
Linux-VM, но partition scanning (`-P` флаг) не работает — устройства
`/dev/loop0p1`, `/dev/loop0p2` не появляются в пространстве имён контейнера.

**Решение:** `kpartx -av /dev/loop0` создаёт device-mapper маппинги
`/dev/mapper/loop0p{1,2,3}`, с которыми `e2fsck`, `resize2fs`, `mkfs.ext4`
работают нормально.

**Стандартная процедура для macOS:**
```bash
losetup -Pf /mnt/host/image.img   # подключить образ
kpartx -av /dev/loop0              # создать /dev/mapper/loop0p*
# работать через /dev/mapper/loop0p2, а не /dev/loop0p2
kpartx -dv /dev/loop0              # убрать маппинги после работы
losetup -d /dev/loop0              # отключить образ
```

**Статус:** закрыт ✅ (задокументировано как стандартная процедура)

---

### P-37 — `overlayroot` пакет недоступен в Raspbian Buster

**Симптом:** `E: Невозможно найти пакет overlayroot` на Pi.

**Причина:** `overlayroot` — Ubuntu-специфичный пакет. В Raspbian Buster
(Debian-based) его нет ни в основных репозиториях, ни в backports.

**Решение:** Raspbian имеет встроенный механизм через `raspi-config`:
```bash
sudo raspi-config nonint enable_overlayfs   # включить
sudo raspi-config nonint disable_overlayfs  # выключить
```
Модифицирует `/boot/cmdline.txt` (`boot=overlay`) и генерирует initramfs
с overlayfs-хуком. Результат идентичен overlayroot: rootfs read-only + tmpfs overlay.

Детекция статуса унифицирована:
```bash
grep -qs ' / overlay ' /proc/mounts
```
Работает одинаково для raspi-config overlayfs и для Ubuntu overlayroot.

**Статус:** закрыт ✅

---

### P-38 — Race condition: dhcpcd стартует до регистрации wlan0 в wpa_supplicant

**Симптом:** После первого старта с overlayfs WiFi не подключается.
`sudo wpa_cli -i wlan0 status` → `Failed to connect: No such file or directory`.
`ip addr show wlan0` → `NO-CARRIER`. После `sudo systemctl restart dhcpcd` WiFi поднимается нормально.

**Причина:** `wpa_supplicant.service` был **disabled** — стартовал только через
D-Bus активацию по запросу dhcpcd. При overlayfs initramfs меняет порядок
инициализации: dhcpcd стартует раньше, чем wpa_supplicant успевает
зарегистрировать интерфейс `wlan0`. На втором ребуте или после `restart dhcpcd`
порядок случайно оказывался правильным.

**Решение:**

```bash
sudo systemctl enable wpa_supplicant.service

```bash
Сервис стартует явно при `multi-user.target` до того как dhcpcd пытается
поднять wlan0. `enabled` создаёт два симлинка:
- `/etc/systemd/system/dbus-fi.w1.wpa_supplicant1.service`
- `/etc/systemd/system/multi-user.target.wants/wpa_supplicant.service`

**Статус:** закрыт ✅

---

### P-39 — `/lower` недоступен из user-space при raspi-config overlayfs

**Симптом:** `touch /lower/test` → `No such file or directory`.
`sudo mount /dev/mmcblk0p2 /mnt/lower_rw` → `already mounted or mount point busy`.

**Причина:** raspi-config overlayfs монтирует нижний слой (p2) в initramfs
через `switch_root`. После `switch_root` путь `/lower` **не** пробрасывается
в основное пространство имён. В отличие от Ubuntu overlayroot, где `/lower`
остаётся смонтированным и доступным, raspi-config скрывает его полностью.
Повторно смонтировать p2 через `mount(2)` невозможно пока OverlayFS держит
его как lower layer (ядро возвращает `EBUSY`).

**Решение:** 2-reboot update cycle вместо прямой записи в `/lower`:

```bash
just pi::update-start    # disable_overlayfs → reboot → rootfs writable
just pi::deploy          # обычный rsync
just pi::restart
just pi::update-finish   # enable_overlayfs → reboot → rootfs protected
```

**Альтернатива (не реализована):** хранить бинари в `/data/bin/` (ext4, writable).
Требует изменения `ExecStart` в `.service` файлах. Актуально если обновления
частые (> 1 раза в неделю).

**Статус:** закрыт ✅ (принят 2-reboot approach, задокументирован в `just/pi.just`)

---

## 3. Архитектурные решения Фазы 6

| Решение | Обоснование |
|---|---|
| **Gold Master** вместо `build_image.sh` | `build_image.sh` (qemu + chroot) оправдан при > ~50 устройств или CI-воспроизводимости. При текущем масштабе Gold Master проще, надёжнее и «что тестировал — то и прошил» |
| overlayfs через `raspi-config` | `overlayroot` пакет недоступен в Buster (P-37); `raspi-config nonint enable/disable_overlayfs` — нативный механизм Raspbian, работает без дополнительных зависимостей |
| factory image с overlayfs **ВЫКЛЮЧЕН** | `first_boot.sh` включает overlayfs в конце своей работы. Позволяет писать в реальный rootfs (hostname, SSH keys, machine-id, wpa_supplicant) без `overlayroot-chroot` |
| WiFi credentials на `/data/` + bind-mount | С активным overlayfs запись в `/etc/wpa_supplicant/wpa_supplicant.conf` уходит в tmpfs и теряется при ребуте. `/data/` (p3, ext4) writable и persistent. Bind-mount прозрачен для wpa_supplicant |
| WiFi seed через `/boot/wpa_supplicant.conf` | `/boot` — FAT32, виден с любого ПК без Linux-инструментов. Положить файл перед первым стартом = provisioning без USB-клавиатуры и экрана |
| `wpa_supplicant.service enable` | Устраняет race condition с dhcpcd при overlayfs initramfs (P-38). Сервис стартует явно, не через D-Bus on-demand activation |
| 2-reboot update cycle | `/lower` недоступен в raspi-config overlayfs (P-39). `disable_overlayfs` → deploy → `enable_overlayfs` — два лишних ребута при обновлении, но нулевые изменения в структуре проекта |
| 3-partition layout: p1/p2/p3 | p3 (ext4, LABEL=data) изолирует writable данные от protected rootfs. Bind-монты из fstab обеспечивают прозрачность для приложений. Порядок в fstab критичен: `LABEL=data` должен быть **до** bind-монтов |
| Cleanup pass в Docker перед финальным gzip | Образ снимается с работающего устройства → содержит device-specific данные. Docker cleanup (cmdline.txt, hostname, SSH keys, machine-id, first_boot_done) создаёт чистый factory image |

---

## 4. Изменённые файлы

```bash
scripts/
├── first_boot.sh     ← +шаг 3.5 (WiFi seed из /boot)
│                        +шаг 4 (raspi-config enable_overlayfs)
│                        +шаг 6 (сводка + sleep 5 + systemctl reboot)
├── run_setup.sh      ← +шаг 0.5 (hostname + overlayfs статус до pull/push)
└── setup_pi.sh       ← +шаг overlayfs (raspi-config disable_overlayfs, ВЫКЛЮЧЕН по умолчанию)

just/
└── pi.just           ← +группа update:
                           update-start    — disable_overlayfs + reboot
                           update-finish   — enable_overlayfs + reboot
                           reboot          — sudo reboot
                           overlay-status  — детектор статуса overlayfs
```

**На устройстве (изменения в образе):**

```bash
/etc/fstab            ← +LABEL=data /data ext4 (до bind-монтов)
                         +/data/wpa_supplicant.conf bind-mount
/data/
└── wpa_supplicant.conf  ← persistent WiFi credentials (новый файл)
```

**Без изменений (C-код):** `src/`, `src_media_ingest/`, `CMakeLists.txt` — не тронуты.

---

## 5. Factory Validation

Все проверки пройдены на образе `indicator-base-20260610.img.gz`:

```bash
── System ───────────────────────────────────────
  hostname    : indicator-873fb0               ✅ (серийник SoC)
  SSH keys    : пересозданы first_boot.sh      ✅
  machine-id  : пересоздан                     ✅

── overlayfs ────────────────────────────────────
  status      : ВКЛЮЧЁН                        ✅

── WiFi ─────────────────────────────────────────
  inet        : 192.168.88.65/24               ✅
  wpa_supplicant : active                       ✅
  boot seed   : removed from /boot             ✅
  bind-mount  : /dev/mmcblk0p3 → /etc/wpa...  ✅

── /data (p3) ───────────────────────────────────
  p3 mounted  : ✅
  first_boot  : done                           ✅
  bind-mounts : pi_nku_configs resources sounds videos ✅

── services ─────────────────────────────────────
  indicator      : active                      ✅
  media-ingest   : active                      ✅
  i2s-silence    : active                      ✅
  wpa_supplicant : active                      ✅
```

---

## 6. Workflow: прошивка нового устройства

```bash
1. Прошить indicator-base-YYYYMMDD.img.gz через balenaEtcher
2. macOS монтирует /boot (FAT32) автоматически
3. Положить wpa_supplicant.conf в /Volumes/boot/ (опционально — без WiFi работает по Ethernet)
4. Извлечь карту, вставить в Pi, включить питание
5. Старт 1: first_boot.sh → hostname + SSH keys + machine-id + WiFi + overlayfs ON + reboot
6. Старт 2: overlayfs активен, indicator работает, устройство готово (~2 мин от включения)
```

**Workflow: обновление ПО на production устройстве:**

```bash
just pi::update-start    # overlayfs OFF → reboot (~30 сек)
just pi::deploy          # rsync бинари + скрипты + systemd + конфиги
just pi::restart         # проверить что всё работает
just pi::update-finish   # overlayfs ON → reboot (~30 сек)
```

---

## 7. Открытые вопросы

| ID | Вопрос | Приоритет | Фаза |
|----|--------|-----------|------|
| P-24 | dbus-daemon timeout при `stop indicator` | низкий | Фаза 9 (полировка) |
| P-29 | ARROW slow path при каждом появлении | низкий | Фаза 9 |
| — | Рефакторинг `main.c`: render_dispatch.c + uart_handler.c | средний | Фаза 9 |
| — | `just pi::factory-test` полный чеклист (сейчас только `update-start/finish`) | низкий | Фаза 9 |

---

## 8. Контекст для следующей сессии

При начале нового треда передать:

1. `PHASE_6_REPORT.md`
2. `MASTER_PLAN.md` (обновлённый)
3. `DEV_ARCH.md` (обновлённый)
4. `just/pi.just`
5. `scripts/first_boot.sh`, `scripts/run_setup.sh`

**Стартовая фраза:**
> Фазы 0–7, Phase Deploy, Deploy Addendum и Фаза 6 закрыты.
> Следующее: Фаза 8 (font renderer) или Фаза 9 (полировка: рефакторинг main.c, P-24, P-29).

---

*Документ составлен по итогам сессии Фазы 6.*
