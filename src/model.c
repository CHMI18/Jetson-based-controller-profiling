#include "model.h"
#include <stddef.h>
 
double plant_step(double speed, double voltage, double dt) {
    double dspeed = (-speed + K * voltage) / TAU;
    return speed + dspeed * dt;
}
 
double pi_step(double error, double *integral, double dt) {
    *integral += error * dt;
    return KP * error + KI * (*integral);
}
 
void add_ms(struct timespec *t, long ms) {
    t->tv_nsec += ms * 1000000L;
    while (t->tv_nsec >= 1000000000L) {
        t->tv_nsec -= 1000000000L;
        t->tv_sec  += 1;
    }
}
 
long diff_us(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * US_PER_SEC
         + (b.tv_nsec - a.tv_nsec) / NS_PER_US;
}
 
/* --- HW stubs ---------------------------------------------------------
 * Replace the bodies below with real I/O (UART/SPI/I2C read,
 * GPIO/PWM/DAC write) once you move off the closed-loop simulation.
 * Keep the signatures stable so main.c's instrumentation doesn't need
 * to change when the stub is swapped for a real driver call. */

double sensor_read(double speed_sim) {
    return speed_sim;
}
 
double actuator_write(double voltage_cmd) {
    return voltage_cmd;
}
 
void stat_summary(const long *values, long n, stat_summary_t *out) {
    out->count = n;
    if (n <= 0) {
        out->min_us  = 0;
        out->max_us  = 0;
        out->mean_us = 0.0;
        return;
    }
    long mn = values[0], mx = values[0];
    double sum = 0.0;
    for (long i = 0; i < n; i++) {
        if (values[i] < mn) mn = values[i];
        if (values[i] > mx) mx = values[i];
        sum += (double)values[i];
    }
    out->min_us  = mn;
    out->max_us  = mx;
    out->mean_us = sum / (double)n;
}