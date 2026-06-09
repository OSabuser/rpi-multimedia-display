/**
 * @file src_media_ingest/ffmpeg_runner.c
 * @brief Поиск MP4, запуск ffmpeg, атомарная замена output.mp4.
 *
 * _GNU_SOURCE требуется для killpg() <signal.h>.
 */
#define _GNU_SOURCE

#include "ffmpeg_runner.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

/* ─── Константы ──────────────────────────────────────────────────────────── */

/** Временный concat-список для ffmpeg demuxer */
static const char *const CONCAT_LIST_PATH = "/tmp/media_ingest_concat.txt";

extern char **environ;

/* ─── Вспомогательные функции ────────────────────────────────────────────── */

/** Проверить что имя файла заканчивается на .mp4 или .MP4 */
static int has_mp4_ext(const char *p_name)
{
    /* Пропустить скрытые файлы: macOS AppleDouble (._video.mp4),
     * .DS_Store и прочие системные файлы начинаются с точки */
    if (p_name[0] == '.')
    {
        return 0;
    }
    size_t len = strlen(p_name);
    if (len <= 4U)
    {
        return 0;
    }
    const char *p_ext = p_name + len - 4U;
    return strcmp(p_ext, ".mp4") == 0 || strcmp(p_ext, ".MP4") == 0;
}

/** Компаратор для qsort: лексикографическая сортировка путей */
static int cmp_path_str(const void *p_a, const void *p_b)
{
    return strcmp((const char *) p_a, (const char *) p_b);
}

/** Записать concat-список ffmpeg в файл */
static int write_concat_list(char p_paths[][FFMPEG_PATH_MAX], int count, const char *p_list_path)
{
    FILE *p_fp = fopen(p_list_path, "w");
    if (p_fp == NULL)
    {
        syslog(LOG_ERR, "ffmpeg_runner: fopen '%s': %s", p_list_path, strerror(errno));
        return -1;
    }

    for (int i = 0; i < count; i++)
    {
        (void) fprintf(p_fp, "file '%s'\n", p_paths[i]);
    }

    (void) fclose(p_fp);
    return 0;
}

/* ─── Публичный API ───────────────────────────────────────────────────────── */

ffmpeg_result_t ffmpeg_find_files(const char *p_mount_dir, char p_out_paths[][FFMPEG_PATH_MAX],
                                  int *p_count)
{
    *p_count = 0;

    DIR *p_dir = opendir(p_mount_dir);
    if (p_dir == NULL)
    {
        syslog(LOG_ERR, "ffmpeg_runner: opendir '%s': %s", p_mount_dir, strerror(errno));
        return FFMPEG_ERROR;
    }

    const struct dirent *p_ent;
    while ((p_ent = readdir(p_dir)) != NULL && *p_count < FFMPEG_FILES_MAX)
    {
        if (!has_mp4_ext(p_ent->d_name))
        {
            continue;
        }
        (void) snprintf(p_out_paths[*p_count], FFMPEG_PATH_MAX, "%s/%s", p_mount_dir,
                        p_ent->d_name);
        (*p_count)++;
    }

    (void) closedir(p_dir);

    if (*p_count == 0)
    {
        syslog(LOG_NOTICE, "ffmpeg_runner: no MP4 files in '%s'", p_mount_dir);
        return FFMPEG_NO_FILES;
    }

    qsort(p_out_paths, (size_t) *p_count, FFMPEG_PATH_MAX, cmp_path_str);
    syslog(LOG_NOTICE, "ffmpeg_runner: found %d MP4 file(s) in '%s'", *p_count, p_mount_dir);
    return FFMPEG_OK;
}

int ffmpeg_spawn(char p_paths[][FFMPEG_PATH_MAX], int count, const char *p_tmp_output,
                 pid_t *p_out_pid)
{
    /* Аргументы для однофайлового и concat вариантов */
    char *argv_single[] = { (char *) "ffmpeg", (char *) "-y",   (char *) "-i",         p_paths[0],
                            (char *) "-c",     (char *) "copy", (char *) p_tmp_output, NULL };

    char *argv_concat[] = {
        (char *) "ffmpeg", (char *) "-y",   (char *) "-f",         (char *) "concat",
        (char *) "-safe",  (char *) "0",    (char *) "-i",         (char *) CONCAT_LIST_PATH,
        (char *) "-c",     (char *) "copy", (char *) p_tmp_output, NULL
    };

    if (count > 1 && write_concat_list(p_paths, count, CONCAT_LIST_PATH) < 0)
    {
        return -1;
    }

    char **argv = (count == 1) ? argv_single : argv_concat;

    /* Перенаправить stdout/stderr ffmpeg в /dev/null — он слишком многословен */
    posix_spawn_file_actions_t actions;
    (void) posix_spawn_file_actions_init(&actions);
    (void) posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    (void) posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    /* Отдельная process group: killpg(pid, SIGKILL) при извлечении носителя */
    posix_spawnattr_t attr;
    (void) posix_spawnattr_init(&attr);
    (void) posix_spawnattr_setpgroup(&attr, 0);
    (void) posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

    pid_t pid;
    int ret = posix_spawnp(&pid, "ffmpeg", &actions, &attr, argv, environ);

    (void) posix_spawn_file_actions_destroy(&actions);
    (void) posix_spawnattr_destroy(&attr);

    if (ret != 0)
    {
        syslog(LOG_ERR, "ffmpeg_runner: posix_spawnp: %s", strerror(ret));
        return -1;
    }

    syslog(LOG_NOTICE, "ffmpeg_runner: spawned pid=%d count=%d → '%s'", (int) pid, count,
           p_tmp_output);
    *p_out_pid = pid;
    return 0;
}

ffmpeg_result_t ffmpeg_finish(pid_t pid, const char *p_tmp_output, const char *p_final_output)
{
    int status;
    pid_t r = waitpid(pid, &status, WNOHANG);

    /* Убрать concat-список в любом случае */
    (void) unlink(CONCAT_LIST_PATH);

    if (r == 0)
    {
        syslog(LOG_WARNING, "ffmpeg_runner: ffmpeg pid=%d still running", (int) pid);
        return FFMPEG_ERROR;
    }

    if (r < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        syslog(LOG_ERR, "ffmpeg_runner: ffmpeg failed pid=%d status=%d", (int) pid, status);
        (void) unlink(p_tmp_output);
        return FFMPEG_ERROR;
    }

    /* Атомарная замена: rename гарантирует что output.mp4 всегда консистентен */
    if (rename(p_tmp_output, p_final_output) < 0)
    {
        syslog(LOG_ERR, "ffmpeg_runner: rename '%s' → '%s': %s", p_tmp_output, p_final_output,
               strerror(errno));
        (void) unlink(p_tmp_output);
        return FFMPEG_ERROR;
    }

    syslog(LOG_NOTICE, "ffmpeg_runner: output ready: '%s'", p_final_output);
    return FFMPEG_OK;
}

void ffmpeg_kill(pid_t pid)
{
    if (pid <= 0)
    {
        return;
    }

    syslog(LOG_NOTICE, "ffmpeg_runner: killing pgid=%d", (int) pid);

    if (killpg(pid, SIGKILL) < 0 && errno != ESRCH)
    {
        syslog(LOG_WARNING, "ffmpeg_runner: killpg pgid=%d: %s", (int) pid, strerror(errno));
    }

    int status;
    (void) waitpid(pid, &status, 0);
}
