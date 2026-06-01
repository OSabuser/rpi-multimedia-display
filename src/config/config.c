/**
 * src/config/config.c
 *
 * Минимальный парсер TOML-подмножества.
 * Без сторонних зависимостей.
 *
 * Два парсера в одном файле:
 *   config_load()       — nku_scheme.toml (строки, массивы, MCU-параметры)
 *   video_config_load() — video.toml (целые числа, только [video] секция)
 *
 * Поддерживаемый subset для nku_scheme.toml:
 *   [section]          — заголовок секции
 *   key = "value"      — строковое значение
 *   key = [            — начало строкового массива (multiline)
 *   "value",           — элемент массива
 *   ]                  — конец массива
 *   # comment          — комментарий
 *
 * Поддерживаемый subset для video.toml:
 *   [section]          — заголовок секции
 *   key = 123          — целочисленное значение (без кавычек)
 *   # comment          — комментарий
 */

#include "config/config.h"

#include <stdio.h>
#include <stdlib.h> /* atoi */
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Внутренние константы
 * ──────────────────────────────────────────────────────────────────────────── */
#define LINE_MAX      256
#define VALUE_MAX     64
#define ARRAY_ENTRIES 32

/* ─────────────────────────────────────────────────────────────────────────────
 * Секции nku_scheme.toml
 * ──────────────────────────────────────────────────────────────────────────── */
typedef enum
{
    SECT_NONE,
    SECT_SOUNDVOLUME,
    SECT_MUSICVOLUME,
    SECT_LOADCAPACITY,
    SECT_OTHER
} section_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Контекст парсинга nku_scheme.toml
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct
{
    char sv_current[VALUE_MAX];
    char sv_default[VALUE_MAX];
    char mv_current[VALUE_MAX];
    char mv_default[VALUE_MAX];
    char lc_current[VALUE_MAX];
    char lc_default[VALUE_MAX];
    char lc_vals[ARRAY_ENTRIES][VALUE_MAX];
    int lc_count;

    section_t section;
    int in_array;
} ctx_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Утилиты — используются обоими парсерами
 * ──────────────────────────────────────────────────────────────────────────── */

/** Обрезать пробелы/табы/\r\n с обоих концов, in-place. */
static void trim(char *p_s)
{
    int len = (int) strlen(p_s);
    while (len > 0 && (p_s[len - 1] == ' ' || p_s[len - 1] == '\t' || p_s[len - 1] == '\r' ||
                       p_s[len - 1] == '\n'))
    {
        p_s[--len] = '\0';
    }
    int start = 0;
    while (p_s[start] == ' ' || p_s[start] == '\t')
    {
        start++;
    }
    if (start > 0)
    {
        memmove(p_s, p_s + start, (size_t) (len - start + 1));
    }
}

/**
 * Извлечь содержимое первой пары кавычек из строки.
 * Возвращает 1 при успехе, 0 если кавычек нет.
 */
static int extract_quoted(const char *p_line, char *p_out, int out_sz)
{
    const char *p = strchr(p_line, '"');
    if (!p)
    {
        return 0;
    }
    p++;
    const char *e = strchr(p, '"');
    if (!e)
    {
        return 0;
    }
    int num = (int) (e - p);
    if (num >= out_sz)
    {
        num = out_sz - 1;
    }
    memcpy(p_out, p, (size_t) num);
    p_out[num] = '\0';
    return 1;
}

/**
 * Проверить, что строка начинается с ключевого слова, за которым идёт '='
 * (с возможными пробелами). Защита от совпадений по префиксу (current_foo и т.п.).
 */
static int key_eq(const char *p_line, const char *p_key)
{
    size_t klen = strlen(p_key);
    if (strncmp(p_line, p_key, klen) != 0)
    {
        return 0;
    }
    const char *char_ptr = p_line + klen;
    while (*char_ptr == ' ' || *char_ptr == '\t')
    {
        char_ptr++;
    }
    return *char_ptr == '=';
}

/** "50%" → 50. Принимает "50" без знака процента тоже. */
static int parse_percent(const char *p_s)
{
    char buf[VALUE_MAX];
    strncpy(buf, p_s, VALUE_MAX - 1);
    buf[VALUE_MAX - 1] = '\0';
    int len            = (int) strlen(buf);
    if (len > 0 && buf[len - 1] == '%')
    {
        buf[len - 1] = '\0';
    }
    int value = atoi(buf);
    if (value < 0)
    {
        value = 0;
    }
    if (value > 100)
    {
        value = 100;
    }
    return value;
}

/** Найти строку needle в массиве arr[count][VALUE_MAX]. Вернуть индекс или -1. */
static int find_in_array(const char p_arr[][VALUE_MAX], int count, const char *p_needle)
{
    for (int i = 0; i < count; i++)
    {
        if (strcmp(p_arr[i], p_needle) == 0)
        {
            return i;
        }
    }
    return -1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Парсер nku_scheme.toml — обработка одной строки
 * ──────────────────────────────────────────────────────────────────────────── */
static void process_line(ctx_t *c, char *p_line)
{
    char *comment = strchr(p_line, '#');
    if (comment)
    {
        *comment = '\0';
    }

    trim(p_line);
    if (p_line[0] == '\0')
    {
        return;
    }

    /* ── Внутри массива ───────────────────────────────────────────────────── */
    if (c->in_array)
    {
        if (p_line[0] == ']')
        {
            c->in_array = 0;
        }
        else if (p_line[0] == '"')
        {
            char val[VALUE_MAX];
            if (extract_quoted(p_line, val, sizeof(val)))
            {
                if (c->section == SECT_LOADCAPACITY && c->lc_count < ARRAY_ENTRIES)
                {
                    strncpy(c->lc_vals[c->lc_count], val, VALUE_MAX - 1);
                    c->lc_vals[c->lc_count][VALUE_MAX - 1] = '\0';
                    c->lc_count++;
                }
            }
        }
        return;
    }

    /* ── Заголовок секции ────────────────────────────────────────────────── */
    if (p_line[0] == '[')
    {
        if (strcmp(p_line, "[soundvolume]") == 0)
        {
            c->section = SECT_SOUNDVOLUME;
        }
        else if (strcmp(p_line, "[musicvolume]") == 0)
        {
            c->section = SECT_MUSICVOLUME;
        }
        else if (strcmp(p_line, "[loadcapacity]") == 0)
        {
            c->section = SECT_LOADCAPACITY;
        }
        else
        {
            c->section = SECT_OTHER;
        }
        return;
    }

    if (c->section == SECT_NONE || c->section == SECT_OTHER)
    {
        return;
    }

    /* ── Ключи ───────────────────────────────────────────────────────────── */
    if (key_eq(p_line, "current"))
    {
        char val[VALUE_MAX];
        if (!extract_quoted(p_line, val, sizeof(val)))
        {
            return;
        }
        if (c->section == SECT_SOUNDVOLUME)
        {
            strncpy(c->sv_current, val, VALUE_MAX - 1);
        }
        else if (c->section == SECT_MUSICVOLUME)
        {
            strncpy(c->mv_current, val, VALUE_MAX - 1);
        }
        else if (c->section == SECT_LOADCAPACITY)
        {
            strncpy(c->lc_current, val, VALUE_MAX - 1);
        }
    }
    else if (key_eq(p_line, "default"))
    {
        char val[VALUE_MAX];
        if (!extract_quoted(p_line, val, sizeof(val)))
        {
            return;
        }
        if (c->section == SECT_SOUNDVOLUME)
        {
            strncpy(c->sv_default, val, VALUE_MAX - 1);
        }
        else if (c->section == SECT_MUSICVOLUME)
        {
            strncpy(c->mv_default, val, VALUE_MAX - 1);
        }
        else if (c->section == SECT_LOADCAPACITY)
        {
            strncpy(c->lc_default, val, VALUE_MAX - 1);
        }
    }
    else if (key_eq(p_line, "possible_values"))
    {
        const char *open = strchr(p_line, '[');
        if (open != NULL && strchr(open + 1, ']') == NULL)
        {
            c->in_array = 1;
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * config_load — загрузить nku_scheme.toml
 * ──────────────────────────────────────────────────────────────────────────── */
int config_load(const char *p_path, config_t *p_cfg)
{
    /* Инициализировать дефолтами — всегда, даже при ошибке файла. */
    p_cfg->sound_volume_percent = CONFIG_DEFAULT_SOUND_VOLUME;
    p_cfg->music_volume_percent = CONFIG_DEFAULT_MUSIC_VOLUME;
    p_cfg->load_capacity_idx    = CONFIG_DEFAULT_LOAD_IDX;

    /* video_win_* — дефолты для случая когда video.toml не найден. */
    p_cfg->video_win_x = CONFIG_DEFAULT_VIDEO_WIN_X;
    p_cfg->video_win_y = CONFIG_DEFAULT_VIDEO_WIN_Y;
    p_cfg->video_win_w = CONFIG_DEFAULT_VIDEO_WIN_W;
    p_cfg->video_win_h = CONFIG_DEFAULT_VIDEO_WIN_H;

    if (p_path == NULL)
    {
        return -1;
    }

    FILE *file_desc = fopen(p_path, "r");
    if (file_desc == NULL)
    {
        return -1;
    }

    ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), file_desc) != NULL)
    {
        process_line(&ctx, line);
    }
    fclose(file_desc);

    /* ── Применить результаты парсинга ──────────────────────────────────── */

    {
        const char *v = ctx.sv_current[0] ? ctx.sv_current : ctx.sv_default;
        if (v[0])
        {
            p_cfg->sound_volume_percent = parse_percent(v);
        }
    }
    {
        const char *v = ctx.mv_current[0] ? ctx.mv_current : ctx.mv_default;
        if (v[0])
        {
            p_cfg->music_volume_percent = parse_percent(v);
        }
    }
    {
        const char *v = ctx.lc_current[0] ? ctx.lc_current : ctx.lc_default;
        if (v[0] && ctx.lc_count > 0)
        {
            int idx = find_in_array((const char(*)[VALUE_MAX]) ctx.lc_vals, ctx.lc_count, v);
            if (idx >= 0)
            {
                p_cfg->load_capacity_idx = idx;
            }
        }
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * video_config_load — загрузить video.toml
 *
 * Формат: секция [video] с целочисленными ключами без кавычек.
 * Пример:
 *   [video]
 *   win_x = 0
 *   win_y = 0
 *   win_w = 600
 *   win_h = 1024
 *
 * Только ключи присутствующие в файле переопределяют значения.
 * Отсутствие файла не ошибка — дефолты из config_load остаются.
 * ──────────────────────────────────────────────────────────────────────────── */
int video_config_load(const char *p_path, config_t *p_cfg)
{
    if (p_path == NULL)
        return -1;

    FILE *f = fopen(p_path, "r");
    if (f == NULL)
        return -1;

    int in_video = 0;
    char line[LINE_MAX];

    while (fgets(line, sizeof(line), f) != NULL)
    {
        /* Удалить комментарий */
        char *comment = strchr(line, '#');
        if (comment)
        {
            *comment = '\0';
        }

        trim(line);
        if (line[0] == '\0')
        {
            continue;
        }

        /* Заголовок секции */
        if (line[0] == '[')
        {
            in_video = (strcmp(line, "[video]") == 0);
            continue;
        }

        if (!in_video)
        {
            continue;
        }

        /* Найти '=' и разбить на ключ / значение */
        char *eq = strchr(line, '=');
        if (eq == NULL)
        {
            continue;
        }

        /* Ключ: от начала до '=' */
        char key[VALUE_MAX];
        size_t klen = (size_t) (eq - line);
        if (klen == 0 || klen >= VALUE_MAX)
        {
            continue;
        }
        memcpy(key, line, klen);
        key[klen] = '\0';
        trim(key);

        /* Значение: после '=', целое число */
        int val = atoi(eq + 1);

        if (strcmp(key, "win_x") == 0)
        {
            p_cfg->video_win_x = val;
        }
        else if (strcmp(key, "win_y") == 0)
        {
            p_cfg->video_win_y = val;
        }
        else if (strcmp(key, "win_w") == 0)
        {
            p_cfg->video_win_w = val;
        }
        else if (strcmp(key, "win_h") == 0)
        {
            p_cfg->video_win_h = val;
        }
    }

    fclose(f);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * renderer_config_load — загрузить renderer.toml
 *
 * Поддерживаемый формат (аналогичен video.toml):
 *   [renderer]
 *   resources_dir = /home/pi/indicator/resources   ← строка без кавычек
 *
 *   [slot.digit_left]
 *   x = 15
 *   y = 675
 *
 *   [slot.digit_right]
 *   x = 185
 *   y = 675
 *
 *   [slot.arrow]
 *   x = 391
 *   y = 745
 *
 *   [slot.weight]
 *   x = 333
 *   y = 37
 *
 * Дефолты всегда выставляются до открытия файла.
 * При отсутствии файла возвращает -1, дефолты сохраняются.
 * ──────────────────────────────────────────────────────────────────────────── */

static void renderer_config_set_defaults(renderer_config_t *p_cfg)
{
    /* Путь к ресурсам */
    strncpy(p_cfg->resources_dir, "/data/resources", RENDERER_RESOURCES_DIR_MAX - 1U);
    p_cfg->resources_dir[RENDERER_RESOURCES_DIR_MAX - 1U] = '\0';

    /* Позиции из indicator.h оригинального проекта:
     *   LEFT_CHAR_X_POS_PX       = 15
     *   LEFT_CHAR_Y_POS_PX       = 675
     *   RIGHT_CHAR_X_POS_PX      = 185
     *   ARROW_ICON_X_POSITION_PX = 391
     *   ARROW_ICON_Y_POSITION_PX = 745
     *   WEIGHT_BAR_X_POSITION_PX = 333
     *   WEIGHT_BAR_Y_POSITION_PX = 37
     */
    p_cfg->digit_left_x  = 15;
    p_cfg->digit_left_y  = 675;
    p_cfg->digit_right_x = 185;
    p_cfg->digit_right_y = 675;
    p_cfg->arrow_x       = 391;
    p_cfg->arrow_y       = 745;
    p_cfg->weight_x      = 333;
    p_cfg->weight_y      = 37;
}

int renderer_config_load(const char *p_path, renderer_config_t *p_cfg)
{
    renderer_config_set_defaults(p_cfg);

    if (p_path == NULL)
        return -1;

    FILE *file_desc = fopen(p_path, "r");
    if (file_desc == NULL)
    {
        return -1;
    }

    char section[64] = "";
    char line[LINE_MAX];

    while (fgets(line, sizeof(line), file_desc) != NULL)
    {
        /* Удалить комментарий */
        char *comment = strchr(line, '#');
        if (comment)
        {
            *comment = '\0';
        }

        trim(line);
        if (line[0] == '\0')
        {
            continue;
        }

        /* ── Заголовок секции: [renderer], [slot.digit_left] и т.д. ──────── */
        if (line[0] == '[')
        {
            char *end = strchr(line + 1, ']');
            if (end != NULL)
            {
                size_t len = (size_t) (end - (line + 1));
                if (len >= sizeof(section))
                {
                    len = sizeof(section) - 1U;
                }
                memcpy(section, line + 1, len);
                section[len] = '\0';
                trim(section);
            }
            continue;
        }

        /* ── key = value ──────────────────────────────────────────────────── */
        char *eq = strchr(line, '=');
        if (eq == NULL)
        {
            continue;
        }

        /* Ключ */
        char key[VALUE_MAX];
        size_t klen = (size_t) (eq - line);
        if (klen == 0 || klen >= VALUE_MAX)
        {
            continue;
        }
        memcpy(key, line, klen);
        key[klen] = '\0';
        trim(key);

        /* Значение — в отдельном буфере чтобы trim не портил line */
        char val[VALUE_MAX];
        strncpy(val, eq + 1, VALUE_MAX - 1);
        val[VALUE_MAX - 1] = '\0';
        trim(val);

        if (val[0] == '\0')
        {
            continue;
        }

        /* ── Диспетчеризация по секции ────────────────────────────────────── */
        if (strcmp(section, "renderer") == 0)
        {
            if (strcmp(key, "resources_dir") == 0)
            {
                strncpy(p_cfg->resources_dir, val, RENDERER_RESOURCES_DIR_MAX - 1U);
                p_cfg->resources_dir[RENDERER_RESOURCES_DIR_MAX - 1U] = '\0';
            }
        }
        else if (strcmp(section, "slot.digit_left") == 0)
        {
            if (strcmp(key, "x") == 0)
            {
                p_cfg->digit_left_x = atoi(val);
            }
            else if (strcmp(key, "y") == 0)
            {
                p_cfg->digit_left_y = atoi(val);
            }
        }
        else if (strcmp(section, "slot.digit_right") == 0)
        {
            if (strcmp(key, "x") == 0)
            {
                p_cfg->digit_right_x = atoi(val);
            }
            else if (strcmp(key, "y") == 0)
            {
                p_cfg->digit_right_y = atoi(val);
            }
        }
        else if (strcmp(section, "slot.arrow") == 0)
        {
            if (strcmp(key, "x") == 0)
            {
                p_cfg->arrow_x = atoi(val);
            }
            else if (strcmp(key, "y") == 0)
            {
                p_cfg->arrow_y = atoi(val);
            }
        }
        else if (strcmp(section, "slot.weight") == 0)
        {
            if (strcmp(key, "x") == 0)
            {
                p_cfg->weight_x = atoi(val);
            }
            else if (strcmp(key, "y") == 0)
            {
                p_cfg->weight_y = atoi(val);
            }
        }
        /* Неизвестные секции/ключи — молча игнорируются */
    }

    fclose(file_desc);
    return 0;
}

#define UART_DEFAULT_PORT "/dev/ttyAMA0"
#define UART_DEFAULT_BAUD 115200

typedef enum
{
    USECT_NONE,
    USECT_DEVICE,
    USECT_BAUDRATE,
    USECT_OTHER
} uart_section_t;

int uart_config_load(const char *p_path, uart_config_t *p_cfg)
{
    /* Дефолты — всегда, даже при ошибке файла */
    strncpy(p_cfg->port, "/dev/ttyAMA0", UART_PORT_MAX - 1);
    p_cfg->port[UART_PORT_MAX - 1] = '\0';
    p_cfg->baudrate                = UART_DEFAULT_BAUD;

    if (p_path == NULL)
    {
        return -1;
    }

    FILE *file_desc = fopen(p_path, "r");
    if (file_desc == NULL)
    {
        return -1;
    }

    uart_section_t section = USECT_NONE;
    int in_array           = 0;

    char port_current[VALUE_MAX] = "";
    char port_default[VALUE_MAX] = "";
    char baud_current[VALUE_MAX] = "";
    char baud_default[VALUE_MAX] = "";

    char line[LINE_MAX];
    while (fgets(line, sizeof(line), file_desc) != NULL)
    {
        char *comment = strchr(line, '#');
        if (comment)
        {
            *comment = '\0';
        }

        trim(line);
        if (line[0] == '\0')
        {
            continue;
        }

        /* Пропускать тело массива possible_values — логика та же что в process_line */
        if (in_array)
        {
            if (line[0] == ']')
            {
                in_array = 0;
            }
            continue;
        }

        /* Заголовок секции */
        if (line[0] == '[')
        {
            if (strcmp(line, "[device]") == 0)
            {
                section = USECT_DEVICE;
            }
            else if (strcmp(line, "[baudrate]") == 0)
            {
                section = USECT_BAUDRATE;
            }
            else
            {
                section = USECT_OTHER;
            }
            continue;
        }

        if (section == USECT_NONE || section == USECT_OTHER)
        {
            continue;
        }

        /* Начало массива */
        if (key_eq(line, "possible_values") && strchr(line, '[') != NULL)
        {
            /* Одностроч. массив [... ] — закрыт на той же строке, in_array не нужен */
            const char *open = strchr(line, '[');
            if (strchr(open + 1, ']') == NULL)
            {
                in_array = 1;
            }
            continue;
        }

        /* current / default — quoted string */
        char val[VALUE_MAX];
        if (key_eq(line, "current") && extract_quoted(line, val, sizeof(val)))
        {
            if (section == USECT_DEVICE)
            {
                strncpy(port_current, val, VALUE_MAX - 1);
            }
            else if (section == USECT_BAUDRATE)
            {
                strncpy(baud_current, val, VALUE_MAX - 1);
            }
        }
        else if (key_eq(line, "default") && extract_quoted(line, val, sizeof(val)))
        {
            if (section == USECT_DEVICE)
            {
                strncpy(port_default, val, VALUE_MAX - 1);
            }
            else if (section == USECT_BAUDRATE)
            {
                strncpy(baud_default, val, VALUE_MAX - 1);
            }
        }
        /* name, should_sync и прочие ключи — молча игнорируются */
    }

    fclose(file_desc);

    /* Применить: current → default → hardcoded */
    const char *p = port_current[0] ? port_current : port_default;
    if (p[0] != '\0')
    {
        strncpy(p_cfg->port, p, UART_PORT_MAX - 1);
        p_cfg->port[UART_PORT_MAX - 1] = '\0';
    }

    const char *b = baud_current[0] ? baud_current : baud_default;
    if (b[0] != '\0')
    {
        int baud = atoi(b);
        if (baud == 9600 || baud == 19200 || baud == 38400 || baud == 57600 || baud == 115200)
        {
            p_cfg->baudrate = baud;
        }
        /* Иначе: неизвестное значение — оставить дефолт 115200.
         * Логировать нельзя (syslog не подключён в config.c).
         * main.c залогирует финальные значения. */
    }

    return 0;
}
