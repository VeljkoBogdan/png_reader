cmake_minimum_required(VERSION 3.30)
project(png_reader)

set(CMAKE_CXX_STANDARD 20)

set(SOURCES
    main.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    RESOURCE_DIR="${PROJECT_NAME}/res"
)
