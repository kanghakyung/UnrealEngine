#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

# Check if already set up
if [ ! "$UE_DOTNET_DIR" == "" ]; then
	return
fi

START_DIR=`pwd`
cd "$1"

IS_DOTNET_INSTALLED=0
DOTNET_VERSION_PATH=$(command -v dotnet) || true

# gcServer can cause "An error occurred trying to start process 'X' with working directory 'Y'. Too many open files"
export DOTNET_gcServer=0

if [ "$UE_USE_SYSTEM_DOTNET" == "1" ] && [ ! $DOTNET_VERSION_PATH == "" ] && [ -f $DOTNET_VERSION_PATH ]; then
	# If dotnet is installed, check that it has a new enough version of the SDK
	DOTNET_SDKS=(`dotnet --list-sdks | grep -P "(\d*)\.(\d*)\..* \[(.*)\]"`)
	for DOTNET_SDK in $DOTNET_SDKS
	do
		if [ ${DOTNET_SDK[0]} -gt 3 ]; then
			IS_DOTNET_INSTALLED=1
		fi

		if [ ${DOTNET_SDK[0]} -eq 3 ]; then
			if [ ${DOTNET_SDK[1]} -ge 1 ]; then
				IS_DOTNET_INSTALLED=1
			fi
		fi
	done
	if [ $IS_DOTNET_INSTALLED -eq 0 ]; then
	echo Unable to find installed dotnet sdk of version 3.1 or newer
	fi
fi

# Setup bundled Dotnet if cannot use installed one
if [ $IS_DOTNET_INSTALLED -eq 0 ]; then
	echo Setting up bundled DotNet SDK
	CUR_DIR=`pwd`

	# Select the preferred architecture for the current system
	ARCH=x64
	[ $(uname -m) == "arm64" ] && ARCH=arm64 
	
	export UE_DOTNET_DIR=$CUR_DIR/../../../Binaries/ThirdParty/DotNet/8.0.300/mac-$ARCH
	chmod u+x "$UE_DOTNET_DIR/dotnet"
	echo $UE_DOTNET_DIR
	export PATH=$UE_DOTNET_DIR:$PATH
	export DOTNET_ROOT=$UE_DOTNET_DIR
else
	export IS_DOTNET_INSTALLED=$IS_DOTNET_INSTALLED
fi

# this is the current assumed location for now
# We use FUnixPlatformProcess::ApplicationSettingsDir() from c++ and
# and for C# it uses Environment.GetFolderPath(SpecialFolder.ApplicationData)
# for this location, so lets share this as our "place to put an AutoSDK file"
AUTO_SDK_PATH_FILE="$HOME/.config/.autosdk"

# if the file exists and we dont currently have a $UE_SDKS_ROOT set, lets setup UE_SDKS_ROOT to our files location path
if [ -f "$AUTO_SDK_PATH_FILE" ] && [ -z "$UE_SDKS_ROOT" ]; then
	export UE_SDKS_ROOT="$(cat $AUTO_SDK_PATH_FILE)"
fi

cd "$START_DIR"
