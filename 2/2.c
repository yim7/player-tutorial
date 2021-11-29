#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

SDL_Renderer *renderer;
SDL_Window *window;
SDL_Texture *texture;

void
save_frame(uint8_t *buf, int linesize, int width, int height, const char *path);

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
    AVCodec *codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (codec == NULL) {
        printf("Unsupported codec\n");
        return -1;
    }

    // codec context 包含流使用的解码器的全部信息
    // 分配内存，初始化解码器默认值
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, video_stream->codecpar);

    // 打开解码器
    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0) {
        printf("Could not open codec\n");
        return -1;
    }

    int width = codec_ctx->width;
    int height = codec_ctx->height;
    init_sdl(width, height);

    // 用来解码一帧图片
    AVFrame *frame = av_frame_alloc();
    // 用来保存 yuv420p
    AVFrame *frame_out = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();

    // 负责图像转换的功能
    struct SwsContext *sws_ctx =
        sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt, codec_ctx->width, codec_ctx->height,
                       AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height, 32);
    uint8_t *buffer = av_malloc(sizeof(uint8_t) * buffer_size);
    // 根据图片宽高、像素格式，计算每行的字节数
    // frame data 数组，第一个元素被设置为我们传入的 buffer
    // linesize 是个数组，表示每个颜色组成一行所占字节数
    av_image_fill_arrays(frame_out->data, frame_out->linesize, buffer, AV_PIX_FMT_YUV420P, codec_ctx->width,
                         codec_ctx->height, 32);

    int frame_count = 0;
    int last_pts = 0;

    AVRational time_base = video_stream->time_base;
    int delta = 60 / av_q2d(time_base);
    while (av_read_frame(fmt_ctx, packet) == 0) {
        // 只要视频流
        if (packet->stream_index != video_stream_index) {
            continue;
        }

        // 把 packet 中的数据传给解码器进行解码
        ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0) {
            printf("Error decoding\n");
            return -1;
        }

        // packet 里可能有多个完整的 frame，都读出来保存为图片
        while (1) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                printf("Error decoding\n");
                return -1;
            }

            frame_count += 1;

            // todo: stride?
            sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, codec_ctx->height,
                      frame_out->data, frame_out->linesize);
            double fps = av_q2d(video_stream->r_frame_rate);
            double sleep_time = 1 / fps;
            SDL_Delay(1000 * sleep_time);
            SDL_Rect rect;
            rect.x = 0;
            rect.y = 0;
            rect.w = width;
            rect.h = height;
            SDL_UpdateYUVTexture(texture, &rect, frame_out->data[0], frame_out->linesize[0], frame_out->data[1],
                                 frame_out->linesize[1], frame_out->data[2], frame_out->linesize[2]);
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
    // 释放分配的 buffer
    av_free(buffer);
    // 释放 freame，注意传入的是 AVFrame 指针的指针，调用后，外面的 AVFrame 会被设置为 NULL
    av_frame_free(&frame_out);
    av_frame_free(&frame);
    // 关闭解码器上下文
    // 解码器是 ffmpeg 内部全局创建的，不需要管
    avcodec_close(codec_ctx);
    // 关闭打开的文件，注意传入的是 AVFormatContext 指针的指针
    avformat_close_input(&fmt_ctx);

    // 清理 sdl 资源
    SDL_DestroyRenderer(renderer);
    SDL_Quit();

    return 0;
}

void
save_frame(uint8_t *buf, int linesize, int width, int height, const char *path) {

    FILE *file = fopen(path, "wb");
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    // printf("line size %d\n", frame->linesize[0]);
    for (size_t i = 0; i < height; i++) {
        fwrite(buf + i * linesize, 1, width * 3, file);
    }

    fclose(file);
}
