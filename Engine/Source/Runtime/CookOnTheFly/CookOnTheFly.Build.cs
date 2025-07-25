// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CookOnTheFly : ModuleRules
{
	public CookOnTheFly(ReadOnlyTargetRules Target) : base(Target)
	{
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
		PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "Sockets",
                "Networking"
            }
        );
    }
}
