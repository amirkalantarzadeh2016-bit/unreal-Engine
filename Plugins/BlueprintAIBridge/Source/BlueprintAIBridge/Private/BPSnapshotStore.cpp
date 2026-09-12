// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPSnapshotStore.h"

#include "BlueprintAIBridgeModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString FBPSnapshotStore::GetSnapshotDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BlueprintAIBridge/Snapshots/"));
}

FString FBPSnapshotStore::AssetPathToSafeFilename(const FString& AssetPath)
{
	FString Safe = AssetPath;
	Safe.ReplaceInline(TEXT("/"), TEXT("_"));
	Safe.ReplaceInline(TEXT("\\"), TEXT("_"));
	Safe.ReplaceInline(TEXT("."), TEXT("_"));
	Safe.ReplaceInline(TEXT(" "), TEXT("_"));
	Safe.ReplaceInline(TEXT(":"), TEXT("_"));

	// Collapse the leading separators left by paths such as "/Game/Blueprints/BP_Door".
	while (Safe.StartsWith(TEXT("_")))
	{
		Safe.RightChopInline(1);
	}

	if (Safe.IsEmpty())
	{
		Safe = TEXT("UnnamedBlueprint");
	}

	return Safe;
}

FString FBPSnapshotStore::SaveSnapshot(const FString& AssetPath, const FString& JsonContent)
{
	if (JsonContent.IsEmpty())
	{
		UE_LOG(LogBlueprintAIBridge, Warning, TEXT("SaveSnapshot: refusing to write an empty snapshot for '%s'."), *AssetPath);
		return FString();
	}

	const FString Directory = GetSnapshotDirectory();

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Directory) && !PlatformFile.CreateDirectoryTree(*Directory))
	{
		UE_LOG(LogBlueprintAIBridge, Error, TEXT("SaveSnapshot: could not create snapshot directory '%s'."), *Directory);
		return FString();
	}

	// Millisecond granularity, not seconds: an export immediately followed by a format would
	// otherwise land on the same filename and the first snapshot would be lost. The stamp stays
	// a plain integer so GetSnapshotsForAsset can keep sorting on it numerically.
	const FDateTime Now = FDateTime::UtcNow();
	const int64 Stamp = Now.ToUnixTimestamp() * 1000 + Now.GetMillisecond();

	const FString FileName = FString::Printf(
		TEXT("%s_%lld.json"),
		*AssetPathToSafeFilename(AssetPath),
		Stamp);

	const FString FullPath = Directory / FileName;

	if (!FFileHelper::SaveStringToFile(JsonContent, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogBlueprintAIBridge, Error, TEXT("SaveSnapshot: failed to write '%s'."), *FullPath);
		return FString();
	}

	UE_LOG(LogBlueprintAIBridge, Log, TEXT("Saved snapshot '%s'."), *FullPath);
	return FullPath;
}

TArray<FString> FBPSnapshotStore::GetSnapshotsForAsset(const FString& AssetPath)
{
	TArray<FString> Results;

	const FString Directory = GetSnapshotDirectory();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Directory))
	{
		return Results;
	}

	const FString Prefix = AssetPathToSafeFilename(AssetPath) + TEXT("_");

	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *(Directory / TEXT("*.json")), /*Files*/ true, /*Directories*/ false);

	for (const FString& FileName : FileNames)
	{
		if (FileName.StartsWith(Prefix))
		{
			Results.Add(Directory / FileName);
		}
	}

	// Filenames end in a fixed-width-ish Unix timestamp, but sorting on the parsed number is
	// the only ordering that stays correct as the timestamp gains digits.
	Results.Sort([&Prefix](const FString& A, const FString& B)
	{
		auto TimestampOf = [&Prefix](const FString& Path) -> int64
		{
			const FString Base = FPaths::GetBaseFilename(Path);
			if (!Base.StartsWith(Prefix))
			{
				return 0;
			}
			return FCString::Atoi64(*Base.RightChop(Prefix.Len()));
		};

		return TimestampOf(A) > TimestampOf(B);
	});

	return Results;
}

bool FBPSnapshotStore::LoadLatestSnapshot(const FString& AssetPath, FString& OutJson)
{
	OutJson.Reset();

	const TArray<FString> Snapshots = GetSnapshotsForAsset(AssetPath);
	if (Snapshots.Num() == 0)
	{
		return false;
	}

	if (!FFileHelper::LoadFileToString(OutJson, *Snapshots[0]))
	{
		UE_LOG(LogBlueprintAIBridge, Error, TEXT("LoadLatestSnapshot: failed to read '%s'."), *Snapshots[0]);
		OutJson.Reset();
		return false;
	}

	return true;
}
