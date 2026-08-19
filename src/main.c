#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "model.h"



int main(void) {
    double speed    = 0.0;
    double voltage  = 0.0;
    double integral = 0.0;

    struct timespec next, wake_actual, prev_wake;
    struct timespec t_s0, t_s1, t_c0, t_c1, t_a0, t_a1;

    /* static: avoids a ~500-entry stack allocation and guarantees the
     * loop below does zero heap traffic. */
    static sample_t samples[N_ITERS];

    clock_gettime(CLOCK_MONOTONIC, &next);
    prev_wake = next;

    for (int i = 0; i < N_ITERS; i++) {

        /* --- Wait for the tick, then work. This is the standard
         * periodic-RT-task idiom (wake -> read inputs -> compute ->
         * write outputs -> advance schedule)*/


        add_ms(&next, DT_MS);
        int rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        if (rc != 0){
          printf("Error: nanosleep \n");
          return -1;
        }

        clock_gettime(CLOCK_MONOTONIC, &wake_actual);

        long jitter_us = diff_us(next, wake_actual);        /* actual - target   */
        long period_us = diff_us(prev_wake, wake_actual);    /* wake[i] - wake[i-1] */
        prev_wake = wake_actual;

        clock_gettime(CLOCK_MONOTONIC, &t_s0);
        double sensed_speed = sensor_read(speed);
        double sensed_voltage = sensor_read(voltage);
        clock_gettime(CLOCK_MONOTONIC, &t_s1);

        clock_gettime(CLOCK_MONOTONIC, &t_c0);
        double error   = REF - sensed_speed;
        double voltage = pi_step(error, &integral, DT_S);
        clock_gettime(CLOCK_MONOTONIC, &t_c1);

        clock_gettime(CLOCK_MONOTONIC, &t_a0);
        double applied_voltage = actuator_write(voltage);
        clock_gettime(CLOCK_MONOTONIC, &t_a1);

        /* Simulation-only: advances the virtual plant so there's a
         * speed to sense next tick. This call (and only this call)
         * disappears once actuator_write() drives a real actuator
         * and the plant evolves in real time instead of in code. */

        speed = plant_step(speed, applied_voltage, DT_S);

        long sensor_us   = diff_us(t_s0, t_s1);
        long control_us  = diff_us(t_c0, t_c1);
        long actuator_us = diff_us(t_a0, t_a1);
        long total_us    = sensor_us + control_us + actuator_us;

        sample_t *s        = &samples[i];
        s->iter            = i;
        s->wake_jitter_us  = jitter_us;
        s->period_us       = period_us;
        s->sensor_us       = sensor_us;
        s->control_us      = control_us;
        s->actuator_us     = actuator_us;
        s->total_exec_us   = total_us;
        s->overrun_period  = period_us > (DT_MS * 1000L);
        s->overrun_budget  = total_us   > (DT_MS * 1000L);
        s->speed           = speed;
        s->voltage         = voltage;
    }

    /* ==================================================================
     * Everything from here on runs AFTER the timing-critical section.
     * No stdio, no malloc, nothing schedule-sensitive happened above.
     * ================================================================== */

    FILE *fp = fopen("profile_run.csv", "w");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }
    fprintf(fp,
        "iter,wake_jitter_us,period_us,sensor_us,control_us,actuator_us,"
        "total_exec_us,overrun_period,overrun_budget,speed,voltage\n");
    for (int i = 0; i < N_ITERS; i++) {
        sample_t *s = &samples[i];
        fprintf(fp, "%d,%ld,%ld,%ld,%ld,%ld,%ld,%d,%d,%.6f,%.6f\n",
                s->iter, s->wake_jitter_us, s->period_us,
                s->sensor_us, s->control_us, s->actuator_us, s->total_exec_us,
                s->overrun_period, s->overrun_budget, s->speed, s->voltage);
    }
    fclose(fp);

    long *period_buf = malloc(N_ITERS * sizeof(long));
    long *jitter_buf = malloc(N_ITERS * sizeof(long));
    long *total_buf  = malloc(N_ITERS * sizeof(long));
    if (!period_buf || !jitter_buf || !total_buf) {
        fprintf(stderr, "malloc failed for summary buffers\n");
        return 1;
    }

    long overrun_period_count = 0, overrun_budget_count = 0;
    for (int i = 0; i < N_ITERS; i++) {
        period_buf[i] = samples[i].period_us;
        jitter_buf[i] = samples[i].wake_jitter_us;
        total_buf[i]  = samples[i].total_exec_us;
        overrun_period_count += samples[i].overrun_period;
        overrun_budget_count += samples[i].overrun_budget;
    }

    stat_summary_t st_period, st_jitter, st_total;
    stat_summary(period_buf, N_ITERS, &st_period);
    stat_summary(jitter_buf, N_ITERS, &st_jitter);
    stat_summary(total_buf,  N_ITERS, &st_total);

    printf("=== Run summary (N=%d, DT_MS=%d) ===\n", N_ITERS, DT_MS);
    printf("period_us       min=%-6ld max=%-6ld mean=%.1f\n", st_period.min_us, st_period.max_us, st_period.mean_us);
    printf("wake_jitter_us  min=%-6ld max=%-6ld mean=%.1f\n", st_jitter.min_us, st_jitter.max_us, st_jitter.mean_us);
    printf("total_exec_us   min=%-6ld max=%-6ld mean=%.1f\n", st_total.min_us,  st_total.max_us,  st_total.mean_us);
    printf("overrun_period : %ld / %d (%.2f%%)\n", overrun_period_count, N_ITERS, 100.0 * overrun_period_count / N_ITERS);
    printf("overrun_budget : %ld / %d (%.2f%%)\n", overrun_budget_count, N_ITERS, 100.0 * overrun_budget_count / N_ITERS);
    printf("Raw per-iteration samples written to profile_run.csv\n");

    free(period_buf);
    free(jitter_buf);
    free(total_buf);

    return 0;
}