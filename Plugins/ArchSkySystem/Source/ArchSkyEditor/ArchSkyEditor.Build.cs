// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

/**
 * ArchSkyEditor - viewport visualiser, details customisation, scene validator
 * and toolbar entries. Declared as "Editor" in the .uplugin, so none of this is
 * ever compiled into a packaged game.
 */
public class ArchSkyEditor : ModuleRules
{
	public ArchSkyEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ArchSkyRuntime"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"ComponentVisualizers",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"InputCore",
				"ToolMenus",
				"MessageLog",
				"EditorFramework"
			});

		IWYUSupport = IWYUSupport.Full;
	}
}
