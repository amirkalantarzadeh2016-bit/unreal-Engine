// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ArchitecturalOpenings : ModuleRules
{
	public ArchitecturalOpenings(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// FCollisionShape / collision channel enums. Engine already re-exports these,
				// but depending on it explicitly keeps the include legal if that ever changes.
				"PhysicsCore"
			});

		// Deliberately no UnrealEd / Slate / editor modules here. Everything editor-facing on
		// this module is behind WITH_EDITOR and uses Engine + CoreUObject APIs only, so the
		// module stays packageable.
	}
}
