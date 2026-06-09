# Transport: UART от STM32

## Что делает этот модуль

`src/transport/uart.c/.h` — единственная точка входа для байт от STM32.

Отвечает за:

1. Открытие `/dev/ttyAMA0` и настройку физических параметров канала (termios).
2. Накопление входящих байт в буфере (non-blocking read).
3. Извлечение полных бинарных фреймов с помощью `protocol_parse_frame()`.
4. Вызов пользовательского коллбэка для каждого валидного (прошедшего CRC) фрейма.

Модуль **ничего не знает** о содержимом фреймов — он передаёт `mu_frame_t` наверх.
Декодирование payload — задача `protocol_parse_payload()` и domain-слоя.

---

## Формат бинарного фрейма

```bash
┌──────┬──────┬────────┬───────────────────┬──────────┬──────────┬──────┐
│ 0xAA │ SIZE │ OPCODE │   DATA (SIZE байт) │  CRC_H   │  CRC_L   │ 0xBB │
└──────┴──────┴────────┴───────────────────┴──────────┴──────────┴──────┘
```

| Поле | Размер | Описание |
|---|---|---|
| `0xAA` | 1 байт | Start of frame (sync1) |
| `SIZE` | 1 байт | Длина DATA в байтах (0–255) |
| `OPCODE` | 1 байт | Тип содержимого. `0xDA` = данные индикатора |
| `DATA` | SIZE байт | Текстовый payload: `#STM:L%d:R%d:A%d:S%d:M%d:E#\r\n` |
| `CRC_H/L` | 2 байта | CRC-16/CCITT-FALSE от `[OPCODE][DATA]` (big-endian) |
| `0xBB` | 1 байт | End of frame (sync2) |

Минимальный фрейм (SIZE=0): 6 байт. Максимальный: 261 байт.

---

## Почему non-blocking + poll(), а не blocking read()

На Pi Zero W нет аппаратного UART FIFO достаточного размера чтобы безопасно
блокировать поток. При blocking read():

- Один заблокированный `read()` не даст обработать сигналы (SIGTERM при
  остановке systemd) и события от других источников (timerfd, media FIFO).
- На практике это означает задержку реакции на SIGTERM до тех пор, пока
  STM32 не пришлёт следующий фрейм.

С `O_NONBLOCK` + `poll()`:

```
poll(fds, N, -1)          ← ждёт события на ЛЮБОМ из fd
  │
  ├── UART: данные есть → uart_process_rx() → читает всё доступное
  │                                         → вызывает коллбэк(и)
  │
  ├── signalfd: SIGTERM → корректное завершение
  │
  └── timerfd: тик → watchdog лог
```

Задержка от прихода байта UART до вызова коллбэка: время до следующего
`poll()` — практически 0, так как `poll()` с timeout=-1 немедленно
просыпается при любом POLLIN.

---

## Почему ring buffer (линейный с memmove), а не byte-by-byte state machine

Альтернатива — обрабатывать по одному байту в state machine внутри uart.c.
Проблема: это дублирование логики `protocol_parse_frame()`, которая уже
тестируется unit-тестами.

Выбранный подход:

```
read() → rx_buf → protocol_parse_frame() → коллбэк
```

`protocol_parse_frame()` сам ищет sync1 (`0xAA`), проверяет длину, CRC,
sync2. Поле `consumed` говорит сколько байт убрать из буфера.
`memmove()` на 260 байт на ARMv6 @ 1 GHz ≈ 0.3 мкс — пренебрежимо мало.

---

## Настройка termios

### Почему ручная установка флагов вместо `cfmakeraw()`

`cfmakeraw()` объявлена под `__USE_MISC` (нужен `_GNU_SOURCE`). Чтобы uart.c
не тянул за собой GNU feature-test макросы, флаги устанавливаются явно —
в `termios_set_raw()`.

Это те же 5 строк, что выполняет `cfmakeraw()` внутри glibc (см. `man 3 cfmakeraw`).

### Почему `fcntl(fd, F_SETFD, FD_CLOEXEC)` вместо `open(..., O_CLOEXEC)`

`O_CLOEXEC` — `#define O_CLOEXEC __O_CLOEXEC`, раскрывается только при
`_GNU_SOURCE`. `FD_CLOEXEC` через `fcntl(F_SETFD)` — чистый POSIX.1-2001.

Close-on-exec нужен потому что `omxplayer` и `aplay` запускаются через
`posix_spawn()` из того же процесса. Без FD_CLOEXEC открытый `/dev/ttyAMA0`
унаследуется дочерним процессом, удерживая порт открытым.

### 8N1 vs 8E1/8O1

Текущая прошивка STM32 использует 8N1 (нет чётности). Параметр `uart_parity_t`
добавлен в `uart_open()` заранее — чтобы при смене прошивки не менять сигнатуру.

```c
uart_open("/dev/ttyAMA0", 115200, UART_PARITY_NONE, on_frame, ctx); /* сейчас  */
uart_open("/dev/ttyAMA0", 115200, UART_PARITY_EVEN, on_frame, ctx); /* если надо */
```

---

## On-target тест (ручной)

Стандартного unit-теста для UART-приёмника на хосте не существует — нет
физического STM32. Для проверки на реальном железе:

### Вариант A: hex dump из `uart_process_rx()`

Собрать индикатор с `CMAKE_BUILD_TYPE=Debug` и подключиться к логам:

```bash
just pi::logs    # journalctl -u indicator -f
```

Каждый фрейм логируется в `on_uart_frame()`. При нормальной работе STM32:

```
indicator[1234]: frame: floor=5 type=0  arrow=1  sound=1  mode=0  ...
```

Если видите только `uart: unknown opcode` или нет сообщений вообще —
STM32 не шлёт данные или неверные параметры порта.

### Вариант B: утилита `uart_rx_dump` (отдельный бинарь, не входит в indicator)

Простейший диагностический инструмент: открывает UART и печатает hex-дамп
каждого полученного фрейма.

```bash
# Собрать вручную на Pi:
gcc -o uart_rx_dump tools/uart_rx_dump.c src/protocol/parser.c -Isrc -lm
./uart_rx_dump /dev/ttyAMA0 115200
```

Пример вывода:
```
[FRAME] opcode=0xDA len=28 CRC=OK
  data: #STM:L16:R5:A1:S1:M0:E#
[FRAME] opcode=0xDA len=28 CRC=OK
  data: #STM:L16:R6:A1:S0:M0:E#
```

Это позволяет проверить правильность CRC, длины фрейма и содержимого payload
независимо от остальной логики индикатора.

### Вариант C: loopback (RX ↔ TX закорочены на плате)

Если Pi-Zero подключена без STM32 и нужно проверить только uart.c:

```bash
# Закоротить RX/TX на /dev/ttyAMA0 (jumper GPIO14↔GPIO15)
# Запустить тест:
./uart_loopback_test /dev/ttyAMA0 115200
```

`uart_loopback_test` — отдельный .c файл в `tools/`, который:
1. Конструирует корректный фрейм с известным payload.
2. Отправляет его через тот же fd.
3. Немедленно читает обратно через `uart_process_rx()`.
4. Проверяет что коллбэк получил идентичный фрейм.