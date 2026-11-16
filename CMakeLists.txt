cmake_minimum_required(VERSION 3.30)
project(png_reader)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    FetchContent_Declare(
            zlib
            GIT_REPOSITORY https://github.com/madler/zlib
            GIT_TAG v1.3.1
    )
    FetchContent_MakeAvailable(zlib)

    # make the fetched build look like the one find_package would give you
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
    target_include_directories(zlibstatic INTERFACE
            ${zlib_SOURCE_DIR}
            ${zlib_BINARY_DIR}
    )
endif()

FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 6.2
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE
        ZLIB::ZLIB
        raylib
)

target_compile_definitions(${PROJECT_NAME} PRIVATE RESOURCE_DIR="${PROJECT_SOURCE_DIR}/res")