# Desktop SDL simulator for the Board 2 UI. Runs our LVGL UI in an 800x480 window
# (mouse = touch), so the look/layout can be iterated WITHOUT flashing the board.
# LVGL is pinned to v9.5.0 to match the firmware (../platformio.ini). NOTE: the desktop
# renderer cannot reproduce the panel's RGB-DMA/PSRAM flicker — that stays a hardware test.
cmake_minimum_required(VERSION 3.16)
project(board2_sim C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Trim the build: the music demo needs no vector graphics / examples.
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})   # so lvgl's SDL driver finds <SDL2/SDL.h>

include(FetchContent)
FetchContent_Declare(lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.5.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)   # uses ./lv_conf.h via LV_CONF_INCLUDE_SIMPLE

# LVGL globs ARM-only SIMD blend asm (helium/neon .S) into the lib; the x86 assembler
# chokes on it. Desktop doesn't need SIMD asm — strip the .S files, the C path covers it.
get_target_property(_lvgl_srcs lvgl SOURCES)
list(FILTER _lvgl_srcs EXCLUDE REGEX "\\.S$")
set_target_properties(lvgl PROPERTIES SOURCES "${_lvgl_srcs}")

# Real-data backend deps: libcurl (HTTP) + two single-header libs (image decode, JSON),
# downloaded on first configure so they're not vendored into the repo.
find_package(CURL REQUIRED)
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
file(MAKE_DIRECTORY ${TP})
if(NOT EXISTS ${TP}/stb_image.h)
  message(STATUS "Downloading stb_image.h")
  file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image.h ${TP}/stb_image.h)
endif()
if(NOT EXISTS ${TP}/json.hpp)
  message(STATUS "Downloading nlohmann/json.hpp")
  file(DOWNLOAD https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp ${TP}/json.hpp)
endif()

add_executable(sim main.cpp ../src/ui.cpp backend.cpp)
target_include_directories(sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src ${TP})
target_link_libraries(sim PRIVATE lvgl SDL2::SDL2 CURL::libcurl)
