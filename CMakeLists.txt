cmake_minimum_required(VERSION 3.27.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(Server)

set(EXE_NAME "server")
add_executable(${EXE_NAME})

#include_directories("${CMAKE_SOURCE_DIR}/dependencies/include")
#target_compile_definitions(${GAME_NAME} PRIVATE GLEW_STATIC)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(
        ${EXE_NAME} PRIVATE
        -O1

        -pedantic -Wall -Wextra -Werror

        -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
        -Wno-unused-but-set-variable -Wno-unused-label -Wno-unused-private-field

        -fansi-escape-codes -fdiagnostics-color=always
    )
endif()
target_compile_features(${EXE_NAME} PUBLIC c_std_99)

file(GLOB_RECURSE EXE_SOURCES "src/*.c")
target_sources(${EXE_NAME} PRIVATE ${EXE_SOURCES})

#target_link_libraries(${GAME_NAME} "${CMAKE_SOURCE_DIR}/dependencies/libs/GLFW/glfw3.lib")
#target_link_libraries(${GAME_NAME} "${CMAKE_SOURCE_DIR}/dependencies/libs/GLEW/glew32s.lib")

#target_link_libraries(${GAME_NAME} opengl32.dll gdi32.dll user32.dll kernel32.dll)
