#include "platform.h"

#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"

#define SCR_W 800
#define SCR_H 480
#define MAX_AUDIO_CHANNELS 16
#define PLATFORM_KEY_CAPACITY 19

typedef struct {
    int16_t * data;
    uint32_t length_samples;
} hardware_wav_t;

typedef struct {
    hardware_wav_t * wav;
    uint32_t current_pos;
    int is_playing;
    float volume;
} audio_channel_t;

static platform_touch_point_t touch_points[PLATFORM_MAX_TOUCH];
static hardware_wav_t key_wavs[PLATFORM_KEY_CAPACITY];
static hardware_wav_t bgm_wav;
static audio_channel_t audio_channels[MAX_AUDIO_CHANNELS];
static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static SDL_AudioDeviceID sdl_audio_device = 0;
static int ui_volume = 80;

static int file_exists(const char * path)
{
    return access(path, F_OK) == 0;
}

static void build_audio_asset_path(char * out, size_t out_size, const char * name)
{
    snprintf(out, out_size, "audio/%s.wav", name);
    if(file_exists(out)) return;
    snprintf(out, out_size, "aduio/%s.wav", name);
}

static void free_wav(hardware_wav_t * wav)
{
    if(wav->data) {
        free(wav->data);
        wav->data = NULL;
        wav->length_samples = 0;
    }
}

static void play_startup_animation_if_available(void)
{
    if(access("movie/boluo.avi", F_OK) != 0) {
        printf("[Info][PC] Startup animation not found: movie/boluo.avi\n");
        return;
    }

    if(system("command -v mplayer >/dev/null 2>&1") != 0) {
        printf("[Info][PC] mplayer not found, skipping startup animation\n");
        return;
    }

    printf("[Info][PC] Playing startup animation: movie/boluo.avi\n");
    system("mplayer -slave -quiet -geometry 0:0 -framedrop movie/boluo.avi");
}

static void sdl_audio_callback(void * userdata, Uint8 * stream, int len)
{
    int sample_count = len / (int)sizeof(int16_t);
    int32_t * mix_buf = (int32_t *)calloc((size_t)sample_count, sizeof(int32_t));
    int16_t * out_buf = (int16_t *)stream;

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

static hardware_wav_t load_wav_into_ram(const char * filepath)
{
    hardware_wav_t res = {NULL, 0};
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

static void pc_touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
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

    data->point.x = touch_points[0].x;
    data->point.y = touch_points[0].y;
    data->state = touch_points[0].state ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int platform_init(void)
{
    play_startup_animation_if_available();
    lv_init();

    lv_display_t * disp = lv_sdl_window_create(SCR_W, SCR_H);
    if(disp == NULL) {
        fprintf(stderr, "[Error] SDL display setup failed\n");
        return -1;
    }

    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, pc_touch_read_cb);

    return 0;
}

const platform_touch_point_t * platform_get_touch_points(void)
{
    return touch_points;
}

void platform_audio_init(const char * const * key_names, int key_count)
{
    static int initialized = 0;
    SDL_AudioSpec desired;

    if(initialized) return;
    if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("[Audio][PC] SDL audio init failed: %s\n", SDL_GetError());
        return;
    }

    memset(audio_channels, 0, sizeof(audio_channels));
    memset(&desired, 0, sizeof(desired));
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 1024;
    desired.callback = sdl_audio_callback;

    sdl_audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
    if(sdl_audio_device == 0) {
        printf("[Audio][PC] Failed to open audio device: %s\n", SDL_GetError());
        return;
    }

    for(int i = 0; i < key_count && i < PLATFORM_KEY_CAPACITY; i++) {
        if(key_names[i] && key_names[i][0] != '\0') {
            char path[128];
            build_audio_asset_path(path, sizeof(path), key_names[i]);
            key_wavs[i] = load_wav_into_ram(path);
        }
    }

    SDL_PauseAudioDevice(sdl_audio_device, 0);
    initialized = 1;
}

void platform_audio_set_volume(int volume)
{
    if(volume < 0) volume = 0;
    if(volume > 100) volume = 100;
    ui_volume = volume;
}

static void play_sound_on_channel(int ch_idx, hardware_wav_t * wav, float vol)
{
    if(!wav || !wav->data) return;
    pthread_mutex_lock(&audio_mutex);
    audio_channels[ch_idx].wav = wav;
    audio_channels[ch_idx].current_pos = 0;
    audio_channels[ch_idx].volume = vol;
    audio_channels[ch_idx].is_playing = 1;
    pthread_mutex_unlock(&audio_mutex);
}

void platform_audio_play_key(int key_idx)
{
    int found_ch = -1;
    if(key_idx < 0 || key_idx >= PLATFORM_KEY_CAPACITY) return;

    pthread_mutex_lock(&audio_mutex);
    for(int i = 1; i < MAX_AUDIO_CHANNELS; i++) {
        if(!audio_channels[i].is_playing) {
            found_ch = i;
            break;
        }
    }
    pthread_mutex_unlock(&audio_mutex);

    if(found_ch == -1) found_ch = 1;
    play_sound_on_channel(found_ch, &key_wavs[key_idx], 1.0f);
}

void platform_audio_play_bgm(const char * path, float volume)
{
    if(path == NULL || path[0] == '\0') return;
    platform_audio_clear_bgm();
    bgm_wav = load_wav_into_ram(path);
    play_sound_on_channel(0, &bgm_wav, volume);
}

void platform_audio_stop_bgm(void)
{
    pthread_mutex_lock(&audio_mutex);
    audio_channels[0].is_playing = 0;
    pthread_mutex_unlock(&audio_mutex);
}

void platform_audio_clear_bgm(void)
{
    platform_audio_stop_bgm();
    free_wav(&bgm_wav);
}
