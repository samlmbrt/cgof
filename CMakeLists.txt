cmake_minimum_required(VERSION 3.28)
project(cgof LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(SDL3 REQUIRED)

add_executable(cgof src/main.c)
target_link_libraries(cgof PRIVATE SDL3::SDL3)

target_compile_options(cgof PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(cgof PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(cgof PRIVATE
        -fsanitize=address,undefined
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(cgof PRIVATE
        -march=native -flto
    )
    target_link_options(cgof PRIVATE
        -flto
    )
endif()
