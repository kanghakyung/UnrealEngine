#!/bin/sh

# Copyright Epic Games, Inc. All Rights Reserved.

SCRIPT_DIR=$(cd $(dirname $0) && pwd)

BUILD_DIR="${SCRIPT_DIR}/../../TVOS/Build"

if [ -d "${BUILD_DIR}" ]; then
	rm -rf "${BUILD_DIR}"
fi
mkdir -pv "${BUILD_DIR}"

VERSION=$(xcodebuild -version 2>&1 | head -n 1)

#if [[ ${VERSION} != "Xcode 12.4" ]]; then
#	echo Please use Xcode 12.4
#	exit 1
#fi

echo Building with ${VERSION}

cd "${BUILD_DIR}"
../../../../../../Extras/ThirdPartyNotUE/CMake/bin/cmake -G "Xcode" -DEXPAT_BUILD_TOOLS=0 -DEXPAT_BUILD_EXAMPLES=0 -DEXPAT_BUILD_TESTS=0 -DEXPAT_SHARED_LIBS=0 -DCMAKE_XCODE_ATTRIBUTE_SDKROOT=appletvos -DCMAKE_XCODE_ATTRIBUTE_TVOS_DEPLOYMENT_TARGET=15.0 -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO -DCMAKE_XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY=3 "${SCRIPT_DIR}/../.."

function build()
{
	CONFIGURATION=$1
	xcodebuild BITCODE_GENERATION_MODE=bitcode -project expat.xcodeproj -configuration $CONFIGURATION -destination generic/platform=tvOS -arch arm64
	mkdir -p ../${CONFIGURATION}/
	cp -v ${CONFIGURATION}-appletvos/* ../${CONFIGURATION}/
}

build Release
build Debug
cd "${SCRIPT_DIR}"
rm -rf "${BUILD_DIR}"
