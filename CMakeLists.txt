cmake_minimum_required(VERSION 3.11)
project(lv_demos)

add_library(lv_demos_ext STATIC)

file(GLOB_RECURSE DEMO_SOURCES "src/*.c")

target_sources(lv_demos_ext PRIVATE ${DEMO_SOURCES})

target_include_directories(lv_demos_ext PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# We assume LVGL is available from the parent project
target_link_libraries(lv_demos_ext PUBLIC lvgl cjson)
