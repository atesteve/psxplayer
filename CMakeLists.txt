cmake_minimum_required(VERSION 3.20)
set(TARGET "psxplayer")
set(TARGET_LIBUPSE "libupse")
project(${TARGET} VERSION 0.0.1 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-g3)
add_link_options(-g3)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt6_add_executable(${TARGET}
  src/main.cpp
  src/main-window.cpp
  src/main-window.h
  src/upse.cpp
  src/upse.h
  src/audio.cpp
  src/audio.h
)
qt_add_ui(${TARGET} SOURCES src/main-window.ui)

target_compile_options(${TARGET} PRIVATE -Wall -Wextra -pedantic)

# Scan files from libupse
file(GLOB_RECURSE SOURCES_LIBUPSE "lib/libupse" "*.c")
add_library(${TARGET_LIBUPSE} STATIC ${SOURCES_LIBUPSE})

# Definitions for libupse
target_compile_definitions(${TARGET_LIBUPSE} PRIVATE EMU_COMPILE EMU_LITTLE_ENDIAN)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  #target_compile_definitions(${TARGET_LIBUPSE} PRIVATE UPSE_DEBUG)
endif()

target_include_directories(${TARGET} PRIVATE lib)
target_link_libraries(${TARGET} PRIVATE ${TARGET_LIBUPSE})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

find_package(fmt REQUIRED)
target_link_libraries(${TARGET} PUBLIC fmt::fmt)

find_package(spdlog REQUIRED)
target_link_libraries(${TARGET} PUBLIC spdlog::spdlog)

find_package(PulseAudio REQUIRED)
target_link_libraries(${TARGET} PRIVATE pulse-simple pulse)

target_link_libraries(${TARGET} PRIVATE Qt6::Widgets)

find_package(ZLIB REQUIRED)
target_link_libraries(${TARGET_LIBUPSE} PUBLIC ZLIB::ZLIB)

set_target_properties(${TARGET} PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION TRUE
    CXX_STANDARD 23
)
