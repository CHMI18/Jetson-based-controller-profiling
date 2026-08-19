# jetson-rt-profiler

**Real-time performance profiling for Jetson-based controllers.**

## What this is

`jetson-rt-profiler` instruments a Jetson-based real-time control loop the
way a PLC reports its own scan cycle: it measures how consistently the
loop actually runs, how long each stage of work takes, whether it ever
misses its deadline, and what the board's CPU/GPU/memory were doing at
the time. The goal is to turn "the controller seems to work" into a
quantified, presentable set of key performance indicators — jitter,
period, per-stage execution time, overrun rate and magnitude, and
resource utilization — rather than relying on informal observation.

## Why this exists

A Jetson is not a PLC: there is no dedicated real-time scan engine, the OS
scheduler is general-purpose Linux, and the control algorithm itself can
range from a fixed-cost PI loop to a variable-cost MPC or deep-learning
inference step. That variability is exactly what needs to be measured
rather than assumed — a controller that meets its deadline in testing can
silently start missing it once compute load, thermal state, or background
system load changes. This tool exists to catch that, attribute it to a
specific cause (scheduling delay vs. execution cost vs. resource
contention), and produce numbers suitable for a report rather than an
anecdote.

## How it works

Two concerns are deliberately kept separate, so that the act of measuring
the system doesn't distort what's being measured.

**1. Control-loop timing — near-zero overhead.**
The loop uses absolute, monotonic-clock scheduling
(`clock_nanosleep` + `CLOCK_MONOTONIC` + `TIMER_ABSTIME`) instead of
relative sleeps, so the schedule doesn't drift with execution time. Every
iteration is timestamped at wake-up and at the boundary of each work
stage (sensor read → control compute → actuator write) using
`clock_gettime`, which reads a hardware counter through the kernel's vDSO
fast path — on the order of tens of nanoseconds per call, negligible
against a millisecond-scale control period. From these timestamps, two
conceptually different quantities are derived:

- **Jitter** — actual wake time vs. the wake time that was scheduled in
  advance. A pure scheduling-fidelity signal; it says nothing about your
  own code.
- **Period** — actual wake time vs. the previous actual wake time. What
  the plant being controlled actually experiences, independent of what
  was planned.

An iteration is flagged as an overrun either because the loop's
real-world cadence broke down (`overrun_period`) or because the code's
own execution time alone would have exceeded the budget even with
perfect scheduling (`overrun_budget`) — two different failure modes with
two different fixes.

**2. System resource monitoring — decoupled, lower rate.**
CPU, GPU, and memory usage are polled from a separate thread at a much
lower rate than the control loop, because reading `/proc/stat`,
`/proc/meminfo`, or a GPU sysfs node involves real, unpredictable-cost
syscalls that must never run on the control loop's own thread. This
stream carries its own timestamps and is joined against the control-loop
data afterward by absolute time, not by iteration index.

Full field-by-field definitions and the reasoning behind each design
choice live in [`docs/measurements.md`](docs/measurements.md) and
[`docs/architecture.md`](docs/architecture.md) — this README is the
summary; those are the reference.

## Current status

| Phase | Scope | Status |
|---|---|---|
| 0 | Per-iteration timing instrumentation: wake jitter, period, sensor/control/actuator stage timing, overrun detection | Implemented (`src/`) |
| 1 | Statistical summary module (min, max, mean, p50/p90/p95/p99/p99.9, overrun rate) | Planned |
| 2 | CPU / GPU / memory polling on a decoupled monitor thread | In progress |
| 3 | Real-time scheduling hardening (SCHED_FIFO, CPU affinity, `mlockall`, `jetson_clocks`) | Planned |
| 4 | Output pipeline / reporting | Planned |

## Repository layout

```
src/     control loop, PI controller + plant model, timing helpers (Phase 0)
tools/   standalone diagnostics (e.g. clock_gettime overhead benchmark)
docs/    design rationale and field-by-field measurement definitions
data/    CSV output lands here at runtime (gitignored)
```

## Building

```
make          # builds bin/profiler
./bin/profiler

make bench    # builds bin/bench_clock (clock_gettime overhead microbenchmark)
./bin/bench_clock
```

`bin/profiler` writes one row per control-loop iteration to `data/profile_run.csv`.

## Target hardware

Mirrored onto the target Jetson board with the same repository structure.
CPU/memory instrumentation (Phase 2) is portable across boards; GPU
instrumentation is Jetson-generation-specific (the sysfs load path
differs across JetPack/L4T releases) and is pending confirmation against
the exact board/JetPack version in use.
