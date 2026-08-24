cmake_minimum_required(VERSION 3.10.2)
project(breathless_native)

add_library(breathless_native SHARED breathless_native.cpp)
find_library(log-lib log)
find_library(gles-lib GLESv2)
target_link_libraries(breathless_native ${log-lib} ${gles-lib})
