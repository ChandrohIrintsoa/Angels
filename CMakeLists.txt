# ─────────────────────────────────────────────────────────────────────────────
#  CMakeLists.txt  —  Angels Memory Engine v2
#  Cible  : UN SEUL .so → libangels.so
#  Contenu: moteur mémoire + pont JNI (NativeCore) + panel UI (AngelsPanel)
#  Usage  : configuré par build.gradle (externalNativeBuild)
# ─────────────────────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.22)
project(angels VERSION 2.0 LANGUAGES CXX)

# ── C++17 obligatoire ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── Flags de compilation ──────────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wshadow
    -Wno-unused-parameter
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
)

# ── Release : O2 + strip ──────────────────────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -DNDEBUG)
    add_link_options(-Wl,--gc-sections -Wl,--strip-all)
else()
    add_compile_options(-O0 -g)
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  SOURCES — tout dans UN seul .so : libangels.so
#   src/core/  → moteur mémoire bas niveau
#   src/jni/   → ponts JNI (NativeCore + AngelsPanel)
#   src/ui/    → état interne du panel
# ─────────────────────────────────────────────────────────────────────────────
set(ANGELS_SOURCES
    # ── Moteur mémoire ────────────────────────────────────────────────────────
    src/core/mem_maps.cpp
    src/core/mem_rw.cpp
    src/core/mem_scan.cpp
    src/core/mem_freeze.cpp
    src/core/string_scan.cpp
    src/core/watch_list.cpp
    src/core/speed_hack.cpp

    # ── Pont JNI — NativeCore (scan / write / freeze / watch / speed) ─────────
    src/jni/jni_entry.cpp

    # ── Pont JNI — AngelsPanel (état overlay, log, paramètres UI) ────────────
    src/jni/jni_panel.cpp

    # ── État interne du panel ─────────────────────────────────────────────────
    src/ui/panel_state.cpp
)

# ─────────────────────────────────────────────────────────────────────────────
#  Bibliothèque partagée unique : libangels.so
# ─────────────────────────────────────────────────────────────────────────────
add_library(angels SHARED ${ANGELS_SOURCES})

target_include_directories(angels
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ── Dépendances Android ───────────────────────────────────────────────────────
find_library(log-lib     log)
find_library(android-lib android)

target_link_libraries(angels
    PRIVATE
        ${log-lib}
        ${android-lib}
)

# ── Informations de build ─────────────────────────────────────────────────────
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
message(STATUS " Angels Memory Engine v2")
message(STATUS " Build   : ${CMAKE_BUILD_TYPE}")
message(STATUS " ABI     : ${ANDROID_ABI}")
message(STATUS " API     : ${ANDROID_PLATFORM}")
message(STATUS " Output  : libangels.so  (unique)")
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
