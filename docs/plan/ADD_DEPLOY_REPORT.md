# Lift Indicator — Addendum к Фазе Deploy

**Дата:** 2026-06-02
**Дополнение к:** `PHASE_DEPLOY_REPORT.md`

---

## 1. Исправление артефактов TUI при загрузке

**Проблема:** загрузочные сообщения ядра и systemd писались в tty1 одновременно
с TUI `pi_nku_menu` (cursive), что давало артефакты и мусор на экране.

**Решение:**

`/boot/cmdline.txt` — перенаправить консоль ядра на tty3 + подавить вывод:
```
console=tty3 ... quiet loglevel=3
```

`scripts/run_setup.sh` — очищать tty1 перед запуском меню:
```bash
printf '\033[2J\033[H' > /dev/tty1
chvt 1
```

`deploy/systemd/indicator-setup.service` — добавить сброс tty после завершения:
```ini
TTYVHangup=yes
TTYReset=yes
```

---

## 2. Boot splash

**Цель:** показать фирменное изображение при загрузке вместо чёрного экрана.

### Что не сработало

| Попытка | Причина отказа |
|---|---|
| Подмена `/usr/share/plymouth/themes/pix/splash.png` | plymouth не установлен на Buster Lite |
| `/boot/splash.png` | GPU bootloader на данной прошивке не поддерживает замену |
| `indicator-splash.service` с `fbi` как systemd-сервис | fbi завершался немедленно без blocking TTY; попытки с `openvt`, `sleep infinity`, `TTYPath` не дали результата |

### Итоговое решение

Показ splash встроен в `scripts/run_setup.sh` — в самом начале, до pull, на 3 секунды:

```bash
# ── 0. Boot splash ────────────────────────────────────────────────────────────
if [ -f "$IND/splash/splash.jpg" ] && command -v fbi &>/dev/null; then
    sudo fbi -T 2 --noverbose "$IND/splash/splash.jpg" &
    FBI_PID=$!
    sleep 3
    sudo kill "$FBI_PID" 2>/dev/null || true
    wait "$FBI_PID" 2>/dev/null || true
fi
```

`fbi` требует `CAP_SYS_TTY_CONFIG` для `VT_ACTIVATE` — решено через sudoers:

```bash
# /etc/sudoers.d/indicator-fbi
pi ALL=(root) NOPASSWD: /usr/bin/fbi
```

Добавлено в `scripts/setup_pi.sh`:
```bash
apt-get install -y -q imagemagick fbi
echo "pi ALL=(root) NOPASSWD: /usr/bin/fbi" > /etc/sudoers.d/indicator-fbi
chmod 0440 /etc/sudoers.d/indicator-fbi
```

### Файловая структура splash

```
deploy/boot/
└── splash.jpg              ← исходник (JPG, 600×1024)

/home/pi/indicator/splash/
└── splash.jpg              ← рабочая копия на устройстве
```

Деплой: `just pi::deploy-splash`

### Итоговый boot sequence

```
GPU bootloader     → чёрный экран (disable_splash=1 убран, splash не поддерживается)
ядро + systemd     → tty3 (quiet loglevel=3, не видно)
indicator-setup    → splash.jpg на tty2 (3 сек) → TUI на чистом tty1 → push
indicator          → DispmanX + omxplayer
```

---

## 3. Изменённые файлы

| Файл | Изменение |
|---|---|
| `/boot/cmdline.txt` | `console=tty3`, `quiet loglevel=3` |
| `deploy/boot/config.txt` | убран `disable_splash=1`, `avoid_safe_mode=1` |
| `deploy/boot/splash.jpg` | новый файл |
| `scripts/run_setup.sh` | секция 0: fbi splash перед pull |
| `scripts/setup_pi.sh` | установка `fbi`, `imagemagick`, sudoers |
| `just/pi.just` | рецепт `deploy-splash` |
| ~~`deploy/systemd/indicator-splash.service`~~ | удалён |

---

*Документ является дополнением к `PHASE_DEPLOY_REPORT.md`.*