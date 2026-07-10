// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

using UnrealBuildTool;

public class VRS : ModuleRules
{
	public VRS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"GameplayTags",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"Json",
				"JsonUtilities",
			}
		);

		// FEditorDelegates (EndPIE flush in the debug recorder) lives in UnrealEd.
		if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
	}
}
