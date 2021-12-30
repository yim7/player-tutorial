#include <SDL2/SDL.h>
#include <fcntl.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

SDL_Renderer *renderer;
SDL_Window *window;
SDL_Texture *texture;
SDL_AudioDeviceID audio_device;

SDL_AudioDeviceID
open_audio_device(int sample_rate, int sample_format, int channels) {
    SDL_AudioSpec wav_spec;
    wav_spec.freq = sample_rate;
    wav_spec.format = sample_format;
    wav_spec.channels = channels;
    wav_spec.samples = 4096;

    // 使用 SDL_QueueAudio，需要把这俩设置为 NULL
    wav_spec.callback = NULL;
    wav_spec.userdata = NULL;

    return SDL_OpenAudioDevice(NULL, 0, &wav_spec, NULL, SDL_AUDIO_ALLOW_ANY_CHANGE);
}

void
init_sdl(int width, int height, int sample_rate, int sample_format, int channels) {
    int ret;
    ret = SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);
    if (ret != 0) {
        printf("Could not initialize SDL - %s\n.", SDL_GetError());
        exit(-1);
    }

    window = SDL_CreateWindow("SDL Video Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width / 2,
                              height / 2, SDL_WINDOW_ALLOW_HIGHDPI);
    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);

    audio_device = open_audio_device(sample_rate, sample_format, channels);
    if (audio_device < 0) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        exit(-1);
    }
    // 打开的音频设备默认是静音状态，取消静音
    SDL_PauseAudioDevice(audio_device, 0);
}

int
main(int argc, char const *argv[]) {
    init_sdl(1920, 1080, 44100, AUDIO_F32, 2);

    const char *filename = "video.mp4";
    int ret;
    AVFormatContext *fmt_ctx = NULL;

    // 打开视频文件
    // 套路代码，注意第一个参数是 fmt_ctx 的地址
    // 因为是函数内部实际创建 AVFormatContext，并修改 fmt_ctx 的值
    ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL);
    if (ret < 0) {
        printf("Could not open file %s\n", filename);
        return -1;
    }

    // 获取 stream 信息，写入到 fmt_ctx 中
    // nb_streams 表示 stream 的个数
    // streams 数组保存所有的 stream 结构
    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        printf("Could not find stream info %s\n", filename);
        return -1;
    }

    // 打印视频的详细信息
    // 第一个参数是文件的 AVFormatContext
    // 第三个参数是打开的文件名
    // 其他参数不用管，写 0
    av_dump_format(fmt_ctx, 0, filename, 0);

    // 找到视频流
    int video_stream_index = -1;
    for (size_t i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *s = fmt_ctx->streams[i];
        if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
        }
    }
    if (video_stream_index == -1) {
        printf("Could not find video stream\n");
        return -1;
    }
    AVStream *video_stream = fmt_ctx->streams[video_stream_index];
    // 找到视频流的解码器
    AVCodec *video_codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (video_codec == NULL) {
        printf("Unsupported video codec\n");
        return -1;
    }
    AVCodecContext *video_codec_ctx = avcodec_alloc_context3(video_codec);
    avcodec_parameters_to_context(video_codec_ctx, video_stream->codecpar);
    ret = avcodec_open2(video_codec_ctx, video_codec, NULL);
    if (ret < 0) {
        printf("Could not open video codec\n");
        return -1;
    }

    // 找到音频流
    int audio_stream_index = -1;
    for (size_t i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *s = fmt_ctx->streams[i];
        if (s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
        }
    }
    if (audio_stream_index == -1) {
        printf("Could not find audio stream\n");
        return -1;
    }
    AVStream *audio_stream = fmt_ctx->streams[audio_stream_index];
    // 找到音频解码器
    AVCodec *audio_codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (audio_codec == NULL) {
        printf("Unsupported audio codec\n");
        return -1;
    }
    AVCodecContext *audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    avcodec_parameters_to_context(audio_codec_ctx, audio_stream->codecpar);
    ret = avcodec_open2(audio_codec_ctx, audio_codec, NULL);
    if (ret < 0) {
        printf("Could not open audio codec\n");
        return -1;
    }

    int width = video_codec_ctx->width;
    int height = video_codec_ctx->height;
    int pix_fmt = video_codec_ctx->pix_fmt;

    int channels = audio_codec_ctx->channels;
    int sample_rate = audio_codec_ctx->sample_rate;
    int format = audio_codec_ctx->sample_fmt;
    int layout = audio_codec_ctx->channel_layout;
    printf("channels: %d, saple_rate: %d, format: %d\n", channels, sample_rate, format);

    // 保存解码出的数据帧
    AVFrame *frame = av_frame_alloc();
    AVFrame *frame_scale = av_frame_alloc();
    AVFrame *frame_resample = av_frame_alloc();
    frame_resample->channel_layout = layout;
    frame_resample->sample_rate = sample_rate;
    frame_resample->channels = channels;
    frame_resample->format = AV_SAMPLE_FMT_FLT;

    AVPacket *packet = av_packet_alloc();

    // 重采样转换音频格式
    SwrContext *swr_ctx = swr_alloc_set_opts(NULL,              // we're allocating a new context
                                             layout,            // out_ch_layout
                                             AV_SAMPLE_FMT_FLT, // out_sample_fmt
                                             sample_rate,       // out_sample_rate
                                             layout,            // in_ch_layout
                                             format,            // in_sample_fmt
                                             sample_rate,       // in_sample_rate
                                             0,                 // log_offset
                                             NULL);             // log_ctx

    struct SwsContext *sws_ctx = sws_getContext(width,              // the width of the source image
                                                height,             // the height of the source image
                                                pix_fmt,            // the source image format
                                                width,              // the width of the destination image
                                                height,             // the height of the destination image
                                                AV_PIX_FMT_YUV420P, // the destination image format
                                                SWS_BILINEAR,       // algorithm and options
                                                NULL,               // the filter of the source image
                                                NULL,               // the filter of the source image
                                                NULL                // extra parameters to tune the used scaler
    );
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 32);
    uint8_t *buffer = av_malloc(sizeof(uint8_t) * buffer_size);
    av_image_fill_arrays(frame_scale->data, frame_scale->linesize, buffer, AV_PIX_FMT_YUV420P, width, height, 32);

    while (av_read_frame(fmt_ctx, packet) == 0) {
        // 只要音频
        if (packet->stream_index == audio_stream_index) {
            // 把 packet 中的数据传给解码器进行解码
            ret = avcodec_send_packet(audio_codec_ctx, packet);
            if (ret < 0) {
                printf("Error decoding\n");
                return -1;
            }

            // packet 里可能有多个完整的 frame
            while (1) {
                ret = avcodec_receive_frame(audio_codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    printf("Error decoding\n");
                    return -1;
                }

                // 转换音频格式
                ret = swr_convert_frame(swr_ctx, frame_resample, frame);
                if (ret < 0) {
                    printf("Resample error\n");
                    return -1;
                }

                int frame_size = frame_resample->nb_samples * frame_resample->channels *
                                 av_get_bytes_per_sample(frame_resample->format);
                // printf("frame sample %d, %d\n", frame->linesize[0], frame_size);
                SDL_QueueAudio(audio_device, frame_resample->data[0], frame_size);
                // 释放 packet 内部数据，并把 packet 一些自动设为默认值
                av_packet_unref(packet);
            }
        } else if (packet->stream_index == video_stream_index) {
            // 把 packet 中的数据传给解码器进行解码
            // 把 packet 中的数据传给解码器进行解码
            ret = avcodec_send_packet(video_codec_ctx, packet);
            if (ret < 0) {
                printf("Error decoding\n");
                return -1;
            }

            // packet 里可能有多个完整的 frame
            while (1) {
                ret = avcodec_receive_frame(video_codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    printf("Error decoding\n");
                    return -1;
                }

                sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, height, frame_scale->data,
                          frame_scale->linesize);

                SDL_Rect rect;
                rect.x = 0;
                rect.y = 0;
                rect.w = width;
                rect.h = height;
                SDL_UpdateYUVTexture(texture, &rect, frame_scale->data[0], frame_scale->linesize[0],
                                     frame_scale->data[1], frame_scale->linesize[1], frame_scale->data[2],
                                     frame_scale->linesize[2]);
                // clear the current rendering target with the drawing color
                SDL_RenderClear(renderer);

                // copy a portion of the texture to the current rendering target
                SDL_RenderCopy(renderer, // the rendering context
                               texture,  // the source texture
                               NULL,     // the source SDL_Rect structure or NULL for the entire texture
                               NULL      // the destination SDL_Rect structure or NULL for the entire rendering
                                         // target; the texture will be stretched to fill the given rectangle
                );

                // update the screen with any rendering performed since the previous call
                SDL_RenderPresent(renderer);
            }
        }

        // handle event
        SDL_Event event;
        SDL_PollEvent(&event);
        switch (event.type) {
        case SDL_QUIT: {
            printf("quit event\n");
            SDL_Quit();
            exit(0);
        } break;

        default: {
            // nothing to do
        } break;
        }
    }
    // printf("wav length: %d\n", wav_length);
    // save_wave("sound1.wav", wav_buf, wav_length, sample_rate, channels, 32);
    // 等待队列的音频播放完
    while (SDL_GetQueuedAudioSize(audio_device) > 0) {
        SDL_Delay(100);
    }

    // 清理分配的资源
    av_frame_free(&frame);
    av_frame_free(&frame_resample);
    av_packet_free(&packet);
    avcodec_close(audio_codec_ctx);
    avformat_close_input(&fmt_ctx);

    // 清理 sdl 资源
    SDL_Quit();

    return 0;
}
