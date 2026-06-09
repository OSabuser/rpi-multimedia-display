/**
 * @file src/media/media_ipc.h
 * @brief IPC между media-ingest и indicator через именованный FIFO.
 *
 * Протокол:
 *   media-ingest записывает один байт (media_status_t) в FIFO.
 *   indicator читает в poll-цикле на дескрипторе FD_FIFO.
 *
 * Жизненный цикл FIFO:
 *   indicator создаёт FIFO при старте через media_ipc_open().
 *   FIFO переживает рестарты: mkfifo() при EEXIST — no-op.
 *   При закрытии всех writer-fd poll() вернёт POLLHUP:
 *   caller вызывает media_ipc_close() + media_ipc_open() для переоткрытия.
 */

#pragma once

/* ─── Пути FIFO ──────────────────────────────────────────────────────────── */

/** Директория для runtime-файлов indicator (tmpfs, создаётся при каждом старте) */
#define MEDIA_STATUS_FIFO_DIR "/run/indicator"

/** Именованный FIFO для статусов media-ingest → indicator */
#define MEDIA_STATUS_FIFO_PATH "/run/indicator/media_status.fifo"

/* ─── Коды статусов ──────────────────────────────────────────────────────── */

/**
 * media_status_t — события жизненного цикла обработки USB-носителя.
 *
 * Значения совпадают с wire-байтами: передаётся один байт без сериализации.
 * Порядок отражает последовательность событий при нормальном сценарии.
 */
typedef enum media_status_e
{
    MEDIA_FOUND = 0, /**< на носителе найдены видеофайлы                */
    MEDIA_PROCESSING = 1, /**< идёт конкатенация ffmpeg                      */
    MEDIA_DONE = 2, /**< обработка завершена, output.mp4 заменён       */
    MEDIA_NO_VIDEO = 3, /**< на носителе нет видеофайлов                   */
    MEDIA_EJECT = 4, /**< просьба извлечь носитель (пауза перед umount) */
    MEDIA_ERROR = 5, /**< ошибка ffmpeg; output.mp4 не изменён          */
    MEDIA_CLEAR = 6, /**< носитель извлечён, скрыть уведомление         */
    MEDIA_STATUS_MAX = 7, /**< sentinel — не передаётся по FIFO              */
} media_status_t;

/* ─── API (indicator-side, reader) ──────────────────────────────────────── */

/**
 * media_ipc_open — создать директорию + FIFO и открыть на чтение.
 *
 * Создаёт MEDIA_STATUS_FIFO_DIR (EEXIST игнорируется),
 * затем MEDIA_STATUS_FIFO_PATH (EEXIST игнорируется),
 * открывает с O_RDONLY | O_NONBLOCK.
 *
 * @return fd >= 0 при успехе; -1 при фатальной ошибке (errno установлен).
 */
int media_ipc_open(void);

/**
 * media_ipc_read — прочитать один байт статуса (non-blocking).
 *
 * Возвращает -1 если данных нет (EAGAIN) или writer закрылся (EOF, n==0).
 * При EOF caller должен обработать POLLHUP от poll() и переоткрыть FIFO.
 *
 * @param fd    fd из media_ipc_open()
 * @param p_out приёмник статуса; не изменяется при возврате -1
 * @return 0 при успехе; -1 иначе
 */
int media_ipc_read(int fd, media_status_t *p_out);

/**
 * media_ipc_close — закрыть fd.
 *
 * FIFO-файл не удаляется — переживает рестарты indicator.
 * Безопасен при fd < 0 (no-op).
 *
 * @param fd  fd из media_ipc_open()
 */
void media_ipc_close(int fd);
