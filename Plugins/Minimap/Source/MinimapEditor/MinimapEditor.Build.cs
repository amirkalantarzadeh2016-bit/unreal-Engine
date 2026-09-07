// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MinimapEditor : ModuleRules
{
	public MinimapEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Minimap"
			});

		// All editor-only. This module is Type "Editor" in the .uplugin, so none of it is
		// compiled into a packaged game and the runtime module keeps zero editor deps.
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"UnrealEd",
				"PropertyEditor",
				"InputCore",
				"EditorStyle"
			});
	}
}
