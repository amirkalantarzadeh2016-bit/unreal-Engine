// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningPreviewManager.h"

#include "ArchOpeningComponent.h"
#include "ArchOpeningLog.h"

#include "Editor.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectSaveContext.h"

void FArchOpeningPreviewManager::Initialize()
{
	// The core ticker runs in the editor with no world ticking, which is exactly what preview needs.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		TEXT("ArchOpeningPreview"), 0.0f,
		[this](float DeltaSeconds) { return Tick(DeltaSeconds); });

	if (GEditor != nullptr)
	{
		PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &FArchOpeningPreviewManager::HandlePreBeginPIE);
	}

	PreSaveWorldHandle = FEditorDelegates::PreSaveWorldWithContext.AddRaw(this, &FArchOpeningPreviewManager::HandlePreSaveWorld);

	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		MapChangeHandle = LevelEditor.OnMapChanged().AddLambda(
			[this](UWorld*, EMapChangeType) { RestoreAll(); });
	}
}

void FArchOpeningPreviewManager::Shutdown()
{
	// Restore before tearing down: a preview pose must never be left behind in a level.
	RestoreAll();

	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	if (PreBeginPIEHandle.IsValid())
	{
		FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
		PreBeginPIEHandle.Reset();
	}

	if (PreSaveWorldHandle.IsValid())
	{
		FEditorDelegates::PreSaveWorldWithContext.Remove(PreSaveWorldHandle);
		PreSaveWorldHandle.Reset();
	}

	if (MapChangeHandle.IsValid() && FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditor.OnMapChanged().Remove(MapChangeHandle);
		MapChangeHandle.Reset();
	}

	Tracked.Reset();
}

void FArchOpeningPreviewManager::Track(UArchOpeningComponent* Opening)
{
	if (!::IsValid(Opening))
	{
		return;
	}

	Tracked.AddUnique(Opening);
}

bool FArchOpeningPreviewManager::IsTracking(const UArchOpeningComponent* Opening) const
{
	return Tracked.ContainsByPredicate(
		[Opening](const TWeakObjectPtr<UArchOpeningComponent>& Entry) { return Entry.Get() == Opening; });
}

void FArchOpeningPreviewManager::StopPreviewing(UArchOpeningComponent* Opening)
{
	if (::IsValid(Opening))
	{
		Opening->StopPreview();
	}
}

void FArchOpeningPreviewManager::RestoreAndStopTracking(UArchOpeningComponent* Opening)
{
	if (::IsValid(Opening))
	{
		Opening->RestorePrePreviewPose();
	}

	Tracked.RemoveAll(
		[Opening](const TWeakObjectPtr<UArchOpeningComponent>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Opening;
		});
}

void FArchOpeningPreviewManager::RestoreAll()
{
	for (const TWeakObjectPtr<UArchOpeningComponent>& Entry : Tracked)
	{
		if (UArchOpeningComponent* Opening = Entry.Get())
		{
			Opening->RestorePrePreviewPose();
		}
	}

	Tracked.Reset();
}

bool FArchOpeningPreviewManager::Tick(float DeltaSeconds)
{
	// Deleted actors drop out here rather than crashing: everything is held weakly.
	for (int32 Index = Tracked.Num() - 1; Index >= 0; --Index)
	{
		UArchOpeningComponent* Opening = Tracked[Index].Get();

		if (Opening == nullptr)
		{
			Tracked.RemoveAtSwap(Index);
			continue;
		}

		if (Opening->IsPreviewActive())
		{
			Opening->TickPreview(DeltaSeconds);
		}
		else if (!Opening->HasPendingPreviewState())
		{
			// Nothing left to restore, so there is nothing to keep watching.
			Tracked.RemoveAtSwap(Index);
		}
	}

	return true;	// Keep ticking.
}

void FArchOpeningPreviewManager::HandlePreBeginPIE(bool /*bIsSimulating*/)
{
	// PIE duplicates the editor world. Restoring first means the play session starts from the
	// authored closed pose, never from whatever the artist happened to be previewing.
	RestoreAll();
}

void FArchOpeningPreviewManager::HandlePreSaveWorld(UWorld* /*World*/, FObjectPreSaveContext /*Context*/)
{
	// A preview pose must never be written into a saved level.
	RestoreAll();
}
