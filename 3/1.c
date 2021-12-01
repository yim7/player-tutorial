#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <stdio.h>

SDL_Renderer *renderer;
SDL_Window *window;
SDL_Texture *texture;

void
init_sdl(int width, int height) {
    int ret;
    ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    if (ret != 0) {
        printf("Could not initialize SDL - %s\n.", SDL_GetError());
        exit(-1);
    }
    window = SDL_CreateWindow("SDL Video Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height,
                              SDL_WINDOW_ALLOW_HIGHDPI);
    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
}

SDL_AudioDeviceID
open_audio_device(int freq, int channels) {
    SDL_AudioSpec want, real;
    want.freq = freq;
    want.format = AUDIO_F32;
    want.channels = channels;
    want.samples = 4096;
    want.callback = NULL;
    SDL_AudioDeviceID device = SDL_OpenAudioDevice(NULL, 0, &want, &real, SDL_AUDIO_ALLOW_ANY_CHANGE);
    return device;
}

int
main(int argc, char const *argv[]) {
    const char *filename = argv[1];
    int ret;
    AVFormatContext *fmt_ctx = NULL;

    // 打开视频文件
    // 只会读取文件开始的一些数据，用来识别封装
    // 套路代码，注意第一个参数是双重指针，
    ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL);
    if (ret < 0) {
        printf("Could not open file %s\n", filename);
        return -1;
    }

    // 获取 stream 信息
    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        printf("Could not find stream info %s\n", filename);
        return -1;
    }

    // 这是一个方便的调试函数，用来打印文件信息
    // 第一个参数是上面打开的 AVFormatContext
    // 第二个参数是 stream 编号，一般 0 是视频，1 是音频
    // 第三个参数是打开的文件名
    // 最后一个参数表示是输入文件还是输出文件，0，表示输入文件，因为我们是读取文件，所以是 0
    av_dump_format(fmt_ctx, 0, filename, 0);

    // 找到视频流和音频流
    int video_stream_index = -1;
    int audio_stream_index = -1;

    for (size_t i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *s = fmt_ctx->streams[i];
        enum AVMediaType type = s->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
        } else if (type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
        }
    }
    printf("stream index, audio %d video %d\n", audio_stream_index, video_stream_index);
    if (video_stream_index == -1) {
        printf("Could not find video stream\n");
        return -1;
    }
    if (audio_stream_index == -1) {
        printf("Could not find audio stream\n");
        return -1;
    }

    AVStream *video_stream = fmt_ctx->streams[video_stream_index];
    AVStream *audio_stream = fmt_ctx->streams[audio_stream_index];
    // 音频解码
    AVCodec *audio_codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (audio_codec == NULL) {
        printf("Unsupported codec\n");
        return -1;
    }
    // codec context 包含流使用的解码器的全部信息
    // 分配内存，初始化解码器默认值
    AVCodecContext *audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    avcodec_parameters_to_context(audio_codec_ctx, audio_stream->codecpar);

    // 打开解码器
    ret = avcodec_open2(audio_codec_ctx, audio_codec, NULL);
    if (ret < 0) {
        printf("Could not open codec\n");
        return -1;
    }

    // 初始化 sdl 和音频设备
    init_sdl(600, 400);
    SDL_AudioDeviceID device = open_audio_device(audio_codec_ctx->sample_rate, audio_codec_ctx->channels);
    // 开始播放声音
    SDL_PauseAudioDevice(device, 0);

    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();

    while (av_read_frame(fmt_ctx, packet) == 0) {
        if (packet->stream_index != audio_stream_index) {
            continue;
        }

        // 把 packet 中的数据传给解码器进行解码
        ret = avcodec_send_packet(audio_codec_ctx, packet);
        if (ret < 0) {
            printf("Error decoding\n");
            return -1;
        }

        // packet 里可能有多个完整的 frame，都读出来保存为图片
        while (1) {
            ret = avcodec_receive_frame(audio_codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                printf("Error decoding\n");
                return -1;
            }

            // 发到音频设备播放
            int ret = SDL_QueueAudio(device, frame->data[0], frame->linesize[0]);
            if (ret < 0) {
                printf("queue audio failed, error: <%s>\n", SDL_GetError());
                exit(-1);
            }

            // 释放 packet 内部数据，并把 packet 一些自动设为默认值
            av_packet_unref(packet);
            // handle Ctrl + C event
            SDL_Event event;
            SDL_PollEvent(&event);
            switch (event.type) {
            case SDL_QUIT: {
                SDL_Quit();
                exit(0);
            } break;

            default: {
                // nothing to do
            } break;
            }
        }
    }

    // 清理分配的资源
    av_frame_free(&frame);
    // 关闭解码器上下文
    // 解码器是 ffmpeg 内部全局创建的，不需要管
    avcodec_close(audio_codec_ctx);
    // 关闭打开的文件，注意传入的是 AVFormatContext 指针的指针
    avformat_close_input(&fmt_ctx);

    // 清理 sdl 资源
    SDL_DestroyRenderer(renderer);
    SDL_Quit();

    return 0;
}
