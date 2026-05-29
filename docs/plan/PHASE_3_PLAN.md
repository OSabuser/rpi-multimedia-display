# Фаза 3 — Video Player

**Цель:** omxplayer запущен под надзором `indicator`, автоматически перезапускается при падении.
После этой фазы на экране постоянно играет фоновое видео поверх которого в Фазе 4 лягут DispmanX-оверлеи.

---

## Что делаем

### 3.1 Реализовать `src/player/video_player.h/.c`

```c
typedef struct video_player_s video_player_t;

video_player_t *video_player_open(const char *p_video_path);
void            video_player_close(video_player_t *p_vp);
void            video_player_check_and_restart(video_player_t *p_vp);
int             video_player_get_pid(const video_player_t *p_vp);
```

Внутри:
- `posix_spawn` omxplayer с флагами `--layer 1 --no-keys --loop --no-osd --win 0,0,600,1024`
- Хранить PID дочернего процесса
- `video_player_check_and_restart()` вызывается из SIGCHLD ветки в `main.c`
- `waitpid(WNOHANG)` — проверить завершился ли процесс; если да — перезапустить
- Задержка перезапуска: 500 мс (`nanosleep`) чтобы не молотить при системной ошибке

### 3.2 Подключить в `main.c`

```c
// инициализация (последовательность: после signalfd/timerfd, до UART)
app.video = video_player_open(VIDEO_PATH);

// в handle_signal, ветка SIGCHLD:
video_player_check_and_restart(p_app->video);

// в cleanup:
video_player_close(app.video);
```

Путь к видео — константа в `main.c`, в будущем переедет в конфиг.

### 3.3 Обновить `indicator.service`

Добавить `After=omxplayer` не нужно — omxplayer запускается как дочерний процесс.
Проверить что `KillMode=control-group` в service файле (убивает всю группу процессов при stop).

---

## Что тестируем

Без unit-тестов — `video_player_t` напрямую зависит от DispmanX и `posix_spawn`, на хосте не тестируется.

**On-target проверки:**

| Тест | Команда | Ожидаем |
|---|---|---|
| Видео запустилось | `just pi::logs` | `omxplayer started pid=N` |
| Watchdog omxplayer | `kill <omxplayer_pid>` | перезапуск < 3 с, лог `omxplayer restarted` |
| Корректный стоп | `systemctl stop indicator` | omxplayer тоже завершён (нет зомби) |

---

## Критерий закрытия Фазы 3

1. `indicator` стартует → omxplayer запускается автоматически, видео играет
2. `kill <omxplayer_pid>` → демон перезапускает omxplayer за < 3 с
3. `systemctl stop indicator` → omxplayer корректно завершён, нет зависших процессов
4. `just build::test` → по-прежнему 6/6 (video_player не входит в host тесты)

---

## Открытый вопрос перед стартом

Путь к видеофайлу: `/home/pi/indicator/videos/output.mp4` — подтвердить что файл уже задеплоен на Pi, иначе omxplayer упадёт сразу при старте (systemd будет перезапускать в цикле).

```bash
ssh pi@indicator-01.local "ls -lh /home/pi/indicator/videos/"
```