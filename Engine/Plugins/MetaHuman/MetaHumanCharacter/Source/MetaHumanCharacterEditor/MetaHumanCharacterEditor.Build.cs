// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class MetaHumanCharacterEditor : ModuleRules
{
	public MetaHumanCharacterEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"MetaHumanCoreTechLib",
			"RigLogicModule",
			"MetaHumanSDKEditor",
		});


		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"ApplicationCore",
			"UnrealEd",
			"Engine",
			"SlateCore",
			"Slate",
			"InputCore",
			"Projects",
			"AppFramework",
			"AssetDefinition",
			"AnimGraph",
			"AnimGraphRuntime",
			"DataflowEngine",
			"BlueprintGraph",
			"EditorFramework",
			"InteractiveToolsFramework",
			"EditorInteractiveToolsFramework",
			"ToolMenus",
			"ToolWidgets",
			"StatusBar",
			"ModelingComponents",
			"ModelingComponentsEditorOnly",
			"GeometryCore",
			"EditorScriptingUtilities",
			"EditorSubsystem",
			"PropertyEditor",
			"ImageCore",
			"RigLogicLib",
			"WidgetRegistration",
			"DeveloperSettings",
			"GeometryFramework",
			"ContentBrowser",
			"ContentBrowserData",
			"Kismet",
			"EditorWidgets",
			"DesktopWidgets",
			"KismetWidgets",
			"ClassViewer",
			"SkeletalMeshDescription",
			"SkeletalMeshUtilitiesCommon",
			"DNACalibModule",
			"PerformanceCaptureCore",
			"IKRig",
			"RigVM",
			"RigVMEditor",
			"RigVMDeveloper",
			"ControlRigDeveloper",
			"ControlRigEditor",
			"ControlRig",
			"DirectoryWatcher",
			"HairStrandsCore",
			"ChaosClothAssetEngine",
			"ChaosOutfitAssetEngine",
			"MeshDescription",
			"MessageLog",
			"DerivedDataCache",
			"Json",
			"TextureUtilitiesCommon",
			"StaticMeshDescription",
			"MetaHumanIdentity",
			"MetaHumanConfig",
			"MetaHumanCore",
			"MetaHumanPipeline",
			"MetaHumanCharacter",
			"MetaHumanCharacterPalette",
			"MetaHumanCharacterPaletteEditor",
			"MetaHumanDefaultPipeline",
			"InterchangeDNA",
			"HTTP",
			"MetaHumanSDKRuntime",
			"PhysicsCore",
			"LiveLinkInterface",
			"AnimGraphRuntime",
			"GeometryScriptingCore",
			"GeometryScriptingEditor",
			"RenderCore",
			"ControlRigDeveloper",
			"LauncherPlatform",
			"TextureGraph",
			"FileUtilities",
			"JsonUtilities",
			"RigLogicLib",
			"MeshConversion",
			"LevelSequence",
			"LevelSequenceEditor",
		});
	}
}