#define _POSIX_C_SOURCE 200809L
#include "sysmon.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- CPU: /proc/stat -------------------------------------------------
 * First line format: "cpu  user nice system idle iowait irq softirq steal ..."
 * All fields are cumulative jiffie counts since boot, never instantaneous.
 * cpu_pct is therefore always an AVERAGE OVER THE INTERVAL between two
 * reads, not a live gauge -- see docs/measurements.md. */

 
static int read_cpu_jiffies(unsigned long long *busy, unsigned long long *total) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    unsigned long long user, nice_, system_, idle, iowait, irq, softirq, steal;
    int n = fscanf(f, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice_, &system_, &idle, &iowait, &irq, &softirq, &steal);
    fclose(f);
    if (n < 8) return -1;
    *busy  = user + nice_ + system_ + irq + softirq + steal;
    *total = *busy + idle + iowait;
    return 0;
}

/* ---- Memory: /proc/meminfo --------------------------------------------
 * A single read = a true instantaneous snapshot, unlike cpu_pct above.
 * MemAvailable (not MemFree) is used since it accounts for reclaimable
 * caches and is the more meaningful "how much can I actually use" figure. */
static int read_mem_kb(unsigned long *mem_total_kb, unsigned long *mem_avail_kb) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[256];
    int found = 0;
    *mem_total_kb = 0;
    *mem_avail_kb = 0;
    while (fgets(line, sizeof(line), f) && found < 2) {
        unsigned long val;
        if (sscanf(line, "MemTotal: %lu kB", &val) == 1)      { *mem_total_kb = val; found++; }
        else if (sscanf(line, "MemAvailable: %lu kB", &val) == 1) { *mem_avail_kb = val; found++; }
    }
    fclose(f);
    return (found == 2) ? 0 : -1;
}

/* ---- GPU: Jetson-generation-specific sysfs, auto-detected -------------
 * Sourcing (see docs/architecture.md for the full citations):
 *
 *   - Orin family (ga10b devfreq node): NVIDIA Jetson Linux Developer
 *     Guide (r35.6.2) states explicitly that the load value divided by
 *     10 gives load percentage. CONFIRMED.
 *   - Xavier family (gv11b devfreq node): same devfreq driver structure
 *     confirmed to exist (min_freq/max_freq/cur_freq siblings at the
 *     analogous path); the /10 scaling is ASSUMED by analogy with Orin,
 *     NOT independently confirmed for gv11b. VERIFY on hardware.
 *   - Nano/TX2 (gk20a, pre-devfreq "load" attribute): path confirmed via
 *     NVIDIA forum users reading it directly; scale factor NOT confirmed
 *     (may already be 0-100, or may be 0-1000 like the newer nodes).
 *     VERIFY on hardware.
 *
 * Ground-truth method for verifying/finding the correct node on any given
 * board, more reliable than any hardcoded guess (including the ones
 * below): tegrastats already reads the correct node, so
 *     sudo strace -f -e trace=openat tegrastats 2>&1 | grep load
 * will show exactly which file it opens on that specific board.
 */
typedef struct {
    const char *path;
    double      divisor;
} gpu_candidate_t;

static const gpu_candidate_t GPU_CANDIDATES[] = {
    { "/sys/devices/17000000.ga10b/devfreq/17000000.ga10b/load", 10.0 },
    { "/sys/devices/17000000.gv11b/devfreq/17000000.gv11b/load", 10.0 },
    { "/sys/devices/platform/host1x/gpu.0/load",                  1.0 },
    { "/sys/devices/gpu.0/load",                                   1.0 },
};
#define GPU_CANDIDATE_COUNT (sizeof(GPU_CANDIDATES) / sizeof(GPU_CANDIDATES[0]))

static int gpu_detect(char *path_out, size_t path_out_sz, double *divisor_out) {
    for (size_t i = 0; i < GPU_CANDIDATE_COUNT; i++) {
        FILE *f = fopen(GPU_CANDIDATES[i].path, "r");
        if (f) {
            fclose(f);
            strncpy(path_out, GPU_CANDIDATES[i].path, path_out_sz - 1);
            path_out[path_out_sz - 1] = '\0';
            *divisor_out = GPU_CANDIDATES[i].divisor;
            return 1;
        }
    }
    return 0;
}

static int gpu_read_pct(const char *path, double divisor, double *out_pct) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long raw;
    int n = fscanf(f, "%ld", &raw);
    fclose(f);
    if (n != 1) return -1;
    *out_pct = (double)raw / divisor;
    return 0;
}

/* ---- public API -------------------------------------------------------- */

void sysmon_init(sysmon_ctx_t *ctx, sysmon_sample_t *buf, long capacity) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->buf      = buf;
    ctx->capacity = capacity;
    ctx->count    = 0;
    atomic_init(&ctx->stop, 0);
    ctx->gpu_available = gpu_detect(ctx->gpu_path, sizeof(ctx->gpu_path), &ctx->gpu_scale_divisor);
}

void *sysmon_thread_main(void *arg) {
    sysmon_ctx_t *ctx = (sysmon_ctx_t *)arg;

    unsigned long long prev_busy = 0, prev_total = 0;
    int have_prev = 0;

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    while (!atomic_load(&ctx->stop) && ctx->count < ctx->capacity) {

        next.tv_nsec += SYSMON_POLL_MS * 1000000L;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec += 1; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        /* Note: if stop is set while sleeping here, it's observed on the
         * next loop check -- worst case ~SYSMON_POLL_MS extra shutdown
         * latency. Fine outside the control loop's hot path. */

        sysmon_sample_t s;
        memset(&s, 0, sizeof(s));
        clock_gettime(CLOCK_MONOTONIC, &s.ts);

        unsigned long long busy, total;
        if (read_cpu_jiffies(&busy, &total) == 0) {
            if (have_prev && total > prev_total) {
                s.cpu_pct = 100.0 * (double)(busy - prev_busy) / (double)(total - prev_total);
            } else {
                s.cpu_pct = -1.0; /* first sample: no valid delta yet */
            }
            prev_busy = busy;
            prev_total = total;
            have_prev = 1;
        } else {
            s.cpu_pct = -1.0;
        }

        unsigned long mem_total_kb, mem_avail_kb;
        if (read_mem_kb(&mem_total_kb, &mem_avail_kb) == 0 && mem_total_kb > 0) {
            s.mem_total_mb = mem_total_kb / 1024.0;
            s.mem_used_mb  = (mem_total_kb - mem_avail_kb) / 1024.0;
            s.mem_pct      = 100.0 * (double)(mem_total_kb - mem_avail_kb) / (double)mem_total_kb;
        } else {
            s.mem_total_mb = s.mem_used_mb = s.mem_pct = -1.0;
        }

        s.gpu_available = ctx->gpu_available;
        s.gpu_pct = -1.0;
        if (ctx->gpu_available) {
            gpu_read_pct(ctx->gpu_path, ctx->gpu_scale_divisor, &s.gpu_pct);
        }

        ctx->buf[ctx->count++] = s;
    }
    return NULL;
}

void sysmon_write_csv(const sysmon_ctx_t *ctx, const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp) { perror("fopen sysmon csv"); return; }
    fprintf(fp, "sample_time_s,cpu_pct,mem_used_mb,mem_total_mb,mem_pct,gpu_pct,gpu_available\n");
    for (long i = 0; i < ctx->count; i++) {
        const sysmon_sample_t *s = &ctx->buf[i];
        double t = (double)s->ts.tv_sec + (double)s->ts.tv_nsec / 1e9;
        fprintf(fp, "%.6f,%.2f,%.2f,%.2f,%.2f,%.2f,%d\n",
                t, s->cpu_pct, s->mem_used_mb, s->mem_total_mb, s->mem_pct,
                s->gpu_pct, s->gpu_available);
    }
    fclose(fp);
}