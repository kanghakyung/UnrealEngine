// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TakeRecorder : ModuleRules
{
	public TakeRecorder(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AppFramework",
				"AssetDefinition",
				"AssetRegistry",
				"ClassViewer",
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"EditorStyle",
				"EditorWidgets",
				"Engine",
				"InputCore",
				"LevelEditor",
				"LevelSequence",
				"MovieScene",
				"MovieSceneTools",
				"Projects",
				"PropertyEditor",
				"TakesCore",
				"TakeMovieScene",
				"TimeManagement",
				"Settings",
				"Slate",
				"SlateCore",
				"EditorFramework",
				"UnrealEd",
				"WorkspaceMenuStructure",
				"Analytics",
				"ToolMenus",
				"ToolWidgets",
				"NamingTokens"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"EditorSubsystem",
				"UMG",
                "TakeTrackRecorders",
                "SerializedRecorderInterface",
                "Sequencer",
				"TraceLog"
            }
        );

		PublicIncludePathModuleNames.AddRange(
			new string[]
			{
				"TakeRecorderNamingTokens"
			}
		);
	}
}
