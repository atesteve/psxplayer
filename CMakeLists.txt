cmake_minimum_required(VERSION 3.20)
set(TARGET "psxplayer")
set(TARGET_LIBUPSE "libupse")
set(TARGET_XPRINTF "xprintf")
project(${TARGET} VERSION 0.0.1 LANGUAGES C CXX)

if(WIN32)
  set(vcpkg "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE "${vcpkg}" CACHE FILEPATH "CMake toolchain file")
  include(${vcpkg})
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(FSANITIZE)
  add_compile_options(-fsanitize=${FSANITIZE})
  add_link_options(-fsanitize=${FSANITIZE})
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

set(TARGET_SRC
  src/main.cpp
  src/main-window.cpp
  src/main-window.h
  src/upse.cpp
  src/upse.h
  src/audio.cpp
  src/audio.h
  src/slider-mouse-events.cpp
  src/slider-mouse-events.h
  src/channel-widget.cpp
  src/channel-widget.h
  src/kcollapsiblegroupbox.cpp
  src/kcollapsiblegroupbox.h
  src/sound-meter-bar.cpp
  src/sound-meter-bar.h
  src/adpcm.cpp
  src/adpcm.h
  src/square-button.h
  src/channel-mapper/channel-mapper.cpp
  src/channel-mapper/channel-mapper.h
  src/channel-mapper/null-mapper.h
  src/channel-mapper/square-mapper.cpp
  src/channel-mapper/square-mapper.h
  src/channel-mapper/square-retro-mapper.cpp
  src/channel-mapper/square-retro-mapper.h
  src/iop/iop.cpp
  src/iop/iop.h
  src/iop/upse_loader_psf2.cpp
  src/iop/iop-stdio.cpp
  src/iop/iop-ioman.cpp
  src/iop/iop-sysmem.cpp
  src/iop/iop-modload.cpp
  src/iop/iop-intrman.cpp
  src/iop/iop-loadcore.cpp
  src/iop/iop-sysclib.cpp
  src/iop/iop-thevent.cpp
  src/iop/iop-thsemap.cpp
)

if(WIN32)
  add_compile_options(-Zc:__cplusplus -Zc:preprocessor)
  add_compile_definitions(NOMINMAX)
  qt6_add_executable(${TARGET} WIN32 ${TARGET_SRC})
else()
  add_compile_options(-g3)
  add_link_options(-g3)
  qt6_add_executable(${TARGET} ${TARGET_SRC})
  target_compile_options(${TARGET} PRIVATE -Wall -Wextra -pedantic)
endif()

qt_add_ui(${TARGET} SOURCES src/main-window.ui src/channel-widget.ui)

# Scan files from libupse
file(GLOB_RECURSE SOURCES_LIBUPSE "lib/libupse" "*.c")
add_library(${TARGET_LIBUPSE} STATIC ${SOURCES_LIBUPSE})

add_library(${TARGET_XPRINTF} STATIC
  lib/xprintf/xprintf.cpp
  lib/xprintf/xprintf.h
)

# Definitions for libupse
target_compile_definitions(${TARGET_LIBUPSE} PRIVATE EMU_COMPILE EMU_LITTLE_ENDIAN)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  #target_compile_definitions(${TARGET_LIBUPSE} PRIVATE UPSE_DEBUG)
endif()

target_include_directories(${TARGET} PRIVATE lib)
target_include_directories(${TARGET} PRIVATE src)
target_link_libraries(${TARGET} PRIVATE ${TARGET_LIBUPSE})
target_link_libraries(${TARGET} PRIVATE ${TARGET_XPRINTF})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

find_package(fmt REQUIRED)
target_link_libraries(${TARGET} PRIVATE fmt::fmt)

find_package(FFTW3 REQUIRED)
if(WIN32)
  target_link_libraries(${TARGET} PRIVATE FFTW3::fftw3)
else()
  target_link_libraries(${TARGET} PRIVATE fftw3)
endif()

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
target_link_libraries(${TARGET} PRIVATE SDL3::SDL3)

target_link_libraries(${TARGET} PRIVATE Qt6::Widgets)

find_package(ZLIB REQUIRED)
target_link_libraries(${TARGET_LIBUPSE} PRIVATE ZLIB::ZLIB)

set_target_properties(${TARGET} PROPERTIES
    CXX_STANDARD 26
)

if(WIN32)
  add_custom_command(TARGET ${TARGET} POST_BUILD COMMAND Qt6::windeployqt "$<TARGET_FILE:${TARGET}>")
endif()
