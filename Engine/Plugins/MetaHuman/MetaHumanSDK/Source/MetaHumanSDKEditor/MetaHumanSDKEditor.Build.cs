﻿// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanSDKEditor : ModuleRules
{
	public MetaHumanSDKEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[]
			{
				// ... add public include paths required here ...
			}
		);


		PrivateIncludePaths.AddRange(
			new string[]
			{
				// ... add other private include paths required here ...
			}
		);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				// ... add other public dependencies that you statically link with here ...
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetDefinition",
				"ApplicationCore",
				"BlueprintGraph",
				"ContentBrowser",
				"ControlRig",
				"ControlRigDeveloper",
				"CoreUObject",
				"DerivedDataCache",
				"EditorScriptingUtilities",
				"FileUtilities",
				"HTTP",
				"HairStrandsCore",
				"InputCore",
				"Json",
				"JsonUtilities",
				"MetaHumanSDKRuntime",
				"Projects",
				"RigLogicModule",
				"RigLogicDeveloper",
				"RigVMDeveloper",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"ToolWidgets",
				"UnrealEd",
			}
		);


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);

		// for MH cloud service request compression
		AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
		// for MH cloud service request format
		AddEngineThirdPartyPrivateStaticDependencies(Target, "Protobuf");

		if (Target.Type == TargetType.Editor)
		{
			// for authentication 
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"EOSSDK",
					"EOSShared"
				}
			);
		}
	}
}