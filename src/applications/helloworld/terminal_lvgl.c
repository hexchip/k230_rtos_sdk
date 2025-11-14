#include "terminal.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#ifdef CONFIG_TERMINAL_FONT_UNSCII_8
#  define TERMINAL_FONT_USE &lv_font_unscii_8
#  define TERMINAL_FONT_WIDTH 8
#  define TERMINAL_FONT_HEIGHT 10
#elif defined(CONFIG_TERMINAL_FONT_UNSCII_16)
#  define TERMINAL_FONT_USE &lv_font_unscii_16
#  define TERMINAL_FONT_WIDTH 16
#  define TERMINAL_FONT_HEIGHT 18
#else
#  define TERMINAL_FONT_USE &lv_font_unscii_8
#  define TERMINAL_FONT_WIDTH 8
#  define TERMINAL_FONT_HEIGHT 10
#endif

static terminal_t *s_terminal;
static const int s_char_width = TERMINAL_FONT_WIDTH;
static const int s_char_height = TERMINAL_FONT_HEIGHT;
static lv_obj_t **s_line_labels = NULL;
static lv_obj_t *s_cursor_obj = NULL;
static bool is_cursor_visible = true;
static lv_style_t label_style;
static lv_timer_t *s_lv_timer = NULL;

static void on_line_update(size_t index, char *buffer, size_t size) {
    // Thread-safe! Because only here does lv_label's text get modified.
    // And no modifications were made to any state of LVGL.
    char *text = lv_label_get_text(s_line_labels[index]);
    lv_memcpy(text, buffer, size);
}

static void on_refresh_dirty_lines(size_t *dirty_line_index_array, size_t size) {
    lv_lock();
    for(size_t i = 0; i < size; i++) {
        lv_obj_t *line_lable = s_line_labels[dirty_line_index_array[i]];
        // Refresh the label with its text
        lv_label_set_text_static(line_lable, lv_label_get_text(line_lable));
    }
    lv_unlock();
}

static void on_cursor_move(size_t row, size_t col) {
    int32_t x = col * s_char_width;
    int32_t y = row * s_char_height;
    lv_lock();
    lv_obj_set_pos(s_cursor_obj, x, y);
    lv_unlock();
}

static terminal_lifecycle_t terminal_lifecycle = {
    .on_line_update = on_line_update,
    .on_refresh_dirty_lines = on_refresh_dirty_lines,
    .on_cursor_move = on_cursor_move
};

void cursor_blink_timer_cb(lv_timer_t *timer) {
    is_cursor_visible = !is_cursor_visible;
    if (s_cursor_obj) {
        if (is_cursor_visible) {
            lv_obj_remove_flag(s_cursor_obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_cursor_obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

int terminal_gui_init(void *context) {
    int32_t width = lv_display_get_horizontal_resolution(NULL);
    int32_t height = lv_display_get_vertical_resolution(NULL);

    lv_obj_t * scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_style_init(&label_style);
    lv_style_set_text_color(&label_style, lv_color_white());
    lv_style_set_text_font(&label_style, TERMINAL_FONT_USE);
    int32_t cols = width / s_char_width;
    int32_t rows = height / s_char_height;

    s_line_labels = (lv_obj_t **)lv_malloc(rows * sizeof(lv_obj_t *));
    for (int i = 0; i < rows; i++) {
        lv_obj_t *lable = lv_label_create(scr);
        lv_obj_set_size(lable, width, s_char_height);
        lv_obj_set_pos(lable, 0, i * s_char_height);
        lv_obj_add_style(lable, &label_style, 0);
        lv_label_set_text_static(lable, lv_malloc_zeroed(sizeof(char) * (cols + 1)));
        s_line_labels[i] = lable;
    }

    s_cursor_obj = lv_obj_create(scr);
    lv_obj_set_size(s_cursor_obj, s_char_width / 4, s_char_height);
    lv_obj_set_pos(s_cursor_obj, 0, 0);
    lv_obj_set_style_bg_color(s_cursor_obj, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_cursor_obj, 0, 0);
    lv_obj_set_style_radius(s_cursor_obj, 0, 0);
    
    s_lv_timer = lv_timer_create(cursor_blink_timer_cb, 500, NULL);

    terminal_gui_config_t terminal_gui_config = {
        .rows = rows,
        .cols = cols,
        .lifecycle = &terminal_lifecycle
    };

    terminal_tty_config_t *terminal_tty_config = context;

    s_terminal = terminal_create("terminal", &terminal_gui_config, terminal_tty_config, 1024 * 8, 19);

    if(s_terminal == NULL) {
        LV_LOG_ERROR("terminal_create failed! %s", strerror(errno));
        return -ENOMEM;
    }

    int err = terminal_init(s_terminal);
    if (err) {
        LV_LOG_ERROR("terminal_init failed! %s", strerror(err));
        return err;
    }
}

void terminal_gui_deinit(void *context) {
    (void)context;

    if(s_lv_timer) {
        lv_timer_delete(s_lv_timer);
    }

    terminal_destroy(s_terminal);
}