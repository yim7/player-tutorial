#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

void
save_frame(uint8_t *buf, int linesize, int width, int height, const char *path);

int
main(int argc, char const *argv[]) {
    const char *filename = argv[1];
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

    // 保存解码出的 frame，是 yuv 格式的图片
    AVFrame *frame = av_frame_alloc();
    // 用来保存 yuv -> rgb 图像
    AVFrame *frame_rbg = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    // 分配存放图片数据的内存，关联到 frame_rbg
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 32);
    uint8_t *buffer = av_malloc(sizeof(uint8_t) * buffer_size);
    av_image_fill_arrays(frame_rbg->data, frame_rbg->linesize, buffer, AV_PIX_FMT_RGB24, codec_ctx->width,
                         codec_ctx->height, 32);

    // 转行 yuv -> rgb
    struct SwsContext *sws_ctx =
        sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt, codec_ctx->width, codec_ctx->height,
                       AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

    int frame_count = 0;
    while (av_read_frame(fmt_ctx, packet) == 0) {
        // 只要视频的包
        if (packet->stream_index != video_stream_index) {
            continue;
        }

        // 解码视频帧
        ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0) {
            printf("Error decoding\n");
            return -1;
        }

        // 一个包里可能有多个视频帧，都读出来保存图片
        while (1) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                printf("Error decoding\n");
                return -1;
            }

            frame_count += 1;
            if (frame_count > 10) {
                return 0;
            }

            // todo: stride?
            sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, codec_ctx->height,
                      frame_rbg->data, frame_rbg->linesize);
            char path[128];
            sprintf(path, "frame_%d.ppm", frame_count);
            // printf("w %d h %d, w %d h %d\n", codec_ctx->width, codec_ctx->height, frame->width, frame->height);
            save_frame(frame_rbg->data[0], frame_rbg->linesize[0], codec_ctx->width, codec_ctx->height, path);

            // 释放 packet 内部数据，并把 packet 一些自动设为默认值
            av_packet_unref(packet);
        }
    }

    // 清理分配的资源
    // 释放分配的 buffer
    av_free(buffer);
    // 释放 freame，注意传入的是 AVFrame 指针的指针，调用后，外面的 AVFrame 会被设置为 NULL
    av_frame_free(&frame_rbg);
    av_frame_free(&frame);
    // 关闭解码器上下文
    // 解码器是 ffmpeg 内部全局创建的，不需要管
    avcodec_close(codec_ctx);
    // 关闭打开的文件，注意传入的是 AVFormatContext 指针的指针
    avformat_close_input(&fmt_ctx);
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
