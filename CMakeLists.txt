cmake_minimum_required(VERSION 3.7)
project(ffdup)

set(CMAKE_C_STANDARD 11)
string(APPEND CMAKE_C_FLAGS "-Wall -Wextra -Werror")

add_executable(ffdup
        main.c
)
