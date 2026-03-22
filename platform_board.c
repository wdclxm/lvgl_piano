#include "platform.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/soundcard.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR_W 800
#define SCR_H 480
#define TOUCH_MAX_X 1024
#define TOUCH_MAX_Y 600
#define MAX_AUDIO_CHANNELS 16
#define AUDIO_MIX_BUFFER_SAMPLES 512
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

static int ts_fd = -1;
static uint32_t * fb_map = NULL;
static lv_color32_t disp_buf[SCR_W * 60];
static platform_touch_point_t touch_points[PLATFORM_MAX_TOUCH];

static hardware_wav_t key_wavs[PLATFORM_KEY_CAPACITY];
static hardware_wav_t bgm_wav;
static audio_channel_t audio_channels[MAX_AUDIO_CHANNELS];
static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static int dsp_fd = -1;
static pthread_t audio_thread_id;
static int ui_volume = 80;
static int key_mix_volume = 80;
static int bgm_mix_volume = 75;
static float bgm_base_volume = 1.0f;

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

static hardware_wav_t load_wav_into_ram(const char * filepath)
{
    hardware_wav_t res = {NULL, 0};
    int fd = open(filepath, O_RDONLY);
    if(fd < 0) {
        printf("[Audio][Board] Missing WAV: %s\n", filepath);
        return res;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if(size <= 44) {
        close(fd);
        return res;
    }

    lseek(fd, 44, SEEK_SET);
    res.data = (int16_t *)malloc((size_t)(size - 44));
    if(res.data == NULL) {
        close(fd);
        return res;
    }

    if(read(fd, res.data, (size_t)(size - 44)) <= 0) {
        free(res.data);
        res.data = NULL;
        close(fd);
        return res;
    }

    close(fd);
    res.length_samples = (uint32_t)(size - 44) / sizeof(int16_t);
    return res;
}

static void free_wav(hardware_wav_t * wav)
{
    if(wav->data) {
        free(wav->data);
        wav->data = NULL;
        wav->length_samples = 0;
    }
}

static void * hardware_audio_mixer_thread(void * arg)
{
    int16_t out_buf[AUDIO_MIX_BUFFER_SAMPLES];
    int32_t mix_buf[AUDIO_MIX_BUFFER_SAMPLES];

    LV_UNUSED(arg);
    dsp_fd = open("/dev/dsp", O_WRONLY);
    if(dsp_fd < 0) return NULL;

    {
        int arg_val = AFMT_S16_LE;
        ioctl(dsp_fd, SNDCTL_DSP_SETFMT, &arg_val);
        arg_val = 2;
        ioctl(dsp_fd, SNDCTL_DSP_CHANNELS, &arg_val);
        arg_val = 44100;
        ioctl(dsp_fd, SNDCTL_DSP_SPEED, &arg_val);
    }

    while(1) {
        int active_count = 0;
        memset(mix_buf, 0, sizeof(mix_buf));

        pthread_mutex_lock(&audio_mutex);
        {
            float master_vol = ui_volume / 100.0f;
            for(int c = 0; c < MAX_AUDIO_CHANNELS; c++) {
                if(audio_channels[c].is_playing && audio_channels[c].wav && audio_channels[c].wav->data) {
                    uint32_t len = audio_channels[c].wav->length_samples;
                    uint32_t pos = audio_channels[c].current_pos;
                    float final_vol = master_vol * audio_channels[c].volume;
                    active_count++;

                    for(int i = 0; i < AUDIO_MIX_BUFFER_SAMPLES; i++) {
                        if(pos < len) {
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

        if(active_count == 0) {
            usleep(2000);
            continue;
        }

        for(int i = 0; i < AUDIO_MIX_BUFFER_SAMPLES; i++) {
            int32_t sample = mix_buf[i];
            if(sample > 32767) sample = 32767;
            else if(sample < -32768) sample = -32768;
            out_buf[i] = (int16_t)sample;
        }

        write(dsp_fd, out_buf, sizeof(out_buf));
    }
}

static void board_touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    struct input_event ev;

    LV_UNUSED(indev);
    while(read(ts_fd, &ev, sizeof(ev)) > 0) {
        if(ev.type == EV_ABS) {
            if(ev.code == ABS_X || ev.code == 0x35) {
                touch_points[0].x = ev.value * SCR_W / TOUCH_MAX_X;
            } else if(ev.code == ABS_Y || ev.code == 0x36) {
                touch_points[0].y = ev.value * SCR_H / TOUCH_MAX_Y;
            }
        } else if(ev.type == EV_KEY && (ev.code == BTN_TOUCH || ev.code == BTN_LEFT)) {
            touch_points[0].state = ev.value ? 1 : 0;
        }
    }

    data->point.x = touch_points[0].x;
    data->point.y = touch_points[0].y;
    data->state = touch_points[0].state ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void board_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    int32_t w = lv_area_get_width(area);
    uint32_t line_bytes = (uint32_t)w * 4U;
    lv_color32_t * src = (lv_color32_t *)px_map;

    LV_UNUSED(disp);
    for(int y = area->y1; y <= area->y2; y++) {
        uint32_t * dst = fb_map + y * SCR_W + area->x1;
        memcpy(dst, src, line_bytes);
        src += w;
    }

    lv_display_flush_ready(disp);
}

int platform_init(void)
{
    int fd = open("/dev/fb0", O_RDWR);
    if(fd == -1) {
        perror("[Error] fb0 setup failed");
        return -1;
    }

    fb_map = (uint32_t *)mmap(NULL, SCR_W * SCR_H * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ts_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);

    printf("[Info] Playing startup animation: movie/boluo.avi\n");
    system("mplayer -slave -quiet -geometry 0:0 -framedrop movie/boluo.avi");

    lv_init();

    lv_display_t * disp = lv_display_create(SCR_W, SCR_H);
    lv_display_set_flush_cb(disp, board_flush_cb);
    lv_display_set_buffers(disp, disp_buf, NULL, sizeof(disp_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, board_touch_read_cb);

    return 0;
}

const platform_touch_point_t * platform_get_touch_points(void)
{
    return touch_points;
}

void platform_audio_init(const char * const * key_names, int key_count)
{
    static int initialized = 0;

    if(initialized) return;
    memset(audio_channels, 0, sizeof(audio_channels));

    for(int i = 0; i < key_count && i < PLATFORM_KEY_CAPACITY; i++) {
        if(key_names[i] && key_names[i][0] != '\0') {
            char path[128];
            build_audio_asset_path(path, sizeof(path), key_names[i]);
            key_wavs[i] = load_wav_into_ram(path);
        }
    }

    pthread_create(&audio_thread_id, NULL, hardware_audio_mixer_thread, NULL);
    initialized = 1;
}

void platform_audio_set_volume(int volume)
{
    if(volume < 0) volume = 0;
    if(volume > 100) volume = 100;
    ui_volume = volume;
}

void platform_audio_set_mix_volumes(int key_volume, int bgm_volume)
{
    if(key_volume < 0) key_volume = 0;
    if(key_volume > 100) key_volume = 100;
    if(bgm_volume < 0) bgm_volume = 0;
    if(bgm_volume > 100) bgm_volume = 100;
    key_mix_volume = key_volume;
    bgm_mix_volume = bgm_volume;

    pthread_mutex_lock(&audio_mutex);
    if(audio_channels[0].is_playing && audio_channels[0].wav == &bgm_wav) {
        audio_channels[0].volume = bgm_base_volume * (bgm_mix_volume / 100.0f);
    }
    pthread_mutex_unlock(&audio_mutex);
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
    play_sound_on_channel(found_ch, &key_wavs[key_idx], key_mix_volume / 100.0f);
}

void platform_audio_play_bgm(const char * path, float volume)
{
    if(path == NULL || path[0] == '\0') return;
    platform_audio_clear_bgm();
    bgm_base_volume = volume;
    bgm_wav = load_wav_into_ram(path);
    play_sound_on_channel(0, &bgm_wav, bgm_base_volume * (bgm_mix_volume / 100.0f));
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
    bgm_base_volume = 1.0f;
}
