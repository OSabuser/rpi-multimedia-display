/**
 * @file src_indicator/app_private.h
 * @brief Общие типы и forward-объявления для TU-декомпозиции main.c.
 *
 * Включается только из: main.c, app_handlers.c, app_render.c.
 * Не является публичным API — не включать в тесты или другие модули.
 */
#ifndef APP_PRIVATE_H
#define APP_PRIVATE_H

#include "audio/audio.h"
#include "config/config.h"
#include "domain/state.h"
#include "media/media_ipc.h"
#include "player/video_player.h"
#include "protocol/types.h"
#include "renderer/renderer.h"
#include "transport/uart.h"

/* ─── Типы ────────────────────────────────────────────────────────────────── */

typedef enum setup_status_e
{
    SETUP_STATUS_UNKNOWN     = 0,
    SETUP_STATUS_OK          = 1,
    SETUP_STATUS_PENDING     = 2,
    SETUP_STATUS_PULL_FAILED = 3,
    SETUP_STATUS_PUSH_FAILED = 4,
} setup_status_t;

typedef struct app_s
{
    config_t cfg;
    indicator_state_t state;
    setup_status_t setup_status;
    uart_t *uart;
    video_player_t *video;
    renderer_t *renderer;
    audio_player_t *audio;
    int fifo_fd;  /**< fd FIFO от media-ingest; -1 если не открыт  */
    int notif_fd; /**< timerfd автоскрытия уведомления; -1 если нет */
} app_t;

typedef struct stats_s
{
    unsigned frames_ok;
    unsigned parse_errors;
    unsigned unknown_opcodes;
    int last_floor_num;
    int last_arrow;
    int last_mode;
} stats_t;

/* ─── Глобальная статистика (определена в main.c) ────────────────────────── */

extern stats_t g_s_stats;

/* ─── Путь к директории уведомлений ──────────────────────────────────────── */

#define NOTIF_DIR "/data/resources/notifications"

/* ─── Объявления из app_render.c ─────────────────────────────────────────── */

/**
 * @brief Обновить MODE-слот по текущему режиму.
 *
 * Вызывается при изменении mode (opcode=0xDA) и при восстановлении
 * после окончания диспетчерского сеанса.
 *
 * @param p_app  контекст приложения
 * @param mode   новый режим индикатора
 */
void renderer_apply_mode(app_t *p_app, indicator_mode_t mode);

/**
 * @brief Обновить MODE-слот при изменении диспетчерского состояния.
 *
 * Dispatch имеет приоритет над mode из 0xDA:
 *   DISPATCH_CALL / DISPATCH_ANSWER → показать соответствующую иконку.
 *   DISPATCH_OFF                    → восстановить mode из текущего состояния.
 *
 * @param p_app    контекст приложения
 * @param dispatch новое диспетчерское состояние
 */
void renderer_apply_dispatch(app_t *p_app, dispatch_state_t dispatch);

/**
 * @brief Обновить цифры, стрелку и режим по результату state_apply_frame().
 *
 * MODE-слот обновляется только если диспетчер не активен.
 *
 * @param p_app    контекст приложения
 * @param p_upd    результат state_apply_frame()
 * @param p_payload разобранный фрейм
 */
void renderer_apply_elevator(app_t *p_app, const state_update_result_t *p_upd,
                             const parsed_frame_t *p_payload);

/* ─── Объявления из app_handlers.c ───────────────────────────────────────── */

/**
 * @brief UART-коллбэк: обработать входящий фрейм протокола MU.
 *
 * Передаётся в uart_open() как function pointer.
 * Диспетчеризует по opcode: 0xAA (dispatch) и 0xDA (elevator status).
 *
 * @param p_frame  входящий фрейм
 * @param p_ctx    указатель на app_t
 */
void on_uart_frame(const mu_frame_t *p_frame, void *p_ctx);

/**
 * @brief Прочитать статус из FIFO media-ingest и обновить SPRITE_NOTIFICATION.
 *
 * MEDIA_CLEAR  → скрыть уведомление, отменить таймер.
 * MEDIA_DONE   → дополнительно вызвать video_player_replace().
 * Остальные    → показать соответствующий PNG.
 *
 * @param p_app  контекст приложения
 */
void on_media_status(app_t *p_app);

/**
 * @brief Периодический тик watchdog: логировать статистику и вызвать keepalive.
 *
 * Вызывается каждые WATCHDOG_INTERVAL_S секунд из poll loop (FD_TIMER).
 * Реализует P-28 митигацию: пустой DispmanX update предотвращает dormant.
 *
 * @param p_app  контекст приложения (только чтение)
 */
void on_watchdog_tick(const app_t *p_app);

/**
 * @brief Скрыть уведомление об отсутствии MCU при первом валидном фрейме.
 *
 * Идемпотентна: после первого срабатывания ничего не делает.
 *
 * @param p_app  контекст приложения
 */
void maybe_clear_mcu_notification(app_t *p_app);

/**
 * @brief Взвести одноразовый timerfd для автоскрытия уведомления.
 *
 * @param notif_fd  fd из timerfd_create(); отрицательный — no-op
 * @param delay_sec задержка в секундах
 */
void notif_arm(int notif_fd, unsigned int delay_sec);

/**
 * @brief Отменить таймер автоскрытия (it_value = 0).
 *
 * @param notif_fd  fd из timerfd_create(); отрицательный — no-op
 */
void notif_disarm(int notif_fd);

#endif /* APP_PRIVATE_H */