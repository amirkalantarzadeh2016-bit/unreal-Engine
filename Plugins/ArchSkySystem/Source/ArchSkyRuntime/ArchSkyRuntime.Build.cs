// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

/**
 * ArchSkyRuntime - the whole shipping surface of the plugin.
 *
 * ARCH NOTE: this module must stay free of every editor module. The visualiser,
 * the details customisation and the scene validator all live in ArchSkyEditor.
 * The only editor-facing code here is guarded by WITH_EDITOR and uses nothing
 * beyond Engine-level API (PostEditChangeProperty, UArrowComponent, ...), which
 * is compiled into Engine itself and therefore safe in a packaged build.
 */
public class ArchSkyRuntime : ModuleRules
{
	public ArchSkyRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public because UArchSkyWidgetBase derives from UUserWidget and
		// UArchSkyViewModel is referenced from public widget headers.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMG",
				"SlateCore"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Widget input handling / focus.
				"Slate",

				// FMaterialParameterCollectionInstanceResource + render-thread
				// safe checks used by the batched MPC write path.
				"RenderCore",

				// UArchSkySettings : UDeveloperSettings.
				"DeveloperSettings",

				// Compact quantised net serialisation of FArchSkyReplicatedState.
				"NetCore",

				// ARCH NOTE: an addition to the originally specified dependency list.
				// PART 6 requires runtime presets to be stored "as JSON in SavedGames",
				// and FJsonObjectConverter - the only reflection-driven struct/JSON bridge
				// in the engine - lives in JsonUtilities. Hand-rolling the serialisation to
				// avoid the dependency would mean re-listing every FArchSkyState field by
				// hand and silently dropping any field added later. JsonUtilities is a
				// Runtime module and packages on every platform.
				"JsonUtilities"
			});

		// Enables IWYU-style compilation on 5.4+ without tripping the
		// deprecated bEnforceIWYU / PCHUsage combinations.
		IWYUSupport = IWYUSupport.Full;
	}
}
