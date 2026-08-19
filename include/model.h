
#ifndef MODEL_H
#define MODEL_H
 
#include <time.h>
 
#define DT_MS   10
#define DT_S    (DT_MS / 1000.0)
#define TAU     0.2      // motor time constant [s]
#define K       1.0      // motor static gain
#define KP      2.0
#define KI      5.0
#define REF     10.0     // target speed
#define N_ITERS 500       // 500 * 10ms = 5s of simulation
 
#define US_PER_SEC 1000000L
#define NS_PER_US  1000L
 
/* Per-iteration timing/telemetry sample. Kept as a flat struct in a
 * preallocated array so the control loop never touches malloc/free
 * or stdio while it's running. */
typedef struct {
    int    iter;
    long   wake_jitter_us;  /* wake_actual - scheduled wake ("next")        */
    long   period_us;       /* wake_actual[i] - wake_actual[i-1]            */
    long   sensor_us;       /* sensor_read() duration                       */
    long   control_us;      /* pi_step() duration                           */
    long   actuator_us;     /* actuator_write() duration                    */
    long   total_exec_us;   /* sensor_us + control_us + actuator_us         */
    int    overrun_period;  /* period_us     > DT_MS*1000                   */
    int    overrun_budget;  /* total_exec_us > DT_MS*1000                   */
    // User variables

    double speed;
    double voltage;
} sample_t;
 
/* Minimal aggregate for Phase 0. Phase 1 replaces/extends this with an
 * online percentile structure (HDR histogram) so a full sample array
 * isn't required for long/continuous runs. */

typedef struct {
    long   count;
    long   min_us;
    long   max_us;
    double mean_us;
} stat_summary_t;
 
double plant_step(double speed, double voltage, double dt); // Replace with actual plant and controller logic
double pi_step(double error, double *integral, double dt);
void   add_ms(struct timespec *t, long ms);
 
/* b - a, in microseconds */
long diff_us(struct timespec a, struct timespec b);
 
/* Stand-ins for real HW access. The signatures are deliberately shaped
 * around the physical quantity (sensed speed in / applied voltage out)
 * so that swapping in a real ADC/UART read or PWM/DAC write later does
 * not change the call sites or the instrumentation wrapped around them. */
double sensor_read(double speed_sim);
double actuator_write(double voltage_cmd);
 
void stat_summary(const long *values, long n, stat_summary_t *out);
 
#endif