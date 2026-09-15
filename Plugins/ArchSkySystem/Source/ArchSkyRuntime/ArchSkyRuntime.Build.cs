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

				// EKeys::SpaceBar and friends, used by the widget's keyboard and gamepad
				// shortcuts. EKeys' members are static data living in InputCore; UMG and
				// SlateCore consume that module without re-exporting it, so a module that
				// names a key has to depend on it directly.
				"InputCore",

				// ARCH NOTE: an addition to the originally specified dependency list.
				// PART 6 requires runtime presets to be stored "as JSON in SavedGames",
				// and FJsonObjectConverter - the only reflection-driven struct/JSON bridge
				// in the engine - lives in JsonUtilities. Hand-rolling the serialisation to
				// avoid the dependency would mean re-listing every FArchSkyState field by
				// hand and silently dropping any field added later. Both modules are
				// Runtime and package on every platform.
				//
				// "Json" is NOT redundant here. JsonObjectStringToUStruct is a template, so
				// TJsonSerializer, the FJsonValue hierarchy and LogJson are all instantiated
				// in THIS module's translation unit rather than inside JsonUtilities - and
				// they resolve out of Json, which JsonUtilities does not re-export.
				// Declaring only JsonUtilities compiles cleanly and then fails at link with
				// several dozen unresolved FJsonValue symbols.
				"Json",
				"JsonUtilities"
			});

		// Enables IWYU-style compilation on 5.4+ without tripping the
		// deprecated bEnforceIWYU / PCHUsage combinations.
		IWYUSupport = IWYUSupport.Full;
	}
}
