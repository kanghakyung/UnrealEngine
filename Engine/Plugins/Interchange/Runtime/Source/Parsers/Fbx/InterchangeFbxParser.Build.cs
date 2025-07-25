// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class InterchangeFbxParser : ModuleRules
	{
		public InterchangeFbxParser(ReadOnlyTargetRules Target) : base(Target)
		{
			if (Target.Type != TargetType.Editor && Target.Type != TargetType.Program)
			{
				throw new BuildException("InterchangeFbxParser module can be build only on editor or program target.");
			}

			if (Target.Platform != UnrealTargetPlatform.Win64 &&
				Target.Platform != UnrealTargetPlatform.Linux &&
				Target.Platform != UnrealTargetPlatform.Mac)
			{
				throw new BuildException("InterchangeFbxParser module do not support target platform {0}.", Target.Platform.ToString());
			}

			bEnableExceptions = true;

			bDisableAutoRTFMInstrumentation = true; // AutoRTFM cannot be used with exceptions

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"InterchangeCore",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"DeveloperSettings",
					"InterchangeCommonParser",
					"InterchangeMessages",
					"InterchangeNodes",
					"MeshDescription",
					"SkeletalMeshDescription",
					"StaticMeshDescription"
				}
			);

			if (Target.bCompileAgainstEngine)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[] {
						"Engine",
						"InterchangeEngine"
					}
				);

				PrivateIncludePathModuleNames.AddRange(
					new string[] {
						"InterchangeImport"
					}
				);
				
			}

			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"FBX"
			);
		}
	}
}
