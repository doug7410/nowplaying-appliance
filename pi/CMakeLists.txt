# Pi build for the Board 2 display: the SHARED UI (../src/ui.cpp) on LVGL's Linux DRM
# (default) or fbdev driver + evdev touch, instead of the sim's SDL window. Build ON the
# Pi (aarch64) — no cross-compile. Reuses ../sim/backend.cpp (plain Linux libcurl) verbatim.
#   cmake -S pi -B pi/build && cmake --build pi/build -j   ->  pi/build/np-display
#   fbdev fallback:  -DNP_USE_FBDEV=ON
cmake_minimum_required(VERSION 3.16)
project(np_display C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NP_USE_FBDEV "Render via /dev/fb0 instead of DRM/KMS at runtime" OFF)

# Trim the build: no vector graphics / examples (matches the sim).
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

# lv_conf.h always enables the LVGL DRM driver, so the lvgl LIBRARY target compiles
# lv_linux_drm.c — which #includes <xf86drmMode.h> -> <drm.h>. Put libdrm's include dir
# (/usr/include/libdrm) on EVERY target globally, before FetchContent builds lvgl. Without
# this the lvgl lib fails "drm.h: No such file or directory" regardless of NP_USE_FBDEV.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED libdrm)
include_directories(${LIBDRM_INCLUDE_DIRS})

include(FetchContent)
FetchContent_Declare(lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.5.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)   # uses ./lv_conf.h via LV_CONF_INCLUDE_SIMPLE

# LVGL globs its SIMD blend asm (.S) into the lib; here the assembler chokes on it
# ("unknown mnemonic typedef"). The C blend path covers it — strip the .S files (same as
# ../sim/CMakeLists.txt). An album-art UI doesn't need NEON blend on a Pi 3B.
get_target_property(_lvgl_srcs lvgl SOURCES)
list(FILTER _lvgl_srcs EXCLUDE REGEX "\\.S$")
set_target_properties(lvgl PROPERTIES SOURCES "${_lvgl_srcs}")

# Real-data backend deps: libcurl + two single-header libs, downloaded into ../sim/third_party
# (shared with the sim so they're fetched once, never vendored into the repo).
find_package(CURL REQUIRED)
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../sim/third_party)
file(MAKE_DIRECTORY ${TP})
if(NOT EXISTS ${TP}/stb_image.h)
  message(STATUS "Downloading stb_image.h")
  file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image.h ${TP}/stb_image.h)
endif()
if(NOT EXISTS ${TP}/json.hpp)
  message(STATUS "Downloading nlohmann/json.hpp")
  file(DOWNLOAD https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp ${TP}/json.hpp)
endif()

add_executable(np-display main.cpp ../src/ui.cpp ../sim/backend.cpp
  ../src/fonts/montserrat_bold_44.c ../src/fonts/montserrat_semibold_24.c
  ../src/fonts/montserrat_semibold_18.c)
# NOTE: do NOT put ../sim on the include path — it holds the sim's SDL lv_conf.h, which
# (searched before lvgl's -isystem pi) would shadow pi/lv_conf.h and disable the DRM driver.
# main.cpp reaches backend.h via an explicit ../sim/ path instead.
target_include_directories(np-display PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src ${TP})
target_compile_definitions(np-display PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)   # font .c -> #include "lvgl.h"
target_link_libraries(np-display PRIVATE lvgl CURL::libcurl)

# Both display drivers are compiled into lvgl; NP_USE_FBDEV only picks the create() call
# in main.cpp. The DRM driver always links libdrm, so link it unconditionally.
target_link_libraries(np-display PRIVATE ${LIBDRM_LIBRARIES})
if(NP_USE_FBDEV)
  target_compile_definitions(np-display PRIVATE NP_USE_FBDEV)
endif()

# NOTE: build only this target — `cmake --build pi/build --target np-display` — so the
# unused lvgl_demos/examples targets aren't compiled.
