#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define MUS_PATH "music.wav"
#define log_error(msg) fprintf(stderr, msg ": %s\n", SDL_GetError())

SDL_Renderer *renderer;
SDL_Window *window;
SDL_Texture *texture;
SDL_AudioDeviceID audio_device;
void
init_sdl(int width, int height) {
    int ret;
    ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    if (ret != 0) {
        printf("Could not initialize SDL - %s\n.", SDL_GetError());
        exit(-1);
    }
    // 初始化图形窗口
    window = SDL_CreateWindow("SDL Video Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height,
                              SDL_WINDOW_ALLOW_HIGHDPI);
    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);

    // 打开音频设备
    audio_device = SDL_OpenAudioDevice(NULL, 0, &wav_spec, NULL, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (audio_device < 0) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        exit(-1);
    }
}

int
main(int argc, char *argv[]) {
    init_sdl(1920, 1080);

    // 加载 wav 音频文件

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
        SDL_Delay(100);
        printf("pos: %p, len: %d\n", audio_pos, audio_len);
    }

    // 等待队列的音频播放完
    while (SDL_GetQueuedAudioSize(device_id) > 0) {
        SDL_Delay(100);
    }

    // 清理资源
    SDL_CloseAudio();
    SDL_FreeWAV(wav_buffer);
}
