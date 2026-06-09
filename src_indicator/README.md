# Main: event loop, сигналы, watchdog

## Зачем `#define _GNU_SOURCE`

В `main.c` используются Linux-специфичные системные вызовы, недоступные
без `_GNU_SOURCE`:

| Символ | Заголовок | Почему нужен |
|---|---|---|
| `signalfd()`, `SFD_NONBLOCK`, `SFD_CLOEXEC` | `<sys/signalfd.h>` | Linux-specific |
| `timerfd_create()`, `TFD_NONBLOCK`, `TFD_CLOEXEC` | `<sys/timerfd.h>` | Linux-specific |
| `CLOCK_MONOTONIC` | `<time.h>` | POSIX, но видим только с _GNU_SOURCE в glibc |
| `sigemptyset`, `sigprocmask`, `SIG_BLOCK` | `<signal.h>` | POSIX.1-2001, но clangd без _GNU_SOURCE не видит |

`#define _GNU_SOURCE` должен стоять **до любых `#include`** — иначе заголовки
уже включились без нужных guard-блоков.

При сборке с `cmake -DCMAKE_C_EXTENSIONS=ON` компилятор использует `-std=gnu11`,
который неявно определяет `_GNU_SOURCE`. Это объясняет почему сборка проходила,
но clangd (LSP) всё равно подчёркивал символы красным — clangd читает исходный
файл независимо, без флагов компилятора, если `_GNU_SOURCE` не определён явно.

---

## Архитектура event loop

Весь `main.c` — это один поток, один системный вызов `poll()`, три файловых
дескриптора.

```bash
                    ┌──────────────────────────────┐
                    │          poll(fds, 3, -1)     │
                    │  ждёт события на ЛЮБОМ из fd  │
                    └──────┬────────────┬─────┬─────┘
                           │            │     │
                      POLLIN          POLLIN POLLIN
                           │            │     │
                    uart_fd         sig_fd  timer_fd
                           │            │     │
              uart_process_rx()   handle_  on_watchdog_
                           │      signal()   tick()
                           │
                    on_uart_frame()
                           │
              protocol_parse_payload()
              floor_decode()
              state_apply_frame()
                           │
              [Фаза 4] renderer_update()
              [Фаза 5] audio_play()
```

### Почему один поток, а не несколько

- Pi Zero W — одноядерный ARMv6. Контексты потоков дороги.
- Весь горячий путь (UART → парсер → рендерер → аудио) завершается
  за единицы миллисекунд. Блокирующих операций нет.
- Отсутствие shared state между потоками = отсутствие мьютексов
  в критическом пути = предсказуемая latency.

Исключение: аудиоплеер (Фаза 5) будет в отдельном pthread, потому что
`waitpid(aplay_pid)` должен ждать завершения аудио, не блокируя loop.

---

## signalfd: почему так, а не `signal()`/`sigaction()`

### Традиционный способ

```c
signal(SIGTERM, my_handler);

void my_handler(int sig) {
    g_running = 0;   // ← async-signal-safe ли это? только если volatile sig_atomic_t
}
```

Проблемы:

- Обработчик вызывается асинхронно, прерывая любой системный вызов.
- Из обработчика безопасно вызывать только `async-signal-safe` функции
  (список короткий — `write()`, `_exit()` и ещё ~30 функций).
- `syslog()`, `malloc()`, `pthread_mutex_lock()` — **не безопасны** внутри обработчика.

### Способ с signalfd

```c
/* 1. Заблокировать сигналы от стандартной доставки */
sigprocmask(SIG_BLOCK, &mask, NULL);

/* 2. Получить fd, из которого можно читать как из файла */
sig_fd = signalfd(-1, &mask, SFD_NONBLOCK);
```

Теперь сигнал — это просто данные в файловом дескрипторе:

```c
if (fds[FD_SIG].revents & POLLIN) {
    struct signalfd_siginfo si;
    read(sig_fd, &si, sizeof(si));   /* читаем сигнал как структуру */
    if (si.ssi_signo == SIGTERM) {
        syslog(LOG_NOTICE, "SIGTERM");  /* syslog безопасен здесь */
        running = 0;
    }
}
```

Преимущества:

- Обработка сигнала происходит в основном потоке, в нужный момент.
- Доступны все функции, не только async-signal-safe.
- Один `poll()` ждёт и данные UART, и сигналы — без гонок.

### SIGCHLD

В Фазе 3 `omxplayer` запускается через `posix_spawn()`. Когда он падает
или завершается, ядро посылает `SIGCHLD` родительскому процессу.
Через signalfd мы получаем это как POLLIN на sig_fd и вызываем
`video_player_check_and_restart()` — без async-signal-safe ограничений.

---

## timerfd: watchdog tick

```c
timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)
timerfd_settime(fd, 0, &ts, NULL)   /* период = WATCHDOG_INTERVAL_S */
```

`CLOCK_MONOTONIC` — монотонные часы, не прыгают при смене системного времени
(NTP-синхронизация, `hwclock`). Для периодических задач всегда лучше чем
`CLOCK_REALTIME`.

При срабатывании таймера `poll()` вернёт POLLIN на timer_fd. Читаем uint64_t —
число пропущенных тиков (обычно 1). Вызываем `on_watchdog_tick()`.

Зачем watchdog лог каждые 30 секунд:

- При диагностике в поле (без live UART к устройству) нужно понять
  работает ли демон и получает ли фреймы от STM32.
- `journalctl -u indicator --since "10 minutes ago"` сразу покажет
  последние счётчики фреймов.

---

## Порядок инициализации и почему он важен

```bash
1. config_load()      — конфиг нужен всем остальным
2. state_init()       — до первого фрейма
3. signalfd           — до запуска любых дочерних процессов
4. timerfd            — не критично, но до event loop
5. video_player_open  — Фаза 3 (дочерний процесс)
6. renderer_init      — Фаза 4 (захватывает DispmanX display)
7. audio_player_open  — Фаза 5 (pthread)
8. uart_open          — последним: только после готовности всех потребителей
```

UART открывается последним намеренно. Иначе `on_uart_frame()` может быть
вызван до инициализации renderer или audio — и упасть на NULL-дереференсе.

---

## Как это взаимодействует с systemd

```bash
systemd
  └── [Restart=always] indicator.service
         └── /home/pi/indicator/indicator
```

`indicator.service` содержит `Restart=always, RestartSec=2`. Это значит:

- Если `indicator` завершится с ненулевым кодом — systemd перезапустит через 2с.
- Если `indicator` завершится с кодом 0 — всё равно перезапустит (Restart=always).
- Корректное завершение по SIGTERM (`running = 0` → `return 0`) systemd
  интерпретирует как штатную остановку при `systemctl stop`.

`LOG_DAEMON` в `openlog()` направляет логи в системный журнал:

```bash
journalctl -u indicator -f        # tail в реальном времени
journalctl -u indicator -n 100    # последние 100 строк
journalctl -u indicator --since "1 hour ago"
```

Уровни `syslog`:

| Уровень | Когда |
|---|---|
| `LOG_CRIT` | Ошибка запуска, невозможно продолжить |
| `LOG_ERR` | Ошибка устройства (UART, DispmanX) |
| `LOG_WARNING` | Ошибка разбора фрейма, нештатная ситуация |
| `LOG_NOTICE` | Старт, стоп, смена режима |
| `LOG_INFO` | Каждый фрейм (нормальная работа) |
| `LOG_DEBUG` | SIGCHLD, неизвестные opcode, детали |

В продакшне `LOG_DEBUG` отключается через `rsyslog.conf` или `journald.conf`,
чтобы не засорять журнал.
