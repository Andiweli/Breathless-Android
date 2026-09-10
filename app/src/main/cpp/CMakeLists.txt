cmake_minimum_required(VERSION 3.10.2)
project(breathless_native)

add_library(breathless_native SHARED breathless_native.cpp)
set_target_properties(breathless_native PROPERTIES
        LINK_FLAGS "-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384")
find_library(log-lib log)
find_library(gles-lib GLESv2)
target_link_libraries(breathless_native ${log-lib} ${gles-lib})
