// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UniversalObjectLocatorEditor : ModuleRules
{
	public UniversalObjectLocatorEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		// @todo: this is temporarily disabled due to some MovieScene code not compiling
		//CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"PropertyEditor",
				"SlateCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"EditorWidgets",
				"Engine",
				"MessageLog",
				"PropertyEditor",
				"SceneOutliner",
				"Slate",
				"SlateCore",
				"InputCore",
				"Sequencer",
				"UniversalObjectLocator",
				"UnrealEd",
			}
		);
	}
}
