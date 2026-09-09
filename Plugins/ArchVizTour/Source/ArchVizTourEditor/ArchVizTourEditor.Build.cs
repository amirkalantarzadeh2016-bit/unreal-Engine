// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

/**
 * Editor module. Declared as Type "Editor" in ArchVizTour.uplugin, so UnrealBuildTool never
 * compiles or stages it into a packaged client. Nothing in ArchVizTourRuntime or
 * ArchVizTourCapture links against it; the dependency arrow only ever points this way.
 */
public class ArchVizTourEditor : ModuleRules
{
	public ArchVizTourEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ArchVizTourRuntime",
				"UnrealEd",
				"DeveloperSettings"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"InputCore",
				"PropertyEditor",
				"EditorFramework",
				"EditorSubsystem",
				"ToolMenus",
				"AssetTools",
				"AssetRegistry",
				"ContentBrowser",
				"DesktopPlatform",
				"Projects",
				"Json",
				"JsonUtilities",
				"CinematicCamera",
				// Level Sequence bake path.
				"LevelSequence",
				"MovieScene",
				"MovieSceneTracks",
				"MovieSceneTools",
				"Sequencer",
				// UTourPresetManagerWidgetBase derives from UEditorUtilityWidget.
				"Blutility",
				"UMG",
				"UMGEditor"
			});
	}
}
