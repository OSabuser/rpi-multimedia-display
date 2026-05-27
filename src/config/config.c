/**
 * src/config/config.c
 *
 * Минимальный парсер TOML-подмножества для nku_scheme.toml.
 * Без сторонних зависимостей. ~250 строк.
 *
 * Поддерживаемый subset:
 *   [section]          — заголовок секции
 *   key = "value"      — строковое значение
 *   key = [            — начало строкового массива (multiline)
 *   "value",           — элемент массива
 *   ]                  — конец массива
 *   # comment          — комментарий
 */

#include "config/config.h"

#include <stdio.h>
#include <stdlib.h> /* atoi */
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Внутренние константы
 * ──────────────────────────────────────────────────────────────────────────── */
#define LINE_MAX        256
#define VALUE_MAX        64
#define ARRAY_ENTRIES   32

/* ─────────────────────────────────────────────────────────────────────────────
 * Секции, которые нас интересуют
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum {
    SECT_NONE,
    SECT_SOUNDVOLUME,
    SECT_MUSICVOLUME,
    SECT_LOADCAPACITY,
    SECT_OTHER
} section_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Контекст парсинга
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct {
    /* Собранные значения */
    char sv_current[VALUE_MAX]; /* soundvolume.current  */
    char sv_default[VALUE_MAX]; /* soundvolume.default  */
    char mv_current[VALUE_MAX]; /* musicvolume.current  */
    char mv_default[VALUE_MAX]; /* musicvolume.default  */
    char lc_current[VALUE_MAX]; /* loadcapacity.current */
    char lc_default[VALUE_MAX]; /* loadcapacity.default */
    char lc_vals[ARRAY_ENTRIES][VALUE_MAX]; /* loadcapacity.possible_values */
    int  lc_count;

    /* Состояние автомата */
    section_t section;
    int       in_array; /* 1 = накапливаем элементы possible_values */
} ctx_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Утилиты
 * ──────────────────────────────────────────────────────────────────────────── */

/** Обрезать пробелы/табы/\r\n с обоих концов, in-place. */
static void trim(char *s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                        s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
    int start = 0;
    while (s[start] == ' ' || s[start] == '\t') start++;
    if (start > 0) {
        memmove(s, s + start, (size_t)(len - start + 1));
    }
}

/**
 * Извлечь содержимое первой пары кавычек из строки.
 * "50%" → "50%", без кавычек в буфере out.
 * Возвращает 1 при успехе, 0 если кавычек нет.
 */
static int extract_quoted(const char *line, char *out, int out_sz) {
    const char *p = strchr(line, '"');
    if (!p) return 0;
    p++;
    const char *e = strchr(p, '"');
    if (!e) return 0;
    int n = (int)(e - p);
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, p, (size_t)n);
    out[n] = '\0';
    return 1;
}

/**
 * Проверить, что строка начинается с ключевого слова, за которым идёт '='
 * (с возможными пробелами). Защита от ложных срабатываний (current_foo etc.).
 */
static int key_eq(const char *line, const char *key) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0) return 0;
    const char *p = line + klen;
    while (*p == ' ' || *p == '\t') p++;
    return *p == '=';
}

/** "50%" → 50. Принимает "50" без знака процента тоже. */
static int parse_percent(const char *s) {
    char buf[VALUE_MAX];
    strncpy(buf, s, VALUE_MAX - 1);
    buf[VALUE_MAX - 1] = '\0';
    int len = (int)strlen(buf);
    if (len > 0 && buf[len-1] == '%') buf[len-1] = '\0';
    int v = atoi(buf);
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    return v;
}

/** Найти строку needle в массиве arr[count][col_sz]. Вернуть индекс или -1. */
static int find_in_array(const char arr[][VALUE_MAX], int count, const char *needle) {
    for (int i = 0; i < count; i++) {
        if (strcmp(arr[i], needle) == 0) return i;
    }
    return -1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Обработка одной строки файла
 * ──────────────────────────────────────────────────────────────────────────── */
static void process_line(ctx_t *c, char *line) {
    /* Убрать комментарий (#...) */
    char *comment = strchr(line, '#');
    if (comment) *comment = '\0';

    trim(line);
    if (line[0] == '\0') return;

    /* ── Внутри массива ───────────────────────────────────────────────────── */
    if (c->in_array) {
        if (line[0] == ']') {
            c->in_array = 0;
        } else if (line[0] == '"') {
            /* Элемент: "value", или "value" */
            char val[VALUE_MAX];
            if (extract_quoted(line, val, sizeof(val))) {
                if (c->section == SECT_LOADCAPACITY && c->lc_count < ARRAY_ENTRIES) {
                    strncpy(c->lc_vals[c->lc_count], val, VALUE_MAX - 1);
                    c->lc_vals[c->lc_count][VALUE_MAX - 1] = '\0';
                    c->lc_count++;
                }
            }
        }
        return;
    }

    /* ── Заголовок секции [name] ─────────────────────────────────────────── */
    if (line[0] == '[') {
        if (strcmp(line, "[soundvolume]") == 0)       c->section = SECT_SOUNDVOLUME;
        else if (strcmp(line, "[musicvolume]") == 0)  c->section = SECT_MUSICVOLUME;
        else if (strcmp(line, "[loadcapacity]") == 0) c->section = SECT_LOADCAPACITY;
        else                                           c->section = SECT_OTHER;
        return;
    }

    /* ── Только интересующие секции ──────────────────────────────────────── */
    if (c->section == SECT_NONE || c->section == SECT_OTHER) return;

    /* ── Ключи ───────────────────────────────────────────────────────────── */
    if (key_eq(line, "current")) {
        char val[VALUE_MAX];
        if (!extract_quoted(line, val, sizeof(val))) return;
        if (c->section == SECT_SOUNDVOLUME)       strncpy(c->sv_current, val, VALUE_MAX - 1);
        else if (c->section == SECT_MUSICVOLUME)  strncpy(c->mv_current, val, VALUE_MAX - 1);
        else if (c->section == SECT_LOADCAPACITY) strncpy(c->lc_current, val, VALUE_MAX - 1);

    } else if (key_eq(line, "default")) {
        char val[VALUE_MAX];
        if (!extract_quoted(line, val, sizeof(val))) return;
        if (c->section == SECT_SOUNDVOLUME)       strncpy(c->sv_default, val, VALUE_MAX - 1);
        else if (c->section == SECT_MUSICVOLUME)  strncpy(c->mv_default, val, VALUE_MAX - 1);
        else if (c->section == SECT_LOADCAPACITY) strncpy(c->lc_default, val, VALUE_MAX - 1);

    } else if (key_eq(line, "possible_values")) {
        /* Начало массива. Массив всегда multiline в nku_scheme.toml */
        if (strchr(line, '[') != NULL) {
            c->in_array = 1;
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * config_load
 * ──────────────────────────────────────────────────────────────────────────── */
int config_load(const char *path, config_t *out) {
    /* Инициализировать дефолтами — всегда, даже при ошибке файла */
    out->sound_volume_percent = CONFIG_DEFAULT_SOUND_VOLUME;
    out->music_volume_percent = CONFIG_DEFAULT_MUSIC_VOLUME;
    out->load_capacity_idx    = CONFIG_DEFAULT_LOAD_IDX;

    if (path == NULL) return -1;

    FILE *f = fopen(path, "r");
    if (f == NULL) return -1;

    ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), f) != NULL) {
        process_line(&ctx, line);
    }
    fclose(f);

    /* ── Применить результаты парсинга ──────────────────────────────────── */

    /* soundvolume: current > default */
    {
        const char *v = ctx.sv_current[0] ? ctx.sv_current : ctx.sv_default;
        if (v[0]) out->sound_volume_percent = parse_percent(v);
    }

    /* musicvolume: current > default */
    {
        const char *v = ctx.mv_current[0] ? ctx.mv_current : ctx.mv_default;
        if (v[0]) out->music_volume_percent = parse_percent(v);
    }

    /* loadcapacity: найти индекс current в possible_values */
    {
        const char *v = ctx.lc_current[0] ? ctx.lc_current : ctx.lc_default;
        if (v[0] && ctx.lc_count > 0) {
            int idx = find_in_array((const char (*)[VALUE_MAX])ctx.lc_vals,
                                    ctx.lc_count, v);
            if (idx >= 0) out->load_capacity_idx = idx;
            /* idx < 0: значение current не найдено в массиве → оставить дефолт */
        }
    }

    return 0;
}