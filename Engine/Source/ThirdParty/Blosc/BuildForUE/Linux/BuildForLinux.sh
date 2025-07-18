#!/bin/bash

set -e

LIBRARY_NAME="Blosc"
REPOSITORY_NAME="c-blosc"

# Informational, for the usage message.
CURRENT_LIBRARY_VERSION=1.21.0

BUILD_SCRIPT_NAME="$(basename $BASH_SOURCE)"
BUILD_SCRIPT_DIR=`cd $(dirname "$BASH_SOURCE"); pwd`

UsageAndExit()
{
    echo "Build $LIBRARY_NAME for use with Unreal Engine on Linux"
    echo
    echo "Usage:"
    echo
    echo "    $BUILD_SCRIPT_NAME <$LIBRARY_NAME Version> <Architecture Name>"
    echo
    echo "Usage examples:"
    echo
    echo "    $BUILD_SCRIPT_NAME $CURRENT_LIBRARY_VERSION x86_64-unknown-linux-gnu"
    echo "      -- Installs $LIBRARY_NAME version $CURRENT_LIBRARY_VERSION for x86_64 architecture."
    echo
    echo "    $BUILD_SCRIPT_NAME $CURRENT_LIBRARY_VERSION aarch64-unknown-linux-gnueabi"
    echo "      -- Installs $LIBRARY_NAME version $CURRENT_LIBRARY_VERSION for arm64 architecture."
    echo
    exit 1
}

# Get version and architecture from arguments.
LIBRARY_VERSION=$1
if [ -z "$LIBRARY_VERSION" ]
then
    UsageAndExit
fi

ARCH_NAME=$2
if [ -z "$ARCH_NAME" ]
then
    UsageAndExit
fi

UE_MODULE_LOCATION=`cd $BUILD_SCRIPT_DIR/../..; pwd`
UE_SOURCE_THIRD_PARTY_LOCATION=`cd $UE_MODULE_LOCATION/..; pwd`
UE_ENGINE_LOCATION=`cd $UE_SOURCE_THIRD_PARTY_LOCATION/../..; pwd`

ZLIB_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/zlib/v1.2.8"
ZLIB_INCLUDE_LOCATION="$ZLIB_LOCATION/include/Unix/$ARCH_NAME"
ZLIB_LIB_LOCATION="$ZLIB_LOCATION/lib/Unix/$ARCH_NAME/libz.a"

SOURCE_LOCATION="$UE_MODULE_LOCATION/$REPOSITORY_NAME-$LIBRARY_VERSION"

BUILD_LOCATION="$UE_MODULE_LOCATION/Intermediate"

INSTALL_INCLUDEDIR=include

INSTALL_LOCATION="$UE_MODULE_LOCATION/Deploy/$REPOSITORY_NAME-$LIBRARY_VERSION"
INSTALL_INCLUDE_LOCATION="$INSTALL_LOCATION/$INSTALL_INCLUDEDIR"
INSTALL_UNIX_ARCH_LOCATION="$INSTALL_LOCATION/Unix/$ARCH_NAME"

rm -rf $BUILD_LOCATION
rm -rf $INSTALL_INCLUDE_LOCATION
rm -rf $INSTALL_UNIX_ARCH_LOCATION

mkdir $BUILD_LOCATION
pushd $BUILD_LOCATION > /dev/null

# Note that we patch the source for the version of LZ4 that is bundled with
# Blosc to add a prefix to all of its functions. This ensures that the symbol
# names do not collide with the version(s) of LZ4 that are embedded in the
# engine.

# Copy the source into the build directory so that we can apply patches.
BUILD_SOURCE_LOCATION="$BUILD_LOCATION/$REPOSITORY_NAME-$LIBRARY_VERSION"

cp -r $SOURCE_LOCATION $BUILD_SOURCE_LOCATION

pushd $BUILD_SOURCE_LOCATION > /dev/null
git apply $UE_MODULE_LOCATION/Blosc_v1.21.0_LZ4_PREFIX.patch
popd > /dev/null

# Run Engine/Build/BatchFiles/Linux/SetupToolchain.sh first to ensure
# that the toolchain is setup and verify that this name matches.
TOOLCHAIN_NAME=v25_clang-18.1.0-rockylinux8

UE_TOOLCHAIN_LOCATION="$UE_ENGINE_LOCATION/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/$TOOLCHAIN_NAME"
UE_TOOLCHAIN_ARCH_INCLUDE_LOCATION="$UE_TOOLCHAIN_LOCATION/$ARCH_NAME/include/c++/v1"
UE_TOOLCHAIN_ARCH_LIB_LOCATION="$UE_TOOLCHAIN_LOCATION/$ARCH_NAME/lib64"

C_FLAGS="-DLZ4_PREFIX=BLOSC_ -fvisibility=hidden"
CXX_FLAGS="-nostdinc++ -I$UE_TOOLCHAIN_ARCH_INCLUDE_LOCATION"
LINKER_FLAGS="-fuse-ld=lld -nodefaultlibs -stdlib=libc++ $UE_TOOLCHAIN_ARCH_LIB_LOCATION/libc++.a $UE_TOOLCHAIN_ARCH_LIB_LOCATION/libc++abi.a -lm -lc -lgcc_s -lgcc"

# Determine whether we're cross compiling for an architecture that doesn't
# match the host. This is the way that CMake determines the value for the
# CMAKE_HOST_SYSTEM_PROCESSOR variable.
HOST_SYSTEM_PROCESSOR=`uname -m`
TARGET_SYSTEM_PROCESSOR=$HOST_SYSTEM_PROCESSOR

if [[ $ARCH_NAME != $HOST_SYSTEM_PROCESSOR* ]]
then
    ARCH_NAME_PARTS=(${ARCH_NAME//-/ })
    TARGET_SYSTEM_PROCESSOR=${ARCH_NAME_PARTS[0]}
fi

( cat <<_EOF_
    # Auto-generated by script: $BUILD_SCRIPT_DIR/$BUILD_SCRIPT_NAME

    message (STATUS "UE_TOOLCHAIN_LOCATION is '${UE_TOOLCHAIN_LOCATION}'")
    message (STATUS "ARCH_NAME is '${ARCH_NAME}'")
    message (STATUS "TARGET_SYSTEM_PROCESSOR is '${TARGET_SYSTEM_PROCESSOR}'")

    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_SYSTEM_PROCESSOR ${TARGET_SYSTEM_PROCESSOR})

    set(CMAKE_SYSROOT ${UE_TOOLCHAIN_LOCATION}/${ARCH_NAME})
    set(CMAKE_LIBRARY_ARCHITECTURE ${ARCH_NAME})

    set(CMAKE_C_COMPILER \${CMAKE_SYSROOT}/bin/clang)
    set(CMAKE_C_COMPILER_TARGET ${ARCH_NAME})
    set(CMAKE_C_FLAGS "-target ${ARCH_NAME} ${C_FLAGS}")

    set(CMAKE_CXX_COMPILER \${CMAKE_SYSROOT}/bin/clang++)
    set(CMAKE_CXX_COMPILER_TARGET ${ARCH_NAME})
    set(CMAKE_CXX_FLAGS "-target ${ARCH_NAME} ${CXX_FLAGS}")

    set(CMAKE_EXE_LINKER_FLAGS "${LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS "${LINKER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS "${LINKER_FLAGS}")

    set(CMAKE_FIND_ROOT_PATH "${UE_TOOLCHAIN_LOCATION}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
_EOF_
) > /tmp/__cmake_toolchain.cmake

CMAKE_ARGS=(
    -DCMAKE_TOOLCHAIN_FILE="/tmp/__cmake_toolchain.cmake"
    -DCMAKE_INSTALL_PREFIX="$INSTALL_LOCATION"
    -DPREFER_EXTERNAL_ZLIB=ON
    -DZLIB_INCLUDE_DIR="$ZLIB_INCLUDE_LOCATION"
    -DZLIB_LIBRARY="$ZLIB_LIB_LOCATION"
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED=OFF
    -DBUILD_TESTS=OFF
    -DBUILD_FUZZERS=OFF
    -DBUILD_BENCHMARKS=OFF
    -DCMAKE_DEBUG_POSTFIX=_d
)

NUM_CPU=`grep -c ^processor /proc/cpuinfo`

echo Configuring Debug build for $LIBRARY_NAME version $LIBRARY_VERSION...
cmake -G "Unix Makefiles" $BUILD_SOURCE_LOCATION -DCMAKE_BUILD_TYPE=Debug "${CMAKE_ARGS[@]}"

echo Building $LIBRARY_NAME for Debug...
cmake --build . -j$NUM_CPU

echo Installing $LIBRARY_NAME for Debug...
cmake --install .

# The Unix Makefiles generator does not support multiple configurations, so we
# need to re-configure for Release.
echo Configuring Release build for $LIBRARY_NAME version $LIBRARY_VERSION...
cmake -G "Unix Makefiles" $BUILD_SOURCE_LOCATION -DCMAKE_BUILD_TYPE=Release "${CMAKE_ARGS[@]}"

echo Building $LIBRARY_NAME for Release...
cmake --build . -j$NUM_CPU

echo Installing $LIBRARY_NAME for Release...
cmake --install .

popd > /dev/null

echo Removing pkgconfig files...
rm -rf "$INSTALL_LOCATION/lib/pkgconfig"

echo Moving lib directory into place...
INSTALL_LIB_LOCATION="$INSTALL_LOCATION/Unix/$ARCH_NAME"
mkdir -p $INSTALL_LIB_LOCATION
mv "$INSTALL_LOCATION/lib" "$INSTALL_LIB_LOCATION"

echo Done.
