#include "lvgl/lvgl.h"
#include "platform.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

// ================= Global config =================
#define SCR_W 800
#define SCR_H 480
#define COLOR_TITLE 0x064E3B
#define COLOR_SUBTITLE 0x115E59

LV_FONT_DECLARE(my_font_full);

#if 0
// ================= Touch driver =================
#define MAX_TOUCH 1
static struct {
    int x, y;
    int state;
} touch_points[MAX_TOUCH];

void my_touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
#if APP_TARGET_PC
    int mouse_x = 0;
    int mouse_y = 0;
    uint32_t buttons = 0;

    LV_UNUSED(indev);
    SDL_PumpEvents();
    buttons = SDL_GetMouseState(&mouse_x, &mouse_y);

    if(mouse_x < 0) mouse_x = 0;
    if(mouse_x >= SCR_W) mouse_x = SCR_W - 1;
    if(mouse_y < 0) mouse_y = 0;
    if(mouse_y >= SCR_H) mouse_y = SCR_H - 1;

    touch_points[0].x = mouse_x;
    touch_points[0].y = mouse_y;
    touch_points[0].state = (buttons & SDL_BUTTON_LMASK) ? 1 : 0;
#else
    struct input_event ev;
    while(read(ts_fd, &ev, sizeof(ev)) > 0) {
        if(ev.type == EV_ABS) {
            // 兼容单点触控和硬件把第一点写在 0x35 的情况
            if(ev.code == ABS_X || ev.code == 0x35) { touch_points[0].x = ev.value * SCR_W / TOUCH_MAX_X; }
            else if(ev.code == ABS_Y || ev.code == 0x36) { touch_points[0].y = ev.value * SCR_H / TOUCH_MAX_Y; }
        } else if(ev.type == EV_KEY && (ev.code == BTN_TOUCH || ev.code == BTN_LEFT)) {
            touch_points[0].state = ev.value ? 1 : 0;
        }
    }
    
#endif
    data->point.x = touch_points[0].x;
    data->point.y = touch_points[0].y;
    data->state = touch_points[0].state ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ================= Flush callback =================
#if !APP_TARGET_PC
void my_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
    int32_t w = lv_area_get_width(area); int32_t h = lv_area_get_height(area);
    uint32_t line_bytes = w * 4; lv_color32_t * src = (lv_color32_t *)px_map;
    for(int y = area->y1; y <= area->y2; y++) {
        uint32_t * dst = fb_map + y * SCR_W + area->x1; memcpy(dst, src, line_bytes); src += w;
    }
    lv_display_flush_ready(disp);
}
#endif
#endif

void *tick_thread(void * data) { while(1) { usleep(5000); lv_tick_inc(5); } return NULL; }

// ================= Login UI logic =================
static lv_obj_t * login_win = NULL;
static lv_obj_t * ta_user = NULL;
static lv_obj_t * ta_pass = NULL;
static lv_obj_t * kb = NULL;
static lv_obj_t * login_active_ta = NULL;
static lv_obj_t * login_pending_ta = NULL;
static lv_obj_t * main_menu_win = NULL;
static lv_obj_t * song_sel_win = NULL;

typedef enum {
    PAGE_TITLE_LOGIN,
    PAGE_TITLE_MAIN_MENU,
    PAGE_TITLE_SONG_SELECTION,
    PAGE_TITLE_FREE_PLAY,
    PAGE_TITLE_GUIDED,
    PAGE_TITLE_AUTO_PLAY,
    PAGE_TITLE_RECORD,
    PAGE_TITLE_RECORD_SELECTION,
    PAGE_TITLE_SAVE_RECORD,
} page_title_t;

#define MAX_MANAGED_ITEMS 128
typedef enum {
    MANAGE_KIND_NONE = 0,
    MANAGE_KIND_GUIDED = 1,
    MANAGE_KIND_RECORD = 2,
} manage_kind_t;

static char hidden_guided_items[MAX_MANAGED_ITEMS][128];
static int hidden_guided_count = 0;
static char hidden_record_items[MAX_MANAGED_ITEMS][128];
static int hidden_record_count = 0;
static manage_kind_t current_manage_kind = MANAGE_KIND_NONE;
static lv_obj_t * manage_overlay = NULL;
static lv_obj_t * manage_popup = NULL;

static void apply_page_bg(lv_obj_t * page) {
    lv_obj_set_style_bg_color(page, lv_color_hex(0xd4f0f0), 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
}

static void apply_glass_button_style(lv_obj_t * btn) {
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, 90, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, 140, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xf5fffd), 0);
    lv_obj_set_style_border_opa(btn, 150, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x7ab8b0), 0);
    lv_obj_set_style_shadow_opa(btn, 75, 0);
    lv_obj_set_style_shadow_width(btn, 18, 0);
    lv_obj_set_style_shadow_spread(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x1f4f55), 0);
}

static void apply_danger_glass_button_style(lv_obj_t * btn) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xffd6da), 0);
    lv_obj_set_style_bg_opa(btn, 105, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xffc2ca), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, 150, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xffeef0), 0);
    lv_obj_set_style_border_opa(btn, 155, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0xd97c87), 0);
    lv_obj_set_style_shadow_opa(btn, 90, 0);
    lv_obj_set_style_shadow_width(btn, 18, 0);
    lv_obj_set_style_shadow_spread(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x7a2130), 0);
}

static void ui_note_float_y_cb(void * var, int32_t v) {
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void ui_note_float_opa_cb(void * var, int32_t v) {
    lv_obj_set_style_text_opa((lv_obj_t *)var, v, 0);
}

static void ui_note_cleanup_cb(lv_timer_t * timer) {
    lv_obj_t * note = (lv_obj_t *)lv_timer_get_user_data(timer);
    if(note) lv_obj_delete(note);
    lv_timer_delete(timer);
}

static void show_ui_button_note(lv_obj_t * btn) {
    static const uint32_t note_colors[] = {
        0xF97316,
        0x22C55E,
        0x3B82F6,
        0xEAB308,
        0xEC4899,
        0x8B5CF6,
        0x06B6D4,
        0xEF4444,
        0x84CC16,
        0xF59E0B,
        0x14B8A6,
        0x6366F1
    };
    lv_area_t area;
    lv_obj_t * note = lv_label_create(lv_layer_top());
    int color_idx = rand() % (int)(sizeof(note_colors) / sizeof(note_colors[0]));
    int x_offset = (rand() % 19) - 9;
    lv_obj_get_coords(btn, &area);
    lv_label_set_text(note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(note, lv_theme_get_font_normal(NULL), 0);
    lv_obj_set_style_text_color(note, lv_color_hex(note_colors[color_idx]), 0);
    lv_obj_set_style_text_opa(note, 255, 0);
    lv_obj_set_pos(note, area.x2 - 4 + x_offset, area.y1 - 8);

    lv_anim_t y_anim;
    lv_anim_init(&y_anim);
    lv_anim_set_var(&y_anim, note);
    lv_anim_set_values(&y_anim, area.y1 - 8, area.y1 - 34);
    lv_anim_set_time(&y_anim, 320);
    lv_anim_set_path_cb(&y_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&y_anim, ui_note_float_y_cb);
    lv_anim_start(&y_anim);

    lv_anim_t opa_anim;
    lv_anim_init(&opa_anim);
    lv_anim_set_var(&opa_anim, note);
    lv_anim_set_values(&opa_anim, 255, 0);
    lv_anim_set_time(&opa_anim, 320);
    lv_anim_set_path_cb(&opa_anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&opa_anim, ui_note_float_opa_cb);
    lv_anim_start(&opa_anim);

    lv_timer_create(ui_note_cleanup_cb, 340, note);
}

static void ui_button_sound_cb(lv_event_t * e) {
    static const int playable_key_indices[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        10, 11, 13, 14, 15, 17, 18
    };
    lv_obj_t * btn = lv_event_get_target(e);
    int idx = rand() % (int)(sizeof(playable_key_indices) / sizeof(playable_key_indices[0]));
    platform_audio_play_key(playable_key_indices[idx]);
    show_ui_button_note(btn);
}

static void attach_ui_button_sound(lv_obj_t * btn) {
    lv_obj_add_event_cb(btn, ui_button_sound_cb, LV_EVENT_PRESSED, NULL);
}

static lv_obj_t * create_back_button(lv_obj_t * parent, lv_event_cb_t cb) {
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, 120, 40);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 20, 20);
    apply_glass_button_style(btn);
    attach_ui_button_sound(btn);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, "返回大厅");
    lv_obj_set_style_text_font(label, &my_font_full, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(label);
    return btn;
}

static void apply_title_visual(lv_obj_t * label, lv_color_t color) {
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, &my_font_full, 0);
    lv_obj_set_style_transform_zoom(label, 320, 0);
    lv_obj_set_style_transform_pivot_x(label, 0, 0);
    lv_obj_set_style_transform_pivot_y(label, 0, 0);
    lv_obj_set_style_pad_all(label, 6, 0);
}

static void set_page_title(lv_obj_t * label, page_title_t title_kind) {
    const char * text = "";
    switch(title_kind) {
        case PAGE_TITLE_LOGIN: text = "欢迎使用电子钢琴"; break;
        case PAGE_TITLE_MAIN_MENU: text = "应用大厅"; break;
        case PAGE_TITLE_SONG_SELECTION: text = "选择曲目"; break;
        case PAGE_TITLE_FREE_PLAY: text = "自由演奏"; break;
        case PAGE_TITLE_GUIDED: text = "跟弹学习"; break;
        case PAGE_TITLE_AUTO_PLAY: text = "自动演奏"; break;
        case PAGE_TITLE_RECORD: text = "录制演奏"; break;
        case PAGE_TITLE_RECORD_SELECTION: text = "选择要自动回放的录音"; break;
        case PAGE_TITLE_SAVE_RECORD: text = "保存录音（仅支持英文及数字输入）"; break;
        default: break;
    }

    lv_label_set_text(label, text);
    apply_title_visual(label, lv_color_hex(COLOR_TITLE));
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
}

static void fix_list_button_fonts(lv_obj_t * btn) {
    uint32_t child_count = lv_obj_get_child_count(btn);
    for(uint32_t i = 0; i < child_count; i++) {
        lv_obj_t * child = lv_obj_get_child(btn, i);
        if(i == 0) {
            lv_obj_set_style_text_font(child, lv_theme_get_font_normal(NULL), 0);
        } else {
            lv_obj_set_style_text_font(child, &my_font_full, 0);
        }
    }
}

static int is_hidden_item(const char hidden_items[][128], int hidden_count, const char * name) {
    for(int i = 0; i < hidden_count; i++) {
        if(strcmp(hidden_items[i], name) == 0) return 1;
    }
    return 0;
}

static void toggle_hidden_item(char hidden_items[][128], int * hidden_count, const char * name) {
    for(int i = 0; i < *hidden_count; i++) {
        if(strcmp(hidden_items[i], name) == 0) {
            for(int j = i; j < *hidden_count - 1; j++) {
                snprintf(hidden_items[j], sizeof(hidden_items[j]), "%s", hidden_items[j + 1]);
            }
            (*hidden_count)--;
            return;
        }
    }

    if(*hidden_count < MAX_MANAGED_ITEMS) {
        snprintf(hidden_items[*hidden_count], sizeof(hidden_items[*hidden_count]), "%s", name);
        (*hidden_count)++;
    }
}

static int is_guided_song_hidden(const char * name) {
    return is_hidden_item(hidden_guided_items, hidden_guided_count, name);
}

static int is_record_hidden(const char * name) {
    return is_hidden_item(hidden_record_items, hidden_record_count, name);
}

static void load_hidden_items(const char * path, char hidden_items[][128], int * hidden_count) {
    FILE * f = fopen(path, "r");
    char line[160];
    *hidden_count = 0;
    if(!f) return;

    while(fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if(len == 0) continue;
        if(*hidden_count >= MAX_MANAGED_ITEMS) break;
        snprintf(hidden_items[*hidden_count], sizeof(hidden_items[*hidden_count]), "%s", line);
        (*hidden_count)++;
    }
    fclose(f);
}

static void save_hidden_items(const char * path, char hidden_items[][128], int hidden_count) {
    FILE * f = fopen(path, "w");
    if(!f) return;
    for(int i = 0; i < hidden_count; i++) {
        fprintf(f, "%s\n", hidden_items[i]);
    }
    fclose(f);
}

static void load_manage_state(void) {
    mkdir("config", 0777);
    load_hidden_items("config/guided_hidden.cfg", hidden_guided_items, &hidden_guided_count);
    load_hidden_items("config/records_hidden.cfg", hidden_record_items, &hidden_record_count);
}

static void save_manage_state(void) {
    mkdir("config", 0777);
    save_hidden_items("config/guided_hidden.cfg", hidden_guided_items, hidden_guided_count);
    save_hidden_items("config/records_hidden.cfg", hidden_record_items, hidden_record_count);
}

extern void create_piano_ui(void);
extern void create_main_menu_ui(void);
void create_song_selection_ui(void);
static int current_app_mode;
static const char * find_song_title(const char * filename);
static void close_manage_popup(void);
static void show_manage_home_popup(void);
static void show_manage_list_popup(manage_kind_t kind);
static void manage_close_cb(lv_event_t * e);
static void manage_entry_cb(lv_event_t * e);
static void manage_home_action_cb(lv_event_t * e);
static void manage_list_back_cb(lv_event_t * e);
static void manage_toggle_item_cb(lv_event_t * e);
static void create_manage_close_button(lv_obj_t * parent, lv_event_cb_t cb);
static void create_manage_row(lv_obj_t * parent, const char * title_text, const char * filename, int hidden, manage_kind_t kind);
static void load_manage_state(void);
static void save_manage_state(void);

int register_user(const char* user, const char* pass) {
    FILE *f = fopen("users.txt", "r");
    if(f) {
        char line[256]; char u[128], p[128];
        while(fgets(line, sizeof(line), f)) {
            if(sscanf(line, "%127s %127s", u, p) >= 1) {
                if(strcmp(user, u) == 0) { fclose(f); return 0; }
            }
        }
        fclose(f);
    }
    f = fopen("users.txt", "a");
    if(f) { fprintf(f, "%s %s\n", user, pass); fclose(f); return 1; }
    return 0;
}

int check_login(const char* user, const char* pass) {
    FILE *f = fopen("users.txt", "r");
    if(!f) return 0;
    char line[256]; char u[128], p[128];
    while(fgets(line, sizeof(line), f)) {
        if(sscanf(line, "%127s %127s", u, p) == 2) {
            if(strcmp(user, u) == 0 && strcmp(pass, p) == 0) { fclose(f); return 1; }
        }
    }
    fclose(f);
    return 0;
}

static void close_err_cb(lv_event_t * e) {
    lv_obj_t * popup = lv_event_get_user_data(e);
    lv_obj_delete(popup);
}

static void auth_popup_timer_cb(lv_timer_t * timer) {
    lv_obj_t * overlay = (lv_obj_t *)lv_timer_get_user_data(timer);
    if(overlay) lv_obj_delete(overlay);
    lv_timer_delete(timer);
}

static void login_success_popup_timer_cb(lv_timer_t * timer) {
    lv_obj_t * overlay = (lv_obj_t *)lv_timer_get_user_data(timer);
    if(overlay) lv_obj_delete(overlay);
    if(login_win) { lv_obj_delete(login_win); login_win = NULL; }
    if(kb) { lv_obj_delete(kb); kb = NULL; }
    current_app_mode = 0;
    create_piano_ui();
    lv_timer_delete(timer);
}

static lv_obj_t * create_modal_overlay(void) {
    lv_obj_t * overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_center(overlay);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0xb7d6d1), 0);
    lv_obj_set_style_bg_opa(overlay, 150, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    return overlay;
}

static lv_obj_t * create_modal_card(lv_obj_t * parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, 242, 0);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xe8fbf7), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x7ab8b0), 0);
    lv_obj_set_style_shadow_opa(card, 95, 0);
    lv_obj_set_style_shadow_width(card, 32, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void show_auth_popup(const char * message) {
    lv_obj_t * overlay = create_modal_overlay();
    lv_obj_t * popup = create_modal_card(overlay, 400, 210);

    lv_obj_t * label = lv_label_create(popup);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_font(label, &my_font_full, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 42);

    lv_obj_t * btn = lv_button_create(popup);
    lv_obj_set_size(btn, 120, 44);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -24);
    apply_glass_button_style(btn);
    attach_ui_button_sound(btn);
    lv_obj_add_event_cb(btn, close_err_cb, LV_EVENT_CLICKED, overlay);

    lv_obj_t * blabel = lv_label_create(btn);
    lv_label_set_text(blabel, "关闭");
    lv_obj_set_style_text_font(blabel, &my_font_full, 0);
    lv_obj_set_style_text_color(blabel, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(blabel);
}

static void show_timed_auth_popup(const char * message, uint32_t delay_ms, lv_timer_cb_t timer_cb) {
    lv_obj_t * overlay = create_modal_overlay();
    lv_obj_t * popup = create_modal_card(overlay, 400, 170);

    lv_obj_t * label = lv_label_create(popup);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_font(label, &my_font_full, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    lv_timer_create(timer_cb ? timer_cb : auth_popup_timer_cb, delay_ms, overlay);
}

static void close_manage_popup(void) {
    if(manage_overlay) {
        lv_obj_delete(manage_overlay);
        manage_overlay = NULL;
    }
    manage_popup = NULL;
    current_manage_kind = MANAGE_KIND_NONE;
}

static void manage_close_cb(lv_event_t * e) {
    LV_UNUSED(e);
    close_manage_popup();
}

static void manage_entry_cb(lv_event_t * e) {
    LV_UNUSED(e);
    show_manage_home_popup();
}

static void manage_home_action_cb(lv_event_t * e) {
    intptr_t action = (intptr_t)lv_event_get_user_data(e);
    if(action == 1) show_manage_list_popup(MANAGE_KIND_GUIDED);
    else if(action == 2) show_manage_list_popup(MANAGE_KIND_RECORD);
    else close_manage_popup();
}

static void manage_list_back_cb(lv_event_t * e) {
    LV_UNUSED(e);
    show_manage_home_popup();
}

static void manage_toggle_item_cb(lv_event_t * e) {
    const char * payload = (const char *)lv_event_get_user_data(e);
    manage_kind_t kind = payload[0] == '1' ? MANAGE_KIND_GUIDED : MANAGE_KIND_RECORD;
    const char * name = payload + 2;

    if(kind == MANAGE_KIND_GUIDED) toggle_hidden_item(hidden_guided_items, &hidden_guided_count, name);
    else toggle_hidden_item(hidden_record_items, &hidden_record_count, name);

    save_manage_state();
    show_manage_list_popup(kind);
}

static void create_manage_close_button(lv_obj_t * parent, lv_event_cb_t cb) {
    lv_obj_t * btn_close = lv_button_create(parent);
    lv_obj_set_size(btn_close, 32, 32);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, 18, -18);
    apply_danger_glass_button_style(btn_close);
    lv_obj_set_style_radius(btn_close, 16, 0);
    lv_obj_set_style_shadow_width(btn_close, 18, 0);
    lv_obj_set_style_shadow_opa(btn_close, 76, 0);
    attach_ui_button_sound(btn_close);
    lv_obj_add_event_cb(btn_close, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "×");
    lv_obj_set_style_text_font(lbl_close, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_close, lv_color_hex(0x7F1D1D), 0);
    lv_obj_center(lbl_close);
}

static void create_manage_row(lv_obj_t * parent, const char * title_text, const char * filename, int hidden, manage_kind_t kind) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_set_size(row, 390, 58);
    lv_obj_set_style_bg_color(row, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(row, 100, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0xe2f5f0), 0);
    lv_obj_set_style_radius(row, 18, 0);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(row);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, &my_font_full, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TITLE), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, -10);

    lv_obj_t * action_btn = lv_button_create(row);
    lv_obj_set_size(action_btn, 86, 36);
    lv_obj_align(action_btn, LV_ALIGN_RIGHT_MID, -8, 0);
    if(hidden) apply_glass_button_style(action_btn);
    else apply_danger_glass_button_style(action_btn);
    attach_ui_button_sound(action_btn);

    {
        char packed[132];
        snprintf(packed, sizeof(packed), "%d|%s", (int)kind, filename);
        lv_obj_add_event_cb(action_btn, manage_toggle_item_cb, LV_EVENT_CLICKED, strdup(packed));
    }

    lv_obj_t * action_label = lv_label_create(action_btn);
    lv_label_set_text(action_label, hidden ? "添加" : "删除");
    lv_obj_set_style_text_font(action_label, &my_font_full, 0);
    lv_obj_set_style_text_color(action_label, hidden ? lv_color_hex(0x1f4f55) : lv_color_hex(0x7a2130), 0);
    lv_obj_center(action_label);
}

static void show_manage_home_popup(void) {
    close_manage_popup();
    manage_overlay = create_modal_overlay();
    manage_popup = create_modal_card(manage_overlay, 360, 210);

    lv_obj_t * title = lv_label_create(manage_popup);
    lv_label_set_text(title, "资源管理");
    apply_title_visual(title, lv_color_hex(COLOR_TITLE));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * btn_guided = lv_button_create(manage_popup);
    lv_obj_set_size(btn_guided, 210, 46);
    lv_obj_align(btn_guided, LV_ALIGN_TOP_MID, 0, 76);
    apply_glass_button_style(btn_guided);
    attach_ui_button_sound(btn_guided);
    lv_obj_add_event_cb(btn_guided, manage_home_action_cb, LV_EVENT_CLICKED, (void *)1);
    lv_obj_t * lbl_guided = lv_label_create(btn_guided);
    lv_label_set_text(lbl_guided, "管理跟弹学习曲目");
    lv_obj_set_style_text_font(lbl_guided, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_guided, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(lbl_guided);

    lv_obj_t * btn_record = lv_button_create(manage_popup);
    lv_obj_set_size(btn_record, 210, 46);
    lv_obj_align(btn_record, LV_ALIGN_TOP_MID, 0, 132);
    apply_glass_button_style(btn_record);
    attach_ui_button_sound(btn_record);
    lv_obj_add_event_cb(btn_record, manage_home_action_cb, LV_EVENT_CLICKED, (void *)2);
    lv_obj_t * lbl_record = lv_label_create(btn_record);
    lv_label_set_text(lbl_record, "管理录制曲目");
    lv_obj_set_style_text_font(lbl_record, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_record, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(lbl_record);

    create_manage_close_button(manage_popup, manage_close_cb);
}

static void show_manage_list_popup(manage_kind_t kind) {
    close_manage_popup();
    current_manage_kind = kind;
    manage_overlay = create_modal_overlay();
    manage_popup = create_modal_card(manage_overlay, 460, 320);

    lv_obj_t * title = lv_label_create(manage_popup);
    lv_label_set_text(title, kind == MANAGE_KIND_GUIDED ? "跟弹曲目管理" : "录制曲目管理");
    apply_title_visual(title, lv_color_hex(COLOR_TITLE));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * btn_back = lv_button_create(manage_popup);
    lv_obj_set_size(btn_back, 32, 32);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, -18, -18);
    apply_glass_button_style(btn_back);
    lv_obj_set_style_radius(btn_back, 16, 0);
    lv_obj_set_style_shadow_width(btn_back, 18, 0);
    lv_obj_set_style_shadow_opa(btn_back, 76, 0);
    attach_ui_button_sound(btn_back);
    lv_obj_add_event_cb(btn_back, manage_list_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(lbl_back, lv_theme_get_font_normal(NULL), 0);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(lbl_back);

    create_manage_close_button(manage_popup, manage_close_cb);

    lv_obj_t * list = lv_obj_create(manage_popup);
    lv_obj_set_size(list, 410, 196);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_bg_opa(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    {
        DIR * d = opendir(kind == MANAGE_KIND_GUIDED ? "music" : "records");
        struct dirent * dir;
        int item_count = 0;
        if(d) {
            while((dir = readdir(d)) != NULL) {
                int matched = (kind == MANAGE_KIND_GUIDED) ? (strstr(dir->d_name, ".wav") != NULL) : (strstr(dir->d_name, ".rec") != NULL);
                if(!matched) continue;
                if(kind == MANAGE_KIND_GUIDED) {
                    create_manage_row(list, find_song_title(dir->d_name), dir->d_name, is_guided_song_hidden(dir->d_name), kind);
                } else {
                    create_manage_row(list, dir->d_name, dir->d_name, is_record_hidden(dir->d_name), kind);
                }
                item_count++;
            }
            closedir(d);
        }

        if(item_count == 0) {
            lv_obj_t * empty = lv_label_create(list);
            lv_label_set_text(empty, kind == MANAGE_KIND_GUIDED ? "暂无可管理的跟弹曲目" : "暂无可管理的录制曲目");
            lv_obj_set_style_text_font(empty, &my_font_full, 0);
            lv_obj_set_style_text_color(empty, lv_color_hex(COLOR_SUBTITLE), 0);
        }
    }
}

static void login_btn_cb(lv_event_t * e) {
    intptr_t action = (intptr_t)lv_event_get_user_data(e);
    const char * u = lv_textarea_get_text(ta_user);
    const char * p = lv_textarea_get_text(ta_pass);
    
    if(strlen(u) == 0 || strlen(p) == 0) {
        if(action == 1) show_auth_popup("登录账号或密码不能为空！");
        else if(action == 2) show_auth_popup("注册账号或密码不能为空！");
        return;
    }
    
    if(action == 1) { // Login
        if(check_login(u, p)) {
            printf("[Login] Validation Success for %s!\n", u);
            show_timed_auth_popup("登录成功！", 900, login_success_popup_timer_cb);
        } else {
            show_auth_popup("登录失败，账号或密码错误！");
        }
    } else if(action == 2) { // Register
        if(register_user(u, p)) {
            printf("[Register] Success: %s\n", u);
            show_timed_auth_popup("注册成功！", 900, auth_popup_timer_cb);
        } else {
            printf("[Register] Failed: User %s may already exist.\n", u);
            show_auth_popup("注册失败，用户名已存在！");
        }
    }
}

static void hide_login_keyboard_except(lv_obj_t * keep_ta) {
    if(kb != NULL) {
        lv_obj_delete(kb);
        kb = NULL;
    }
    if(ta_user && ta_user != keep_ta) lv_obj_remove_state(ta_user, LV_STATE_FOCUSED);
    if(ta_pass && ta_pass != keep_ta) lv_obj_remove_state(ta_pass, LV_STATE_FOCUSED);
    login_active_ta = keep_ta;
}

static void show_login_keyboard(lv_obj_t * ta) {
    if(ta == NULL) return;
    if(kb != NULL) {
        lv_obj_delete(kb);
        kb = NULL;
    }
    kb = lv_keyboard_create(lv_screen_active());
    lv_keyboard_set_textarea(kb, ta);
    login_active_ta = ta;
}

static void reopen_login_keyboard_cb(lv_timer_t * timer) {
    lv_obj_t * ta = (lv_obj_t *)lv_timer_get_user_data(timer);
    if(ta != NULL) {
        lv_obj_add_state(ta, LV_STATE_FOCUSED);
        show_login_keyboard(ta);
    }
    login_pending_ta = NULL;
    lv_timer_delete(timer);
}

static void login_bg_event_cb(lv_event_t * e) {
    lv_obj_t * target = lv_event_get_target(e);
    if(target == ta_user || target == ta_pass || target == kb) return;
    login_pending_ta = NULL;
    hide_login_keyboard_except(NULL);
}

static void ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    if(code == LV_EVENT_FOCUSED) {
        if(login_active_ta != NULL && login_active_ta != ta) {
            login_pending_ta = ta;
            hide_login_keyboard_except(NULL);
            lv_timer_create(reopen_login_keyboard_cb, 30, ta);
        } else {
            hide_login_keyboard_except(ta);
            lv_obj_add_state(ta, LV_STATE_FOCUSED);
            show_login_keyboard(ta);
        }
    } else if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        LV_UNUSED(ta);
        login_pending_ta = NULL;
        hide_login_keyboard_except(NULL);
    }
}

void create_login_ui(void) {
    login_win = lv_obj_create(lv_screen_active());
    lv_obj_set_size(login_win, 800, 480);
    lv_obj_center(login_win);
    apply_page_bg(login_win);
    login_active_ta = NULL;
    login_pending_ta = NULL;
    lv_obj_add_event_cb(login_win, login_bg_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t * title = lv_label_create(login_win);
    lv_label_set_text(title, "欢迎使用电子钢琴");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &my_font_full, 0); 
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    set_page_title(title, PAGE_TITLE_LOGIN);
    
    ta_user = lv_textarea_create(login_win);
    lv_obj_set_style_text_font(ta_user, &my_font_full, 0); // 确保占位符和输入文字使用全量字库
    lv_obj_set_size(ta_user, 300, 40);
    lv_obj_align(ta_user, LV_ALIGN_CENTER, 0, -60);
    lv_textarea_set_placeholder_text(ta_user, "请输入用户名");
    lv_obj_add_event_cb(ta_user, ta_event_cb, LV_EVENT_ALL, NULL);
    lv_textarea_set_one_line(ta_user, true);
    
    ta_pass = lv_textarea_create(login_win);
    lv_obj_set_style_text_font(ta_pass, &my_font_full, 0); // 确保占位符和输入文字使用全量字库
    lv_obj_set_size(ta_pass, 300, 40);
    lv_obj_align(ta_pass, LV_ALIGN_CENTER, 0, -10);
    lv_textarea_set_placeholder_text(ta_pass, "请输入密码");
    lv_textarea_set_password_mode(ta_pass, true);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);
    lv_textarea_set_one_line(ta_pass, true);
    
    lv_obj_t * btn_login = lv_button_create(login_win);
    lv_obj_set_size(btn_login, 120, 40);
    lv_obj_align(btn_login, LV_ALIGN_CENTER, -70, 50);
    apply_glass_button_style(btn_login);
    attach_ui_button_sound(btn_login);
    lv_obj_add_event_cb(btn_login, login_btn_cb, LV_EVENT_CLICKED, (void*)1);
    lv_obj_t * lbl = lv_label_create(btn_login);
    lv_obj_set_style_text_font(lbl, &my_font_full, 0); // 按钮文字必须也使用全量字库
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1f4f55), 0);
    lv_label_set_text(lbl, "登录");
    lv_obj_center(lbl);

    lv_obj_t * btn_reg = lv_button_create(login_win);
    lv_obj_set_size(btn_reg, 120, 40);
    lv_obj_align(btn_reg, LV_ALIGN_CENTER, 70, 50);
    apply_glass_button_style(btn_reg);
    attach_ui_button_sound(btn_reg);
    lv_obj_add_event_cb(btn_reg, login_btn_cb, LV_EVENT_CLICKED, (void*)2);
    lbl = lv_label_create(btn_reg);
    lv_obj_set_style_text_font(lbl, &my_font_full, 0); // 按钮文字必须也使用全量字库
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1f4f55), 0);
    lv_label_set_text(lbl, "注册");
    lv_obj_center(lbl);
    
    kb = NULL;
}

// ================= Piano UI logic =================
static lv_obj_t * piano_win = NULL;
static lv_timer_t * piano_timer = NULL;
static lv_obj_t * piano_keys[19];
static const char * piano_key_names[19];
static int key_is_playing[19];
static const char * default_key_names[19] = {
    "C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5",
    "C4#", "D4#", "", "F4#", "G4#", "A4#", "", "C5#", "D5#"
};

// ================= Rhythm Game Logic =================
typedef struct { int key_idx; int hit_time_ms; } SongNote;
typedef struct { lv_obj_t * obj; int target_key; int hit_time_ms; int is_active; } ActiveNote;

static int current_app_mode = 0; // 0: FREE, 1: GUIDED, 2: AUTO, 3: RECORD
static int game_score = 0;
static int game_combo = 0;
static int game_max_combo = 0;
static int game_time_ms = -4000; 
static uint32_t game_last_tick = 0;
static int game_bgm_started = 0;
static lv_timer_t * game_timer = NULL;
static lv_obj_t * lbl_score = NULL;
static lv_obj_t * lbl_combo = NULL;
static lv_obj_t * track_container = NULL;
static lv_obj_t * guided_countdown_label = NULL;
static lv_obj_t * guided_judgement_label = NULL;
static int guided_countdown_stage = -1;
static lv_obj_t * guided_finish_overlay = NULL;
static lv_obj_t * guided_finish_popup = NULL;

static ActiveNote active_notes[50];
static int next_note_idx = 0;

#define GUIDED_COUNTDOWN_MS 4000
#define GUIDED_SONG_START_OFFSET_MS 1000

static const SongNote map_two_tigers[] = {
    {0, 1000}, {1, 1500}, {2, 2000}, {0, 2500},
    {0, 3000}, {1, 3500}, {2, 4000}, {0, 4500},
    {2, 5000}, {3, 5500}, {4, 6000},
    {2, 7000}, {3, 7500}, {4, 8000},
    {4, 9000}, {5, 9250}, {4, 9500}, {3, 9750}, {2, 10000}, {0, 10500},
    {4, 11000}, {5, 11250}, {4, 11500}, {3, 11750}, {2, 12000}, {0, 12500},
    {0, 13000}, {4, 13500}, {0, 14000},
    {0, 15000}, {4, 15500}, {0, 16000},
    {-1, 0}
};

static const SongNote map_little_star[] = {
    {0, 1000}, {0, 1500}, {4, 2000}, {4, 2500}, {5, 3000}, {5, 3500}, {4, 4000},
    {3, 5000}, {3, 5500}, {2, 6000}, {2, 6500}, {1, 7000}, {1, 7500}, {0, 8000},
    {4, 9000}, {4, 9500}, {3, 10000}, {3, 10500}, {2, 11000}, {2, 11500}, {1, 12000},
    {4, 13000}, {4, 13500}, {3, 14000}, {3, 14500}, {2, 15000}, {2, 15500}, {1, 16000},
    {0, 17000}, {0, 17500}, {4, 18000}, {4, 18500}, {5, 19000}, {5, 19500}, {4, 20000},
    {3, 21000}, {3, 21500}, {2, 22000}, {2, 22500}, {1, 23000}, {1, 23500}, {0, 24000},
    {-1, 0}
};

static const SongNote map_happy_birthday[] = {
    {0, 1000}, {0, 1250}, {1, 1500}, {0, 2000}, {3, 2500}, {2, 3000},
    {0, 4000}, {0, 4250}, {1, 4500}, {0, 5000}, {4, 5500}, {3, 6000},
    {0, 7000}, {0, 7250}, {7, 7500}, {5, 8000}, {3, 8500}, {2, 9000}, {1, 9500},
    {15, 10500}, {15, 10750}, {5, 11000}, {3, 11500}, {4, 12000}, {3, 12500},
    {-1, 0}
};

static const SongNote empty_map[] = {{-1, 0}};

typedef struct {
    const char * filename;
    const char * title;
    const SongNote * notes;
} SongRegistry;

static const SongRegistry song_db[] = {
    {"two_tigers.wav", "两只老虎 (Two Tigers)", map_two_tigers},
    {"little_star.wav", "小星星 (Little Star)", map_little_star},
    {"happy_birthday.wav", "生日快乐 (Happy Birthday)", map_happy_birthday},
    {NULL, NULL, NULL}
};

static const char * find_song_title(const char * filename) {
    for(int i = 0; song_db[i].filename != NULL; i++) {
        if(strcmp(song_db[i].filename, filename) == 0) return song_db[i].title;
    }
    return filename;
}

static const SongNote * current_song_map = empty_map;
static char current_song_title[128] = "跟弹学习";
static char current_song_filename[128] = "";

// ================= Recording & AutoPlay =================
#define MAX_RECORD_NOTES 1024
static SongNote recorded_buffer[MAX_RECORD_NOTES];
static int record_note_count = 0;
static uint32_t record_start_tick = 0;
static int is_recording = 0;
static lv_obj_t * lbl_record_status = NULL;
static lv_obj_t * btn_record_toggle = NULL;
static lv_obj_t * lbl_record_toggle = NULL;

static void update_record_toggle_button(void) {
    if(btn_record_toggle == NULL || lbl_record_toggle == NULL) return;

    if(is_recording) {
        apply_danger_glass_button_style(btn_record_toggle);
        lv_label_set_text(lbl_record_toggle, "结束录制");
        lv_obj_set_style_text_color(lbl_record_toggle, lv_color_hex(0x7a2130), 0);
    } else {
        apply_glass_button_style(btn_record_toggle);
        lv_label_set_text(lbl_record_toggle, "开始录制");
        lv_obj_set_style_text_color(lbl_record_toggle, lv_color_hex(0x1f4f55), 0);
    }

    lv_obj_set_style_text_font(lbl_record_toggle, &my_font_full, 0);
    lv_obj_center(lbl_record_toggle);
}

static SongNote playback_buffer[MAX_RECORD_NOTES];
static int auto_play_idx = 0;
static uint32_t auto_play_start_tick = 0;
static uint32_t auto_play_release_tick[19] = {0}; 
static lv_timer_t * auto_play_timer = NULL;
static lv_obj_t * lbl_auto_status = NULL;
static lv_obj_t * auto_finish_overlay = NULL;
static lv_obj_t * auto_finish_popup = NULL;
static char current_auto_record_name[128] = "";

static void create_naming_ui(void);
static void create_record_selection_ui(void);
static void stop_recording_cb(lv_event_t * e);
static void record_toggle_cb(lv_event_t * e);
static void show_guided_finish_popup(void);
static void show_auto_finish_popup(void);
static void start_auto_play_from_record(const char * filename);
static void close_auto_finish_popup(void);
static void close_guided_finish_popup(void);
static void restart_guided_song(void);
static void restart_auto_play_current(void);
static void guided_finish_popup_delay_cb(lv_timer_t * timer);
static void auto_finish_popup_delay_cb(lv_timer_t * timer);
static void delayed_song_start_cb(lv_timer_t * timer);
static void delayed_auto_play_start_cb(lv_timer_t * timer);
static void close_manage_popup(void);
static void show_manage_home_popup(void);
static void show_manage_list_popup(manage_kind_t kind);

extern void create_login_ui(void);

// ================= Volume Control =================
static int piano_mix_volume = 80;
static int bgm_mix_volume = 75;

static lv_obj_t * vol_panel = NULL;
static lv_obj_t * vol_slider_piano = NULL;
static lv_obj_t * vol_slider_bgm = NULL;
static lv_obj_t * vol_icon = NULL;

// ================= Hardware Audio Mixer =================
#if 0
#define MAX_AUDIO_CHANNELS 16
#define AUDIO_MIX_BUFFER_SAMPLES 512

typedef struct { int16_t * data; uint32_t length_samples; } HardwareWav;
typedef struct { HardwareWav * wav; uint32_t current_pos; int is_playing; float volume; } AudioChannel;

static HardwareWav key_wavs[19];
static HardwareWav bgm_wav;
static AudioChannel audio_channels[MAX_AUDIO_CHANNELS];
static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static int dsp_fd = -1;
static pthread_t audio_thread_id;

#if APP_TARGET_PC
static SDL_AudioDeviceID sdl_audio_device = 0;

static void sdl_audio_callback(void * userdata, Uint8 * stream, int len) {
    int32_t * mix_buf = (int32_t *)calloc((size_t)len / sizeof(int16_t), sizeof(int32_t));
    int16_t * out_buf = (int16_t *)stream;
    int sample_count = len / (int)sizeof(int16_t);

    LV_UNUSED(userdata);
    if(mix_buf == NULL) {
        SDL_memset(stream, 0, (size_t)len);
        return;
    }

    pthread_mutex_lock(&audio_mutex);
    {
        float master_vol = ui_volume / 100.0f;
        for(int c = 0; c < MAX_AUDIO_CHANNELS; c++) {
            if(audio_channels[c].is_playing && audio_channels[c].wav && audio_channels[c].wav->data) {
                uint32_t len_samples = audio_channels[c].wav->length_samples;
                uint32_t pos = audio_channels[c].current_pos;
                float final_vol = master_vol * audio_channels[c].volume;

                for(int i = 0; i < sample_count; i++) {
                    if(pos < len_samples) {
                        mix_buf[i] += (int32_t)(audio_channels[c].wav->data[pos] * final_vol);
                        pos++;
                    } else {
                        audio_channels[c].is_playing = 0;
                        break;
                    }
                }

                audio_channels[c].current_pos = pos;
            }
        }
    }
    pthread_mutex_unlock(&audio_mutex);

    for(int i = 0; i < sample_count; i++) {
        int32_t sample = mix_buf[i];
        if(sample > 32767) sample = 32767;
        else if(sample < -32768) sample = -32768;
        out_buf[i] = (int16_t)sample;
    }

    free(mix_buf);
}

static HardwareWav load_wav_into_ram(const char * filepath) {
    HardwareWav res = {NULL, 0};
    SDL_AudioSpec wav_spec;
    Uint8 * wav_buf = NULL;
    Uint32 wav_len = 0;
    SDL_AudioCVT cvt;
    Uint8 * src_buf = NULL;
    Uint32 src_len = 0;

    if(SDL_LoadWAV(filepath, &wav_spec, &wav_buf, &wav_len) == NULL) {
        printf("[Audio][PC] Failed to load WAV: %s (%s)\n", filepath, SDL_GetError());
        return res;
    }

    if(SDL_BuildAudioCVT(&cvt, wav_spec.format, wav_spec.channels, wav_spec.freq,
                         AUDIO_S16SYS, 1, 44100) < 0) {
        printf("[Audio][PC] Failed to build audio converter for %s (%s)\n", filepath, SDL_GetError());
        SDL_FreeWAV(wav_buf);
        return res;
    }

    src_buf = wav_buf;
    src_len = wav_len;

    if(cvt.needed) {
        cvt.len = (int)wav_len;
        cvt.buf = (Uint8 *)SDL_malloc((size_t)wav_len * (size_t)cvt.len_mult);
        if(cvt.buf == NULL) {
            SDL_FreeWAV(wav_buf);
            return res;
        }

        SDL_memcpy(cvt.buf, wav_buf, wav_len);
        if(SDL_ConvertAudio(&cvt) < 0) {
            printf("[Audio][PC] Failed to convert audio for %s (%s)\n", filepath, SDL_GetError());
            SDL_free(cvt.buf);
            SDL_FreeWAV(wav_buf);
            return res;
        }

        src_buf = cvt.buf;
        src_len = (Uint32)cvt.len_cvt;
    }

    res.data = (int16_t *)malloc(src_len);
    if(res.data != NULL) {
        memcpy(res.data, src_buf, src_len);
        res.length_samples = src_len / sizeof(int16_t);
    }

    if(cvt.needed) SDL_free(cvt.buf);
    SDL_FreeWAV(wav_buf);
    return res;
}

static void play_hardware_sound(int ch_idx, HardwareWav * wav, float vol) {
    if(!wav || !wav->data) return;
    pthread_mutex_lock(&audio_mutex);
    audio_channels[ch_idx].wav = wav;
    audio_channels[ch_idx].current_pos = 0;
    audio_channels[ch_idx].volume = vol;
    audio_channels[ch_idx].is_playing = 1;
    pthread_mutex_unlock(&audio_mutex);
}

static void play_key_sound(int key_idx) {
    if(key_idx < 0 || key_idx > 18) return;
    int found_ch = -1;
    pthread_mutex_lock(&audio_mutex);
    for(int i = 1; i < MAX_AUDIO_CHANNELS; i++) {
        if(!audio_channels[i].is_playing) {
            found_ch = i;
            break;
        }
    }
    if(found_ch == -1) found_ch = 1;
    pthread_mutex_unlock(&audio_mutex);
    play_hardware_sound(found_ch, &key_wavs[key_idx], 1.0f);
}

static void play_bgm_sound() {
    play_hardware_sound(0, &bgm_wav, 0.4f);
}

static void stop_bgm_sound() {
    pthread_mutex_lock(&audio_mutex);
    audio_channels[0].is_playing = 0;
    pthread_mutex_unlock(&audio_mutex);
}

void init_audio_mixer_once() {
    static int initialized = 0;
    if(initialized) return;

    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;

    if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("[Audio][PC] SDL audio init failed: %s\n", SDL_GetError());
        return;
    }

    memset(audio_channels, 0, sizeof(audio_channels));
    memset(&desired, 0, sizeof(desired));
    memset(&obtained, 0, sizeof(obtained));

    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 1024;
    desired.callback = sdl_audio_callback;

    sdl_audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if(sdl_audio_device == 0) {
        printf("[Audio][PC] Failed to open audio device: %s\n", SDL_GetError());
        return;
    }

    for(int i = 0; i < 19; i++) {
        if(piano_key_names[i] && strlen(piano_key_names[i]) > 0) {
            char path[128];
            snprintf(path, sizeof(path), "aduio/%s.wav", piano_key_names[i]);
            key_wavs[i] = load_wav_into_ram(path);
        }
    }

    SDL_PauseAudioDevice(sdl_audio_device, 0);
    initialized = 1;
}
#else
static HardwareWav load_wav_into_ram(const char * filepath) {
    HardwareWav res = {NULL, 0};
    int fd = open(filepath, O_RDONLY);
    if(fd < 0) { printf("[Audio] Missing/Faied to load: %s\n", filepath); return res; }
    off_t size = lseek(fd, 0, SEEK_END);
    if(size <= 44) { close(fd); return res; }
    lseek(fd, 44, SEEK_SET);
    uint32_t pcm_bytes = size - 44;
    res.data = (int16_t *)malloc(pcm_bytes);
    if(!res.data) { close(fd); return res; }
    read(fd, res.data, pcm_bytes);
    close(fd);
    res.length_samples = pcm_bytes / 2;
    return res;
}

static void * hardware_audio_mixer_thread(void * arg) {
    dsp_fd = open("/dev/dsp", O_WRONLY);
    if(dsp_fd < 0) return NULL;
    int arg_val = AFMT_S16_LE; ioctl(dsp_fd, SNDCTL_DSP_SETFMT, &arg_val);
    arg_val = 2;               ioctl(dsp_fd, SNDCTL_DSP_CHANNELS, &arg_val);
    arg_val = 44100;           ioctl(dsp_fd, SNDCTL_DSP_SPEED, &arg_val);
    int16_t out_buf[AUDIO_MIX_BUFFER_SAMPLES];
    int32_t mix_buf[AUDIO_MIX_BUFFER_SAMPLES];
    while(1) {
        memset(mix_buf, 0, sizeof(mix_buf));
        int active_count = 0;
        pthread_mutex_lock(&audio_mutex);
        float master_vol = ui_volume / 100.0f;
        for(int c=0; c<MAX_AUDIO_CHANNELS; c++) {
            if(audio_channels[c].is_playing && audio_channels[c].wav && audio_channels[c].wav->data) {
                active_count++;
                uint32_t len = audio_channels[c].wav->length_samples;
                uint32_t pos = audio_channels[c].current_pos;
                float final_vol = master_vol * audio_channels[c].volume;
                for(int i=0; i<AUDIO_MIX_BUFFER_SAMPLES; i++) {
                    if(pos < len) {
                        mix_buf[i] += (int32_t)(audio_channels[c].wav->data[pos] * final_vol);
                        pos++;
                    } else {
                        audio_channels[c].is_playing = 0; break;
                    }
                }
                audio_channels[c].current_pos = pos;
            }
        }
        pthread_mutex_unlock(&audio_mutex);
        if(active_count == 0) { usleep(2000); continue; }
        for(int i=0; i<AUDIO_MIX_BUFFER_SAMPLES; i++) {
            int32_t sample = mix_buf[i];
            if(sample > 32767) sample = 32767;
            else if(sample < -32768) sample = -32768; // 物理裁切防爆音
            out_buf[i] = (int16_t)sample;
        }
        write(dsp_fd, out_buf, sizeof(out_buf));
    }
    return NULL;
}

static void play_hardware_sound(int ch_idx, HardwareWav * wav, float vol) {
    if(!wav || !wav->data) return;
    pthread_mutex_lock(&audio_mutex);
    audio_channels[ch_idx].wav = wav;
    audio_channels[ch_idx].current_pos = 0;
    audio_channels[ch_idx].volume = vol;
    audio_channels[ch_idx].is_playing = 1;
    pthread_mutex_unlock(&audio_mutex);
}

static void play_key_sound(int key_idx) {
    if(key_idx < 0 || key_idx > 18) return;
    int found_ch = -1;
    pthread_mutex_lock(&audio_mutex);
    for(int i=1; i<MAX_AUDIO_CHANNELS; i++) {
        if(!audio_channels[i].is_playing) { found_ch = i; break; }
    }
    if(found_ch == -1) found_ch = 1; // 挤占发声
    pthread_mutex_unlock(&audio_mutex);
    play_hardware_sound(found_ch, &key_wavs[key_idx], 1.0f);
}

static void play_bgm_sound() { play_hardware_sound(0, &bgm_wav, 0.4f); }
static void stop_bgm_sound() { pthread_mutex_lock(&audio_mutex); audio_channels[0].is_playing = 0; pthread_mutex_unlock(&audio_mutex); }

void init_audio_mixer_once() {
    static int initialized = 0;
    if(initialized) return;
    memset(audio_channels, 0, sizeof(audio_channels));
    for(int i=0; i<19; i++) {
        if(piano_key_names[i] && strlen(piano_key_names[i]) > 0) {
            char path[128];
            snprintf(path, sizeof(path), "aduio/%s.wav", piano_key_names[i]); // 动态索引你的 .wav
            key_wavs[i] = load_wav_into_ram(path);
        }
    }
    pthread_create(&audio_thread_id, NULL, hardware_audio_mixer_thread, NULL);
    initialized = 1;
}
#endif
#endif

static void update_score_ui() {
    if(lbl_score) lv_label_set_text_fmt(lbl_score, "Score: %d    Combo: %d", game_score, game_combo);
}

static void guided_countdown_rotation_anim_cb(void * var, int32_t v) {
    lv_obj_set_style_transform_rotation((lv_obj_t *)var, v, 0);
}

static void guided_countdown_zoom_anim_cb(void * var, int32_t v) {
    lv_obj_set_style_transform_zoom((lv_obj_t *)var, v, 0);
}

static void guided_hit_note_delete_cb(lv_timer_t * timer) {
    lv_obj_t * note_obj = (lv_obj_t *)lv_timer_get_user_data(timer);
    if(note_obj) lv_obj_delete(note_obj);
    lv_timer_delete(timer);
}

static void guided_hide_judgement_cb(lv_timer_t * timer) {
    LV_UNUSED(timer);
    if(guided_judgement_label) lv_obj_add_flag(guided_judgement_label, LV_OBJ_FLAG_HIDDEN);
    lv_timer_delete(timer);
}

static void show_guided_judgement(int is_perfect) {
    if(guided_judgement_label == NULL) return;

    lv_label_set_text(guided_judgement_label, is_perfect ? "Perfect" : "Good");
    lv_obj_set_style_text_color(guided_judgement_label,
                                lv_color_hex(is_perfect ? 0x166534 : 0xFACC15), 0);
    lv_obj_set_style_transform_zoom(guided_judgement_label, is_perfect ? 320 : 280, 0);
    lv_obj_clear_flag(guided_judgement_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(guided_judgement_label, LV_ALIGN_CENTER, 0, 74);
    lv_timer_create(guided_hide_judgement_cb, 260, NULL);
}

static void guided_finish_popup_delay_cb(lv_timer_t * timer) {
    LV_UNUSED(timer);
    show_guided_finish_popup();
    lv_timer_delete(timer);
}

static void auto_finish_popup_delay_cb(lv_timer_t * timer) {
    LV_UNUSED(timer);
    show_auto_finish_popup();
    lv_timer_delete(timer);
}

static void delayed_song_start_cb(lv_timer_t * timer) {
    char * filename = (char *)lv_timer_get_user_data(timer);
    if(filename) {
        current_song_map = empty_map;
        snprintf(current_song_filename, sizeof(current_song_filename), "%s", filename);
        snprintf(current_song_title, sizeof(current_song_title), "%s", filename);
        for(int i = 0; song_db[i].filename; i++) {
            if(strcmp(song_db[i].filename, filename) == 0) {
                current_song_map = song_db[i].notes;
                snprintf(current_song_title, sizeof(current_song_title), "%s", song_db[i].title);
                break;
            }
        }
    }

    if(song_sel_win) { lv_obj_delete(song_sel_win); song_sel_win = NULL; }
    create_piano_ui();
    free(filename);
    lv_timer_delete(timer);
}

static void delayed_auto_play_start_cb(lv_timer_t * timer) {
    char * filename = (char *)lv_timer_get_user_data(timer);
    start_auto_play_from_record(filename);
    free(filename);
    lv_timer_delete(timer);
}

static int try_guided_hit_for_key(int key_idx, int guided_song_time_ms) {
    for(int j = 0; j < 50; j++) {
        if(active_notes[j].is_active && active_notes[j].target_key == key_idx) {
            int diff = guided_song_time_ms - active_notes[j].hit_time_ms;
            if(diff >= -260 && diff <= 260) {
                int is_perfect = (diff >= -90 && diff <= 90);
                game_score += 10;
                game_combo += 1;
                if(game_combo > game_max_combo) game_max_combo = game_combo;

                lv_obj_set_style_bg_color(active_notes[j].obj,
                                          lv_color_hex(is_perfect ? 0x166534 : 0xFACC15), 0);
                show_guided_judgement(is_perfect);
                lv_timer_create(guided_hit_note_delete_cb, 120, active_notes[j].obj);
                active_notes[j].is_active = 0;
                update_score_ui();
                return 1;
            }
        }
    }
    return 0;
}

static int has_blocking_popup(void) {
    return guided_finish_popup != NULL ||
           auto_finish_popup != NULL ||
           manage_popup != NULL;
}

static void update_guided_countdown_prompt(void) {
    static const char * texts[] = {"3", "2", "1", "开始"};
    static const uint32_t colors[] = {0x22C55E, 0xEAB308, 0xF97316, 0xEF4444};
    int stage = -1;

    if(game_time_ms < -3000) stage = 0;
    else if(game_time_ms < -2000) stage = 1;
    else if(game_time_ms < -1000) stage = 2;
    else if(game_time_ms < 0) stage = 3;

    if(guided_countdown_label == NULL) return;

    if(stage < 0) {
        lv_obj_add_flag(guided_countdown_label, LV_OBJ_FLAG_HIDDEN);
        guided_countdown_stage = -1;
        return;
    }

    if(stage == guided_countdown_stage) return;
    guided_countdown_stage = stage;

    lv_label_set_text(guided_countdown_label, texts[stage]);
    lv_obj_set_style_text_color(guided_countdown_label, lv_color_hex(colors[stage]), 0);
    lv_obj_set_style_text_font(guided_countdown_label, &my_font_full, 0);
    lv_obj_set_style_transform_zoom(guided_countdown_label, 260, 0);
    lv_obj_set_style_transform_rotation(guided_countdown_label, 0, 0);
    lv_obj_set_style_transform_pivot_x(guided_countdown_label, 0, 0);
    lv_obj_set_style_transform_pivot_y(guided_countdown_label, 0, 0);
    lv_obj_clear_flag(guided_countdown_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(guided_countdown_label, LV_ALIGN_CENTER, 0, 24);

    lv_anim_t zoom_anim;
    lv_anim_init(&zoom_anim);
    lv_anim_set_var(&zoom_anim, guided_countdown_label);
    lv_anim_set_values(&zoom_anim, 260, 520);
    lv_anim_set_time(&zoom_anim, 420);
    lv_anim_set_playback_time(&zoom_anim, 420);
    lv_anim_set_path_cb(&zoom_anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&zoom_anim, guided_countdown_zoom_anim_cb);
    lv_anim_start(&zoom_anim);

    lv_anim_t shake_anim;
    lv_anim_init(&shake_anim);
    lv_anim_set_var(&shake_anim, guided_countdown_label);
    lv_anim_set_values(&shake_anim, -120, 120);
    lv_anim_set_time(&shake_anim, 120);
    lv_anim_set_playback_time(&shake_anim, 120);
    lv_anim_set_repeat_count(&shake_anim, 2);
    lv_anim_set_path_cb(&shake_anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&shake_anim, guided_countdown_rotation_anim_cb);
    lv_anim_start(&shake_anim);
}

static void game_timer_cb(lv_timer_t * timer) {
    LV_UNUSED(timer);
    uint32_t current_tick = lv_tick_get();
    uint32_t dt = current_tick - game_last_tick;
    game_last_tick = current_tick;
    int has_active_notes = 0;
    int song_time_ms = 0;

    // 回退：移除底层强同步，恢复简单的视觉计时。并且彻底移除伴奏播放。
    game_time_ms += dt;
    update_guided_countdown_prompt();
    song_time_ms = game_time_ms - GUIDED_SONG_START_OFFSET_MS;

    if(!game_bgm_started && current_song_filename[0] != '\0' && song_time_ms >= -500) {
        char bgm_path[192];
        snprintf(bgm_path, sizeof(bgm_path), "music/%s", current_song_filename);
        platform_audio_play_bgm(bgm_path, 0.75f);
        game_bgm_started = 1;
    }

    // 1. Spawning logic (修改为提前 2000ms 生成，放慢一倍的掉落速度！)
    while(current_song_map && current_song_map[next_note_idx].key_idx != -1) {
        int spawn_time = current_song_map[next_note_idx].hit_time_ms - 2000;
        if(song_time_ms >= spawn_time) {
            for(int i=0; i<50; i++) {
                if(!active_notes[i].is_active) {
                    active_notes[i].is_active = 1;
                    active_notes[i].target_key = current_song_map[next_note_idx].key_idx;
                    active_notes[i].hit_time_ms = current_song_map[next_note_idx].hit_time_ms;
                    
                    lv_obj_t * note_obj = lv_obj_create(track_container); // 在音轨区生成下落音符
                    int is_black = active_notes[i].target_key >= 10;
                    if(!is_black) {
                        lv_obj_set_size(note_obj, 76, 30);
                        int x = active_notes[i].target_key * 80 + 2; 
                        lv_obj_set_pos(note_obj, x, 0);
                        lv_obj_set_style_bg_color(note_obj, lv_color_hex(0xff7f50), 0);
                    } else {
                        lv_obj_set_size(note_obj, 46, 30);
                        int w_idx = active_notes[i].target_key - 10;
                        int x = w_idx * 80 + 57; 
                        lv_obj_set_pos(note_obj, x, 0);
                        lv_obj_set_style_bg_color(note_obj, lv_color_hex(0xff7f50), 0);
                    }
                    lv_obj_set_style_border_width(note_obj, 0, 0);
                    lv_obj_set_style_radius(note_obj, 8, 0);
                    lv_obj_remove_flag(note_obj, LV_OBJ_FLAG_SCROLLABLE);
                    
                    active_notes[i].obj = note_obj;
                    break;
                }
            }
            next_note_idx++;
        } else {
            break; // 还没到生成时间
        }
    }

    // 2. Movement logic
    for(int i=0; i<50; i++) {
        if(active_notes[i].is_active) {
            has_active_notes = 1;
            int time_alive = song_time_ms - (active_notes[i].hit_time_ms - 2000);
            int y = time_alive * 272 / 2000; // 降低速度：2秒钟从 0 掉落到 272
            lv_obj_set_y(active_notes[i].obj, y);
            
            // 如果掉出了屏幕还没被打中（Miss判定）
            if(y > 320) { 
                lv_obj_delete(active_notes[i].obj);
                active_notes[i].is_active = 0;
                game_combo = 0;
                update_score_ui();
            }
        }
    }

    if(current_song_map &&
       current_song_map[next_note_idx].key_idx == -1 &&
       !has_active_notes) {
        if(game_timer) {
            lv_timer_delete(game_timer);
            game_timer = NULL;
        }
        platform_audio_stop_bgm();
        lv_timer_create(guided_finish_popup_delay_cb, 500, NULL);
    }
}

// 核心抛弃 LV_EVENT 事件系统，改为底层高频轮询的 Timer。
// 完全解决了按键不回弹的问题以及实现了单点无缝滑动机制（滑音）！
static void piano_scan_timer_cb(lv_timer_t * timer) {
    int hits[19] = {0};
    const platform_touch_point_t * touch_points = platform_get_touch_points();

    LV_UNUSED(timer);

    if(has_blocking_popup()) {
        for(int k = 0; k < 19; k++) {
            if(piano_keys[k] == NULL) continue;
            if(key_is_playing[k]) {
                key_is_playing[k] = 0;
                lv_obj_remove_state(piano_keys[k], LV_STATE_PRESSED);
            }
        }
        return;
    }
    
    // 我们遍历单个手指的落点
    for(int i=0; i<PLATFORM_MAX_TOUCH; i++) {
        if(touch_points[i].state == 1) {
            lv_point_t p = {touch_points[i].x, touch_points[i].y};
            int hit_idx = -1;
            // 先判定层级较高的黑键（索引 10 到 18）
            for(int k=18; k>=0; k--) {
                if(piano_keys[k] != NULL && !lv_obj_has_flag(piano_keys[k], LV_OBJ_FLAG_HIDDEN)) {
                    lv_area_t area;
                    lv_obj_get_coords(piano_keys[k], &area);
                    if(p.x >= area.x1 && p.x <= area.x2 && p.y >= area.y1 && p.y <= area.y2) {
                        hit_idx = k; 
                        break; // 找到了所在的键，结束该手指对其他键的搜查
                    }
                }
            }
            if(hit_idx != -1) hits[hit_idx] = 1;
        }
    }
    
    // 更新所有的琴键状态并下发播放命令
    for(int k=0; k<19; k++) {
        if(piano_keys[k] == NULL) continue;
        if(hits[k] && !key_is_playing[k]) {
            // 这个键刚刚被任一手指按下或者滑到了！
            key_is_playing[k] = 1;
            lv_obj_add_state(piano_keys[k], LV_STATE_PRESSED);
            
            // 全局零延迟发音（伴随软件混叠，无论自由弹琴还是伴奏模式都能出声）
            platform_audio_play_key(k);
            
            if(current_app_mode == 3 && is_recording) { // Recording Mode
                if(record_start_tick == 0) {
                    record_start_tick = lv_tick_get();
                    if(lbl_record_status) lv_label_set_text(lbl_record_status, "录音进行中!");
                }
                if(record_note_count < MAX_RECORD_NOTES - 1) {
                    recorded_buffer[record_note_count].key_idx = k;
                    recorded_buffer[record_note_count].hit_time_ms = lv_tick_get() - record_start_tick;
                    record_note_count++;
                }
            } else if(current_app_mode == 1) { // Guided learning mode
                int guided_song_time_ms = game_time_ms - GUIDED_SONG_START_OFFSET_MS;
                (void)try_guided_hit_for_key(k, guided_song_time_ms);
            }
        } else if(hits[k] && current_app_mode == 1) {
            int guided_song_time_ms = game_time_ms - GUIDED_SONG_START_OFFSET_MS;
            (void)try_guided_hit_for_key(k, guided_song_time_ms);
        } else if(!hits[k] && key_is_playing[k]) {
            // 这个键被所有手指松开或者手指滑出了它的范围（完美实现自动回弹）
            key_is_playing[k] = 0;
            lv_obj_remove_state(piano_keys[k], LV_STATE_PRESSED);
        }
    }
}

static void update_volume_icon_state(void) {
    int max_volume = (current_app_mode == 1 && piano_mix_volume < bgm_mix_volume) ? bgm_mix_volume : piano_mix_volume;
    if(max_volume == 0) lv_label_set_text(vol_icon, LV_SYMBOL_MUTE);
    else if(max_volume < 50) lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MID);
    else lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MAX);
}

static void vol_slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);

    if(slider == vol_slider_piano) piano_mix_volume = lv_slider_get_value(slider);
    else if(slider == vol_slider_bgm) bgm_mix_volume = lv_slider_get_value(slider);

    platform_audio_set_mix_volumes(piano_mix_volume, bgm_mix_volume);
    update_volume_icon_state();
}

static void vol_icon_event_cb(lv_event_t * e) {
    LV_UNUSED(e);
    if(lv_obj_has_flag(vol_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(vol_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(vol_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void back_to_menu_cb(lv_event_t * e) {
    if(piano_timer) { lv_timer_delete(piano_timer); piano_timer = NULL; }
    if(game_timer) { lv_timer_delete(game_timer); game_timer = NULL; }
    if(auto_play_timer) { lv_timer_delete(auto_play_timer); auto_play_timer = NULL; }
    if(piano_win) { lv_obj_delete(piano_win); piano_win = NULL; }
    
    is_recording = 0;
    btn_record_toggle = NULL;
    lbl_record_toggle = NULL;
    lbl_record_status = NULL;
    lbl_auto_status = NULL;
    close_auto_finish_popup();
    platform_audio_stop_bgm();

    create_main_menu_ui();
}

static void main_menu_btn_cb(lv_event_t * e) {
    intptr_t action = (intptr_t)lv_event_get_user_data(e);
    if(action == 1) { // 自由演奏
        current_app_mode = 0;
        if(main_menu_win) { lv_obj_delete(main_menu_win); main_menu_win = NULL; }
        create_piano_ui();
    } else if(action == 2) { // 跟弹学习
        current_app_mode = 1;
        if(main_menu_win) { lv_obj_delete(main_menu_win); main_menu_win = NULL; }
        create_song_selection_ui();
    } else if(action == 3) { // 自动演奏
        current_app_mode = 2;
        if(main_menu_win) { lv_obj_delete(main_menu_win); main_menu_win = NULL; }
        create_record_selection_ui();
    } else if(action == 4) { // 录制演奏
        current_app_mode = 3;
        if(main_menu_win) { lv_obj_delete(main_menu_win); main_menu_win = NULL; }
        create_piano_ui();
    } else if(action == 99) { // 退出登录
        if(main_menu_win) { lv_obj_delete(main_menu_win); main_menu_win = NULL; }
        create_login_ui();
    }
}

static void song_start_cb(lv_event_t * e) {
    const char * filename = (const char *)lv_event_get_user_data(e);
    if(filename) lv_timer_create(delayed_song_start_cb, 500, strdup(filename));
}

static void song_back_cb(lv_event_t * e) {
    if(song_sel_win) { lv_obj_delete(song_sel_win); song_sel_win = NULL; }
    create_main_menu_ui();
}

static void guided_finish_action_cb(lv_event_t * e) {
    intptr_t action = (intptr_t)lv_event_get_user_data(e);

    close_guided_finish_popup();

    if(piano_timer) { lv_timer_delete(piano_timer); piano_timer = NULL; }
    if(game_timer) { lv_timer_delete(game_timer); game_timer = NULL; }
    if(piano_win) { lv_obj_delete(piano_win); piano_win = NULL; }

    if(action == 1) {
        restart_guided_song();
    } else if(action == 2) {
        create_song_selection_ui();
    } else {
        create_main_menu_ui();
    }
}

static void restart_guided_song(void) {
    current_app_mode = 1;
    game_score = 0;
    game_combo = 0;
    game_max_combo = 0;
    next_note_idx = 0;
    memset(active_notes, 0, sizeof(active_notes));
    platform_audio_stop_bgm();
    platform_audio_clear_bgm();
    create_piano_ui();
}

static void close_guided_finish_popup(void) {
    if(guided_finish_popup) {
        lv_obj_delete(guided_finish_popup);
        guided_finish_popup = NULL;
    }
    if(guided_finish_overlay) {
        lv_obj_delete(guided_finish_overlay);
        guided_finish_overlay = NULL;
    }
}

static void guided_finish_close_cb(lv_event_t * e) {
    LV_UNUSED(e);
    close_guided_finish_popup();
}

static void show_guided_finish_popup(void) {
    if(guided_finish_popup) return;

    guided_finish_overlay = create_modal_overlay();

    guided_finish_popup = create_modal_card(guided_finish_overlay, 310, 300);

    lv_obj_t * title = lv_label_create(guided_finish_popup);
    lv_label_set_text(title, "本曲练习完成");
    apply_title_visual(title, lv_color_hex(COLOR_TITLE));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, current_app_mode == 2 ? 26 : 20);

    lv_obj_t * summary = lv_label_create(guided_finish_popup);
    lv_label_set_text_fmt(summary, "得分：%d    最大连击：%d", game_score, game_max_combo);
    lv_obj_set_style_text_font(summary, &my_font_full, 0);
    lv_obj_set_style_text_color(summary, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_align(summary, LV_ALIGN_TOP_MID, 0, 62);

    lv_obj_t * btn_close = lv_button_create(guided_finish_popup);
    lv_obj_set_size(btn_close, 32, 32);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, 18, -18);
    apply_danger_glass_button_style(btn_close);
    attach_ui_button_sound(btn_close);
    lv_obj_set_style_radius(btn_close, 16, 0);
    lv_obj_set_style_border_width(btn_close, 1, 0);
    lv_obj_set_style_shadow_width(btn_close, 18, 0);
    lv_obj_set_style_shadow_opa(btn_close, 76, 0);
    lv_obj_add_event_cb(btn_close, guided_finish_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "×");
    lv_obj_set_style_text_font(lbl_close, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_close, lv_color_hex(0x7F1D1D), 0);
    lv_obj_center(lbl_close);

    lv_obj_t * btn_retry = lv_button_create(guided_finish_popup);
    lv_obj_set_size(btn_retry, 180, 44);
    lv_obj_align(btn_retry, LV_ALIGN_TOP_MID, 0, 112);
    apply_glass_button_style(btn_retry);
    attach_ui_button_sound(btn_retry);
    lv_obj_add_event_cb(btn_retry, guided_finish_action_cb, LV_EVENT_CLICKED, (void *)1);
    lv_obj_t * lbl_retry = lv_label_create(btn_retry);
    lv_label_set_text(lbl_retry, "再来一次");
    lv_obj_set_style_text_font(lbl_retry, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_retry, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(lbl_retry);

    lv_obj_t * btn_choose = lv_button_create(guided_finish_popup);
    lv_obj_set_size(btn_choose, 180, 44);
    lv_obj_align(btn_choose, LV_ALIGN_TOP_MID, 0, 166);
    apply_glass_button_style(btn_choose);
    attach_ui_button_sound(btn_choose);
    lv_obj_add_event_cb(btn_choose, guided_finish_action_cb, LV_EVENT_CLICKED, (void *)2);
    lv_obj_t * lbl_choose = lv_label_create(btn_choose);
    lv_label_set_text(lbl_choose, "选择其他曲目");
    lv_obj_set_style_text_font(lbl_choose, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_choose, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(lbl_choose);

    lv_obj_t * btn_back = lv_button_create(guided_finish_popup);
    lv_obj_set_size(btn_back, 180, 44);
    lv_obj_align(btn_back, LV_ALIGN_TOP_MID, 0, 220);
    apply_glass_button_style(btn_back);
    attach_ui_button_sound(btn_back);
    lv_obj_add_event_cb(btn_back, guided_finish_action_cb, LV_EVENT_CLICKED, (void *)3);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "返回大厅");
    lv_obj_set_style_text_font(lbl_back, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(lbl_back);
}

static void close_auto_finish_popup(void) {
    if(auto_finish_popup) {
        lv_obj_delete(auto_finish_popup);
        auto_finish_popup = NULL;
    }
    if(auto_finish_overlay) {
        lv_obj_delete(auto_finish_overlay);
        auto_finish_overlay = NULL;
    }
}

static void auto_finish_close_cb(lv_event_t * e) {
    LV_UNUSED(e);
    close_auto_finish_popup();
}

static void auto_finish_action_cb(lv_event_t * e) {
    intptr_t action = (intptr_t)lv_event_get_user_data(e);

    close_auto_finish_popup();

    if(piano_timer) { lv_timer_delete(piano_timer); piano_timer = NULL; }
    if(auto_play_timer) { lv_timer_delete(auto_play_timer); auto_play_timer = NULL; }
    if(piano_win) { lv_obj_delete(piano_win); piano_win = NULL; }
    lbl_auto_status = NULL;

    if(action == 1) {
        restart_auto_play_current();
    } else if(action == 2) {
        create_record_selection_ui();
    } else {
        create_main_menu_ui();
    }
}

static void restart_auto_play_current(void) {
    if(current_auto_record_name[0] == '\0') {
        create_record_selection_ui();
        return;
    }
    current_app_mode = 2;
    {
        char filename_copy[sizeof(current_auto_record_name)];
        snprintf(filename_copy, sizeof(filename_copy), "%s", current_auto_record_name);
        start_auto_play_from_record(filename_copy);
    }
}

static void show_auto_finish_popup(void) {
    if(auto_finish_popup) return;

    auto_finish_overlay = create_modal_overlay();
    auto_finish_popup = create_modal_card(auto_finish_overlay, 310, 340);

    lv_obj_t * title = lv_label_create(auto_finish_popup);
    lv_label_set_text(title, "自动演奏完成");
    apply_title_visual(title, lv_color_hex(COLOR_TITLE));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * summary = lv_label_create(auto_finish_popup);
    lv_label_set_text(summary, "本段录音已播放结束");
    lv_obj_set_style_text_font(summary, &my_font_full, 0);
    lv_obj_set_style_text_color(summary, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_align(summary, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t * btn_close = lv_button_create(auto_finish_popup);
    lv_obj_set_size(btn_close, 32, 32);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, 18, -18);
    apply_danger_glass_button_style(btn_close);
    attach_ui_button_sound(btn_close);
    lv_obj_set_style_radius(btn_close, 16, 0);
    lv_obj_set_style_shadow_width(btn_close, 18, 0);
    lv_obj_set_style_shadow_opa(btn_close, 76, 0);
    lv_obj_add_event_cb(btn_close, auto_finish_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "×");
    lv_obj_set_style_text_font(lbl_close, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_close, lv_color_hex(0x7F1D1D), 0);
    lv_obj_center(lbl_close);

    lv_obj_t * btn_retry = lv_button_create(auto_finish_popup);
    lv_obj_set_size(btn_retry, 180, 44);
    lv_obj_align(btn_retry, LV_ALIGN_TOP_MID, 0, 126);
    apply_glass_button_style(btn_retry);
    attach_ui_button_sound(btn_retry);
    lv_obj_add_event_cb(btn_retry, auto_finish_action_cb, LV_EVENT_CLICKED, (void *)1);
    lv_obj_t * lbl_retry = lv_label_create(btn_retry);
    lv_label_set_text(lbl_retry, "再来一次");
    lv_obj_set_style_text_font(lbl_retry, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_retry, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(lbl_retry);

    lv_obj_t * btn_choose = lv_button_create(auto_finish_popup);
    lv_obj_set_size(btn_choose, 180, 44);
    lv_obj_align(btn_choose, LV_ALIGN_TOP_MID, 0, 180);
    apply_glass_button_style(btn_choose);
    attach_ui_button_sound(btn_choose);
    lv_obj_add_event_cb(btn_choose, auto_finish_action_cb, LV_EVENT_CLICKED, (void *)2);
    lv_obj_t * lbl_choose = lv_label_create(btn_choose);
    lv_label_set_text(lbl_choose, "选择其他曲目");
    lv_obj_set_style_text_font(lbl_choose, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_choose, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(lbl_choose);

    lv_obj_t * btn_back = lv_button_create(auto_finish_popup);
    lv_obj_set_size(btn_back, 180, 44);
    lv_obj_align(btn_back, LV_ALIGN_TOP_MID, 0, 234);
    apply_glass_button_style(btn_back);
    attach_ui_button_sound(btn_back);
    lv_obj_add_event_cb(btn_back, auto_finish_action_cb, LV_EVENT_CLICKED, (void *)3);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "返回大厅");
    lv_obj_set_style_text_font(lbl_back, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_center(lbl_back);
}

void create_song_selection_ui(void) {
    song_sel_win = lv_obj_create(lv_screen_active());
    lv_obj_set_size(song_sel_win, 800, 480);
    apply_page_bg(song_sel_win);
    
    lv_obj_t * title = lv_label_create(song_sel_win);
    lv_label_set_text(title, "选择曲目");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &my_font_full, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    set_page_title(title, PAGE_TITLE_SONG_SELECTION);

    lv_obj_t * btn_back = create_back_button(song_sel_win, song_back_cb);
    lv_obj_t * lbl_back = lv_obj_get_child(btn_back, 0);
    lv_label_set_text(lbl_back, "返回大厅");
    lv_obj_set_style_text_font(lbl_back, &my_font_full, 0);
    lv_obj_center(lbl_back);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x1f4f55), 0);

    lv_obj_t * list = lv_list_create(song_sel_win);
    lv_obj_set_size(list, 500, 300);
    lv_obj_center(list);

    DIR *d;
    struct dirent *dir;
    d = opendir("music");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".wav")) {
                if(is_guided_song_hidden(dir->d_name)) continue;
                const SongRegistry * match = NULL;
                for(int i=0; song_db[i].filename != NULL; i++) {
                    if(strcmp(song_db[i].filename, dir->d_name) == 0) {
                        match = &song_db[i];
                        break;
                    }
                }
                char btn_txt[128];
                if(match) snprintf(btn_txt, sizeof(btn_txt), " %s", match->title);
                else snprintf(btn_txt, sizeof(btn_txt), " %s (无同步谱面)", dir->d_name);
                
                lv_obj_t * btn = lv_list_add_button(list, LV_SYMBOL_AUDIO, btn_txt);
                fix_list_button_fonts(btn);
                attach_ui_button_sound(btn);
                
                char * payload = strdup(dir->d_name);
                lv_obj_add_event_cb(btn, song_start_cb, LV_EVENT_CLICKED, payload);
            }
        }
        closedir(d);
    }
}

void create_main_menu_ui(void) {
    main_menu_win = lv_obj_create(lv_screen_active());
    lv_obj_set_size(main_menu_win, 800, 480);
    lv_obj_center(main_menu_win);
    apply_page_bg(main_menu_win);

    lv_obj_t * title = lv_label_create(main_menu_win);
    lv_label_set_text(title, "应用大厅");
    apply_title_visual(title, lv_color_hex(COLOR_TITLE));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * btn_logout = lv_button_create(main_menu_win);
    lv_obj_set_size(btn_logout, 120, 40);
    lv_obj_align(btn_logout, LV_ALIGN_TOP_LEFT, 20, 20);
    apply_danger_glass_button_style(btn_logout);
    attach_ui_button_sound(btn_logout);
    lv_obj_add_event_cb(btn_logout, main_menu_btn_cb, LV_EVENT_CLICKED, (void*)99);
    
    lv_obj_t * lbl_out = lv_label_create(btn_logout);
    lv_label_set_text(lbl_out, "退出登录");
    lv_obj_set_style_text_font(lbl_out, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_out, lv_color_hex(0x7a2130), 0);
    lv_obj_center(lbl_out);

    // 2x2 Grid Buttons (拆分 Icon 和 文字，防止中文字库缺少矢量符号导致不显示)
    const char * icons[4] = { LV_SYMBOL_AUDIO, LV_SYMBOL_EDIT, LV_SYMBOL_PLAY, LV_SYMBOL_SAVE };
    const char * texts[4] = { "自由演奏", "跟弹学习", "自动演奏", "录制演奏" };
    int coords[4][2] = { {-110, -50}, {110, -50}, {-110, 70}, {110, 70} };
    
    for(int i=0; i<4; i++) {
        lv_obj_t * btn = lv_button_create(main_menu_win);
        lv_obj_set_size(btn, 180, 100);
        lv_obj_align(btn, LV_ALIGN_CENTER, coords[i][0], coords[i][1]);
        apply_glass_button_style(btn);
        attach_ui_button_sound(btn);
        if(i == 0) lv_obj_add_event_cb(btn, main_menu_btn_cb, LV_EVENT_CLICKED, (void*)1);
        if(i == 1) lv_obj_add_event_cb(btn, main_menu_btn_cb, LV_EVENT_CLICKED, (void*)2);
        if(i == 2) lv_obj_add_event_cb(btn, main_menu_btn_cb, LV_EVENT_CLICKED, (void*)3);
        if(i == 3) lv_obj_add_event_cb(btn, main_menu_btn_cb, LV_EVENT_CLICKED, (void*)4);
        
        lv_obj_t * lbl_icon = lv_label_create(btn);
        lv_label_set_text(lbl_icon, icons[i]);
        lv_obj_set_style_text_font(lbl_icon, lv_theme_get_font_normal(NULL), 0);
        lv_obj_set_style_text_color(lbl_icon, lv_color_hex(0x1c4f7a), 0);
        // 不绑定中文字库，系统默认自带的英文字库完美包含这些 LV_SYMBOL 矢量符号
        lv_obj_align(lbl_icon, LV_ALIGN_CENTER, 0, -15);
        
        lv_obj_t * lbl_text = lv_label_create(btn);
        lv_label_set_text(lbl_text, texts[i]);
        lv_obj_set_style_text_font(lbl_text, &my_font_full, 0); // 中文强行挂载字库
        lv_obj_set_style_text_color(lbl_text, lv_color_hex(0x1f4f55), 0);
        lv_obj_align(lbl_text, LV_ALIGN_CENTER, 0, 15);
    }

    lv_obj_t * btn_manage = lv_button_create(main_menu_win);
    lv_obj_set_size(btn_manage, 46, 46);
    lv_obj_align(btn_manage, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    apply_glass_button_style(btn_manage);
    lv_obj_set_style_radius(btn_manage, 23, 0);
    attach_ui_button_sound(btn_manage);
    lv_obj_add_event_cb(btn_manage, manage_entry_cb, LV_EVENT_CLICKED, NULL);

    for(int i = 0; i < 3; i++) {
        lv_obj_t * line = lv_obj_create(btn_manage);
        lv_obj_set_size(line, 18, 2);
        lv_obj_set_style_bg_color(line, lv_color_hex(COLOR_SUBTITLE), 0);
        lv_obj_set_style_bg_opa(line, 255, 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_set_style_radius(line, 1, 0);
        lv_obj_align(line, LV_ALIGN_CENTER, 0, -6 + i * 6);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }
}

void create_piano_ui(void) {
    memset(piano_keys, 0, sizeof(piano_keys));
    memset(key_is_playing, 0, sizeof(key_is_playing));
    guided_countdown_label = NULL;
    guided_judgement_label = NULL;
    guided_countdown_stage = -1;

    piano_win = lv_obj_create(lv_screen_active());
    lv_obj_set_size(piano_win, 800, 480);
    lv_obj_center(piano_win);
    lv_obj_set_style_bg_color(piano_win, lv_color_hex(0xd4f0f0), 0); // 薄荷绿"小清新"背景
    lv_obj_set_style_border_width(piano_win, 0, 0);
    lv_obj_set_style_pad_all(piano_win, 0, 0);
    lv_obj_remove_flag(piano_win, LV_OBJ_FLAG_SCROLLABLE);
    apply_page_bg(piano_win);

    lv_obj_t * scr = piano_win; // Re-alias for safe memory closure

    // 返回菜单按钮
    lv_obj_t * btn_back = create_back_button(scr, back_to_menu_cb);
    lv_obj_t * lbl_back = lv_obj_get_child(btn_back, 0);
    lv_label_set_text(lbl_back, "返回大厅");
    lv_obj_set_style_text_font(lbl_back, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x1f4f55), 0);
    lv_obj_center(lbl_back);

    if(current_app_mode == 3) {
        btn_record_toggle = lv_button_create(scr);
        lv_obj_set_size(btn_record_toggle, 160, 50);
        lv_obj_align(btn_record_toggle, LV_ALIGN_TOP_MID, 0, 80);
        attach_ui_button_sound(btn_record_toggle);
        lv_obj_add_event_cb(btn_record_toggle, record_toggle_cb, LV_EVENT_CLICKED, NULL);

        lbl_record_toggle = lv_label_create(btn_record_toggle);
        lv_obj_set_style_text_font(lbl_record_toggle, &my_font_full, 0);
        lv_obj_center(lbl_record_toggle);

        lbl_record_status = NULL;
        memset(recorded_buffer, 0, sizeof(recorded_buffer));
        record_note_count = 0;
        record_start_tick = 0;
        is_recording = 0;
        update_record_toggle_button();
    } else if(current_app_mode == 2) {
        lbl_auto_status = lv_label_create(scr);
        lv_label_set_text(lbl_auto_status, "演奏中");
        lv_obj_set_style_text_font(lbl_auto_status, &my_font_full, 0);
        lv_obj_set_style_text_color(lbl_auto_status, lv_color_hex(COLOR_SUBTITLE), 0);
        lv_obj_align(lbl_auto_status, LV_ALIGN_TOP_MID, 0, 72);
    }

    // Title label
    lv_obj_t * title = lv_label_create(scr);
    if(current_app_mode == 1) {
        lv_label_set_text(title, current_song_title);
        apply_title_visual(title, lv_color_hex(COLOR_TITLE));
    }
    else if(current_app_mode == 2) set_page_title(title, PAGE_TITLE_AUTO_PLAY);
    else if(current_app_mode == 3) set_page_title(title, PAGE_TITLE_RECORD);
    else set_page_title(title, PAGE_TITLE_FREE_PLAY);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TITLE), 0); // 深色标题保证薄荷背景可读性
    lv_obj_set_style_text_font(title, &my_font_full, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Volume Control Button (巨大点击热区，解决物理屏点不准的问题)
    lv_obj_t * vol_btn = lv_button_create(scr);
    lv_obj_set_size(vol_btn, 80, 80);
    lv_obj_align(vol_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_opa(vol_btn, 0, 0); // 完全透明，不破坏清新背景
    lv_obj_set_style_border_width(vol_btn, 0, 0); // 去除边框
    lv_obj_set_style_shadow_width(vol_btn, 0, 0); // 去除阴影
    lv_obj_add_event_cb(vol_btn, vol_icon_event_cb, LV_EVENT_CLICKED, NULL);

    vol_icon = lv_label_create(vol_btn);
    lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(vol_icon, lv_color_hex(0x2c3e50), 0); // 深绿色/深灰图标
    lv_obj_set_style_text_font(vol_icon, lv_theme_get_font_normal(NULL), 0); // 确保符号正常渲染
    lv_obj_center(vol_icon); // 居中在大按钮内部

    vol_panel = lv_obj_create(scr);
    lv_obj_set_size(vol_panel, 170, current_app_mode == 1 ? 106 : 62);
    lv_obj_align_to(vol_panel, vol_btn, LV_ALIGN_OUT_LEFT_TOP, -16, 0);
    lv_obj_set_style_bg_color(vol_panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(vol_panel, 200, 0);
    lv_obj_set_style_border_width(vol_panel, 1, 0);
    lv_obj_set_style_border_color(vol_panel, lv_color_hex(0xe6faf6), 0);
    lv_obj_set_style_radius(vol_panel, 18, 0);
    lv_obj_set_style_shadow_color(vol_panel, lv_color_hex(0x7ab8b0), 0);
    lv_obj_set_style_shadow_opa(vol_panel, 75, 0);
    lv_obj_set_style_shadow_width(vol_panel, 20, 0);
    lv_obj_set_style_pad_all(vol_panel, 10, 0);
    lv_obj_remove_flag(vol_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(vol_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * lbl_piano = lv_label_create(vol_panel);
    lv_label_set_text(lbl_piano, "钢琴");
    lv_obj_set_style_text_font(lbl_piano, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_piano, lv_color_hex(COLOR_SUBTITLE), 0);
    lv_obj_align(lbl_piano, LV_ALIGN_TOP_LEFT, 8, 8);

    vol_slider_piano = lv_slider_create(vol_panel);
    lv_slider_set_range(vol_slider_piano, 0, 100);
    lv_slider_set_value(vol_slider_piano, piano_mix_volume, LV_ANIM_OFF);
    lv_obj_set_size(vol_slider_piano, 96, 14);
    lv_obj_align(vol_slider_piano, LV_ALIGN_TOP_RIGHT, -8, 10);
    lv_obj_add_event_cb(vol_slider_piano, vol_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    vol_slider_bgm = NULL;
    if(current_app_mode == 1) {
        lv_obj_t * lbl_bgm = lv_label_create(vol_panel);
        lv_label_set_text(lbl_bgm, "伴奏");
        lv_obj_set_style_text_font(lbl_bgm, &my_font_full, 0);
        lv_obj_set_style_text_color(lbl_bgm, lv_color_hex(COLOR_SUBTITLE), 0);
        lv_obj_align(lbl_bgm, LV_ALIGN_TOP_LEFT, 8, 54);

        vol_slider_bgm = lv_slider_create(vol_panel);
        lv_slider_set_range(vol_slider_bgm, 0, 100);
        lv_slider_set_value(vol_slider_bgm, bgm_mix_volume, LV_ANIM_OFF);
        lv_obj_set_size(vol_slider_bgm, 96, 14);
        lv_obj_align(vol_slider_bgm, LV_ALIGN_TOP_RIGHT, -8, 56);
        lv_obj_add_event_cb(vol_slider_bgm, vol_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    platform_audio_set_mix_volumes(piano_mix_volume, bgm_mix_volume);
    update_volume_icon_state();

    // ==== 打击游戏 UI 注入 ====
    if(current_app_mode == 1) {
        track_container = lv_obj_create(piano_win);
        lv_obj_set_size(track_container, 800, 272);
        lv_obj_align(track_container, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(track_container, lv_color_hex(0xd4f0f0), 0);
        lv_obj_set_style_border_width(track_container, 0, 0);
        lv_obj_set_style_radius(track_container, 0, 0);
        lv_obj_set_style_pad_all(track_container, 0, 0);
        lv_obj_remove_flag(track_container, LV_OBJ_FLAG_SCROLLABLE);

        lbl_score = lv_label_create(track_container);
        lv_obj_set_style_text_color(lbl_score, lv_color_hex(COLOR_SUBTITLE), 0);
        lv_obj_set_style_text_font(lbl_score, &my_font_full, 0);
        lv_obj_align(lbl_score, LV_ALIGN_TOP_MID, 0, 92);
        lbl_combo = NULL;

        guided_countdown_label = lv_label_create(track_container);
        lv_obj_set_style_text_font(guided_countdown_label, &my_font_full, 0);
        lv_obj_set_style_text_color(guided_countdown_label, lv_color_hex(0x22C55E), 0);
        lv_obj_align(guided_countdown_label, LV_ALIGN_CENTER, 0, 24);
        lv_label_set_text(guided_countdown_label, "");
        guided_countdown_stage = -1;

        guided_judgement_label = lv_label_create(track_container);
        lv_obj_set_style_text_font(guided_judgement_label, &my_font_full, 0);
        lv_obj_set_style_text_color(guided_judgement_label, lv_color_hex(0x166534), 0);
        lv_obj_align(guided_judgement_label, LV_ALIGN_CENTER, 0, 74);
        lv_label_set_text(guided_judgement_label, "");
        lv_obj_add_flag(guided_judgement_label, LV_OBJ_FLAG_HIDDEN);
        
        game_score = 0; game_combo = 0; game_max_combo = 0;
        game_time_ms = -GUIDED_COUNTDOWN_MS;
        game_bgm_started = 0;
        game_last_tick = lv_tick_get();
        next_note_idx = 0;
        memset(active_notes, 0, sizeof(active_notes));
        update_score_ui();
        update_guided_countdown_prompt();
        
        // 按用户要求：彻底移除读取和播放伴奏的逻辑，仅保留音符下落和弹奏的交响音
        platform_audio_stop_bgm();
        usleep(50000);
        platform_audio_clear_bgm();

        game_timer = lv_timer_create(game_timer_cb, 10, NULL);
        
        // 修复顶部控件被音轨区遮挡的问题，提到最上层图层
        lv_obj_move_foreground(btn_back);
        lv_obj_move_foreground(title);
        lv_obj_move_foreground(vol_btn);
        lv_obj_move_foreground(vol_panel);
    }

    // Piano container
    lv_obj_t * piano_container = lv_obj_create(scr);
    lv_obj_set_size(piano_container, 800, 208); // 满宽 800
    lv_obj_align(piano_container, LV_ALIGN_BOTTOM_MID, 0, 0); // 贴底
    lv_obj_set_style_bg_color(piano_container, lv_color_hex(0x111111), 0); // 深入黑色的背景顶栏
    lv_obj_set_style_border_width(piano_container, 0, 0); // 去除边缘边框，彻底释放 800 像素
    lv_obj_set_style_radius(piano_container, 0, 0); // 去除边缘圆角，彻底填平左右两端顶部的缺口
    lv_obj_set_style_pad_all(piano_container, 0, 0); // 覆盖内边距
    lv_obj_remove_flag(piano_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // 【核心改造】：抛弃不稳定的系统事件系统，使用后台极速轮询监听所有 5 个落点，无缝跨越任意琴键和多点触发。
    // 这将立刻解决“不回弹”以及“多点触控失灵”的问题。
    piano_timer = lv_timer_create(piano_scan_timer_cb, 10, NULL);
    
    // We will create basic white and black keys layout
    const int white_keys_count = 10;
    const int white_key_w = 80; // 绝对铺满 800 像素宽屏 (80 * 10 = 800)
    const int white_key_h = 190; 
    const int black_key_w = 46; 
    const int black_key_h = 108; 
    
    const char * white_key_names_arr[] = {"C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5"};
    const char * black_key_names_arr[] = {"C4#", "D4#", "", "F4#", "G4#", "A4#", "", "C5#", "D5#", ""};
    
    // First, draw all white keys
    for(int i = 0; i < white_keys_count; i++) {
        lv_obj_t * btn = lv_button_create(piano_container);
        piano_keys[i] = btn; piano_key_names[i] = white_key_names_arr[i];
        
        lv_obj_set_size(btn, white_key_w - 4, white_key_h); // 维持 4 像素缝隙 (占去 76)
        lv_obj_set_pos(btn, i * white_key_w + 2, 6); // 头尾正好均衡留白 2 像素
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xcccccc), LV_STATE_PRESSED); // light gray on press
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        
        // 关键：去掉子键的 Clickable，避免截胡容器事件
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t * label = lv_label_create(btn);
        lv_label_set_text(label, white_key_names_arr[i]);
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
    
    // Second, draw black keys on top
    for(int i = 0; i < white_keys_count - 1; i++) {
        if(strlen(black_key_names_arr[i]) > 0) {
            int k = 10 + i;
            lv_obj_t * btn = lv_button_create(piano_container);
            piano_keys[k] = btn; piano_key_names[k] = black_key_names_arr[i];
            
            lv_obj_set_size(btn, black_key_w, black_key_h);
            lv_obj_set_pos(btn, i * white_key_w + 57, 6); // 绝对精准的中缝坐标 (i*80 + 80 - 23)
            lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), LV_STATE_PRESSED);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            
            lv_obj_remove_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    
    // 初始化无损硬件层混音引擎 (仅执行一次，加载所有 WAV 数据入内存)
    platform_audio_init(piano_key_names, 19);
}

static void auto_play_timer_cb(lv_timer_t * t) {
    uint32_t current_tick = lv_tick_get();
    uint32_t dt = 0;

    LV_UNUSED(t);
    if(current_tick < auto_play_start_tick) {
        return;
    }
    dt = current_tick - auto_play_start_tick;
    
    while(playback_buffer[auto_play_idx].key_idx != -1) {
        if(dt >= playback_buffer[auto_play_idx].hit_time_ms) {
            int k = playback_buffer[auto_play_idx].key_idx;
            lv_obj_add_state(piano_keys[k], LV_STATE_PRESSED);
            platform_audio_play_key(k);
            auto_play_release_tick[k] = current_tick + 150; // 动画残影表现释放延迟 150ms
            auto_play_idx++;
        } else {
            break;
        }
    }
    
    // 执行幽灵键自动松开消散
    for(int k=0; k<19; k++) {
        if(auto_play_release_tick[k] && current_tick >= auto_play_release_tick[k]) {
            if(piano_keys[k]) lv_obj_remove_state(piano_keys[k], LV_STATE_PRESSED);
            auto_play_release_tick[k] = 0;
        }
    }

    if(playback_buffer[auto_play_idx].key_idx == -1) {
        int has_pending_release = 0;
        for(int k = 0; k < 19; k++) {
            if(auto_play_release_tick[k]) {
                has_pending_release = 1;
                break;
            }
        }

        if(!has_pending_release) {
            if(auto_play_timer) {
                lv_timer_delete(auto_play_timer);
                auto_play_timer = NULL;
            }
            if(lbl_auto_status) {
                lv_label_set_text(lbl_auto_status, "演奏结束");
            }
            lv_timer_create(auto_finish_popup_delay_cb, 500, NULL);
        }
    }
}

static lv_obj_t * naming_win = NULL;
static lv_obj_t * naming_ta = NULL;

static void save_record_cb(lv_event_t * e) {
    const char * name = lv_textarea_get_text(naming_ta);
    if(strlen(name) > 0) {
        mkdir("records", 0777); // Ensure directory exists safely
        char path[128];
        snprintf(path, sizeof(path), "records/%s.rec", name);
        FILE *f = fopen(path, "wb");
        if(f) {
            recorded_buffer[record_note_count].key_idx = -1; // 写入终止结束帧封口
            recorded_buffer[record_note_count].hit_time_ms = 0;
            fwrite(recorded_buffer, sizeof(SongNote), record_note_count + 1, f);
            fclose(f);
        }
    }
    if(naming_win) { lv_obj_delete(naming_win); naming_win = NULL; naming_ta = NULL; }
    create_main_menu_ui();
}

static void create_naming_ui(void) {
    naming_win = lv_obj_create(lv_screen_active());
    lv_obj_set_size(naming_win, 800, 480);
    lv_obj_center(naming_win);
    apply_page_bg(naming_win);
    
    lv_obj_t * title = lv_label_create(naming_win);
    lv_label_set_text(title, "保存录音 (仅支持英文及数字输入)");
    lv_obj_set_style_text_font(title, &my_font_full, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    set_page_title(title, PAGE_TITLE_SAVE_RECORD);
    
    naming_ta = lv_textarea_create(naming_win);
    lv_obj_set_size(naming_ta, 400, 50);
    lv_obj_align(naming_ta, LV_ALIGN_TOP_MID, 0, 60);
    lv_textarea_set_one_line(naming_ta, true);
    
    lv_obj_t * kb = lv_keyboard_create(naming_win);
    lv_keyboard_set_textarea(kb, naming_ta);
    lv_obj_add_event_cb(kb, save_record_cb, LV_EVENT_READY, NULL); // 点击右下角的打勾或确认保存
    lv_obj_add_event_cb(kb, save_record_cb, LV_EVENT_CANCEL, NULL);
}

static void stop_recording_cb(lv_event_t * e) {
    LV_UNUSED(e);
    is_recording = 0;
    btn_record_toggle = NULL;
    lbl_record_toggle = NULL;
    lbl_record_status = NULL;
    if(piano_timer) { lv_timer_delete(piano_timer); piano_timer = NULL; }
    if(piano_win) { lv_obj_delete(piano_win); piano_win = NULL; }
    create_naming_ui();
}

static void record_toggle_cb(lv_event_t * e) {
    if(is_recording) {
        stop_recording_cb(e);
        return;
    }

    memset(recorded_buffer, 0, sizeof(recorded_buffer));
    record_note_count = 0;
    record_start_tick = 0;
    is_recording = 1;
    update_record_toggle_button();
}

static void start_auto_play_from_record(const char * filename) {
    memset(playback_buffer, 0, sizeof(playback_buffer));
    playback_buffer[0].key_idx = -1;
    if(filename) {
        snprintf(current_auto_record_name, sizeof(current_auto_record_name), "%s", filename);
    } else {
        current_auto_record_name[0] = '\0';
    }
    if(filename) {
        char path[128];
        snprintf(path, sizeof(path), "records/%s", filename);
        FILE *f = fopen(path, "rb");
        if(f) {
            size_t read_count = fread(playback_buffer, sizeof(SongNote), MAX_RECORD_NOTES, f);
            if(read_count == 0) {
                playback_buffer[0].key_idx = -1;
            } else if(read_count < MAX_RECORD_NOTES) {
                playback_buffer[read_count].key_idx = -1;
                playback_buffer[read_count].hit_time_ms = 0;
            } else {
                playback_buffer[MAX_RECORD_NOTES - 1].key_idx = -1;
                playback_buffer[MAX_RECORD_NOTES - 1].hit_time_ms = 0;
            }
            (void)read_count;
            fclose(f);
        }
    }
    if(song_sel_win) { lv_obj_delete(song_sel_win); song_sel_win = NULL; }
    close_auto_finish_popup();
    create_piano_ui();
    
    auto_play_idx = 0;
    auto_play_start_tick = lv_tick_get() + 800; // 延时 800 毫秒后幽灵之手开始发力
    memset(auto_play_release_tick, 0, sizeof(auto_play_release_tick));
    auto_play_timer = lv_timer_create(auto_play_timer_cb, 20, NULL);
}

static void auto_play_start_cb(lv_event_t * e) {
    const char * filename = (const char *)lv_event_get_user_data(e);
    if(filename) lv_timer_create(delayed_auto_play_start_cb, 500, strdup(filename));
}

static void create_record_selection_ui(void) {
    song_sel_win = lv_obj_create(lv_screen_active());
    lv_obj_set_size(song_sel_win, 800, 480);
    apply_page_bg(song_sel_win);
    
    lv_obj_t * title = lv_label_create(song_sel_win);
    lv_label_set_text(title, "选择要自动回放的亲传神作");
    apply_title_visual(title, lv_color_hex(COLOR_TITLE));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * btn_back = create_back_button(song_sel_win, song_back_cb);
    lv_obj_t * lbl_back = lv_obj_get_child(btn_back, 0);
    lv_label_set_text(lbl_back, "返回大厅");
    lv_obj_set_style_text_font(lbl_back, &my_font_full, 0);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x1f4f55), 0);
    lv_obj_center(lbl_back);

    lv_obj_t * list = lv_list_create(song_sel_win);
    lv_obj_set_size(list, 500, 300);
    lv_obj_center(list);

    DIR *d = opendir("records");
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".rec")) {
                if(is_record_hidden(dir->d_name)) continue;
                lv_obj_t * btn = lv_list_add_button(list, LV_SYMBOL_PLAY, dir->d_name);
                fix_list_button_fonts(btn);
                attach_ui_button_sound(btn);
                char * payload = strdup(dir->d_name);
                lv_obj_add_event_cb(btn, auto_play_start_cb, LV_EVENT_CLICKED, payload);
            }
        }
        closedir(d);
    }
}

int main(void) {
    srand((unsigned int)time(NULL));
    if(platform_init() != 0) {
        return -1;
    }
    platform_audio_init(default_key_names, 19);
    platform_audio_set_mix_volumes(piano_mix_volume, bgm_mix_volume);
    load_manage_state();
#if 0
    // 1. Display & Input Setup
#if APP_TARGET_PC
    printf("[Info] Starting PC simulator at %dx%d\n", SCR_W, SCR_H);
#else
    int fd = open("/dev/fb0", O_RDWR); 
    if(fd == -1) {
        perror("[Error] fb0 setup failed");
        return -1;
    }
    fb_map = (uint32_t *)mmap(NULL, SCR_W*SCR_H*4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    ts_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    
    // 2. Play Boot Animation
    // blocking call
    printf("[Info] Playing startup animation: movie/boluo.avi\n");
    // 开发板 CPU 性能有限，取消 -zoom -x 800 -y 480（极耗CPU的软件缩放），并加上 -framedrop 允许必要时掉帧保障播放不卡死
    system("mplayer -slave -quiet -geometry 0:0 -framedrop movie/boluo.avi");
    // ================================================
#endif

    // 3. LVGL Init
    lv_init();

#if APP_TARGET_PC
    lv_display_t * disp = lv_sdl_window_create(SCR_W, SCR_H);
    if(disp == NULL) {
        fprintf(stderr, "[Error] SDL display setup failed\n");
        return -1;
    }
#else
    lv_display_t * disp = lv_display_create(SCR_W, SCR_H);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_buffers(disp, disp_buf, NULL, sizeof(disp_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif

    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touch_read_cb);
#endif

    create_login_ui();
    
    // 5. System Tick Thread
    pthread_t tid; 
    pthread_create(&tid, NULL, tick_thread, NULL);
    
    // 6. Main LVGL Event Loop
    while(1) { 
        lv_timer_handler(); 
        usleep(5000); // 5ms sleep
    }
    return 0;
}
