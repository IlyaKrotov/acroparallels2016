cmake_minimum_required(VERSION 3.4)
project(BizzBuzz)

set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -std=c99")

set(SOURCE_FILES bizzbuzz.c)
add_executable(BizzBuzz ${SOURCE_FILES})