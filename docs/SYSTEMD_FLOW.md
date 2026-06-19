# Systemd Services — Interaction Diagrams

> Порядок старта, дерево активации, runtime IPC.

---

## 1. Порядок старта (After / Before)

Стрелки означают «должен завершить запуск до меня».
Параллельные ветки (provisioning + i2s) сходятся в `indicator.service`.

```mermaid
flowchart TD
    classDef sys     fill:#455a64,stroke:#78909c,color:#eceff1
    classDef oneshot fill:#1a237e,stroke:#5c6bc0,color:#e8eaf6
    classDef daemon  fill:#1b5e20,stroke:#66bb6a,color:#e8f5e9

    LFS(["local-fs.target"]):::sys
    SND(["sound.target"]):::sys

    IFB["indicator-firstboot.service\noneshot · root\nCond: !/data/first_boot_done"]:::oneshot
    IST["indicator-setup.service\noneshot · pi · /dev/tty1\nrun_setup.sh  ·  120 s"]:::oneshot

    I2S["i2s-silence.service\nRestart: always · 2 s\naplay -D dmixer /dev/zero\n48 kHz / S32_LE"]:::daemon
    IND["indicator.service\nRestart: always · 2 s\nKillMode: control-group"]:::daemon
    MED["media-ingest.service\nRestart: on-failure · 10 s\nCAP_SYS_ADMIN"]:::daemon

    LFS -->|After| IFB
    IFB -->|Before| IST
    IST -->|Before| IND

    SND -->|After| I2S
    I2S -->|After| IND
    SND -->|After| IND

    LFS -->|After| MED
    IND -->|After| MED
```

---

## 2. Дерево активации (Wants / WantedBy)

Пунктирные стрелки означают «хочу, чтобы ты запустился вместе со мной».
Все сервисы имеют `WantedBy=multi-user.target` (стандартный enable).
Ключевые нестандартные связи — `indicator.target` и `indicator → {setup, i2s}`.

```mermaid
flowchart LR
    classDef sys     fill:#455a64,stroke:#78909c,color:#eceff1
    classDef oneshot fill:#1a237e,stroke:#5c6bc0,color:#e8eaf6
    classDef daemon  fill:#1b5e20,stroke:#66bb6a,color:#e8f5e9
    classDef tgt     fill:#4a148c,stroke:#ba68c8,color:#f3e5f5

    MUT(["multi-user.target"]):::sys
    SND(["sound.target"]):::sys
    IT["indicator.target"]:::tgt

    IFB["indicator-firstboot"]:::oneshot
    IST["indicator-setup"]:::oneshot
    I2S["i2s-silence"]:::daemon
    IND["indicator"]:::daemon
    MED["media-ingest"]:::daemon

    MUT -.->|Wants| IT
    MUT -.->|Wants| IFB
    MUT -.->|Wants| IST
    MUT -.->|Wants| I2S
    MUT -.->|Wants| IND

    IT -.->|Wants| IND
    IT -.->|Wants| MED

    IND -.->|Wants| IST
    IND -.->|Wants| I2S
    IND -.->|Wants| SND
```

> **Заметка:** `indicator.target` — grouping unit, обеспечивает,
> что `media-ingest` запускается как часть системы индикатора,
> даже если `indicator.service` перезапустится.

---

## 3. Runtime IPC (FIFO)

Однобайтовый протокол: `media-ingest` пишет, `indicator` читает в poll-цикле.
FIFO (`/run/indicator/media_status.fifo`) создаёт `indicator` при старте;
переживает рестарты обоих процессов.

```mermaid
sequenceDiagram
    actor USB as USB flash drive
    participant MED as media-ingest
    participant FIFO as FIFO\n/run/indicator/\nmedia_status.fifo
    participant IND as indicator
    participant OMP as omxplayer\n(дочерний процесс)

    IND->>FIFO: media_ipc_open()\n(mkdir + mkfifo + O_RDONLY|O_NONBLOCK)

    USB->>MED: inotify ADD /dev/sdX

    MED->>MED: mounter_mount() [CAP_SYS_ADMIN]
    MED->>MED: ffmpeg_find_files() — поиск *.mp4

    alt MP4 найдены
        MED->>FIFO: MEDIA_FOUND (0)
        FIFO->>IND: poll POLLIN → media_ipc_read()
        IND->>IND: показать notification PNG

        Note over MED: arm 1 s timer

        MED->>MED: ffmpeg_spawn() → output_tmp.mp4
        MED->>FIFO: MEDIA_PROCESSING (1)
        FIFO->>IND: poll POLLIN → media_ipc_read()
        IND->>IND: обновить notification

        alt ffmpeg завершился успешно
            MED->>MED: rename output_tmp.mp4 → output.mp4
            MED->>FIFO: MEDIA_DONE (2)
            FIFO->>IND: poll POLLIN → media_ipc_read()
            IND->>OMP: video_player_replace()\nkillpg(SIGKILL, pgid)
            OMP-->>IND: SIGCHLD → check_and_restart()\nwaitpid() + spawn с тем же путём
        else ffmpeg завершился с ошибкой
            MED->>FIFO: MEDIA_ERROR (5)
            FIFO->>IND: poll POLLIN → media_ipc_read()
            IND->>IND: показать error notification
        end

    else MP4 не найдены
        MED->>FIFO: MEDIA_NO_VIDEO (3)
        FIFO->>IND: poll POLLIN → media_ipc_read()
        IND->>IND: показать no-video notification
    end

    Note over MED: arm 3 s timer

    MED->>FIFO: MEDIA_EJECT (4)
    FIFO->>IND: poll POLLIN → media_ipc_read()
    IND->>IND: показать "извлеките носитель"

    Note over MED: arm 5 s timer

    MED->>MED: mounter_umount()
    MED->>FIFO: MEDIA_CLEAR (6)
    FIFO->>IND: poll POLLIN → media_ipc_read()
    IND->>IND: скрыть notification

    Note over FIFO: Если media-ingest упал/перезапустился:\npoll() вернёт POLLHUP →\nindicator: media_ipc_close() + media_ipc_open()
```

---

## Итоговая сводка

| Сервис | Тип | Restart | Условие | Активирует |
|---|---|---|---|---|
| `indicator-firstboot` | oneshot | — | `!/data/first_boot_done` | → indicator-setup |
| `indicator-setup` | oneshot | — | всегда | → indicator |
| `i2s-silence` | simple | always · 2 s | card 0 ready | → indicator |
| `indicator` | simple | always · 2 s | — | → media-ingest |
| `media-ingest` | simple | on-failure · 10 s | бинарь существует | — |
| `indicator.target` | target | — | — | groups indicator + media-ingest |