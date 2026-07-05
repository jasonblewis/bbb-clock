/*
 * p2stress.c — P2 glitch stress harness for the BBB TLC5947 clock.
 *
 * Purpose: raise the reproduction rate of the P2 runtime bright-channel glitch
 * so it is debuggable, and discriminate its cause (spontaneous VCC/ground vs.
 * triggered transfer/latch vs. SPI-clock/framing). See docs/diagnostic-plan.org
 * Phase 2 and README.org "* Findings".
 *
 * It reuses the exact SPI path of clock.c (open /dev/spidev1.0 O_WRONLY,
 * bits_per_word = 12, mode 0 by default, a single write() of uint16_t[48] with
 * CS acting as XLAT) so any glitch it provokes is the same physical mechanism
 * as the real clock — NOT a different code path.
 *
 * Grayscale is driven DIRECTLY into the buffer (0..4095), bypassing the ambient
 * sensor / BRIGHTNESS_FACTOR path, so a software brightness bug cannot be
 * mistaken for the hardware glitch (plan ground rule).
 *
 * IMPORTANT: the running `clock -t` service holds spidev1.0 open and refreshes
 * every 500 ms. Stop it first (`/etc/init.d/clock stop`) or the two will fight
 * over the display.
 *
 * Two arms (plan Phase 2):
 *   -a a  Arm A "static hold": fill all channels to --gray, write ONCE, then
 *         stop sending and hold. A glitch here (zero bus activity) = spontaneous
 *         corruption on the hold side (VCC/ground integrity).
 *   -a b  Arm B "hammer": rewrite+relatch all channels as fast as possible.
 *         A glitch here but NOT in Arm A = triggered by sending/latching.
 *         Sweep --speed (100000 / 1000000 / 4000000) to test framing/SI:
 *         a glitch rate that rises with SPI clock => framing / signal integrity.
 *
 * Ground rules honoured: drive DIM (default --gray 410 ~= 10% of 4095) because
 * the glitch jumps *upward* and full brightness hides it; log uptime and (if
 * exposed) temperature alongside each run.
 *
 * Build on-device (native, like the original clock). The BBB has gcc 4.6.3
 * (Debian 7 wheezy, EGLIBC 2.13), which predates -std=gnu11, so use gnu99:
 *     gcc -std=gnu99 -O2 -Wall -o p2stress p2stress.c
 * Do NOT cross-compile from a modern host: its glibc is far newer than the
 * BBB's 2.13 and the binary would not run.
 */

#define _DEFAULT_SOURCE
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#define TLC5947_COUNT     2
#define TLC5947_CHANNELS  24
#define CHANNELS          (TLC5947_COUNT * TLC5947_CHANNELS)  /* 48 */
#define BPW               12          /* bits per word — matches clock.c */
#define GS_MAX            0x0FFF      /* 12-bit grayscale ceiling */
#define DIM_DEFAULT       410         /* ~10% of 4095 */

static const char *SPIDEV = "/dev/spidev1.0";

static int   file = -1;
static uint16_t buf[CHANNELS];
static volatile sig_atomic_t stop_flag = 0;

static void on_signal(int sig) { (void)sig; stop_flag = 1; }

/* ---- environment logging (plan: log temp + uptime per run) ---- */

static double read_uptime(void) {
    FILE *f = fopen("/proc/uptime", "r");
    double up = -1.0;
    if (f) { if (fscanf(f, "%lf", &up) != 1) up = -1.0; fclose(f); }
    return up;
}

/* BBB 3.8 kernels often do NOT expose a thermal zone; return -1000 if absent. */
static double read_temp_c(void) {
    static const char *paths[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/hwmon/hwmon0/temp1_input",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "r");
        if (!f) continue;
        long milli = 0;
        int ok = (fscanf(f, "%ld", &milli) == 1);
        fclose(f);
        if (ok) return milli / 1000.0;
    }
    return -1000.0;
}

static void log_env(const char *tag) {
    double up = read_uptime();
    double t  = read_temp_c();
    if (t <= -999.0)
        fprintf(stderr, "[env %s] uptime=%.0fs temp=n/a\n", tag, up);
    else
        fprintf(stderr, "[env %s] uptime=%.0fs temp=%.1fC\n", tag, up, t);
    fflush(stderr);
}

/* ---- SPI setup: identical semantics to clock.c spi_init() ---- */

static int spi_open(uint32_t speed, uint8_t mode) {
    uint8_t  bpw = BPW;
    uint32_t rd_speed = 0;
    uint8_t  rd_bpw = 0, rd_mode = 0;

    file = open(SPIDEV, O_WRONLY);
    if (file < 0) { perror("open spidev1.0"); return -1; }

    if (ioctl(file, SPI_IOC_WR_MODE, &mode) < 0)
        perror("WR_MODE (continuing)");
    if (ioctl(file, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
        perror("WR_MAX_SPEED_HZ (continuing)");
    if (ioctl(file, SPI_IOC_WR_BITS_PER_WORD, &bpw) < 0)
        perror("WR_BITS_PER_WORD (continuing)");

    /* read back what actually took on this fd */
    ioctl(file, SPI_IOC_RD_MAX_SPEED_HZ, &rd_speed);
    ioctl(file, SPI_IOC_RD_BITS_PER_WORD, &rd_bpw);
    ioctl(file, SPI_IOC_RD_MODE, &rd_mode);
    fprintf(stderr, "[spi] fd=%d speed=%u Hz bpw=%u mode=%u\n",
            file, rd_speed, rd_bpw, rd_mode);
    if (rd_bpw != BPW)
        fprintf(stderr, "[spi] WARNING: bpw readback %u != %d — framing will be wrong\n",
                rd_bpw, BPW);
    fflush(stderr);
    return 0;
}

/* one frame = shift 48 x 12 bits (2 x 288) then latch on CS deassert */
static int frame_write(void) {
    int n = (int)(sizeof(buf[0]) * CHANNELS);
    int w = (int)write(file, &buf, n);
    if (w != n) { perror("write spi"); return -1; }
    return 0;
}

static void fill_all(uint16_t v) {
    for (int i = 0; i < CHANNELS; i++) buf[i] = v & GS_MAX;
}

/* ---- Arm A: static hold ---- */
static int arm_a(uint16_t gray, long total_secs, int hb) {
    fprintf(stderr, "[arm A] static hold: all %d channels = %u, write once then hold\n",
            CHANNELS, (unsigned)(gray & GS_MAX));
    fill_all(gray);
    log_env("A-before-write");
    if (frame_write() < 0) return 1;
    log_env("A-after-write");
    fprintf(stderr, "[arm A] holding. NO further SPI activity. Watch/film the DIM display.\n");
    fprintf(stderr, "[arm A] any bright jump now = spontaneous (VCC/ground) corruption.\n");
    fflush(stderr);

    time_t start = time(NULL);
    long last_hb = 0;
    while (!stop_flag) {
        sleep(1);
        long el = (long)(time(NULL) - start);
        if (hb > 0 && el - last_hb >= hb) { last_hb = el; log_env("A-hold"); }
        if (total_secs > 0 && el >= total_secs) break;
    }
    fprintf(stderr, "[arm A] done after %lds.\n", (long)(time(NULL) - start));
    return 0;
}

/* ---- Arm B: hammer ---- */
enum { PAT_CYCLE, PAT_AA55, PAT_STEADY };

static const char *pat_name(int p) {
    return p == PAT_AA55 ? "aa55" : p == PAT_STEADY ? "steady" : "cycle";
}

static int arm_b(uint16_t gray, int pattern, long total_secs, int hb) {
    fprintf(stderr, "[arm B] hammer: rewrite+relatch as fast as possible, pattern=%s\n",
            pat_name(pattern));
    fprintf(stderr, "[arm B] a glitch here but not in Arm A = triggered by sending/latching.\n");
    if (pattern == PAT_STEADY)
        fprintf(stderr, "[arm B] steady: same DIM frame re-latched at max rate; image looks calm,\n"
                        "        so any bright FLASH = a latch-triggered glitch (rate scales with hammering).\n");
    fill_all(gray);   /* initial frame; PAT_STEADY keeps this unchanged */
    log_env("B-start");

    time_t start = time(NULL);
    long last_hb = 0;
    unsigned long frames = 0;
    int toggle = 0;

    while (!stop_flag) {
        if (pattern == PAT_AA55) {
            fill_all(toggle ? 0x0555 : 0x0AAA);   /* flip every bit each frame */
        } else if (pattern == PAT_CYCLE) {
            fill_all(toggle ? 0 : gray);          /* dim <-> off each frame */
        }
        /* PAT_STEADY: leave buf at gray — hammer the latch, not the data */
        toggle ^= 1;
        if (frame_write() < 0) return 1;
        frames++;

        long el = (long)(time(NULL) - start);
        if (hb > 0 && el - last_hb >= hb) {
            last_hb = el;
            double rate = el > 0 ? (double)frames / el : 0.0;
            fprintf(stderr, "[arm B] t=%lds frames=%lu rate=%.0f/s ", el, frames, rate);
            log_env("B-run");
        }
        if (total_secs > 0 && el >= total_secs) break;
    }
    double el = (double)(time(NULL) - start);
    fprintf(stderr, "[arm B] done: %lu frames in %.0fs (%.0f/s)\n",
            frames, el, el > 0 ? frames / el : 0.0);
    return 0;
}

static void usage(const char *me) {
    fprintf(stderr,
      "Usage: %s -a <a|b> [options]\n"
      "  -a a|b     arm: a=static hold, b=hammer (required)\n"
      "  -g N       grayscale 0..4095 (default %d ~= 10%%). DIM on purpose.\n"
      "  -s HZ      SPI speed (default 1000000). Sweep 100000/1000000/4000000.\n"
      "  -p cycle|aa55|steady  Arm B pattern (default cycle):\n"
      "               cycle  = dim<->off each frame (max data toggling)\n"
      "               aa55   = 0xAAA<->0x555 each frame (flip every bit; framing/SI stress)\n"
      "               steady = same DIM frame re-latched at max rate (best glitch VISIBILITY;\n"
      "                        isolates latch-triggered corruption)\n"
      "  -m N       SPI mode 0..3 (default 0)\n"
      "  -T SECS    stop after SECS (default: run until Ctrl-C)\n"
      "  -H SECS    heartbeat/env-log interval (default 30; 0=off)\n"
      "\n"
      "STOP THE CLOCK FIRST:  /etc/init.d/clock stop\n"
      "Watch/film the DIM display; note which digits glitch (Chip A = right 3, Chip B = left+colon).\n",
      me, DIM_DEFAULT);
}

int main(int argc, char **argv) {
    int   arm = 0;                 /* 'a' or 'b' */
    long  gray = DIM_DEFAULT;
    uint32_t speed = 1000000;
    int   pattern = PAT_CYCLE;
    uint8_t mode = 0;
    long  total_secs = 0;
    int   hb = 30;

    int c;
    while ((c = getopt(argc, argv, "a:g:s:p:m:T:H:h")) != -1) {
        switch (c) {
        case 'a': arm = optarg[0]; break;
        case 'g': gray = strtol(optarg, NULL, 0); break;
        case 's': speed = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'p':
            pattern = strcmp(optarg, "aa55") == 0   ? PAT_AA55
                    : strcmp(optarg, "steady") == 0 ? PAT_STEADY
                    : PAT_CYCLE;
            break;
        case 'm': mode = (uint8_t)strtoul(optarg, NULL, 0); break;
        case 'T': total_secs = strtol(optarg, NULL, 0); break;
        case 'H': hb = (int)strtol(optarg, NULL, 0); break;
        case 'h': default: usage(argv[0]); return (c == 'h') ? 0 : 2;
        }
    }
    if (arm != 'a' && arm != 'b') { usage(argv[0]); return 2; }
    if (gray < 0) gray = 0;
    if (gray > GS_MAX) gray = GS_MAX;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (spi_open(speed, mode) < 0) return 1;

    int rc = (arm == 'a')
        ? arm_a((uint16_t)gray, total_secs, hb)
        : arm_b((uint16_t)gray, pattern, total_secs, hb);

    /* leave the last frame latched on exit (do not blank) so a held glitch
     * stays visible for inspection; the clock service will reclaim on restart. */
    close(file);
    return rc;
}
