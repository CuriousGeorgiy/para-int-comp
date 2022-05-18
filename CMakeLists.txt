cmake_minimum_required(VERSION 2.8)

project(para-int-comp C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(para-int-comp main.c)
target_compile_definitions(para-int-comp PRIVATE _GNU_SOURCE NDEBUG)
target_link_libraries(para-int-comp m pthread)
