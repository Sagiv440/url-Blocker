cmake_minimum_required(VERSION 3.16)
project(urlblocker CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(urlblocker
    src/main.cpp
    src/proxy.cpp
)

target_include_directories(urlblocker PRIVATE src)

target_compile_options(urlblocker PRIVATE
    -O2 -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3 -march=native>
)

target_link_libraries(urlblocker PRIVATE pthread)
