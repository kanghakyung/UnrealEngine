#!/bin/bash

set -e

LIBRARY_NAME=OpenUSD
REPOSITORY_NAME=OpenUSD

BUILD_SCRIPT_NAME="$(basename $BASH_SOURCE)"
BUILD_SCRIPT_DIR=`cd $(dirname "$BASH_SOURCE"); pwd`

LIBRARY_VERSION=25.02a

# This path may be adjusted to point to wherever the OpenUSD source is located.
# It is typically obtained by either downloading a zip/tarball of the source
# code, or more commonly by cloning the GitHub repository, e.g. for the
# current engine OpenUSD version:
#     git clone --branch v25.02a https://github.com/PixarAnimationStudios/OpenUSD.git OpenUSD_src
# Then from inside the cloned OpenUSD_src directory, apply all patches sitting
# next to this build script:
#     git apply <build script dir>/OpenUSD_v2502a_*.patch
# Note also that this path may be emitted as part of OpenUSD error messages, so
# it is suggested that it not reveal any sensitive information.
OPENUSD_SOURCE_LOCATION="/tmp/${REPOSITORY_NAME}_src"

ARCH_NAME=x86_64-unknown-linux-gnu

UE_ENGINE_LOCATION=`cd $BUILD_SCRIPT_DIR/../../../../../..; pwd`

UE_SOURCE_THIRD_PARTY_LOCATION="$UE_ENGINE_LOCATION/Source/ThirdParty"
TBB_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/Intel/TBB/Deploy/oneTBB-2021.13.0"
TBB_LIB_LOCATION="$TBB_LOCATION/Unix/$ARCH_NAME/lib"
TBB_CMAKE_LOCATION="$TBB_LIB_LOCATION/cmake/TBB"
IMATH_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/Imath/Deploy/Imath-3.1.12"
IMATH_LIB_LOCATION="$IMATH_LOCATION/Unix/$ARCH_NAME/lib"
IMATH_CMAKE_LOCATION="$IMATH_LIB_LOCATION/cmake/Imath"
OPENSUBDIV_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/OpenSubdiv/Deploy/OpenSubdiv-3.6.0"
OPENSUBDIV_INCLUDE_DIR="$OPENSUBDIV_LOCATION/include"
OPENSUBDIV_LIB_LOCATION="$OPENSUBDIV_LOCATION/Unix/$ARCH_NAME/lib"
ALEMBIC_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/Alembic/Deploy/alembic-1.8.7"
ALEMBIC_LIB_LOCATION="$ALEMBIC_LOCATION/Unix/$ARCH_NAME/lib"
ALEMBIC_CMAKE_LOCATION="$ALEMBIC_LIB_LOCATION/cmake/Alembic"
MATERIALX_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/MaterialX/Deploy/MaterialX-1.38.10"
MATERIALX_LIB_LOCATION="$MATERIALX_LOCATION/Unix/$ARCH_NAME/lib"
MATERIALX_CMAKE_LOCATION="$MATERIALX_LIB_LOCATION/cmake/MaterialX"

PYTHON_BINARIES_LOCATION="$UE_ENGINE_LOCATION/Binaries/ThirdParty/Python3/Linux"
PYTHON_EXECUTABLE_LOCATION="$PYTHON_BINARIES_LOCATION/bin/python3"
PYTHON_SOURCE_LOCATION="$UE_SOURCE_THIRD_PARTY_LOCATION/Python3/Linux"
PYTHON_INCLUDE_LOCATION="$PYTHON_SOURCE_LOCATION/include"
PYTHON_LIBRARY_LOCATION="$PYTHON_BINARIES_LOCATION/lib/libpython3.11.so"

UE_MODULE_USD_LOCATION=$BUILD_SCRIPT_DIR

BUILD_LOCATION="$UE_MODULE_USD_LOCATION/Intermediate"

# OpenUSD build products are written into a deployment directory and must then
# be manually copied from there into place.
INSTALL_LOCATION="$BUILD_LOCATION/Deploy/$REPOSITORY_NAME-$LIBRARY_VERSION"

rm -rf $BUILD_LOCATION

mkdir $BUILD_LOCATION
pushd $BUILD_LOCATION > /dev/null

# Run Engine/Build/BatchFiles/Linux/SetupToolchain.sh first to ensure
# that the toolchain is setup and verify that this name matches.
TOOLCHAIN_NAME=v25_clang-18.1.0-rockylinux8

UE_TOOLCHAIN_LOCATION="$UE_ENGINE_LOCATION/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/$TOOLCHAIN_NAME"
UE_TOOLCHAIN_ARCH_INCLUDE_LOCATION="$UE_TOOLCHAIN_LOCATION/$ARCH_NAME/include/c++/v1"
UE_TOOLCHAIN_ARCH_LIB_LOCATION="$UE_TOOLCHAIN_LOCATION/$ARCH_NAME/lib64"

C_COMPILER="$UE_TOOLCHAIN_LOCATION/$ARCH_NAME/bin/clang"
CXX_COMPILER="$UE_TOOLCHAIN_LOCATION/$ARCH_NAME/bin/clang++"

CXX_FLAGS="-fPIC -nostdinc++ -I$UE_TOOLCHAIN_ARCH_INCLUDE_LOCATION -I$PYTHON_INCLUDE_LOCATION"
CXX_LINKER="-fuse-ld=lld -nodefaultlibs -stdlib=libc++ $UE_TOOLCHAIN_ARCH_LIB_LOCATION/libc++.a $UE_TOOLCHAIN_ARCH_LIB_LOCATION/libc++abi.a $PYTHON_LIBRARY_LOCATION -lm -lc -lgcc_s -lgcc -lutil -lpthread -ldl"

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_LOCATION"
    -DCMAKE_C_COMPILER="$C_COMPILER"
    -DCMAKE_CXX_COMPILER="$CXX_COMPILER"
    -DCMAKE_CXX_FLAGS="$CXX_FLAGS"
    -DCMAKE_EXE_LINKER_FLAGS="$CXX_LINKER"
    -DCMAKE_MODULE_LINKER_FLAGS="$CXX_LINKER"
    -DCMAKE_SHARED_LINKER_FLAGS="$CXX_LINKER"
    -DCMAKE_STATIC_LINKER_FLAGS="$CXX_LINKER"
    -DCMAKE_PREFIX_PATH="$TBB_CMAKE_LOCATION;$IMATH_CMAKE_LOCATION;$ALEMBIC_CMAKE_LOCATION;$MATERIALX_CMAKE_LOCATION"
    -DPython3_EXECUTABLE="$PYTHON_EXECUTABLE_LOCATION"
    -DPython3_INCLUDE_DIR="$PYTHON_INCLUDE_LOCATION"
    -DPython3_LIBRARY="$PYTHON_LIBRARY_LOCATION"
    -DPXR_BUILD_ALEMBIC_PLUGIN=ON
    -DPXR_ENABLE_HDF5_SUPPORT=OFF
    -DOPENSUBDIV_INCLUDE_DIR="$OPENSUBDIV_INCLUDE_DIR"
    -DOPENSUBDIV_ROOT_DIR="$OPENSUBDIV_LIB_LOCATION"
    -DPXR_ENABLE_MATERIALX_SUPPORT=ON
    -DBUILD_SHARED_LIBS=ON
    -DPXR_BUILD_TESTS=OFF
    -DPXR_BUILD_EXAMPLES=OFF
    -DPXR_BUILD_TUTORIALS=OFF
    -DPXR_BUILD_USD_TOOLS=OFF
    -DPXR_BUILD_IMAGING=ON
    -DPXR_BUILD_USD_IMAGING=ON
    -DPXR_ENABLE_GL_SUPPORT=OFF
    -DPXR_BUILD_USDVIEW=OFF
)

NUM_CPU=`grep -c ^processor /proc/cpuinfo`

echo Configuring Release build for $LIBRARY_NAME version $LIBRARY_VERSION...
cmake -G "Unix Makefiles" $OPENUSD_SOURCE_LOCATION -DCMAKE_BUILD_TYPE=Release "${CMAKE_ARGS[@]}"

echo Building $LIBRARY_NAME for Release...
cmake --build . -j$NUM_CPU

echo Installing $LIBRARY_NAME for Release...
cmake --install .

popd > /dev/null

INSTALL_BIN_LOCATION="$INSTALL_LOCATION/bin"
INSTALL_LIB_LOCATION="$INSTALL_LOCATION/lib"

echo Removing command-line tools...
rm -rf "$INSTALL_BIN_LOCATION"

echo Moving built-in $LIBRARY_NAME plugins to UsdResources plugins directory...
INSTALL_RESOURCES_LOCATION="$INSTALL_LOCATION/Resources/UsdResources/Linux"
INSTALL_RESOURCES_PLUGINS_LOCATION="$INSTALL_RESOURCES_LOCATION/plugins"
mkdir -p $INSTALL_RESOURCES_LOCATION
mv "$INSTALL_LIB_LOCATION/usd" "$INSTALL_RESOURCES_PLUGINS_LOCATION"

echo Moving $LIBRARY_NAME plugin shared libraries to lib directory...
INSTALL_PLUGIN_LOCATION="$INSTALL_LOCATION/plugin"
INSTALL_PLUGIN_USD_LOCATION="$INSTALL_PLUGIN_LOCATION/usd"
mv $INSTALL_PLUGIN_USD_LOCATION/*.so "$INSTALL_LIB_LOCATION"

echo Removing top-level $LIBRARY_NAME plugins plugInfo.json file...
rm -f "$INSTALL_PLUGIN_USD_LOCATION/plugInfo.json"

echo Moving $LIBRARY_NAME plugin resource directories to UsdResources plugins directory
mv "$INSTALL_PLUGIN_USD_LOCATION/hioAvif" "$INSTALL_RESOURCES_PLUGINS_LOCATION"
mv "$INSTALL_PLUGIN_USD_LOCATION/sdrGlslfx" "$INSTALL_RESOURCES_PLUGINS_LOCATION"
mv "$INSTALL_PLUGIN_USD_LOCATION/usdAbc" "$INSTALL_RESOURCES_PLUGINS_LOCATION"
mv "$INSTALL_PLUGIN_USD_LOCATION/usdShaders" "$INSTALL_RESOURCES_PLUGINS_LOCATION"

rmdir "$INSTALL_PLUGIN_USD_LOCATION"
rmdir "$INSTALL_PLUGIN_LOCATION"

echo Removing CMake files...
rm -rf "$INSTALL_LOCATION/cmake"
rm -f $INSTALL_LOCATION/*.cmake

echo Removing Python .pyc files...
find "$INSTALL_LOCATION" -name "*.pyc" -delete

echo Removing pxr.Tf.testenv Python module...
rm -rf "$INSTALL_LOCATION/lib/python/pxr/Tf/testenv"

echo Moving Python modules to Content...
INSTALL_CONTENT_LOCATION="$INSTALL_LOCATION/Content/Python/Lib/Linux/site-packages"
mkdir -p "$INSTALL_CONTENT_LOCATION"
mv "$INSTALL_LOCATION/lib/python/pxr" "$INSTALL_CONTENT_LOCATION"
rmdir "$INSTALL_LOCATION/lib/python"

echo Removing share directory...
rm -rf "$INSTALL_LOCATION/share"

# The locations of the shared libraries where they will live when ultimately
# deployed are used to generate relative paths for use as rpaths and as
# LibraryPaths in plugInfo.json files.
# The OpenUSD Python module shared libraries and OpenUSD plugins all exist at
# the same directory level, so any of them can be used to generate a relative
# path.
USD_PLUGIN_LOCATION="$UE_ENGINE_LOCATION/Plugins/Runtime/USDCore/Resources/UsdResources/Linux/plugins/usd"
USD_PYTHON_MODULE_LOCATION="$UE_ENGINE_LOCATION/Plugins/Runtime/USDCore/Content/Python/Lib/Linux/site-packages/pxr/Usd"
USD_LIBS_LOCATION="$UE_ENGINE_LOCATION/Plugins/Runtime/USDCore/Source/ThirdParty/Linux/bin/$ARCH_NAME"

echo Adjusting plugInfo.json LibraryPath fields...
USD_PLUGIN_TO_USD_LIBS_REL_PATH=`python -c "import os.path; print(os.path.relpath('$USD_LIBS_LOCATION', '$USD_PLUGIN_LOCATION'))"`

for PLUG_INFO_FILE in `find $INSTALL_RESOURCES_LOCATION -name plugInfo.json | xargs grep LibraryPath -l`
do
    sed -i "s|\"LibraryPath\": \"[\./]\+\(.*\)\"|\"LibraryPath\": \"$USD_PLUGIN_TO_USD_LIBS_REL_PATH/\1\"|" $PLUG_INFO_FILE
done

echo Cleaning @rpath entries for shared libraries...
# The OpenUSD Python modules link first against the OpenUSD libraries within
# the plugin directory followed by libraries in the engine binaries.
ENGINE_BINARIES_LOCATION="$UE_ENGINE_LOCATION/Binaries/Linux"
PYTHON_TO_USD_LIBS_REL_PATH=`python -c "import os.path; print(os.path.relpath('$USD_LIBS_LOCATION', '$USD_PYTHON_MODULE_LOCATION'))"`
PYTHON_TO_ENGINE_BINARIES_REL_PATH=`python -c "import os.path; print(os.path.relpath('$ENGINE_BINARIES_LOCATION', '$USD_PYTHON_MODULE_LOCATION'))"`

for PY_SHARED_LIB in `find $INSTALL_CONTENT_LOCATION -name '*.so'`
do
    patchelf --set-rpath "\$ORIGIN/$PYTHON_TO_USD_LIBS_REL_PATH:\$ORIGIN/$PYTHON_TO_ENGINE_BINARIES_REL_PATH" --force-rpath $PY_SHARED_LIB
done

# The OpenUSD libraries link first against sibling libraries in the same
# directory followed by libraries in the engine binaries.
USD_LIBS_TO_ENGINE_BINARIES_REL_PATH=`python -c "import os.path; print(os.path.relpath('$ENGINE_BINARIES_LOCATION', '$USD_LIBS_LOCATION'))"`

for USD_SHARED_LIB in `find $INSTALL_LIB_LOCATION -name '*.so'`
do
    patchelf --set-rpath "\$ORIGIN/.:\$ORIGIN/$USD_LIBS_TO_ENGINE_BINARIES_REL_PATH" --force-rpath $USD_SHARED_LIB
done

echo Done.
