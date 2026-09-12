// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BlueprintAIBridge : ModuleRules
{
	public BlueprintAIBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"EditorFramework",
				"UnrealEd",
				"BlueprintGraph",
				"KismetCompiler",
				"GraphEditor",
				"Json",
				"JsonUtilities",
				"ApplicationCore",
				"DeveloperSettings",
				"ToolMenus",
				"LevelEditor"
			});

		// Editor-only support modules. PropertyEditor supplies SObjectPropertyEntryBox for the
		// Blueprint picker; WorkspaceMenuStructure supplies the Window menu category for the tab.
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore",
				"Projects",
				"PropertyEditor",
				"WorkspaceMenuStructure"
			});
	}
}
