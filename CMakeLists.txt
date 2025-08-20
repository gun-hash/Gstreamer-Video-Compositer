cmake_minimum_required(VERSION 3.10)
project(video_compositor)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find GStreamer packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-base-1.0 gstreamer-video-1.0)

# Include directories
include_directories(${GST_INCLUDE_DIRS})
link_directories(${GST_LIBRARY_DIRS})

# Add compiler flags
add_definitions(${GST_CFLAGS_OTHER})

# Create executable
add_executable(video_compositor video_compositor.c)

# Link libraries
target_link_libraries(video_compositor ${GST_LIBRARIES})

# Set compiler flags
target_compile_options(video_compositor PRIVATE ${GST_CFLAGS_OTHER})
