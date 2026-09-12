// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Saves and loads named snapshots of export JSON under Saved/BlueprintAIBridge/Snapshots/,
 * so the plugin can diff against what was actually handed to the AI and offer a revert.
 *
 * Every function logs and fails soft; file I/O problems never propagate as exceptions.
 */
class BLUEPRINTAIBRIDGE_API FBPSnapshotStore
{
public:
	/**
	 * Saves JSON as a snapshot keyed by asset path + timestamp.
	 * @return the full path of the written file, or an empty string on failure.
	 */
	static FString SaveSnapshot(const FString& AssetPath, const FString& JsonContent);

	/** Loads the most recent snapshot for an asset path. */
	static bool LoadLatestSnapshot(const FString& AssetPath, FString& OutJson);

	/** Returns all snapshot file paths for an asset, sorted newest first. */
	static TArray<FString> GetSnapshotsForAsset(const FString& AssetPath);

	/** Absolute path of the snapshot directory (created on demand by SaveSnapshot). */
	static FString GetSnapshotDirectory();

private:
	static FString AssetPathToSafeFilename(const FString& AssetPath);
};
