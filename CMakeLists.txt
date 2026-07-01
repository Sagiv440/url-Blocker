cmake_minimum_required(VERSION 3.16)
project(url-block CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(url-block
    src/main.cpp
    src/proxy.cpp
)

target_include_directories(url-block PRIVATE src)

if(MSVC)
    target_compile_options(url-block PRIVATE /W4 /O2)
else()
    target_compile_options(url-block PRIVATE
        -O2 -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3 -march=native>
    )
endif()

if(WIN32)
    target_link_libraries(url-block PRIVATE ws2_32 iphlpapi shell32)
else()
    target_link_libraries(url-block PRIVATE pthread)
endif()
