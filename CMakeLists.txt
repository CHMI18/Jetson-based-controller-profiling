cmake_minimum_required(VERSION 3.10)
project(jetson_rt_profiler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")

add_compile_options(-Wall -Wextra)

# Binaries land in bin/ at the repo root regardless of where the build/
# directory lives -- keeps README.md's usage instructions unchanged
# whether you build with CMake or (previously) with the plain Makefile.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# pthread, portably (CMake picks -pthread vs -lpthread as appropriate
# for the platform/compiler instead of hardcoding one or the other).
find_package(Threads REQUIRED)

add_executable(profiler
    src/main.c
    src/model.c
    src/sysmon.c
)
target_link_libraries(profiler PRIVATE Threads::Threads)

# EXCLUDE_FROM_ALL: not built by a plain `cmake --build build`, matching
# the old Makefile's `make` (builds profiler only) vs `make bench`
# (builds the benchmark) distinction. Build it explicitly with:
#   cmake --build build --target bench_clock
#
# Guarded with EXISTS so that a missing optional diagnostic tool degrades
# to a warning instead of failing configuration for the whole project --
# `profiler` (the actual deliverable) must still configure and build even
# if tools/bench_clock.c isn't present in a given checkout.
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/bench_clock.c")
    add_executable(bench_clock EXCLUDE_FROM_ALL
        tools/bench_clock.c
    )
else()
    message(WARNING "tools/bench_clock.c not found -- skipping bench_clock target. "
                     "profiler is unaffected. Copy tools/bench_clock.c into place "
                     "and re-run 'cmake -B build -S .' to enable it.")
endif()