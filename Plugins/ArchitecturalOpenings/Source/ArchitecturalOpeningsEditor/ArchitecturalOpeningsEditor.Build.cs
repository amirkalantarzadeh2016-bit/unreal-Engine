// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ArchitecturalOpeningsEditor : ModuleRules
{
	public ArchitecturalOpeningsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ArchitecturalOpenings"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"EditorFramework",
				"EditorSubsystem",
				"ToolMenus",
				"PropertyEditor",
				"Projects",
				"LevelEditor",
				"WorkspaceMenuStructure",
				"AssetTools",
				"AssetRegistry",
				"RenderCore",

				// Assisted mesh extraction.
				"MeshDescription",
				"StaticMeshDescription"
			});

		// This module is Type "Editor" in the .uplugin, so none of it ships in a packaged game.
	}
}
