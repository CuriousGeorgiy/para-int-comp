cmake_minimum_required(VERSION 3.16)

project(para-int-comp C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE RelWithDebInfo)

add_executable(para-int-comp main.c)
target_compile_definitions(para-int-comp PRIVATE _GNU_SOURCE)
target_link_libraries(para-int-comp m pthread)
target_compile_options(para-int-comp PRIVATE -O2)
