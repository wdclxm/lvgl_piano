#ifndef PLATFORM_H
#define PLATFORM_H

#include "lvgl/lvgl.h"

#define PLATFORM_MAX_TOUCH 1

typedef struct {
    int x;
    int y;
    int state;
} platform_touch_point_t;

int platform_init(void);
const platform_touch_point_t * platform_get_touch_points(void);

void platform_audio_init(const char * const * key_names, int key_count);
void platform_audio_set_volume(int volume);
void platform_audio_set_mix_volumes(int key_volume, int bgm_volume);
void platform_audio_play_key(int key_idx);
void platform_audio_override_key(int key_idx, const char * path);
void platform_audio_clear_overrides(void);
void platform_audio_set_override_only(int enabled);
void platform_audio_play_bgm(const char * path, float volume);
void platform_audio_stop_bgm(void);
void platform_audio_clear_bgm(void);

#endif
