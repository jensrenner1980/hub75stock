cmake_minimum_required(VERSION 3.21)
project(hub75stock LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CMake's own default Release flags already include -O3 (unlike qmake's,
# which default to -O2) - the panel refresh is a tight bit-banging loop and
# should not be left at less than that. Make Release the default so a plain
# `cmake -B build` gets it without extra flags.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network HttpServer)
qt_standard_project_setup()

# libqrencode ships no CMake config, only a pkg-config file.
find_package(PkgConfig REQUIRED)
pkg_check_modules(QRencode REQUIRED IMPORTED_TARGET libqrencode)

# For pngwriter.cpp's own minimal PNG encoder (see its own comments for why
# this project writes PNGs itself rather than linking Qt6::Gui just for
# QImage::save() - Qt6::Gui's hard dependencies were confirmed too much for
# a Raspberry Pi's SD card). zlib is tiny and near-universal by comparison.
find_package(ZLIB REQUIRED)

# ---------------------------------------------------------------------------
# Vendored rpi-rgb-led-matrix.
#
# The library sources are compiled straight into this binary instead of
# linking lib/librgbmatrix.a. Two reasons:
#   * the result is a single self-contained executable, and
#   * the library's own config.mk hardcodes -march=native/-mtune=native, which
#     breaks as soon as a cross-compiler that is not named *aarch64-linux-gnu*
#     is used. Building the sources here keeps one consistent toolchain.
# ---------------------------------------------------------------------------
set(RGB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/rpi-rgb-led-matrix)
set(RGB_LIB  ${RGB_ROOT}/lib)
set(RGB_PIO  ${RGB_LIB}/rp1/rp1_pio_vendor)

# HARDWARE_DESC matches the same-named knob in lib/Makefile; override with
# -DHARDWARE_DESC=adafruit-hat if needed.
set(HARDWARE_DESC "regular" CACHE STRING "Default --led-gpio-mapping baked into the binary")

set(RGBMATRIX_SOURCES
    ${RGB_LIB}/bdf-font.cc
    ${RGB_LIB}/content-streamer.cc
    ${RGB_LIB}/framebuffer.cc
    ${RGB_LIB}/gpio.cc
    ${RGB_LIB}/graphics.cc
    ${RGB_LIB}/led-matrix.cc
    ${RGB_LIB}/multiplex-mappers.cc
    ${RGB_LIB}/options-initialize.cc
    ${RGB_LIB}/pixel-mapper.cc
    ${RGB_LIB}/thread.cc
    ${RGB_LIB}/rp1/rp1_pio_backend.cc
    ${RGB_LIB}/rp1/rp1_rio_backend.cc
    ${RGB_LIB}/hardware-mapping.c
    ${RGB_LIB}/rp1/rp1_pio_support.c
)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
qt_add_executable(hub75stock
    src/main.cpp
    src/config/appconfig.cpp
    src/config/appconfig.h
    src/config/symbol.cpp
    src/config/symbol.h
    src/display/matrixwall.cpp
    src/display/matrixwall.h
    src/display/memorycanvas.cpp
    src/display/memorycanvas.h
    src/display/panelview.cpp
    src/display/panelview.h
    src/display/fontstore.cpp
    src/display/fontstore.h
    src/display/stockrenderer.cpp
    src/display/stockrenderer.h
    src/display/testpatterns.cpp
    src/display/testpatterns.h
    src/display/textutil.cpp
    src/display/textutil.h
    src/stocks/mockprovider.cpp
    src/stocks/mockprovider.h
    src/stocks/pricedata.h
    src/stocks/stockdataprovider.h
    src/stocks/yahoodataprovider.cpp
    src/stocks/yahoodataprovider.h
    src/net/connectivitymonitor.cpp
    src/net/connectivitymonitor.h
    src/display/qrsplash.cpp
    src/display/qrsplash.h
    src/display/panelexport.cpp
    src/display/panelexport.h
    src/display/pngwriter.cpp
    src/display/pngwriter.h
    src/web/configserver.cpp
    src/web/configserver.h
    ${RGBMATRIX_SOURCES}
)

qt_add_resources(hub75stock "fonts"
    PREFIX "/fonts"
    BASE resources/fonts
    FILES
        resources/fonts/5x8.bdf
        resources/fonts/4x6.bdf
        resources/fonts/tom-thumb.bdf
)

target_include_directories(hub75stock PRIVATE
    src
    ${RGB_ROOT}/include
    ${RGB_LIB}
    ${RGB_PIO}/piolib/include
    ${RGB_PIO}/include
)

target_compile_definitions(hub75stock PRIVATE
    DEFAULT_HARDWARE="${HARDWARE_DESC}"
)

# The library is warning-clean under its own flag set; keep the vendored
# sources from spamming the build log. Applied to the whole target rather
# than split out per-file - simpler, and the app code is warning-clean anyway.
target_compile_options(hub75stock PRIVATE -Wno-unused-parameter)

target_link_libraries(hub75stock PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::HttpServer
    PkgConfig::QRencode
    ZLIB::ZLIB
    rt
    m
    pthread
)

install(TARGETS hub75stock RUNTIME DESTINATION bin)

# Placed flat next to the binary (not in a "config" subdirectory), matching
# main.cpp's second fallback path (applicationDirPath()/hub75stock.json) -
# that one doesn't depend on the process's working directory at launch,
# unlike the first fallback ("config/hub75stock.json" relative to CWD).
install(FILES config/hub75stock.json DESTINATION bin)
