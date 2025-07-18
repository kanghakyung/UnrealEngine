// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DataLink : ModuleRules
{
	public DataLink(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
			}
		);

		PublicDefinitions.Add("WITH_DATALINK_CONTEXT=!NO_LOGGING");
	}
}
