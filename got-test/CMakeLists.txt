cmake_minimum_required(VERSION 4.0)
project(got_test C)

set(CMAKE_C_STANDARD 99)

add_executable(got_test main.c
        victim.c
        inject_hook.c)
