/*
 * seg7.c -- 7-segment clock on /dev/fb0
 *
 * Displays current time as HH:mm:ss.nnn (9 digits + separators).
 * nnn = milliseconds (top 3 digits of nanoseconds).
 * Updates every millisecond.
 *
 * Each half has 10% padding on left and right edges.
 * Segment size auto-scales to fit the padded area.
 *
 * Usage:
 *   sudo ./seg7              -- full screen
 *   sudo ./seg7 --left       -- left half only
 *   sudo ./seg7 --right      -- right half only
 *
 * Run side by side:
 *   sudo bash -c './seg7 --left & ./seg7 --right'
 *
 * Build:  gcc seg7.c -o seg7
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <time.h>

/* -- Signal ---------------------------------------------------------------- */

static volatile sig_atomic_t g_quit = 0;
static void on_sigint(int s) { (void)s; g_quit = 1; }

/* -- Framebuffer ------------------------------------------------------------ */

typedef struct {
    int      fd;
    uint8_t *mem;
    uint8_t *back;
    size_t   mem_size;
    uint32_t w, h;
    uint32_t bpp;
    uint32_t line_len;
} FB;

static FB fb_open(const char *dev)
{
    FB fb;
    memset(&fb, 0, sizeof(fb));

    fb.fd = open(dev, O_RDWR);
    if (fb.fd < 0) { perror(dev); exit(EXIT_FAILURE); }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fb.fd, FBIOGET_VSCREENINFO, &vinfo) < 0) { perror("vscreeninfo"); exit(EXIT_FAILURE); }
    if (ioctl(fb.fd, FBIOGET_FSCREENINFO, &finfo) < 0) { perror("fscreeninfo"); exit(EXIT_FAILURE); }

    fb.w        = vinfo.xres;
    fb.h        = vinfo.yres;
    fb.bpp      = vinfo.bits_per_pixel;
    fb.line_len = finfo.line_length;
    fb.mem_size = (size_t)fb.line_len * fb.h;

    fb.mem = mmap(NULL, fb.mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);
    if (fb.mem == MAP_FAILED) { perror("mmap"); exit(EXIT_FAILURE); }

    fb.back = calloc(1, fb.mem_size);
    if (!fb.back) { perror("calloc backbuf"); exit(EXIT_FAILURE); }

    fprintf(stderr, "FB: %ux%u  %ubpp\n", fb.w, fb.h, fb.bpp);
    return fb;
}

static void fb_close(FB *fb)
{
    if (fb->mem && fb->mem != MAP_FAILED) munmap(fb->mem, fb->mem_size);
    if (fb->fd >= 0) close(fb->fd);
    free(fb->back);
}

/* Flip only our horizontal region to the real framebuffer */
static void fb_flip(FB *fb, uint32_t region_x, uint32_t region_w)
{
    uint32_t bytes_pp  = fb->bpp / 8;
    uint32_t row_bytes = region_w * bytes_pp;
    uint32_t col_off   = region_x * bytes_pp;

    for (uint32_t y = 0; y < fb->h; y++) {
        uint8_t *dst = fb->mem  + y * fb->line_len + col_off;
        uint8_t *src = fb->back + y * fb->line_len + col_off;
        memcpy(dst, src, row_bytes);
    }
}

/* -- Pixel helpers ---------------------------------------------------------- */

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Pixels outside region are silently dropped */
static void put_pixel(const FB *fb,
                      uint32_t x, uint32_t y, uint32_t color,
                      uint32_t region_x, uint32_t region_w)
{
    if (x >= fb->w || y >= fb->h) return;
    if (x < region_x || x >= region_x + region_w) return;

    if (fb->bpp == 32) {
        uint32_t *p = (uint32_t *)(fb->back + y * fb->line_len + x * 4);
        *p = color;
    } else {
        uint16_t *p = (uint16_t *)(fb->back + y * fb->line_len + x * 2);
        *p = (uint16_t)color;
    }
}

static void fill_rect(const FB *fb,
                      uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h,
                      uint32_t color,
                      uint32_t region_x, uint32_t region_w)
{
    for (uint32_t row = y; row < y + h; row++)
        for (uint32_t col = x; col < x + w; col++)
            put_pixel(fb, col, row, color, region_x, region_w);
}

/* -- Separator (colon or dot) ----------------------------------------------- */

static void draw_separator(const FB *fb,
                            uint32_t ox, uint32_t oy,
                            uint32_t cell_h, uint32_t dot_r,
                            int is_colon, uint32_t color,
                            uint32_t region_x, uint32_t region_w)
{
    uint32_t cx = ox + dot_r;

    if (is_colon) {
        uint32_t y1 = oy + cell_h / 3 - dot_r;
        fill_rect(fb, cx - dot_r, y1, dot_r * 2, dot_r * 2, color, region_x, region_w);
        uint32_t y2 = oy + cell_h * 2 / 3 - dot_r;
        fill_rect(fb, cx - dot_r, y2, dot_r * 2, dot_r * 2, color, region_x, region_w);
    } else {
        uint32_t y1 = oy + cell_h - dot_r * 3;
        fill_rect(fb, cx - dot_r, y1, dot_r * 2, dot_r * 2, color, region_x, region_w);
    }
}

/* -- 7-segment drawing ------------------------------------------------------ */

static const uint8_t SEG_MAP[10] = {
    0b0111111, /* 0 */
    0b0000110, /* 1 */
    0b1011011, /* 2 */
    0b1001111, /* 3 */
    0b1100110, /* 4 */
    0b1101101, /* 5 */
    0b1111101, /* 6 */
    0b0000111, /* 7 */
    0b1111111, /* 8 */
    0b1101111, /* 9 */
};

static void draw_digit(const FB *fb,
                       int digit,
                       uint32_t ox, uint32_t oy,
                       uint32_t seg_w, uint32_t seg_len,
                       uint32_t color_on, uint32_t color_off,
                       uint32_t region_x, uint32_t region_w)
{
    if (digit < 0 || digit > 9) return;
    uint8_t segs = SEG_MAP[digit];
    uint32_t gap = 2;

    struct { uint32_t x, y, w, h; } S[7];

    S[0] = (typeof(S[0])){ ox + seg_w + gap,           oy,                                    seg_len, seg_w   }; /* a top       */
    S[1] = (typeof(S[1])){ ox + seg_w + seg_len + gap,  oy + seg_w + gap,                      seg_w,  seg_len }; /* b top-right */
    S[2] = (typeof(S[2])){ ox + seg_w + seg_len + gap,  oy + seg_w + seg_len + gap * 2,        seg_w,  seg_len }; /* c bot-right */
    S[3] = (typeof(S[3])){ ox + seg_w + gap,            oy + seg_w * 2 + seg_len * 2 + gap * 2, seg_len, seg_w }; /* d bottom    */
    S[4] = (typeof(S[4])){ ox,                          oy + seg_w + seg_len + gap * 2,        seg_w,  seg_len }; /* e bot-left  */
    S[5] = (typeof(S[5])){ ox,                          oy + seg_w + gap,                      seg_w,  seg_len }; /* f top-left  */
    S[6] = (typeof(S[6])){ ox + seg_w + gap,            oy + seg_w + seg_len + gap,            seg_len, seg_w  }; /* g middle    */

    for (int i = 0; i < 7; i++) {
        uint32_t color = (segs & (1 << i)) ? color_on : color_off;
        fill_rect(fb, S[i].x, S[i].y, S[i].w, S[i].h, color, region_x, region_w);
    }
}

static void restore_cursor(void) {
    fprintf(stdout, "\033[?25h");
    fflush(stdout);
}

/* -- Main ------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    atexit(restore_cursor);
    typedef enum { FULL, LEFT, RIGHT } Side;
    Side side = FULL;
    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--left")  == 0) side = LEFT;
        else if (strcmp(argv[i], "--right") == 0) side = RIGHT;
    }

    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    FB fb = fb_open("/dev/fb0");

    uint32_t region_x = (side == RIGHT) ? fb.w / 2 : 0;
    uint32_t region_w = (side == FULL)  ? fb.w     : fb.w / 2;

    fprintf(stderr, "Region: x=%u  w=%u\n", region_x, region_w);

    /*
     * 10% padding on each side of the region.
     * The clock is drawn inside the inner 80% of the region width.
     */
    uint32_t pad     = region_w / 10;
    uint32_t inner_x = region_x + pad;
    uint32_t inner_w = region_w - pad * 2;

    /*
     * Auto-scale segment size to fill the padded area.
     * Proportions: seg_w = seg_len/5, spacing = seg_len/4, sep_w = seg_len/3
     * Shrink seg_len until 9 digits + 3 separators fit inside inner_w.
     */
    uint32_t seg_w, seg_len, spacing, sep_w, dot_r;
    uint32_t cell_w, cell_h, total_w;

    seg_len = inner_w / 9;
    do {
        seg_w   = seg_len / 5; if (seg_w < 2) seg_w = 2;
        spacing = seg_len / 4;
        sep_w   = seg_len / 3;
        dot_r   = seg_w  / 2; if (dot_r < 2) dot_r = 2;
        cell_w  = seg_w * 2 + seg_len + spacing;
        cell_h  = seg_w * 3 + seg_len * 2 + 10;
        total_w = cell_w * 9 + sep_w * 3;
        if (total_w > inner_w) seg_len--;
    } while (total_w > inner_w && seg_len > 1);

    /* Center horizontally within padded area, vertically on screen */
    uint32_t ox = inner_x + (inner_w > total_w ? (inner_w - total_w) / 2 : 0);
    uint32_t oy = (fb.h > cell_h) ? (fb.h - cell_h) / 2 : 0;

    fprintf(stderr, "seg_len=%u  total_w=%u  inner_w=%u\n", seg_len, total_w, inner_w);

    uint32_t COLOR_BG  = 0x00000000;
    uint32_t COLOR_ON  = 0x00FFFFFF;
    uint32_t COLOR_OFF = 0x00000000;
    uint32_t COLOR_SEP = 0x00FFFFFF;

    while (!g_quit) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm *t = localtime(&ts.tv_sec);

        int ms = (int)(ts.tv_nsec / 1000000);

        int digits[9] = {
            t->tm_hour / 10,
            t->tm_hour % 10,
            t->tm_min  / 10,
            t->tm_min  % 10,
            t->tm_sec  / 10,
            t->tm_sec  % 10,
            ms / 100,
            (ms / 10) % 10,
            ms % 10,
        };

        fill_rect(&fb, region_x, 0, region_w, fb.h, COLOR_BG, region_x, region_w);

        uint32_t cursor_x = ox;

        for (int i = 0; i < 9; i++) {
            draw_digit(&fb, digits[i], cursor_x, oy,
                       seg_w, seg_len, COLOR_ON, COLOR_OFF,
                       region_x, region_w);
            cursor_x += cell_w;

            if (i == 1 || i == 3) {
                draw_separator(&fb, cursor_x, oy, cell_h, dot_r, 1, COLOR_SEP, region_x, region_w);
                cursor_x += sep_w;
            } else if (i == 5) {
                draw_separator(&fb, cursor_x, oy, cell_h, dot_r, 0, COLOR_SEP, region_x, region_w);
                cursor_x += sep_w;
            }
        }

        fb_flip(&fb, region_x, region_w);
        usleep(1000);
    }

    fill_rect(&fb, region_x, 0, region_w, fb.h, COLOR_BG, region_x, region_w);
    fb_flip(&fb, region_x, region_w);

    fb_close(&fb);
    return EXIT_SUCCESS;
}
