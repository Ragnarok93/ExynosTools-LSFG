cmake_minimum_required(VERSION 3.22.1)
project(phase3c_probe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(log-lib log REQUIRED)
find_library(vulkan-lib vulkan REQUIRED)

add_library(phase3c_probe SHARED probe.cpp)

target_link_libraries(phase3c_probe PRIVATE
    ${vulkan-lib}
    ${log-lib}
)
