// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Minimap : ModuleRules
{
	public Minimap(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// UMG is public because UMinimapWidgetBase / UMinimapMarkerWidget derive from
		// UUserWidget and are exposed in public headers.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMG"
			});

		// Slate is only needed for widget geometry + render transforms inside the .cpp files.
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore"
			});

		// No editor-only modules: AMinimapBoundsVolume's editor helpers are guarded by
		// WITH_EDITOR and use only Engine-level APIs (TActorIterator), so the module
		// stays a pure Runtime module and is safe in packaged/shipping builds.
	}
}
