// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

/**
 * Runtime capture / video-export module.
 *
 * Backend B (SceneCapture + ImageWriteQueue + ffmpeg) has no optional dependencies and is
 * therefore always compiled. Backend A (Movie Render Pipeline) lives inside the engine's
 * MovieRenderPipeline plugin, which is NOT enabled in a default project; the plugin is
 * listed as "Optional" in ArchVizTour.uplugin so a project without it still builds.
 *
 * Because an Optional plugin reference means "enable it if it exists", the module list has
 * to be probed on disk before it can be referenced: naming a module from a disabled or
 * absent plugin is a hard UBT error, not a link-time one. WITH_ARCHVIZTOUR_MRP is the single
 * switch every Backend A source file keys off.
 */
public class ArchVizTourCapture : ModuleRules
{
	public ArchVizTourCapture(ReadOnlyTargetRules Target) : base(Target)
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
				"DeveloperSettings",
				"LevelSequence",
				"MovieScene"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RenderCore",
				"RHI",
				"ImageCore",
				"ImageWrapper",
				// Asynchronous, off-game-thread PNG/EXR writing.
				"ImageWriteQueue",
				"CinematicCamera",
				"Projects",
				"Json",
				"JsonUtilities"
			});

		bool bMovieRenderPipelineAvailable = IsPluginPresent("MovieRenderPipeline");
		if (bMovieRenderPipelineAvailable)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"MovieRenderPipelineCore",
					"MovieRenderPipelineRenderPasses",
					"MovieRenderPipelineSettings"
				});
		}

		PublicDefinitions.Add("WITH_ARCHVIZTOUR_MRP=" + (bMovieRenderPipelineAvailable ? "1" : "0"));
	}

	/** True when <PluginName>.uplugin exists anywhere under the engine or project plugin trees. */
	private bool IsPluginPresent(string PluginName)
	{
		string FileName = PluginName + ".uplugin";

		string EnginePluginRoot = Path.Combine(EngineDirectory, "Plugins");
		if (Directory.Exists(EnginePluginRoot)
			&& Directory.EnumerateFiles(EnginePluginRoot, FileName, SearchOption.AllDirectories).GetEnumerator().MoveNext())
		{
			return true;
		}

		if (Target.ProjectFile != null)
		{
			string ProjectPluginRoot = Path.Combine(Target.ProjectFile.Directory.FullName, "Plugins");
			if (Directory.Exists(ProjectPluginRoot)
				&& Directory.EnumerateFiles(ProjectPluginRoot, FileName, SearchOption.AllDirectories).GetEnumerator().MoveNext())
			{
				return true;
			}
		}

		return false;
	}
}
