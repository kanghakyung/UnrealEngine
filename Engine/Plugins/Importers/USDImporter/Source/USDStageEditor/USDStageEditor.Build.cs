// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDStageEditor : ModuleRules
	{
		public USDStageEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"DesktopPlatform",
					"DesktopWidgets",
					"EditorFramework",
					"Engine",
					"InputCore",
					"LevelEditor",
					"LevelSequence",
					"LiveLinkEditor", // For SLiveLinkSubjectRepresentationPicker
					"LiveLinkInterface", // For the ULiveLinkAnimation/TransformRole classes
					"Projects", // So that we can use the IPluginManager, required for our custom style
					"SceneOutliner",
					"Slate",
					"SlateCore",
					"UnrealEd",
					"UnrealUSDWrapper",
					"USDClasses",
					"USDSchemas",
					"USDStage",
					"USDStageEditorViewModels",
					"USDStageImporter",
					"USDUtilities",
					"WorkspaceMenuStructure",
				}
			);

			PrivateDefinitions.Add("SUPPRESS_PER_MODULE_INLINE_FILE"); // This module does not use core's standard operator new/delete overloads
		}
	}
}
