cmake_minimum_required(VERSION 3.19)

project(
    StockChecker
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG"
    "${CMAKE_CURRENT_BINARY_DIR}/CommonLibSSE-NG"
)

include(
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake"
)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    NAME "Stock Checker"
    AUTHOR "Stock Checker"
    SOURCES src/main.cpp
)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/extern"
)

target_precompile_headers(
    ${PROJECT_NAME}
    PRIVATE <StockChecker/PCH.h>
)

set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}/package/SKSE/Plugins"
)
