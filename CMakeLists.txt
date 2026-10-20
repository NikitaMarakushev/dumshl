cmake_minimum_required(VERSION 3.10)
project(dumshl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For good code
add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(dumshl 
    src/main.cpp
    src/parser.cpp
    src/builtins.cpp
    src/executor.cpp
)