cmake_minimum_required(VERSION 3.10)
project(control_loop C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra")

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(control_loop
    src/main.c
    src/model.c
)

target_link_libraries(control_loop m rt)