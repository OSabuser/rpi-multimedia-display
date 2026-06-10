# uart_rx_dump

Диагностическая утилита для наблюдения за бинарным UART-трафиком от STM32.

Показывает **всё**: сырые байты каждого `read()`, результат разбора каждого
фрейма включая ошибки, и полностью декодированный payload для opcode `0xDA`
и `0xAA`. Предназначена для отладки на Pi без осциллографа или логического
анализатора.

---

## Сборка и деплой

```bash
# devcontainer — только утилита:
just build::pi-dump

# или все Pi-бинари сразу:
just build::pi

# хост — задеплоить только tools/:
just pi::deploy-tools

# или полный деплой:
just pi::deploy
```

Бинарь попадает в `/home/pi/indicator/tools/uart_rx_dump`.

---

## Запуск

### Через just (рекомендуется)

```bash
just pi::dump
```

Команда автоматически:

1. Останавливает `indicator.service` (освобождает `/dev/ttyAMA0`)
2. Запускает `uart_rx_dump` через SSH с интерактивным выводом
3. При выходе (Ctrl+C) поднимает `indicator.service` обратно

### Вручную на Pi

```bash
sudo systemctl stop indicator.service
./tools/uart_rx_dump                        # /dev/ttyAMA0 @ 115200 baud
./tools/uart_rx_dump /dev/ttyAMA0 115200   # явные параметры
```

Ctrl+C для выхода.

---

## Параметры

```bash
uart_rx_dump [device] [baud]
```

| Аргумент | По умолчанию | Описание |
|---|---|---|
| `device` | `/dev/ttyAMA0` | Путь к UART-устройству |
| `baud` | `115200` | Скорость. Допустимые: 9600, 19200, 38400, 57600, 115200 |

---

## Пример вывода

```bash
uart_rx_dump: /dev/ttyAMA0 @ 115200 baud
Verbose mode: showing all bytes and parse events
─────────────────────────────────────────

12:07:04.441  raw +8 bytes:  AA 1A DA 23 53 54 4D 3A
12:07:04.444  raw +24 bytes: 4C 31 36 3A 52 35 3A 41 31 3A 53 31 3A 4D 30 3A 45 23 0D 0A 00 19 95 BB
12:07:04.445  #1     PARSE_OK  opcode=0xDA  len=26
         hex:  23 53 54 4D 3A 4C 31 36 3A 52 35 3A 41 31 3A 53 31 3A 4D 30 3A 45 23 0D 0A 00
         text: #STM:L16:R5:A1:S1:M0:E#...
         floor: 5
         arrow: UP
         sound: DING
         mode:  NORMAL

12:07:13.290  #3     PARSE_OK  opcode=0xDA  len=26
         floor: UNKNOWN (L=22 R=0)
         arrow: NONE
         sound: DING
         mode:  NORMAL

11:11:56.626  #30    PARSE_OK  opcode=0xAA  len=16
         hex:  44 49 53 50 41 54 43 48 20 43 41 4C 4C 0D 0A 00
         text: DISPATCH CALL...
         dispatch: CALL
```

---

## Что показывает каждая строка

### Сырые байты

```bash
12:07:04.441  raw +8 bytes:  AA 1A DA 23 53 54 4D 3A
```

Каждый вызов `read()` печатается отдельно. Полезно чтобы видеть на какие
части дробится фрейм при передаче — это нормально, `uart_process_rx` склеивает
их в кольцевом буфере.

### PARSE_OK

Фрейм прошёл CRC и полностью разобран. Для `opcode=0xDA` выводятся:

- `floor` — номер этажа в человекочитаемом виде (число, П, П1–П9, -1..-9, UNKNOWN)
- `arrow` — NONE / UP / DOWN / BOTH
- `sound` — NONE / DING / UP / DOWN / CLOSING / OPENING / OVERLOAD / FIRE_ALARM / DONT_WORK / BUTTON
- `mode` — NORMAL / FIRE_ALARM / MALFUNCTION / OVERLOAD / ... / CONN_LOST

Для `opcode=0xAA` выводится:

- `dispatch` — OFF / CALL / ANSWER

### NEED_MORE_DATA

```bash
12:07:34.721  NEED_MORE_DATA  buf=8 bytes (waiting for complete frame)
```

Фрейм начался, но пришёл не целиком за один `read()`. Утилита ждёт
следующей порции байт. Это штатная ситуация при медленной передаче.

### ERROR_CRC

```bash
12:07:00.100  ERROR_CRC
         size=26  opcode=0xDA
         CRC received:  0x1234
         CRC computed:  0x5678  MISMATCH
         raw frame: AA 1A DA ...
```

Показывает оба значения CRC, чтобы сразу было видно расхождение.
Появление таких строк на реальном трафике означает:

- Неверный byte order CRC в прошивке STM32
- Физические помехи на линии
- Неверные параметры UART (baud, parity)

### ERROR_SYNC1

```bash
10:30:00.001  ERROR_SYNC1  no 0xAA in 12 bytes, discarding: 01 02 03 ...
```

Байты без маркера начала фрейма — мусор, сброшен.

### ERROR_SYNC2

```bash
10:30:00.002  ERROR_SYNC2  expected 0xBB at offset 31, got 0xCC
```

Найден `0xAA`, прочитан фрейм нужной длины, но конечный маркер не совпал.

---

## Типичные сценарии использования

**Первый запуск с новым STM32 — проверить что фреймы вообще приходят:**

```bash
just pi::dump
# Ждём несколько секунд. Если строки PARSE_OK появляются — протокол работает.
# Если только raw bytes без PARSE_OK — проблема CRC или byte order.
```

**Проверить конкретное событие (например DING при прибытии лифта):**

```bash
just pi::dump
# Нажать кнопку вызова на пульте. Ожидаем:
#   sound: DING  (один фрейм)
#   sound: NONE  (следующий фрейм — сброс)
```

**Проверить диспетчерский сигнал:**

```bash
just pi::dump
# Подать сигнал на оптовход CALL. Ожидаем:
#   dispatch: CALL
# Снять сигнал:
#   dispatch: OFF
```

**Диагностика при подозрении на помехи:**

Если видны редкие `ERROR_CRC` или `ERROR_SYNC2` среди успешных фреймов —
помехи на линии или плохой контакт. Если `ERROR_CRC` на каждом фрейме —
несовпадение byte order CRC.

---

## Сборка вне проекта (прямо на Pi)

Если нужно собрать утилиту без devcontainer и без кросс-компилятора:

```bash
# На самой Pi:
cd /home/pi/indicator
gcc -O2 -o tools/uart_rx_dump \
    -I src \
    tools/uart_rx_dump.c \
    src/transport/uart.c \
    src/protocol/parser.c \
    src/domain/floor.c
```

Требует установленного `gcc` на Pi (`sudo apt install gcc`).
