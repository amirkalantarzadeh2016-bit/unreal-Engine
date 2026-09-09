// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

/**
 * Runtime module: data layer, actors, subsystem, geometry math, persistence and the UMG
 * base classes. Contains NO editor-only dependencies so the whole module compiles into a
 * Shipping client. Editor-only helpers inside this module are guarded with WITH_EDITOR and
 * only ever call Engine-level APIs (Modify / MarkPackageDirty / PostEditChangeProperty),
 * all of which exist in the Engine module rather than UnrealEd.
 */
public class ArchVizTourRuntime : ModuleRules
{
	public ArchVizTourRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				// UUserWidget bases (UTourPlaybackWidgetBase / UTourAuthoringWidgetBase) are public.
				"UMG",
				// FGameplayTag appears in the public FTourStep struct.
				"GameplayTags",
				// UCineCameraComponent / ACineCameraActor appear in public headers.
				"CinematicCamera",
				// ETourPlaybackBackend::Sequencer needs ULevelSequencePlayer at runtime.
				"LevelSequence",
				"MovieScene",
				// UTourRuntimeSettings derives from UDeveloperSettings.
				"DeveloperSettings",
				// UTourInputConfig exposes UInputMappingContext / UInputAction.
				"EnhancedInput"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"InputCore",
				// FJsonObjectConverter for the preset import/export path.
				"Json",
				"JsonUtilities",
				"MovieSceneTracks",
				"Projects"
			});
	}
}
