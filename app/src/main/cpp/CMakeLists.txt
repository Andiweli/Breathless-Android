cmake_minimum_required(VERSION 3.10.2)
project(breathless_native)

# The game is a software renderer: an unoptimised -O0 debug library is several
# times slower and made OUYA test builds run at about 5 FPS. Debug builds keep
# their symbols but are optimised like release builds.
string(APPEND CMAKE_CXX_FLAGS_DEBUG " -O2")
add_library(breathless_native SHARED breathless_native.cpp)
set_target_properties(breathless_native PROPERTIES
        LINK_FLAGS "-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384")
find_library(log-lib log)
find_library(gles-lib GLESv2)
target_link_libraries(breathless_native ${log-lib} ${gles-lib})
