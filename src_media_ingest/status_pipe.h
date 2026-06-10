/**
 * @file src_media_ingest/status_pipe.h
 * @brief Запись media_status_t в FIFO indicator (writer-side).
 *
 * Открывает FIFO на каждый вызов: надёжнее чем держать fd открытым —
 * при рестарте indicator FIFO пересоздаётся, новый open() найдёт нового reader.
 */

#pragma once

#include "media/media_ipc.h"

/**
 * status_pipe_write — отправить один байт статуса в FIFO indicator.
 *
 * O_WRONLY | O_NONBLOCK: не блокируется если нет reader.
 * ENXIO (нет reader) — не ошибка: indicator не запущен или ещё стартует.
 *
 * @param p_fifo_path  путь к FIFO (обычно MEDIA_STATUS_FIFO_PATH)
 * @param status       статус для передачи
 * @return 0 при успехе; -1 при ошибке
 */
int status_pipe_write(const char *p_fifo_path, media_status_t status);
