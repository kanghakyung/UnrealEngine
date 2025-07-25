// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

namespace UE::AnimNext::ControlRig::Editor
{

class FAnimNextControlRigEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle OnNodeDblClickDelegateHandle;

};

} // end namespace
