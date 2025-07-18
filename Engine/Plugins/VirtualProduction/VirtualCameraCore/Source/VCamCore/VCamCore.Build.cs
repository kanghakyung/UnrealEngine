// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VCamCore : ModuleRules
{
	public VCamCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"Composure",
				"Core",
				"CinematicCamera",
				"DeveloperSettings",
				"EnhancedInput",
				"RemoteSession",
				"VPUtilities"
			});
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"UMG",
				"GameplayTags",
				"LiveLinkInterface",
				"MediaIOCore",
				"InputCore",
				"RenderCore",
				"Slate",
				"SlateCore",
				"VPRoles",
				"VPSettings"
			});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Concert",
					"ConcertSyncClient",
					"EditorFramework",
					"InputEditor",
					"LevelEditor",
					"MultiUserClient",
					"Projects",
					"UnrealEd"
				}
			);
		}
	}
}
