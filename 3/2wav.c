#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define MUS_PATH "sound.wav"
#define log_error(msg) fprintf(stderr, msg ": %s\n", SDL_GetError())

int
main(int argc, char *argv[]) {
    // 初始化 sdl 音频
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        return 1;
    }

    // 加载 wav 音频文件
    static Uint32 wav_length;
    static Uint8 *wav_buffer;
    static SDL_AudioSpec wav_spec;
    if (SDL_LoadWAV(MUS_PATH, &wav_spec, &wav_buffer, &wav_length) == NULL) {
        return 1;
    }

    printf("audio spec, format: %x, freq: %d, channels: %d, samples: %d\n", wav_spec.format, wav_spec.freq,
           wav_spec.channels, wav_spec.samples);
    // 使用 SDL_QueueAudio，需要把这俩设置为 NULL
    wav_spec.callback = NULL;
    wav_spec.userdata = NULL;

    SDL_AudioDeviceID device_id = SDL_OpenAudioDevice(NULL, 0, &wav_spec, NULL, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (device_id < 0) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        exit(-1);
    }

    // 打开的音频设备默认是静音状态，取消静音
    SDL_PauseAudioDevice(device_id, 0);

    // 分块读取音频数据，放入播放队列中
    Uint32 size = 4096;
    // 剩下的音频长度
    Uint32 audio_len = wav_length;
    // 当前播放到的位置
    Uint8 *audio_pos = wav_buffer;
    while (audio_len > 0) {
        int len = audio_len < size ? audio_len : size;
        if (SDL_QueueAudio(device_id, audio_pos, len) < 0) {
            log_error("播放音频失败");
            exit(-1);
        }
        audio_pos += len;
        audio_len -= len;
        // SDL_Delay(100);
        // printf("pos: %p, len: %d\n", audio_pos, audio_len);
    }

    // 等待队列的音频播放完
    while (SDL_GetQueuedAudioSize(device_id) > 0) {
        SDL_Delay(100);
    }

    // 清理资源
    SDL_CloseAudio();
    SDL_FreeWAV(wav_buffer);
}
