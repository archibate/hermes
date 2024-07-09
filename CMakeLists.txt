cmake_minimum_required(VERSION 3.12)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 20)

project(hermes LANGUAGES CXX)

add_executable(hermes main.cpp hermes.cpp)
# target_compile_options(hermes PRIVATE -march=native)
