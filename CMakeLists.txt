cmake_minimum_required(VERSION 3.16)
project(ApePlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

# sf2cute is old, we need to compile only what we need

file(GLOB SF2CUTE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/sf2cute/src/sf2cute/*.cpp")

if(NOT SF2CUTE_SOURCES)
    message(FATAL_ERROR "sf2cute sources not found! Check libs/sf2cute/src/sf2cute/")
endif()

add_executable(ApePlayer
    src/main.cpp
    src/apeloader.cpp src/apeloader.h
    src/vagdecoder.cpp src/vagdecoder.h
    src/vagaudiosource.cpp src/vagaudiosource.h
    src/waveformwidget.cpp src/waveformwidget.h
    src/sf2exporter.cpp src/sf2exporter.h
    src/mainwindow.cpp src/mainwindow.h src/mainwindow.ui
    src/common.h
    ${SF2CUTE_SOURCES}
)


target_include_directories(ApePlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/sf2cute/include
)

target_precompile_headers(ApePlayer PRIVATE <cstdint> <vector> <string> <memory>)

target_link_libraries(ApePlayer PRIVATE Qt6::Widgets Qt6::Multimedia)
