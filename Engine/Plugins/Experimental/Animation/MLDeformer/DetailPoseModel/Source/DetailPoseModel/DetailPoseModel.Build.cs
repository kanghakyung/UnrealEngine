// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DetailPoseModel : ModuleRules
{
	public DetailPoseModel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
		);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
		);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
				// ... add other public dependencies that you statically link with here ...
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ComputeFramework",
				"CoreUObject",
				"Engine",
				"GeometryCache",
				"OptimusCore",
				"Projects",
				"RenderCore",
				"RHI",
				"MLDeformerFramework",
				"NNE",
				"NNERuntimeBasicCpu",
				"NeuralMorphModel"
			}
		);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
	}
}
