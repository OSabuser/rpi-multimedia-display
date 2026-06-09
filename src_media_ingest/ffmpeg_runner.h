/**
 * @file src_media_ingest/ffmpeg_runner.h
 * @brief Поиск MP4-файлов на USB-носителе и асинхронный запуск ffmpeg.
 *
 * Жизненный цикл:
 *   ffmpeg_find_files() → ffmpeg_spawn() → (SIGCHLD) → ffmpeg_finish()
 *
 * При извлечении носителя во время обработки:
 *   ffmpeg_kill() → waitpid в ffmpeg_finish() / caller
 */

#pragma once

#include <sys/types.h>

/* ─── Лимиты ─────────────────────────────────────────────────────────────── */

/** Максимальное количество MP4-файлов на носителе */
enum
{
    FFMPEG_FILES_MAX = 32
};

/** Максимальная длина пути к файлу на носителе */
enum
{
    FFMPEG_PATH_MAX = 512
};

/* ─── Результат операции ─────────────────────────────────────────────────── */

typedef enum ffmpeg_result_e
{
    FFMPEG_OK       = 0, /**< операция завершена успешно          */
    FFMPEG_NO_FILES = 1, /**< MP4-файлы на носителе не найдены    */
    FFMPEG_ERROR    = 2, /**< ошибка ffmpeg, rename или ФС        */
} ffmpeg_result_t;

/* ─── API ────────────────────────────────────────────────────────────────── */

/**
 * ffmpeg_find_files — найти .mp4/.MP4 файлы в директории.
 *
 * Результат сортируется лексикографически (предсказуемый порядок склейки).
 *
 * @param p_mount_dir   директория монтирования, например "/mnt/usb"
 * @param p_out_paths   выходной массив путей [FFMPEG_FILES_MAX][FFMPEG_PATH_MAX]
 * @param p_count       записывается количество найденных файлов
 * @return FFMPEG_NO_FILES если 0 найдено; FFMPEG_OK иначе; FFMPEG_ERROR при ошибке ФС
 */
ffmpeg_result_t ffmpeg_find_files(const char *p_mount_dir, char p_out_paths[][FFMPEG_PATH_MAX],
                                  int *p_count);

/**
 * ffmpeg_spawn — запустить ffmpeg асинхронно (posix_spawnp).
 *
 * Один файл:  ffmpeg -y -i <input> -c copy <tmp_output>
 * Несколько:  ffmpeg -y -f concat -safe 0 -i <list> -c copy <tmp_output>
 *
 * stdout/stderr ffmpeg перенаправляются в /dev/null.
 * Новая process group (POSIX_SPAWN_SETPGROUP) — можно killpg().
 *
 * @param p_paths       массив путей из ffmpeg_find_files()
 * @param count         количество файлов
 * @param p_tmp_output  путь для временного выходного файла
 * @param p_out_pid     записывается PID ffmpeg
 * @return 0 при успехе; -1 при ошибке spawn
 */
int ffmpeg_spawn(char p_paths[][FFMPEG_PATH_MAX], int count, const char *p_tmp_output,
                 pid_t *p_out_pid);

/**
 * ffmpeg_finish — вычитать статус дочернего ffmpeg и выполнить rename.
 *
 * Вызывать из SIGCHLD-обработчика.
 * При успехе атомарно переименовывает tmp_output → final_output.
 * При ошибке удаляет tmp_output (final_output не меняется).
 *
 * @param pid             PID из ffmpeg_spawn()
 * @param p_tmp_output    временный файл ffmpeg
 * @param p_final_output  финальный путь, например "/data/videos/output.mp4"
 * @return FFMPEG_OK / FFMPEG_ERROR
 */
ffmpeg_result_t ffmpeg_finish(pid_t pid, const char *p_tmp_output, const char *p_final_output);

/**
 * ffmpeg_kill — послать SIGKILL process group ffmpeg и дождаться завершения.
 *
 * Безопасен при pid <= 0 (no-op).
 *
 * @param pid  PID из ffmpeg_spawn()
 */
void ffmpeg_kill(pid_t pid);