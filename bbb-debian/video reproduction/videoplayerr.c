/*
 * player.c — MP4 player that writes directly to /dev/fb0
 *
 * Video is displayed on the left half of the screen; the right half stays black.
 * Supports 16 bpp (RGB565) and 32 bpp framebuffers.
 * Loops forever until you press Ctrl+C.
 *
 * Build:
 *   gcc player.c -o player \
 *       -lavformat -lavcodec -lavutil -lswscale
 *
 * Usage:
 *   ./player video.mp4
 *   ./player --fb /dev/fb1 video.mp4
 *
 * Stop: Ctrl+C
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>

/* ── Constants ── */
#define VIDEO_SCREEN_FRACTION   2       /* video occupies 1/2 of screen width */
#define DEFAULT_FB_DEV          "/dev/fb0"
#define DECODER_THREADS         4
#define DEFAULT_FPS             25.0
#define MIN_SLEEP_US            1000

/* ── ANSI escape sequences ── */
#define ANSI_HIDE_CURSOR   "\033[?25l"
#define ANSI_CLEAR_SCREEN  "\033[2J"
#define ANSI_CURSOR_HOME   "\033[H"
#define ANSI_SHOW_CURSOR   "\033[?25h"


static volatile sig_atomic_t g_quit = 0;
static void on_sigint(int s) { (void)s; g_quit = 1; }

/* ── Error helpers ── */
static void die(const char *msg)             { perror(msg); exit(EXIT_FAILURE); }
static void die_msg(const char *msg)         { fprintf(stderr,"ERROR: %s\n",msg); exit(EXIT_FAILURE); }
static void die_av(const char *msg, int err) {
    char buf[256]; av_strerror(err, buf, sizeof(buf));
    fprintf(stderr,"ERROR: %s — %s\n", msg, buf); exit(EXIT_FAILURE);
}
static void sleep_ms(unsigned ms) {
    struct timespec ts = { .tv_sec = ms/1000, .tv_nsec = (long)(ms%1000)*1000000L };
    nanosleep(&ts, NULL);
}

typedef struct {
    int      fd;
    uint8_t *mem;         /* pointer to the mmap'd framebuffer memory */
    size_t   mem_size;    /* total size of the framebuffer in bytes   */
    uint32_t fb_w;        /* full screen width in pixels              */
    uint32_t fb_h;        /* full screen height in pixels             */
    uint32_t bpp;         /* bits per pixel: 16 or 32                 */
    uint32_t line_len;    /* bytes per row in the framebuffer         */
    uint32_t vid_w;       /* scaled video width  (fits inside slot)   */
    uint32_t vid_h;       /* scaled video height                      */
    uint32_t off_x;       /* left edge of the video within the screen */
    uint32_t off_y;       /* top  edge of the video (letterbox)       */
} FB;

/*
 * fb_open — open the framebuffer and map it into process memory.
 *
 * Steps:
 *   1. Open /dev/fb0 for reading and writing
 *   2. Query screen resolution and bpp via ioctl()
 *   3. Map the framebuffer into RAM with mmap()
 *   4. Scale the video to fit the left half of the screen (keeps aspect ratio)
 *   5. Clear the screen to black
 */
static FB fb_open(const char *dev, int src_w, int src_h)
{
    FB fb; memset(&fb, 0, sizeof(fb));

    fb.fd = open(dev, O_RDWR);
    if (fb.fd < 0) die(dev);

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fb.fd, FBIOGET_VSCREENINFO, &vinfo) < 0) die("FBIOGET_VSCREENINFO");
    if (ioctl(fb.fd, FBIOGET_FSCREENINFO, &finfo) < 0) die("FBIOGET_FSCREENINFO");

    fb.fb_w     = vinfo.xres;
    fb.fb_h     = vinfo.yres;
    fb.bpp      = vinfo.bits_per_pixel;
    fb.line_len = finfo.line_length;

    if (fb.bpp != 16 && fb.bpp != 32) {
        fprintf(stderr,"Unsupported bpp: %u (only 16 and 32 are supported)\n", fb.bpp);
        exit(EXIT_FAILURE);
    }

    fb.mem_size = (size_t)fb.line_len * fb.fb_h;

    fb.mem = mmap(NULL, fb.mem_size, PROT_READ|PROT_WRITE,
                  MAP_SHARED, fb.fd, 0);
    if (fb.mem == MAP_FAILED) die("mmap /dev/fb0");

    /* Scale video into the LEFT HALF of the screen while preserving aspect ratio.*/

    uint32_t slot_w = fb.fb_w / VIDEO_SCREEN_FRACTION;
    uint32_t slot_h = fb.fb_h;

    uint32_t sw = slot_w;
    uint32_t sh = (uint32_t)((int64_t)src_h * slot_w / src_w);
    if (sh > slot_h) {
        sh = slot_h;
        sw = (uint32_t)((int64_t)src_w * slot_h / src_h);
    }
    fb.vid_w = sw;
    fb.vid_h = sh;

    fb.off_x = (slot_w - sw) / 2;
    fb.off_y = (slot_h - sh) / 2;

    /* Black out the entire screen before we start */
    memset(fb.mem, 0, fb.mem_size);

    fprintf(stderr,
        "Screen %ux%u %ubpp | Left slot %ux%u | Video %ux%u at (%u,%u)\n",
        fb.fb_w, fb.fb_h, fb.bpp,
        slot_w, slot_h,
        fb.vid_w, fb.vid_h, fb.off_x, fb.off_y);

    return fb;
}

static void fb_close(FB *fb) {
    if (fb->mem && fb->mem != MAP_FAILED) munmap(fb->mem, fb->mem_size);
    if (fb->fd >= 0) close(fb->fd);
}

/* ─────────────────────────────────────────────────────────────────────────
 * fb_blit — copy one decoded video frame onto the screen
 *
 * Copies the frame row by row into the framebuffer, respecting off_x/off_y
 * so the video lands in the correct position.
 * ───────────────────────────────────────────────────────────────────────── */
static void fb_blit(const FB *fb, const uint8_t *src, int src_stride)
{
    uint32_t bytes_per_px = fb->bpp / 8;
    uint32_t row_bytes    = fb->vid_w * bytes_per_px;

    for (uint32_t y = 0; y < fb->vid_h; y++) {
        uint8_t *dst = fb->mem
                     + (fb->off_y + y) * fb->line_len
                     + fb->off_x * bytes_per_px;
        memcpy(dst, src + (size_t)y * (size_t)src_stride, row_bytes);
    }
}

int main(int argc, char *argv[])
{
    const char *fbdev    = DEFAULT_FB_DEV;
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i],"--fb") == 0 && i+1 < argc)
            fbdev = argv[++i];
        else
            filename = argv[i];
    }
    if (!filename) {
        fprintf(stderr,"Usage: %s [--fb /dev/fbN] <video.mp4>\n", argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    printf(ANSI_HIDE_CURSOR ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME);
    fflush(stdout);

    /* ── 1. Open the video file ── */
    AVFormatContext *fmt_ctx = NULL;
    int ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL);
    if (ret < 0) die_av("avformat_open_input", ret);

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) die_av("avformat_find_stream_info", ret);
    av_dump_format(fmt_ctx, 0, filename, 0);

    /* ── 2. Find the video stream ── */
    int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1,-1,NULL,0);
    if (video_idx < 0) die_msg("No video stream found in file");

    AVStream          *vstream  = fmt_ctx->streams[video_idx];
    AVCodecParameters *codecpar = vstream->codecpar;

    /* ── 3. Open the decoder ── */
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) die_msg("No decoder found for this codec");

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) die_msg("avcodec_alloc_context3 failed");

    ret = avcodec_parameters_to_context(codec_ctx, codecpar);
    if (ret < 0) die_av("avcodec_parameters_to_context", ret);

    /* Use multiple threads for faster decoding */
    codec_ctx->thread_count = DECODER_THREADS;
    codec_ctx->thread_type  = FF_THREAD_FRAME;

    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0) die_av("avcodec_open2", ret);

    int src_w = codec_ctx->width;
    int src_h = codec_ctx->height;

    /* Calculate per-frame sleep time from the stream's average frame rate */
    double fps = DEFAULT_FPS;
    if (vstream->avg_frame_rate.den && vstream->avg_frame_rate.num)
        fps = av_q2d(vstream->avg_frame_rate);
    unsigned frame_delay_ms = (unsigned)(1000.0 / fps + 0.5);

    
    FB fb = fb_open(fbdev, src_w, src_h);

    /* Choose the pixel format that matches what the framebuffer expects */
    enum AVPixelFormat dst_fmt =
        (fb.bpp == 16) ? AV_PIX_FMT_RGB565LE : AV_PIX_FMT_BGR32;

    /* ── 5. Allocate decode + conversion buffers ── */
    AVFrame *frame     = av_frame_alloc();
    AVFrame *frame_dst = av_frame_alloc();
    if (!frame || !frame_dst) die_msg("av_frame_alloc failed");

    int      dst_buf_size = av_image_get_buffer_size(dst_fmt,
                                (int)fb.vid_w, (int)fb.vid_h, 1);
    uint8_t *dst_buf      = (uint8_t *)av_malloc((size_t)dst_buf_size);
    if (!dst_buf) die_msg("av_malloc failed for conversion buffer");

    av_image_fill_arrays(frame_dst->data, frame_dst->linesize,
                         dst_buf, dst_fmt,
                         (int)fb.vid_w, (int)fb.vid_h, 1);

    /* SwsContext handles two things in one pass:
     *   1. Pixel-format conversion (YUV → RGB)
     *   2. Scaling to the target dimensions */
    struct SwsContext *sws_ctx =
        sws_getContext(src_w, src_h, codec_ctx->pix_fmt,
                       (int)fb.vid_w, (int)fb.vid_h, dst_fmt,
                       SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) die_msg("sws_getContext failed");

    /* ── 6. Playback loop ── */
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) die_msg("av_packet_alloc failed");

    /* Outer loop: restart the video when it reaches the end */
    while (!g_quit) {

        /* Seek back to the beginning for each replay */
        av_seek_frame(fmt_ctx, video_idx, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codec_ctx);

        /* Reference timestamp for frame-accurate timing */
        int64_t t_start_us = av_gettime_relative();

        /* Inner loop: read, decode, scale, and display every frame */
        while (!g_quit && av_read_frame(fmt_ctx, pkt) >= 0) {

            /* Skip audio and subtitle packets */
            if (pkt->stream_index != video_idx) {
                av_packet_unref(pkt);
                continue;
            }

            /* Send the compressed packet to the decoder */
            ret = avcodec_send_packet(codec_ctx, pkt);
            av_packet_unref(pkt);
            if (ret < 0) continue;

            /* Drain all decoded frames from the decoder */
            while (!g_quit && ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) { fprintf(stderr,"Decode error — skipping frame\n"); break; }

                /* Convert pixel format and scale to the display slot size */
                sws_scale(sws_ctx,
                          (const uint8_t * const *)frame->data, frame->linesize,
                          0, src_h,
                          frame_dst->data, frame_dst->linesize);

                /* Write the frame to the framebuffer */
                fb_blit(&fb, frame_dst->data[0], frame_dst->linesize[0]);

                /* Frame timing: sleep until it is time to show the next frame */
                if (frame->pts != AV_NOPTS_VALUE) {
                    int64_t pts_us = av_rescale_q(frame->pts,
                                                  vstream->time_base,
                                                  AV_TIME_BASE_Q);
                    int64_t now_us = av_gettime_relative() - t_start_us;
                    int64_t diff   = pts_us - now_us;
                    if (diff > MIN_SLEEP_US)
                        usleep((useconds_t)diff);
                } else {
                    sleep_ms(frame_delay_ms);
                }
            }
        }

        /* Flush any frames still buffered inside the decoder */
        avcodec_send_packet(codec_ctx, NULL);
        while (avcodec_receive_frame(codec_ctx, frame) >= 0) { /* drain */ }
    }

    /* Black out the screen on exit */
    memset(fb.mem, 0, fb.mem_size);

    /* Restore the terminal cursor */
    printf(ANSI_SHOW_CURSOR);
    fflush(stdout);

    /* ── Free all resources ── */
    av_packet_free(&pkt);
    av_free(dst_buf);
    av_frame_free(&frame_dst);
    av_frame_free(&frame);
    sws_freeContext(sws_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    fb_close(&fb);

    return EXIT_SUCCESS;
}
