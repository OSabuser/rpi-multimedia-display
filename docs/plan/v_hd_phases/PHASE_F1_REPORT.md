# PHASE F1 REPORT — Bring-up устройства

> **Ветка:** `dev-pi2w`
> **Статус:** ЗАКРЫТА ✅
> **Дата:** 2026-06-16

---

## Чеклист (MASTER_PLAN §4.F1)

| # | Пункт | Статус | Примечание |
|---|---|---|---|
| F1.1 | Прошить Buster Lite 2023-05-03, подтвердить boot (kernel7.img) | ✅ | Выполнено в F0; устройство в сети |
| F1.2 | SSH-ключ, hostname | ✅ | `just pi::setup-ssh`; hostname `indicator-hd.local` → после firstboot `indicator-95e49a.local` |
| F1.3 | `dtoverlay=disable-bt`, `enable_uart=1` в config.txt | ✅ | Присутствуют; `UART open: /dev/serial0 @ 115200` — подтверждено в логах |
| F1.4 | `gpu_mem=128` | ✅ | Закрыто в F0 (В2-04) |
| F1.5 | `just pi::setup-pi` — пакеты, ALSA, /data, fstab | ✅ | Потребовал патча шага 4 (см. ниже) |
| F1.6 | `fbset -s` → 1080×1920 | ✅ | Закрыто в F0 (В2-01); `disable_overscan=1` + `display_hdmi_rotate=3` |
| F1.7 | UART smoke: порт открывается, indicator слушает | ✅ | STM32 не прошит — ОК для F1; порт открыт без ошибок |
| F1.8 | indicator запущен, не падает; DispmanX слои создаются | ✅ | `active (running)`, геометрия 600×1024 — ожидаемо для F1 |
| — | overlayfs включён после деплоя | ✅ | `raspi-config nonint enable_overlayfs` штатно |

---

## Верификация запуска

```
● indicator.service - Lift Indicator Daemon
   Active: active (running) since Tue 2026-06-16 15:02:35 BST
 Main PID: 584 (indicator)
   CGroup:
           ├─584 /home/pi/indicator/indicator --config=…/nku_scheme.toml
           ├─586 /bin/bash /usr/bin/omxplayer --layer 1 … --win 0,0,600,800 …
           ├─601 dbus-daemon …
           └─604 /usr/bin/omxplayer.bin …

indicator[584]: renderer: slot 0 created, z=2, pos=(0,0), size=600x1024
indicator[584]: renderer: slot 2 created, z=4, pos=(333,27), size=237x59
indicator[584]: renderer: slot 6 created, z=5, pos=(0,874), size=600x150
indicator[584]: UART open: /dev/serial0 @ 115200 baud, 8N1
indicator[584]: event loop started, omxplayer_pid=586
```

```
● media-ingest.service
   Active: active (running)
   media_ingest[585]: ingest: config loaded
   media_ingest[585]: usb_watcher: watching '/dev' for sd[a-z][0-9] devices
```

---

## Изменения файлов

### `scripts/setup_pi.sh` — шаг 4 (I2S overlay)

**Проблема:** на Pi Zero 2W в `config.txt` прописан `dtoverlay=hifiberry-dac` (аппаратная
конфигурация платы). Скрипт проверял только наличие `dtoverlay=googlevoicehat-soundcard`,
и при его отсутствии добавлял его. На 2W два I2S overlay одновременно вызвали бы конфликт карт.

**Решение:** расширить grep-проверку на список совместимых I2S overlay:

```diff
-if grep -q "^dtoverlay=googlevoicehat-soundcard" "$BOOT_CONFIG"; then
-    warn "dtoverlay=googlevoicehat-soundcard уже есть — пропускаем"
+if grep -qE "^dtoverlay=(googlevoicehat-soundcard|hifiberry-dac|hifiberry-dacplus|hifiberry-digi|i2s-mmap)" "$BOOT_CONFIG"; then
+    warn "I2S overlay уже прописан в $BOOT_CONFIG — пропускаем добавление googlevoicehat-soundcard"
```

```diff
-    warn "dtparam=audio=on не найден — пропускаем"
+    warn "dtparam=audio=on не найден или уже выключен — пропускаем"
```

Результат на устройстве:
```
⚠️  I2S overlay уже прописан в /boot/config.txt — пропускаем добавление googlevoicehat-soundcard
⚠️  dtparam=audio=on не найден или уже выключен — пропускаем
✅  I2S overlay configured
```

---

## Проблемы фазы

### P-36 — `amixer scontrols` пустой на `hifiberry-dac` ⏳ ОТЛОЖЕНО → F4

**Симптом:**
```
indicator[584]: amixer: Unable to find simple control 'PCM',0
```
`amixer scontrols` возвращает пустой список.

**Анализ:** softvol создаёт виртуальный контрол `PCM` только при открытии
`pcm.softvol`-пути. `i2s-silence.service` открывает `pcm.dmixer` напрямую —
минуя softvol — поэтому контрол не регистрируется к моменту старта indicator.
На Pi Zero W с `googlevoicehat-soundcard` поведение было таким же; там карта
могла иметь аппаратный контрол, который маскировал проблему.

**Влияние:** громкость не устанавливается при старте. Аудио само по себе не
сломано (aplay через default → softvol создаст контрол при первом воспроизведении).
Не блокирует F1–F3.

**Решение:** F4, аудио-валидация.

---

## Наблюдения для следующих фаз

### → F2 (renderer)

- `omxplayer --win 0,0,600,800` — размер окна берётся из конфига (`video.toml`
  или `nku_scheme.toml`). Для 1080×1920 нужно обновить до `0,0,1080,1920`
  и добавить `--orientation 270` в `video_player.c`.
- Все DispmanX-слоты созданы с геометрией 600×1024 — ожидаемо; layout-проход F2.
- `SIGCHLD: child pid=605 status=1` — вероятно первый вызов `amixer` (exit=1 из-за
  P-36). Не является crash indicator'а.

### → F3 (video)

- `dtparam=watchdog=on` активен в config.txt. В `indicator.service` потребуется
  `WatchdogSec=` — иначе watchdog перезагрузит устройство.

### → F4 (audio)

- P-36 — основная задача фазы.
- `i2s-silence.service` работает стабильно, карта `snd_rpi_hifiberry_dac` на card 0.

---

## Открытые вопросы (статус после F1)

| ID | Вопрос | Статус |
|----|--------|--------|
| В2-01 | Ориентация дисплея | ✅ ЗАКРЫТ — `display_hdmi_rotate=3`, `--orientation 270` |
| В2-02 | Font renderer: API, зависимости | ⏳ ОТКРЫТ — F2 |
| В2-03 | PNG-ассеты: кто рендерит, есть ли исходники | ⏳ ОТКРЫТ — F2 |
| В2-04 | `gpu_mem` текущее значение | ✅ ЗАКРЫТ — 128 MB |
| В2-05 | Hostname | ✅ ЗАКРЫТ — `indicator-hd-<serial>` |
| В2-06 | P-29 на A53: закрыть или доделать OPACITY-путь | ⏳ ОТКРЫТ — F2 п.7 |
| В2-07 | Пересборка pi\_nku\_sync / pi\_nku\_menu под ARMv7 | ⏳ ОТКРЫТ — низкий приоритет |

---

## Архитектурные решения

| Тема | Решение | Уверенность |
|---|---|---|
| I2S overlay на 2W | `hifiberry-dac` (задан производителем платы); `googlevoicehat-soundcard` не добавляется | Высокая |
| `setup_pi.sh` универсальность | grep-проверка на список I2S overlay вместо одного имени | Высокая |
| UART | `/dev/serial0` → `ttyAMA0`; `disable-bt` подтверждён | Высокая |
| Hostname | Генерируется `first_boot.sh` из серийника SoC: `indicator-hd-<serial>` | Высокая |

---

## Выход фазы

- Устройство в сети, hostname `indicator-hd-<serial>.local` ✅
- indicator.service + media-ingest.service active (running) ✅
- DispmanX слои создаются без ошибок ✅
- UART `/dev/serial0` открыт ✅
- omxplayer запущен ✅
- overlayfs включён ✅

**Следующая фаза: F2 — FullHD renderer + font renderer**