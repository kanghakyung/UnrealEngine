#!/bin/sh
# Copyright Epic Games, Inc. All Rights Reserved.

## 
## Most of the following script is intended to be consistent for building all Mac 
## third-party source. The sequence of steps are -
## 1) Set up constants, create temp dir, checkout files, save file info
## 2) lib-specific build steps
## 3) Check files were updated

##
## Lib specific constants

# library versions - expected to match tarball and directory names
VER=libvpx-1.14.1

# Name of lib
LIB_NAME="vpx"
BUILD_UNIVERSAL=true
# Drops from the location of this script to where libfiles are relative to
#  e.g.
#  {DROP_TO_LIBROOT}/README
#  {DROP_TO_LIBROOT}/include)
#  ${DROP_TO_LIBROOT}/$LIBFILES[0])
DROP_TO_LIBROOT=../..
# Drops from the location of LIBROOT to Engine/Source/ThirdParrty
DROP_TO_THIRDPARTY=..

# Path to libs from libroot
LIB_PATH="${VER}/lib/Mac"

# files we build
LIBFILES=( 
    "${LIB_PATH}/libvpx.a"
    "${LIB_PATH}/libvpx_fPIC.a"
)

##
## Common setup steps

# Build script will be in <lib>/Build/Mac so get that path and drop two folders to leave us
# in the actual lib folder
pushd . > /dev/null

SCRIPT_DIR="$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)"
cd ${SCRIPT_DIR}/${DROP_TO_LIBROOT}
LIB_ROOT_DIR=${PWD}
echo Changed to ${LIB_ROOT_DIR}

# We should be in ThirdParty/LibName and we want to pull in some common things from
# ThirdParty/BuildScripts/Mac/Common
source ${DROP_TO_THIRDPARTY}/BuildScripts/Mac/Common/Common.sh

echo Rebuilding ${LIB_NAME} in $PWD

# create a tempdir and save it (note the tmpdir variable is used by the functions that 
# check file state)
TMPDIR="/tmp/${LIB_NAME}-$$"
mkdir -p ${TMPDIR} > /dev/null 2>&1

# checkout the library list and save their state (uses TMPDIR)
checkoutFiles ${LIBFILES[@]}
saveFileStates ${LIBFILES[@]}

###################
# Determine the python binary
# Check if "python" is available
if command -v python &>/dev/null; then
    PYTHON_CMD="python"
# If "python" is not found, check for "python3"
elif command -v python3 &>/dev/null; then
    PYTHON_CMD="python3"
else
    echo "Error: Python is not installed." >&2
    exit 1
fi

echo "Using $PYTHON_CMD for running Python commands."


####################
# vpx specific steps

#####################
# configuration

# don't forget to match archive options with tarball type (bz/gz)
TARBALL=${SCRIPT_DIR}/../$VER.tar.gz
PYTHONFILE=add_build_version.py
SRC_PYTHONFILE=${SCRIPT_DIR}/$PYTHONFILE

# includ PID in scratch dir - needs to be absolute
SCRATCH_DIR=${TMPDIR}/build
SOURCE_DIR=$SCRATCH_DIR/$VER


#####################
# unpack

rm -rf $SCRATCH_DIR
mkdir -p $SCRATCH_DIR

echo "#######################################"
echo "# Unpacking the tarballs"
tar xzf $TARBALL -C $SCRATCH_DIR

if [ $? -ne 0 ]; then
	echo ""
	echo "#######################################"
	echo "# tarball $PWD/$TARBALL not found !"
	echo ""
	exit 1
fi

####################
# copy python script to add build version to asm files
cp $SRC_PYTHONFILE $SOURCE_DIR/$PYTHONFILE

#####################
# build

cd $SOURCE_DIR
echo changed to $PWD

echo "Running python script to add build version to asm files"
$PYTHON_CMD $PYTHONFILE 

if [ "$BUILD_UNIVERSAL" = true ] ; then
	SLICES=( 
		"x86_64-darwin22-gcc"
		"arm64-darwin22-gcc"
	)
else
	SLICES=( 
		"x86_64-darwin22-gcc"
	)
fi

for SLICE in "${SLICES[@]}"
do
	BUILD_CFLAGS="-fvisibility=hidden -mmacosx-version-min=14.0"
	BUILD_CXXFLAGS="-fvisibility=hidden -mmacosx-version-min=15.1"
	CONFIG_OPTIONS="--enable-postproc --enable-multi-res-encoding --enable-temporal-denoising --enable-vp9-temporal-denoising --enable-vp9-postproc --size-limit=16384x16384 --enable-realtime-only --disable-examples --disable-tools --disable-docs --disable-unit-tests --disable-libyuv --enable-vp9-highbitdepth --disable-avx512 --disable-neon-dotprod --disable-shared --enable-static --as=yasm"

	echo "#######################################"
	echo "# Configuring $VER for ${SLICE}"
	./configure --target=${SLICE} ${CONFIG_OPTIONS} --disable-pic --extra-cflags="${BUILD_CFLAGS}"
	if [ $? -ne 0 ]; then
		echo "#######################################"
		echo "# ERROR: Failed to run .configure"
		exit 1
	fi

	echo "# Building $VER"
	make clean >> /dev/null && make -j$(get_core_count)

	if [ $? -ne 0 ]; then
		echo "#######################################"
		echo "# ERROR: Make Failed "
		exit 1
	fi

	# use some hardcoded knowledge and get static library out
	#cp $SOURCE_DIR/libvpx.a ${LIB_ROOT_DIR}/${LIB_PATH}
	cp $SOURCE_DIR/libvpx.a $TMPDIR/${SLICE}_libvpx.a

	#####################
	# build PIC version

	cd $SOURCE_DIR
	echo "#######################################"

	echo "# Configuring $VER for ${SLICE} with PIC"
	./configure --target=${SLICE} ${CONFIG_OPTIONS} --enable-pic --extra-cflags="${BUILD_CFLAGS}"

	if [ $? -ne 0 ]; then
		echo "#######################################"
		echo "# ERROR: Failed to run .configure"
		exit 1
	fi

	echo "# Building $VER with PIC"
	make clean >> /dev/null && make -j$(get_core_count)

	if [ $? -ne 0 ]; then
		echo "#######################################"
		echo "# ERROR: Make Failed "
		exit 1
	fi

	# use some hardcoded knowledge and get static library out
	#cp $SOURCE_DIR/libvpx.a ${LIB_ROOT_DIR}/${LIB_PATH}/libvpx_fPIC.a
	cp $SOURCE_DIR/libvpx.a $TMPDIR/${SLICE}_libvpx_fPIC.a
done

if [ "$BUILD_UNIVERSAL" = true ] ; then
	echo "Creating universal libraries from slices"
	lipo -create $TMPDIR/${SLICES[0]}_libvpx.a $TMPDIR/${SLICES[1]}_libvpx.a -o ${LIB_ROOT_DIR}/${LIB_PATH}/libvpx.a
	lipo -create $TMPDIR/${SLICES[0]}_libvpx_fPIC.a $TMPDIR/${SLICES[1]}_libvpx_fPIC.a -o ${LIB_ROOT_DIR}/${LIB_PATH}/libvpx_fPIC.a
else
	# just copy the single x86 slice
	cp -v $TMPDIR/${SLICES[0}_libvpx.a ${LIB_ROOT_DIR}/${LIB_PATH}/libvpx.a
	cp -v $TMPDIR/${SLICES[0}_libvpx_fPIC.a ${LIB_ROOT_DIR}/${LIB_PATH}/libvpx_fPIC.a
fi

if [ $? -eq 0 ]; then
	echo ""
	echo "#######################################"
	echo "# Newly built libs have been put into ${LIB_ROOT_DIR}/${LIB_PATH}"
	echo ""
fi

# back to where our libs are relative to
cd ${LIB_ROOT_DIR}

# check the files were all touched
checkFilesWereUpdated ${LIBFILES[@]}

checkFilesAreFatBinaries ${LIBFILES[@]}

echo The following files were rebuilt: ${LIBFILES[@]}

popd > /dev/null
