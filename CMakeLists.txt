cmake_minimum_required(VERSION 3.27.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(Server)

set(EXE_NAME "server")

include_directories(${CMAKE_SOURCE_DIR}/dep)

add_executable(${EXE_NAME})
target_compile_definitions(${EXE_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)

set(CMAKE_C_FLAGS_DEBUG "-O1")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    #-pedantic -Wall -Wextra -Werror

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter -Wno-unused-variable")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function -Wno-unused-but-set-variable")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-label -Wno-unused-private-field")

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fansi-escape-codes -fdiagnostics-color=always")
endif()
target_compile_features(${EXE_NAME} PUBLIC c_std_99)

file(GLOB_RECURSE EXE_SOURCES "src/*.c")
target_sources(${EXE_NAME} PRIVATE ${EXE_SOURCES})

file(GLOB_RECURSE SHA1_SOURCES "dep/sha1/*.c")
target_sources(${EXE_NAME} PRIVATE ${SHA1_SOURCES})
