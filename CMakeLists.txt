cmake_minimum_required(VERSION 3.16)
project(ApePlayer VERSION 1.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Generate version.h
set(APP_NAME "ApePlayer")
set(APP_DESCRIPTION "Ape Escape BD/HD to SF2 and WAV converter")
set(APP_HOMEPAGE "https://github.com/jooqw/apeplayer")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
    @ONLY
)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# sf2cute is old, we need to compile only what we need
file(GLOB SF2CUTE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/sf2cute/src/sf2cute/*.cpp")

if(NOT SF2CUTE_SOURCES)
    message(FATAL_ERROR "sf2cute sources not found! Check libs/sf2cute/src/sf2cute/")
endif()

add_executable(ApePlayer
    src/main.cpp
    src/common.h
    
    src/engine/audio.cpp src/engine/audio.h
    src/engine/adsr.cpp src/engine/adsr.h
    src/engine/vibrato.cpp src/engine/vibrato.h
    src/engine/reverb.cpp src/engine/reverb.h
    
    src/exporters/renderwav.cpp src/exporters/renderwav.h
    src/exporters/sf2exporter.cpp src/exporters/sf2exporter.h
    
    src/format/bd.cpp src/format/bd.h
    src/format/hd.cpp src/format/hd.h
    src/format/mid.cpp src/format/mid.h
    src/format/sq.cpp src/format/sq.h
    
    src/ui/mainwindow.cpp src/ui/mainwindow.h src/ui/mainwindow.ui
    src/ui/waveformwidget.cpp src/ui/waveformwidget.h
    
    ${SF2CUTE_SOURCES}
)

target_include_directories(ApePlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libs
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/sf2cute/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(UNIX AND NOT APPLE)
    target_link_libraries(ApePlayer PRIVATE dl pthread m)
endif()

target_precompile_headers(ApePlayer PRIVATE <cstdint> <vector> <string> <memory> <cmath>)

target_link_libraries(ApePlayer PRIVATE Qt6::Widgets)
