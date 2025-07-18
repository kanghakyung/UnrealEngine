// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SVGDistanceField : ModuleRules
{
	public SVGDistanceField(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine"
			}
		);
	}
}
