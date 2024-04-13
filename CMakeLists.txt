cmake_minimum_required(VERSION 3.27.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(Server)

set(EXE_NAME "server")

include_directories(${CMAKE_SOURCE_DIR}/dep/include)

add_executable(${EXE_NAME})
target_compile_definitions(${EXE_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)

if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(
        ${EXE_NAME} PRIVATE
        -pedantic -Wall -Wextra -Werror

        -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
        -Wno-unused-but-set-variable -Wno-unused-label -Wno-unused-private-field

        -fansi-escape-codes -fdiagnostics-color=always
    )
endif()
target_compile_features(${EXE_NAME} PUBLIC c_std_99)

file(GLOB_RECURSE EXE_SOURCES "src/*.c")
target_sources(${EXE_NAME} PRIVATE ${EXE_SOURCES})
