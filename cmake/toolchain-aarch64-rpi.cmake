# Cross-compilation toolchain for Raspberry Pi OS 64-bit (aarch64), using
# Debian's own arm64 packages via multiarch rather than a separate sysroot.
#
# Setup this file relies on (see README.md "Cross-compiling"):
#   sudo dpkg --add-architecture arm64
#   sudo apt update
#   sudo apt install crossbuild-essential-arm64 qt6-base-dev:arm64
#
# Usage:
#   cmake -G Ninja -B build-pi \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-rpi.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-pi

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR           /usr/bin/aarch64-linux-gnu-ar)
set(CMAKE_STRIP        /usr/bin/aarch64-linux-gnu-strip)

# Not a separate sysroot tree: the arm64 dev files live alongside the amd64
# ones under Debian's multiarch layout (/usr/lib/aarch64-linux-gnu, /usr/
# include/aarch64-linux-gnu), so point find_package(Qt6 ...) straight at the
# arm64 CMake config dir rather than fighting CMAKE_FIND_ROOT_PATH_MODE over
# a /usr shared between both architectures.
set(CMAKE_PREFIX_PATH /usr/lib/aarch64-linux-gnu/cmake)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Qt6's cross-compilation support needs this to find the host-native moc/uic/
# rcc (Debian keeps them at /usr/lib/qt6/libexec/, discovered relative to this
# prefix) instead of trying to run the target's own (non-executable-here) arm64
# tools. Verified working set from inside the toolchain file, not just via
# -DQT_HOST_PATH on the command line - one flag is enough to use this file.
set(QT_HOST_PATH /usr CACHE PATH "Host Qt install, for cross-compiling moc/uic/rcc")

# pkg-config (used to find libqrencode - it ships no CMake config, only a
# .pc file) has no cross-awareness of its own: run unmodified, it's still
# the plain host pkg-config binary (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM is
# NEVER above, same reasoning as Qt's moc/uic/rcc), which would happily
# resolve the amd64 .pc file and hand back amd64 flags/libs. Overriding
# PKG_CONFIG_LIBDIR forces it to look only under the arm64 multiarch
# directory instead - the standard way to point a native pkg-config at a
# different architecture's packages.
set(ENV{PKG_CONFIG_LIBDIR} /usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig)
