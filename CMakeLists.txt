cmake_minimum_required(VERSION 3.10)
project(myshell CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# for good code
add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(myshell src/main.cpp)