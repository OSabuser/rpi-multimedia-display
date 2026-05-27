/**
 * src/config/config.h
 *
 * Конфигурация, читаемая из nku_scheme.toml.
 *
 * Парсим три секции:
 *   [soundvolume]  → current "N%" → sound_volume_percent
 *   [musicvolume]  → current "N%" → music_volume_percent
 *   [loadcapacity] → current + possible_values → load_capacity_idx
 *
 * Приоритет: поле current → поле default (если current отсутствует).
 * При ошибке чтения файла: используются встроенные значения по умолчанию.
 *
 * Парсер — минимальный встроенный (без сторонних зависимостей),
 * поддерживает только subset TOML нужного нам формата.
 */

#pragma once

/* ─────────────────────────────────────────────────────────────────────────────
 * Значения по умолчанию
 * ──────────────────────────────────────────────────────────────────────────── */
#define CONFIG_DEFAULT_SOUND_VOLUME  50
#define CONFIG_DEFAULT_MUSIC_VOLUME   0
#define CONFIG_DEFAULT_LOAD_IDX       0

/* ─────────────────────────────────────────────────────────────────────────────
 * Конфигурация устройства
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct {
    int sound_volume_percent;  /* 0 | 25 | 50 | 75 | 100   */
    int music_volume_percent;  /* 0 | 25 | 50 | 75 | 100   */
    int load_capacity_idx;     /* 0-based индекс в possible_values → load_N.png */
} config_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * API
 * ──────────────────────────────────────────────────────────────────────────── */

/**
 * config_load — загрузить конфигурацию из файла TOML.
 *
 * @param path  путь к nku_scheme.toml (например, "/home/pi/indicator/configs/device/nku_scheme.toml")
 * @param out   [out] заполненная структура
 *
 * @return  0  — успех (файл прочитан полностью)
 *         -1  — файл не найден или ошибка чтения (out заполнен дефолтами)
 *
 * Функция всегда заполняет out, даже при ошибке (дефолты).
 */
int config_load(const char *path, config_t *out);