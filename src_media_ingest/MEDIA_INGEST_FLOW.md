# media-ingest: Flow Diagrams

Этот документ содержит Mermaid-диаграммы для `src_media_ingest/main.c`.

> **Рендеринг:** GitHub, GitLab, Obsidian, VS Code (расширение _Markdown Preview Mermaid Support_).

---

## 1. Startup / Shutdown (`main`)

Последовательность инициализации и корректного завершения.

```mermaid
flowchart TD
    START([▶ main]) --> OL[openlog\nmedia_ingest]
    OL --> CFG_DEF[config_set_defaults\nзаполнить cfg defaults]

    CFG_DEF --> ARG{аргумент\n--config=?}
    ARG -- есть --> CFG_LOAD[config_load\nперезаписать совпадающие ключи]
    ARG -- нет  --> SIG

    CFG_LOAD --> SIG[setup_signalfd\nblock SIGTERM + SIGCHLD]
    SIG -- fail --> FAIL
    SIG -- ok   --> TMR[setup_timerfd\nCLOCK_MONOTONIC]
    TMR -- fail --> FAIL
    TMR -- ok   --> USB[usb_watcher_open\ninotify on /dev]
    USB -- fail --> FAIL
    USB -- ok   --> INIT[state = ST_IDLE\nffmpeg_pid = −1]

    INIT --> LOOP[run_loop]

    LOOP --> CL_FF{ST_FFMPEG_RUNNING\nпри выходе?}
    CL_FF -- да  --> KFF[ffmpeg_kill]
    CL_FF -- нет --> CL_MNT
    KFF --> CL_MNT{смонтирован\nmount_point?}
    CL_MNT -- да  --> UMT[mounter_umount]
    CL_MNT -- нет --> CL_USB
    UMT --> CL_USB[usb_watcher_close]
    CL_USB --> CL_FDS[close timer_fd\nclose sig_fd]
    CL_FDS --> OK([✓ exit 0])

    FAIL([✗ exit 1])
```

---

## 2. Event Loop (`run_loop`)

`poll()` ожидает три файловых дескриптора одновременно; все ветки возвращают управление обратно в цикл.

```mermaid
flowchart TD
    ENTER([run_loop]) --> POLL["poll(inotify_fd, sig_fd, timer_fd, −1)"]

    POLL -- EINTR --> POLL
    POLL -- ошибка --> BREAK([выход из цикла])

    POLL -- inotify POLLIN --> UW[usb_watcher_process\n→ on_usb_event]
    UW --> POLL

    POLL -- signalfd POLLIN --> SIG_READ[read signalfd_siginfo]
    SIG_READ --> SIG_CHK{ssi_signo}
    SIG_CHK -- SIGTERM --> STOP[running = 0\nвыход из цикла]
    SIG_CHK -- SIGCHLD --> CHLD[on_sigchld]
    CHLD --> POLL
    STOP --> BREAK

    POLL -- timerfd POLLIN --> TMR_READ[read timerfd exp]
    TMR_READ --> TICK[on_timer_tick]
    TICK --> POLL
```

---

## 3. Конечный автомат

Все состояния, переходы, таймеры и IPC-сообщения в FIFO.

```mermaid
stateDiagram-v2
    direction LR

    [*] --> ST_IDLE

    ST_IDLE --> ST_WAIT_PROCESSING : USB insert\nmount OK + MP4 найдены\n→ MEDIA_FOUND\narm 1 s

    ST_IDLE --> ST_WAIT_EJECT : USB insert\nmount fail / нет MP4\n→ MEDIA_ERROR / MEDIA_NO_VIDEO\narm 3 s

    ST_WAIT_PROCESSING --> ST_FFMPEG_RUNNING : timer tick\nffmpeg_spawn OK\n→ MEDIA_PROCESSING

    ST_WAIT_PROCESSING --> ST_WAIT_EJECT : timer tick\nffmpeg_spawn FAIL\n→ MEDIA_ERROR\narm 3 s

    ST_FFMPEG_RUNNING --> ST_WAIT_EJECT : SIGCHLD\nffmpeg_finish + rename\n→ MEDIA_DONE / MEDIA_ERROR\narm 3 s

    ST_WAIT_EJECT --> ST_WAIT_UMOUNT : timer tick\n→ MEDIA_EJECT\narm 5 s

    ST_WAIT_UMOUNT --> ST_IDLE : timer tick\numount\n→ MEDIA_CLEAR

    ST_WAIT_PROCESSING --> ST_IDLE : USB remove\n→ disarm · umount · MEDIA_CLEAR
    ST_FFMPEG_RUNNING  --> ST_IDLE : USB remove\n→ ffmpeg_kill · disarm · umount · MEDIA_CLEAR
    ST_WAIT_EJECT      --> ST_IDLE : USB remove\n→ disarm · umount · MEDIA_CLEAR
    ST_WAIT_UMOUNT     --> ST_IDLE : USB remove\n→ disarm · umount · MEDIA_CLEAR
```

---

## 4. Тайминги состояний

```bash
USB вставлен
     │
     ├─ mount + scan ─────────────────────────────────────┐
     │                                                     │
  [1 s]                                              ошибка / нет MP4
     │                                                     │
     ▼                                                     ▼
ffmpeg запущен ──── SIGCHLD ──────────► [3 s] ──► MEDIA_EJECT ──► [5 s] ──► umount ──► ST_IDLE
                  (done/error)
```

| Переход                        | Задержка | Константа                      |
|-------------------------------|----------|-------------------------------|
| MEDIA_FOUND → ffmpeg start    | 1 s      | `DELAY_FOUND_TO_PROCESSING_S` |
| result → MEDIA_EJECT          | 3 s      | `DELAY_RESULT_TO_EJECT_S`     |
| MEDIA_EJECT → umount          | 5 s      | `DELAY_EJECT_TO_UMOUNT_S`     |
