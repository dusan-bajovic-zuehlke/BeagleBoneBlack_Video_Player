/*
 * mp4player.c -- MP4 player writing directly to /dev/fb0
 *
 * No SDL2, no X11.  Uses:
 *   libavformat / libavcodec  -- demux + decode
 *   libswscale               -- pixel-format + scale conversion
 *   Linux framebuffer API    -- /dev/fb0 via ioctl + mmap
 *
 * Supported framebuffer depths: 32 bpp (ARGB/XRGB) and 16 bpp (RGB565).
 * Quit: send SIGINT (Ctrl-C) or let the video finish.
 *
 * Usage:
 *   ./mp4player video.mp4              -- full screen
 *   ./mp4player --left  video.mp4      -- left half
 *   ./mp4player --right video.mp4      -- right half
 *
 * Run side by side with seg7:
 *   sudo bash -c './mp4player --left video.mp4 & ./seg7 --right'
 *
 * Build:
 *   gcc mp4player.c -o mp4player -O3 -march=native -funroll-loops -ffast-math \
 *     -pthread $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale)
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

/* -- Signal ---------------------------------------------------------------- */

static volatile sig_atomic_t g_quit = 0;
static void on_sigint(int s) { (void)s; g_quit = 1; }

/* -- Helpers --------------------------------------------------------------- */

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void die_msg(const char *msg)
{
    fprintf(stderr, "ERROR: %s\n", msg);
    exit(EXIT_FAILURE);
}

static void die_av(const char *msg, int err)
{
    char buf[256];
    av_strerror(err, buf, sizeof(buf));
    fprintf(stderr, "ERROR: %s -- %s\n", msg, buf);
    exit(EXIT_FAILURE);
}

static void sleep_ms(unsigned ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* -- Framebuffer ----------------------------------------------------------- */

typedef struct {
    int      fd;
    uint8_t *mem;
    size_t   mem_size;
    uint32_t fb_w;        /* full framebuffer width  */
    uint32_t fb_h;        /* full framebuffer height */
    uint32_t bpp;
    uint32_t line_len;
    /* our horizontal region */
    uint32_t region_x;
    uint32_t region_w;
    /* scaled video placement (relative to full fb) */
    uint32_t vid_w;
    uint32_t vid_h;
    uint32_t off_x;       /* includes region_x */
    uint32_t off_y;
} FB;

/*
 * fb_open -- open framebuffer and compute video placement.
 * region_x, region_w define which horizontal slice we own.
 * The video is scaled to fit inside that slice (with letterbox/pillarbox).
 */
static FB fb_open(const char *dev,
                  int src_w, int src_h,
                  uint32_t region_x, uint32_t region_w)
{
    FB fb;
    memset(&fb, 0, sizeof(fb));

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
        fprintf(stderr, "Unsupported framebuffer depth: %u bpp\n", fb.bpp);
        close(fb.fd);
        exit(EXIT_FAILURE);
    }

    fb.region_x = region_x;
    fb.region_w = region_w;

    fb.mem_size = (size_t)fb.line_len * fb.fb_h;
    fb.mem = mmap(NULL, fb.mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);
    if (fb.mem == MAP_FAILED) die("mmap /dev/fb0");

    /* Scale video to fit inside our region, preserving aspect ratio */
    uint32_t scaled_w = region_w;
    uint32_t scaled_h = (uint32_t)((int64_t)src_h * region_w / src_w);
    if (scaled_h > fb.fb_h) {
        scaled_h = fb.fb_h;
        scaled_w = (uint32_t)((int64_t)src_w * fb.fb_h / src_h);
    }

    fb.vid_w = scaled_w;
    fb.vid_h = scaled_h;
    fb.off_x = region_x + (region_w - scaled_w) / 2;  /* center in region */
    fb.off_y = (fb.fb_h - scaled_h) / 2;

    fprintf(stderr,
            "Framebuffer : %ux%u  %u bpp\n"
            "Region      : x=%u  w=%u\n"
            "Video slot  : %ux%u  at (%u,%u)\n",
            fb.fb_w, fb.fb_h, fb.bpp,
            fb.region_x, fb.region_w,
            fb.vid_w, fb.vid_h, fb.off_x, fb.off_y);

    return fb;
}

static void fb_close(FB *fb)
{
    if (fb->mem && fb->mem != MAP_FAILED) munmap(fb->mem, fb->mem_size);
    if (fb->fd >= 0) close(fb->fd);
}

/*
 * fb_blit -- write one decoded frame into our region of the framebuffer.
 * off_x already includes region_x so no extra offset needed here.
 */
static void fb_blit(const FB *fb, const uint8_t *src, int src_stride)
{
    uint32_t bytes_pp  = fb->bpp / 8;
    uint32_t row_bytes = fb->vid_w * bytes_pp;

    for (uint32_t y = 0; y < fb->vid_h; ++y) {
        uint8_t *dst = fb->mem
                     + (fb->off_y + y) * fb->line_len
                     + fb->off_x * bytes_pp;
        memcpy(dst, src + (size_t)y * (size_t)src_stride, row_bytes);
    }
}

/* -- Main ------------------------------------------------------------------ */

static void restore_cursor(void) {
    fprintf(stdout, "\033[?25h");
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    atexit(restore_cursor);
    const char *fbdev    = "/dev/fb0";
    const char *filename = NULL;

    typedef enum { FULL, LEFT, RIGHT } Side;
    Side side = FULL;

    /* Argument parsing: [--fb /dev/fbN] [--left|--right] <video.mp4> */
    for (int i = 1; i < argc; ++i) {
        if      (strcmp(argv[i], "--fb")    == 0 && i + 1 < argc) fbdev = argv[++i];
        else if (strcmp(argv[i], "--left")  == 0) side = LEFT;
        else if (strcmp(argv[i], "--right") == 0) side = RIGHT;
        else filename = argv[i];
    }

    if (!filename) {
        fprintf(stderr, "Usage: %s [--fb /dev/fbN] [--left|--right] <video.mp4>\n", argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    /* -- 1. Open container ------------------------------------------------- */
    AVFormatContext *fmt_ctx = NULL;
    int ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL);
    if (ret < 0) die_av("avformat_open_input", ret);

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) die_av("avformat_find_stream_info", ret);

    av_dump_format(fmt_ctx, 0, filename, 0);

    /* -- 2. Find video stream ---------------------------------------------- */
    int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_idx < 0) die_msg("No video stream found");

    AVStream          *vstream  = fmt_ctx->streams[video_idx];
    AVCodecParameters *codecpar = vstream->codecpar;

    /* -- 3. Open decoder --------------------------------------------------- */
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) die_msg("Decoder not found");

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) die_msg("avcodec_alloc_context3");

    ret = avcodec_parameters_to_context(codec_ctx, codecpar);
    if (ret < 0) die_av("avcodec_parameters_to_context", ret);

    codec_ctx->thread_count = 4;
    codec_ctx->thread_type  = FF_THREAD_FRAME;

    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0) die_av("avcodec_open2", ret);

    int src_w = codec_ctx->width;
    int src_h = codec_ctx->height;

    double fps = 25.0;
    if (vstream->avg_frame_rate.den && vstream->avg_frame_rate.num)
        fps = av_q2d(vstream->avg_frame_rate);
    unsigned frame_delay_ms = (unsigned)(1000.0 / fps + 0.5);

    /* -- 4. Open framebuffer ----------------------------------------------- */

    /*
     * We need the real screen width to compute regions, but fb_open needs
     * region info. Do a quick ioctl just to get the screen width first.
     */
    {
        int tmp_fd = open(fbdev, O_RDONLY);
        if (tmp_fd < 0) die(fbdev);
        struct fb_var_screeninfo vinfo;
        if (ioctl(tmp_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) die("FBIOGET_VSCREENINFO");
        close(tmp_fd);

        uint32_t full_w    = vinfo.xres;
        uint32_t region_x  = (side == RIGHT) ? full_w / 2 : 0;
        uint32_t region_w  = (side == FULL)  ? full_w     : full_w / 2;

        FB fb = fb_open(fbdev, src_w, src_h, region_x, region_w);

        enum AVPixelFormat dst_fmt =
            (fb.bpp == 16) ? AV_PIX_FMT_RGB565LE : AV_PIX_FMT_BGR32;

        /* -- 5. Allocate decode + conversion buffers ----------------------- */
        AVFrame *frame     = av_frame_alloc();
        AVFrame *frame_dst = av_frame_alloc();
        if (!frame || !frame_dst) die_msg("av_frame_alloc");

        int dst_buf_size = av_image_get_buffer_size(dst_fmt, (int)fb.vid_w, (int)fb.vid_h, 1);
        uint8_t *dst_buf = (uint8_t *)av_malloc((size_t)dst_buf_size);
        if (!dst_buf) die_msg("av_malloc for conversion buffer");

        av_image_fill_arrays(frame_dst->data, frame_dst->linesize,
                             dst_buf, dst_fmt, (int)fb.vid_w, (int)fb.vid_h, 1);

        struct SwsContext *sws_ctx =
            sws_getContext(src_w, src_h, codec_ctx->pix_fmt,
                           (int)fb.vid_w, (int)fb.vid_h, dst_fmt,
                           SWS_FAST_BILINEAR, NULL, NULL, NULL);
        if (!sws_ctx) die_msg("sws_getContext");

        /* -- 6. Decode + blit loop ----------------------------------------- */
        AVPacket *pkt = av_packet_alloc();
        if (!pkt) die_msg("av_packet_alloc");

        /* Hide terminal cursor */
        fprintf(stdout, "\033[?25l");
        fflush(stdout);

        /* Black out the entire framebuffer before starting */
        memset(fb.mem, 0, fb.mem_size);

        /* -- 6. Outer loop: restart from beginning when video ends --------- */
        while (!g_quit) {
            int64_t t_start_us = av_gettime_relative();

            while (!g_quit && av_read_frame(fmt_ctx, pkt) >= 0) {
                if (pkt->stream_index != video_idx) {
                    av_packet_unref(pkt);
                    continue;
                }

                ret = avcodec_send_packet(codec_ctx, pkt);
                av_packet_unref(pkt);
                if (ret < 0) continue;

                while (!g_quit && ret >= 0) {
                    ret = avcodec_receive_frame(codec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    if (ret < 0) { fprintf(stderr, "avcodec_receive_frame error\n"); break; }

                    sws_scale(sws_ctx,
                            (const uint8_t * const *)frame->data, frame->linesize,
                            0, src_h,
                            frame_dst->data, frame_dst->linesize);

                    memset(fb.mem, 0, fb.off_y * fb.line_len);

                    memset(fb.mem + (fb.off_y + fb.vid_h) * fb.line_len, 0, (fb.fb_h - fb.off_y - fb.vid_h) * fb.line_len);

                    fb_blit(&fb, frame_dst->data[0], frame_dst->linesize[0]);

                    if (frame->pts != AV_NOPTS_VALUE) {
                        int64_t pts_us = av_rescale_q(frame->pts, vstream->time_base, AV_TIME_BASE_Q);
                        int64_t now_us = av_gettime_relative() - t_start_us;
                        int64_t diff   = pts_us - now_us;
                        if (diff > 1000) usleep((useconds_t)diff);
                    } else {
                        sleep_ms(frame_delay_ms);
                    }
                }
            } 

            /* Flush decoder */
            avcodec_send_packet(codec_ctx, NULL);
            while (avcodec_receive_frame(codec_ctx, frame) >= 0) {}

            /* Seek back to start for next iteration */
            avcodec_flush_buffers(codec_ctx);
            av_seek_frame(fmt_ctx, video_idx, 0, AVSEEK_FLAG_BACKWARD);
        }        

        /* Restore cursor on exit */
        fprintf(stdout, "\033[?25h");
        fflush(stdout);

        /* Clear our region on exit */
        uint32_t bytes_pp = fb.bpp / 8;
        for (uint32_t y = 0; y < fb.fb_h; y++) {
            memset(fb.mem + y * fb.line_len + fb.region_x * bytes_pp, 0,
                   fb.region_w * bytes_pp);
        }

        /* -- 7. Clean up --------------------------------------------------- */
        av_packet_free(&pkt);
        av_free(dst_buf);
        av_frame_free(&frame_dst);
        av_frame_free(&frame);
        sws_freeContext(sws_ctx);
        fb_close(&fb);
    }

    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    return EXIT_SUCCESS;
}