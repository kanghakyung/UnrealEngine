// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ColorCorrectRegionsEditor : ModuleRules
{
	public ColorCorrectRegionsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ActorPickerMode",
				"Core",
				"CoreUObject",
				"Engine",
				"LevelEditor",
				"Projects",
				"UnrealEd",
				"Slate",
				"SlateCore",
				"ColorCorrectRegions",
				"ColorGradingEditor",
				"PlacementMode",
				"SceneOutliner",
				"ObjectMixerEditor",
				"ToolMenus",
				"TypedElementRuntime"
			}
		);
	}
}
