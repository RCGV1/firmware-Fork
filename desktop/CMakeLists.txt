# SPDX-License-Identifier: GPL-3.0-or-later
# wadamesh in the browser: the real touch UI (src/ui-touch) built for a host OS.
#
#   web/build.sh host   -> build-host/wadamesh-host  (native, SDL2 window)
#   web/build.sh web    -> build-web/wadamesh.{js,wasm} + dist/ (Emscripten, same CMake via emcmake)
#
# Layout (see WEB_BUILD.md at the repo root):
#   third_party/  pinned sources fetched by fetch-deps.sh (gitignored)
#   compat/       Arduino / ESP-IDF / FreeRTOS surface on a host OS
#   mesh/         MyMesh stand-in (the UI's compile-time contract) + backends
#   platform/     SDL2 display/input, the "web" board globals, main()
cmake_minimum_required(VERSION 3.20)
project(wadamesh_meshtastic_desktop C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(DESKTOP ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(FIRMWARE "${DESKTOP}/.." ABSOLUTE)
set(REPO ${FIRMWARE}/lib/WadaApp)
set(WADA_WEB_ROOT "${DESKTOP}/.deps/wadamesh/web" CACHE PATH "Pinned WADAMESH host support and third-party dependencies")
set(WEB ${WADA_WEB_ROOT})
set(TP ${WEB}/third_party)
set(SRC ${REPO}/vendor)
set(UI ${SRC}/ui-touch)
set(CORE ${REPO}/core)
set(NANOPB ${FIRMWARE}/.pio/libdeps/t-deck-tft/Nanopb)

if(NOT EXISTS ${CORE}/Mesh.h OR NOT EXISTS ${TP}/lvgl/lvgl.h)
  message(FATAL_ERROR "third-party sources missing: run desktop/fetch-deps.sh or set WADA_WEB_ROOT")
endif()

# ---- defines every translation unit sees -------------------------------------
set(WADA_COMMON_DEFS
  WADA_WEB=1                 # the board macro (src/ui-touch/device_caps.h)
  ARDUINO=10819              # "an Arduino API is present" (libraries test it)
  LV_CONF_INCLUDE_SIMPLE=1
  LV_CONF_PATH=lv_conf.h
  LV_MEMCPY_MEMSET_STD=1     # as every touch env in platformio.ini
)
set(WADA_COMPAT_INC ${DESKTOP}/hostcompat ${WEB}/compat ${WEB}/compat/arduino)

# ---- the browser build (Emscripten) -------------------------------------------------
# Every object is compiled with -pthread (shared wasm memory: the UI's background
# FreeRTOS tasks are pthreads, compat/src/freertos_host.cpp); LVGL + the UI loop stay on
# the browser main thread (platform/web_main.cpp). See WEB_BUILD.md, "Phase 2".
if(EMSCRIPTEN)
  # Release = the deploy: WADA_WEB_OPT (-O2 by default; -Os trades ~speed for size),
  # no debug info. RelWithDebInfo = the debug variant (-O2 -g, runtime assertions).
  set(WADA_WEB_OPT "-O2" CACHE STRING "optimisation level of the release web build")
  foreach(_lang C CXX)
    set(CMAKE_${_lang}_FLAGS_RELEASE "${WADA_WEB_OPT} -DNDEBUG")
    set(CMAKE_${_lang}_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
  endforeach()
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${WADA_WEB_OPT}")
  set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "-O2 -g")
  # WADA_NO_THREADS: the variant for a page that is not cross-origin isolated (the
  # http copy: no SharedArrayBuffer, so no pthreads). The FreeRTOS tasks become
  # cooperative fibers (compat/src/freertos_coop.cpp, Asyncify); web/build.sh web-nothreads.
  option(WADA_NO_THREADS "browser build without pthreads (cooperative tasks)" OFF)
  if(WADA_NO_THREADS)
    list(APPEND WADA_COMMON_DEFS WADA_NO_THREADS=1)
  else()
    add_compile_options(-pthread)
  endif()
  list(APPEND WADA_COMMON_DEFS WADA_DEFAULT_DATA_DIR="/data")   # the IDBFS mount (shell/wadamesh.js)
endif()

# ---- LVGL 8.4 (same lv_conf.h as the firmware: include/lv_conf.h) ----------------
file(GLOB_RECURSE LVGL_SRCS CONFIGURE_DEPENDS ${TP}/lvgl/src/*.c)
add_library(wada_lvgl STATIC ${LVGL_SRCS})
target_include_directories(wada_lvgl PUBLIC ${REPO}/include ${TP}/lvgl ${TP} PRIVATE ${WADA_COMPAT_INC})
target_compile_definitions(wada_lvgl PUBLIC ${WADA_COMMON_DEFS})
target_compile_options(wada_lvgl PRIVATE -w -O2)

# ---- Lua 5.4 (lib/lua, as the firmware) ------------------------------------------
file(GLOB LUA_SRCS ${REPO}/lib/lua/src/*.c)
list(FILTER LUA_SRCS EXCLUDE REGEX "/(lua|luac)\\.c$")
add_library(wada_lua STATIC ${LUA_SRCS})
target_include_directories(wada_lua PUBLIC ${REPO}/lib/lua/src)
target_compile_options(wada_lua PRIVATE -w)

# ---- third-party libraries (no ESP32 define: their portable code paths) ----------
set(CRYPTO ${TP}/arduinolibs/libraries/Crypto)
add_library(wada_thirdparty STATIC
  ${CRYPTO}/Crypto.cpp ${CRYPTO}/Hash.cpp ${CRYPTO}/SHA256.cpp ${CRYPTO}/SHA512.cpp
  ${CRYPTO}/BlockCipher.cpp ${CRYPTO}/AESCommon.cpp ${CRYPTO}/AES128.cpp ${CRYPTO}/AES192.cpp ${CRYPTO}/AES256.cpp
  ${CRYPTO}/Ed25519.cpp ${CRYPTO}/Curve25519.cpp ${CRYPTO}/BigNumberUtil.cpp ${CRYPTO}/SHA3.cpp ${CRYPTO}/KeccakCore.cpp
  ${TP}/CayenneLPP/src/CayenneLPP.cpp
  ${TP}/miniz/miniz.c ${TP}/miniz/miniz_tinfl.c ${TP}/miniz/miniz_tdef.c   # ROM tinfl stand-in + the PNG writer
  # the String/Print/Stream/FS layer of arduino-esp32 2.0.17, unmodified
  ${WEB}/compat/arduino/WString.cpp ${WEB}/compat/arduino/Print.cpp ${WEB}/compat/arduino/Stream.cpp
  ${WEB}/compat/arduino/StreamString.cpp ${WEB}/compat/arduino/stdlib_noniso.c ${WEB}/compat/arduino/IPAddress.cpp
  ${WEB}/compat/arduino/FS.cpp ${WEB}/compat/arduino/vfs_api.cpp
)
target_include_directories(wada_thirdparty PUBLIC ${WADA_COMPAT_INC} ${CRYPTO} ${TP}/CayenneLPP/src
                           ${TP}/ArduinoJson/src ${TP}/miniz)
target_compile_definitions(wada_thirdparty PUBLIC ${WADA_COMMON_DEFS})
target_compile_options(wada_thirdparty PRIVATE -w)
if(EMSCRIPTEN)
  # vfs_api.cpp's mkdir mode; musl only defines it under _GNU_SOURCE / _BSD_SOURCE.
  target_compile_definitions(wada_thirdparty PRIVATE ACCESSPERMS=0777)
endif()

# ---- MeshCore core: only the plain-data / crypto parts the UI touches ------------
# (No Dispatcher/Mesh/BaseChatMesh .cpp: the mesh is the stand-in in mesh/.)
file(GLOB ED25519_SRCS ${REPO}/ed25519/*.c)
add_library(wada_core STATIC
  ${CORE}/Identity.cpp ${CORE}/Utils.cpp ${CORE}/Packet.cpp
  ${CORE}/helpers/AdvertDataHelpers.cpp ${CORE}/helpers/TxtDataHelpers.cpp
  ${ED25519_SRCS})
target_include_directories(wada_core PUBLIC ${CORE} ${REPO}/ed25519)
target_link_libraries(wada_core PUBLIC wada_thirdparty)
target_compile_options(wada_core PRIVATE -w)


# Compile the actual modified native UI and adapter, with host-only platform glue.
file(GLOB UI_SRCS CONFIGURE_DEPENDS ${UI}/*.cpp ${UI}/*.c)
list(FILTER UI_SRCS EXCLUDE REGEX "/(BleKeyboard|UsbFilesSession)\\.cpp$")
file(GLOB PORT_SRCS CONFIGURE_DEPENDS ${REPO}/port/*.cpp)
list(FILTER PORT_SRCS EXCLUDE REGEX "/WadaApp\\.cpp$")
file(GLOB PROTO_SRCS CONFIGURE_DEPENDS ${FIRMWARE}/src/mesh/generated/meshtastic/*.pb.cpp)
file(GLOB DESKTOP_SRCS CONFIGURE_DEPENDS ${DESKTOP}/*.cpp)
file(GLOB COMPAT_SRCS CONFIGURE_DEPENDS ${WEB}/compat/src/*.cpp)
list(FILTER COMPAT_SRCS EXCLUDE REGEX "/freertos_coop\\.cpp$")
set(PLATFORM_SRCS ${WEB}/platform/WebDisplay.cpp ${WEB}/platform/WebTouch.cpp
  ${WEB}/platform/WebLocation.cpp ${WEB}/platform/WebFiles.cpp
  ${WEB}/platform/WebLvglInput.cpp)
find_package(SDL2 REQUIRED)
add_executable(wadamesh-meshtastic-sim ${DESKTOP_SRCS} ${UI_SRCS} ${PORT_SRCS} ${PROTO_SRCS}
  ${COMPAT_SRCS} ${PLATFORM_SRCS}
  ${FIRMWARE}/src/mesh/Throttle.cpp
  ${REPO}/backend/WebMesh.cpp ${SRC}/LvglPsramAlloc.cpp
  ${SRC}/helpers/TouchDiagTrace.cpp ${SRC}/helpers/esp32/TouchPrefsStore.cpp
  ${SRC}/helpers/esp32/SdNvsPrefs.cpp ${SRC}/helpers/esp32/WebMirror.cpp
  ${SRC}/helpers/ui/UIColorPalette.cpp
  ${CORE}/helpers/RegionMap.cpp ${CORE}/helpers/TransportKeyStore.cpp ${CORE}/helpers/IdentityStore.cpp
  ${FIRMWARE}/lib/WadaPacketComms/src/PacketClient.cpp
  ${FIRMWARE}/lib/WadaPacketComms/src/PacketServer.cpp
  ${FIRMWARE}/lib/WadaPacketComms/src/SharedQueue.cpp
  ${NANOPB}/pb_common.c ${NANOPB}/pb_encode.c ${NANOPB}/pb_decode.c)
target_include_directories(wadamesh-meshtastic-sim BEFORE PRIVATE
  ${DESKTOP}/hostcompat ${REPO}/backend ${REPO}/port ${SRC} ${UI} ${REPO}/include ${CORE}
  ${WEB}/platform ${WADA_COMPAT_INC} ${FIRMWARE}/lib/WadaPacketComms/include
  ${FIRMWARE}/src ${FIRMWARE}/src/mesh/generated ${NANOPB} ${TP}/ArduinoJson/src)
target_compile_definitions(wadamesh-meshtastic-sim PRIVATE
  PB_VALIDATE_UTF8=1
  WADA_WEB=1 WADA_MESHTASTIC=1 WADA_DESKTOP=1 ESP32=1 ARDUINO=10819 HAS_TOUCH_UI=1 UI_LVGL=1
  WADAMESH_FORK_BUILD=1 HAS_TDECK_GT911=1 HAS_CARDKB=1
  MAX_CONTACTS=256 MAX_GROUP_CHANNELS=8 ENABLE_ADVERT_ON_BOOT=0
  LV_CONF_INCLUDE_SIMPLE=1 LV_CONF_PATH=lv_conf.h LV_MEMCPY_MEMSET_STD=1
  WADA_DEFAULT_DATA_DIR="${DESKTOP}/data"
  WADA_TILE_SERVER="http://tile.openstreetmap.org" WADA_STORE_BASE="http://firmware.wadamesh.com/apps"
  NDEBUG)
target_link_libraries(wadamesh-meshtastic-sim PRIVATE wada_core wada_lvgl wada_lua wada_thirdparty SDL2::SDL2)
target_compile_options(wadamesh-meshtastic-sim PRIVATE -w -O0 -g0 -fno-strict-aliasing -Wno-c++11-narrowing)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(wadamesh-meshtastic-sim PRIVATE -fbracket-depth=512)
endif()
set_source_files_properties(${UI}/UITask.cpp PROPERTIES COMPILE_FLAGS "-include ${DESKTOP}/hostcompat/HostUI.h")
