/**
 * @file src_media_ingest/usb_watcher.h
 * @brief Мониторинг появления/извлечения USB block-устройств через inotify.
 *
 * Отслеживает /dev на события IN_CREATE / IN_DELETE.
 * Фильтр: sd[a-z][0-9] — только разделы (sda1, sdb2, …).
 * Сырой диск (sda без цифры) игнорируется: mounter при необходимости
 * сам пробует /dev/sdX как fallback.
 */

#pragma once

/**
 * usb_event_cb_t — коллбэк при появлении/извлечении USB-раздела.
 *
 * @param p_devpath  полный путь устройства, например "/dev/sda1"
 * @param inserted   1 — устройство появилось; 0 — извлечено
 * @param p_ctx      пользовательский контекст из usb_watcher_open()
 */
typedef void (*usb_event_cb_t)(const char *p_devpath, int inserted, void *p_ctx);

typedef struct usb_watcher_s usb_watcher_t;

/**
 * usb_watcher_open — создать watcher и запустить inotify на /dev.
 *
 * @param cb     коллбэк для событий устройства
 * @param p_ctx  пользовательский контекст; передаётся в каждый cb-вызов
 * @return указатель на watcher; NULL при ошибке (errno установлен)
 */
usb_watcher_t *usb_watcher_open(usb_event_cb_t p_cb, void *p_ctx);

/**
 * usb_watcher_get_fd — получить fd для poll(POLLIN).
 *
 * @param p_w  watcher из usb_watcher_open()
 * @return inotify fd; -1 если p_w == NULL
 */
int usb_watcher_get_fd(const usb_watcher_t *p_w);

/**
 * usb_watcher_process — вычитать все накопленные inotify-события.
 *
 * Вызывать когда poll() вернул POLLIN на usb_watcher_get_fd().
 * Для каждого matching-события вызывает cb.
 *
 * @param p_w  watcher из usb_watcher_open()
 */
void usb_watcher_process(usb_watcher_t *p_w);

/**
 * usb_watcher_close — закрыть inotify fd и освободить ресурсы.
 *
 * @param p_w  watcher; NULL — no-op
 */
void usb_watcher_close(usb_watcher_t *p_w);
