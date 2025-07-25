// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "ILauncherProfile.h"


/**
 * Delegate type for SLegacyProjectLauncher Profile running.
 *
 * The first parameter is the SLegacyProjectLauncher Profile that you want to run.
 */
DECLARE_DELEGATE_OneParam(FOnProfileRun, const ILauncherProfileRef&)
