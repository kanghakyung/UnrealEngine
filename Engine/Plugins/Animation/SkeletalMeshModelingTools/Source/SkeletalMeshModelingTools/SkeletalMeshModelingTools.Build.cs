﻿// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SkeletalMeshModelingTools : ModuleRules
{
	public SkeletalMeshModelingTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"EditorFramework",
				"EditorInteractiveToolsFramework",
				"Engine",
				"InputCore",
				"InteractiveToolsFramework",
				"MeshModelingTools",
				"MeshModelingToolsEditorOnly",
				"ModelingComponentsEditorOnly",
				"ModelingToolsEditorMode",
				"SkeletalMeshEditor",
				"SkeletalMeshUtilitiesCommon",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"ToolWidgets",
				"UnrealEd",
				"StatusBar",
				"PropertyEditor",
				"AnimationCore",
				"AnimationWidgets",
				"ApplicationCore",
				"WidgetRegistration",
				"SkeletalMeshModifiers",
				"Persona",
				"MeshDescription",
				"StaticMeshDescription",
				"SkeletalMeshDescription",
				"GeometryCore",
				"ModelingComponents",
				"MeshConversion",
				"InterchangeEngine"
			}
		);

		ShortName = "SkelMeshModTools";
	}
}
