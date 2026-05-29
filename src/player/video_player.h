/**
 * @file video_player.h
 * @brief omxplayer supervised child process adapter.
 *
 * Ответственность:
 *   - запустить omxplayer через posix_spawnp
 *   - хранить PID дочернего процесса
 *   - перезапускать omxplayer при падении (вызов из SIGCHLD-ветки main)
 *   - корректно завершить omxplayer при shutdown
 *
 * Не знает о:
 *   - DispmanX, UART, state machine, config, audio
 *   - как выбирается путь к видео и параметры окна (передаётся снаружи)
 *
 * Только Pi: зависит от posix_spawnp и бинаря omxplayer.
 * На хосте не тестируется без мока.
 */

#pragma once

#include <sys/types.h> /* pid_t */

/* ─── Параметры окна воспроизведения ─────────────────────────────────────── */

/**
 * video_window_t — прямоугольник окна omxplayer в экранных пикселях.
 *
 * Формируется в main.c из config_t.video_win_* и передаётся в
 * video_player_open(). video_player не знает о config_t.
 */
typedef struct video_window_s
{
    int x;      /* левый край  */
    int y;      /* верхний край */
    int width;  /* ширина       */
    int height; /* высота       */
} video_window_t;

/* ─── Непрозрачный дескриптор ────────────────────────────────────────────── */

typedef struct video_player_s video_player_t;

/* ─── API ────────────────────────────────────────────────────────────────── */

/**
 * video_player_open — выделить дескриптор и запустить omxplayer.
 *
 * @param p_video_path  абсолютный путь к видеофайлу
 * @param window        прямоугольник окна воспроизведения
 * @return              дескриптор при успехе, NULL при ошибке malloc или spawn
 *
 * Строка окна "--win x,y,w,h" форматируется один раз при open и хранится
 * внутри дескриптора. Последующие spawn при рестарте используют её повторно.
 */
video_player_t *video_player_open(const char *p_video_path, video_window_t window);

/**
 * video_player_close — завершить omxplayer (SIGTERM) и освободить дескриптор.
 *
 * Блокирует до завершения дочернего процесса (waitpid).
 * KillMode=control-group в systemd — дополнительная страховка.
 * Безопасно вызывать с NULL.
 */
void video_player_close(video_player_t *p_vp);

/**
 * video_player_check_and_restart — пожать дочерний процесс и перезапустить
 * если он завершился.
 *
 * Вызывается из SIGCHLD-ветки главного poll-цикла.
 * Внутри: waitpid(WNOHANG) — не блокирует.
 * Перед перезапуском — nanosleep(500 мс) чтобы не молотить при системной ошибке.
 * Безопасно вызывать с NULL.
 */
void video_player_check_and_restart(video_player_t *p_vp);

/**
 * video_player_get_pid — вернуть текущий PID omxplayer, или -1 если не запущен.
 */
int video_player_get_pid(const video_player_t *p_vp);
