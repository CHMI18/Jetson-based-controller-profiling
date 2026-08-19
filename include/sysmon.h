#ifndef SYSMON_H
#define SYSMON_H

#include <time.h>
#include <stdatomic.h>

/* Polling period for the monitor thread. Deliberately much coarser than
 * the control loop's DT_MS — see docs/architecture.md for why this must
 * run on its own thread at its own, slower cadence. */
#define SYSMON_POLL_MS 100

typedef struct {
    struct timespec ts;        /* CLOCK_MONOTONIC, same clock/epoch as the
                                 * control loop's wake_time_s, so the two
                                 * streams can be joined by absolute time */
    double cpu_pct;            /* -1.0 = not yet available (first sample) */
    double mem_used_mb;
    double mem_total_mb;
    double mem_pct;
    double gpu_pct;            /* -1.0 = GPU node unavailable/undetected */
    int    gpu_available;
} sysmon_sample_t;

typedef struct {
    sysmon_sample_t *buf;       /* caller-owned, preallocated */
    long             capacity;
    long             count;     /* written only by the monitor thread */
    atomic_int       stop;      /* written by main thread, read by monitor thread */

    char   gpu_path[256];
    double gpu_scale_divisor;   /* raw sysfs value / divisor = percent */
    int    gpu_available;
} sysmon_ctx_t;

/* Prepares ctx, attempts GPU sysfs auto-detection (see sysmon.c for the
 * candidate list and sourcing). Safe to call even if no GPU node is found
 * -- gpu_available will simply be 0 and all gpu_pct samples will read -1. */
void sysmon_init(sysmon_ctx_t *ctx, sysmon_sample_t *buf, long capacity);

/* pthread entry point. arg must be a sysmon_ctx_t*. Runs until
 * atomic_store(&ctx->stop, 1) is observed or capacity is reached. */
void *sysmon_thread_main(void *arg);

void sysmon_write_csv(const sysmon_ctx_t *ctx, const char *path);

#endif