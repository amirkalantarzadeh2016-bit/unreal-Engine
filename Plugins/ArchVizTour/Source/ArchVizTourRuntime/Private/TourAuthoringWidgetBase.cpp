// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourAuthoringWidgetBase.h"

#include "ArchVizTourLog.h"
#include "Camera/PlayerCameraManager.h"
#include "CineCameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TourPath.h"
#include "TourPersistenceLibrary.h"
#include "TourSequencePreset.h"
#include "TourSubsystem.h"

UTourSubsystem* UTourAuthoringWidgetBase::GetTourSubsystem() const
{
	return UTourSubsystem::Get(this);
}

bool UTourAuthoringWidgetBase::WarnIfNoTargetPath(const TCHAR* Operation) const
{
	if (IsValid(TargetPath))
	{
		return false;
	}

	UE_LOG(LogArchVizTour, Warning,
		TEXT("Tour authoring widget '%s': %s needs a Target Path, but none is set."),
		*GetName(), Operation);
	return true;
}

void UTourAuthoringWidgetBase::CommitPathEdit()
{
	if (!IsValid(TargetPath))
	{
		return;
	}

	TargetPath->SyncSplineFromPoints();
	TargetPath->RebuildRailMeshes();
	OnPathEdited(TargetPath->Points.Num());
}

bool UTourAuthoringWidgetBase::GetCurrentViewState(FTourCameraState& OutState) const
{
	OutState = FTourCameraState();

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const APlayerController* Controller = World->GetFirstPlayerController();
	if (Controller == nullptr || Controller->PlayerCameraManager == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("Tour authoring: no player camera to read a viewpoint from."));
		return false;
	}

	OutState.Location = Controller->PlayerCameraManager->GetCameraLocation();
	OutState.Rotation = Controller->PlayerCameraManager->GetCameraRotation();

	// Capture the real lens when the player is already looking through a cine camera; otherwise
	// the struct defaults stand in, which is the honest answer for a plain game camera.
	if (const AActor* ViewTarget = Controller->GetViewTarget())
	{
		if (const UCineCameraComponent* Cine = ViewTarget->FindComponentByClass<UCineCameraComponent>())
		{
			OutState.FocalLength = Cine->CurrentFocalLength;
			OutState.Aperture    = Cine->CurrentAperture;
			if (Cine->FocusSettings.FocusMethod == ECameraFocusMethod::Manual)
			{
				OutState.FocusDistance = Cine->FocusSettings.ManualFocusDistance;
			}
		}
	}

	OutState.bValid = true;
	return true;
}

int32 UTourAuthoringWidgetBase::AddPointFromCurrentView(bool bUseExplicitRotation)
{
	if (WarnIfNoTargetPath(TEXT("AddPointFromCurrentView")))
	{
		return INDEX_NONE;
	}

	FTourCameraState ViewState;
	if (!GetCurrentViewState(ViewState))
	{
		return INDEX_NONE;
	}

	const FTransform WorldTransform(ViewState.Rotation, ViewState.Location);
	const int32 NewIndex = InsertPoint(WorldTransform, /*InsertIndex*/ -1);

	if (TargetPath->Points.IsValidIndex(NewIndex))
	{
		FTourPoint& Point = TargetPath->Points[NewIndex];
		Point.bUseExplicitRotation = bUseExplicitRotation;
		Point.FocalLength          = ViewState.FocalLength;
		Point.Aperture             = ViewState.Aperture;
		Point.ManualFocusDistance  = ViewState.FocusDistance;
		CommitPathEdit();
	}

	return NewIndex;
}

int32 UTourAuthoringWidgetBase::InsertPoint(const FTransform& WorldTransform, int32 InsertIndex)
{
	if (WarnIfNoTargetPath(TEXT("InsertPoint")))
	{
		return INDEX_NONE;
	}

	const FTransform WorldToLocal = TargetPath->GetActorTransform().Inverse();

	FTourPoint Point;
	Point.Location = WorldToLocal.TransformPosition(WorldTransform.GetLocation());
	Point.Rotation = (WorldToLocal.GetRotation() * WorldTransform.GetRotation()).Rotator();

	// Seed the tangents from the neighbouring point so a freshly added point does not produce a
	// zero-length tangent and a visible corner.
	if (TargetPath->Points.Num() > 0)
	{
		const FTourPoint& Neighbour = TargetPath->Points.Last();
		const FVector Chord = Point.Location - Neighbour.Location;
		Point.ArriveTangent = Chord;
		Point.LeaveTangent  = Chord;
		Point.Speed         = Neighbour.Speed;
		Point.FocalLength   = Neighbour.FocalLength;
		Point.Aperture      = Neighbour.Aperture;
	}

	int32 NewIndex = INDEX_NONE;
	if (TargetPath->Points.IsValidIndex(InsertIndex))
	{
		TargetPath->Points.Insert(Point, InsertIndex);
		NewIndex = InsertIndex;
	}
	else
	{
		NewIndex = TargetPath->Points.Add(Point);
	}

	CommitPathEdit();
	return NewIndex;
}

bool UTourAuthoringWidgetBase::DeletePoint(int32 PointIndex)
{
	if (WarnIfNoTargetPath(TEXT("DeletePoint")))
	{
		return false;
	}

	if (!TargetPath->Points.IsValidIndex(PointIndex))
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("DeletePoint(%d): out of range (%d points)."),
			PointIndex, TargetPath->Points.Num());
		return false;
	}

	TargetPath->Points.RemoveAt(PointIndex);
	CommitPathEdit();
	return true;
}

bool UTourAuthoringWidgetBase::ReorderPoint(int32 FromIndex, int32 ToIndex)
{
	if (WarnIfNoTargetPath(TEXT("ReorderPoint")))
	{
		return false;
	}

	if (!TargetPath->Points.IsValidIndex(FromIndex) || !TargetPath->Points.IsValidIndex(ToIndex))
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("ReorderPoint(%d -> %d): out of range (%d points)."),
			FromIndex, ToIndex, TargetPath->Points.Num());
		return false;
	}

	if (FromIndex == ToIndex)
	{
		return true;
	}

	const FTourPoint Moved = TargetPath->Points[FromIndex];
	TargetPath->Points.RemoveAt(FromIndex);
	TargetPath->Points.Insert(Moved, ToIndex);

	CommitPathEdit();
	return true;
}

int32 UTourAuthoringWidgetBase::DuplicatePoint(int32 PointIndex)
{
	if (WarnIfNoTargetPath(TEXT("DuplicatePoint")))
	{
		return INDEX_NONE;
	}

	if (!TargetPath->Points.IsValidIndex(PointIndex))
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("DuplicatePoint(%d): out of range (%d points)."),
			PointIndex, TargetPath->Points.Num());
		return INDEX_NONE;
	}

	const FTourPoint Copy = TargetPath->Points[PointIndex];
	const int32 NewIndex = PointIndex + 1;
	TargetPath->Points.Insert(Copy, NewIndex);

	CommitPathEdit();
	return NewIndex;
}

bool UTourAuthoringWidgetBase::UpdatePointFromCurrentView(int32 PointIndex)
{
	if (WarnIfNoTargetPath(TEXT("UpdatePointFromCurrentView")))
	{
		return false;
	}

	if (!TargetPath->Points.IsValidIndex(PointIndex))
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("UpdatePointFromCurrentView(%d): out of range (%d points)."),
			PointIndex, TargetPath->Points.Num());
		return false;
	}

	FTourCameraState ViewState;
	if (!GetCurrentViewState(ViewState))
	{
		return false;
	}

	const FTransform WorldToLocal = TargetPath->GetActorTransform().Inverse();

	FTourPoint& Point = TargetPath->Points[PointIndex];
	Point.Location             = WorldToLocal.TransformPosition(ViewState.Location);
	Point.Rotation             = (WorldToLocal.GetRotation() * ViewState.Rotation.Quaternion()).Rotator();
	Point.bUseExplicitRotation = true;
	Point.FocalLength          = ViewState.FocalLength;
	Point.Aperture             = ViewState.Aperture;
	Point.ManualFocusDistance  = ViewState.FocusDistance;

	CommitPathEdit();
	return true;
}

bool UTourAuthoringWidgetBase::GetPoint(int32 PointIndex, FTourPoint& OutPoint) const
{
	if (!IsValid(TargetPath) || !TargetPath->Points.IsValidIndex(PointIndex))
	{
		OutPoint = FTourPoint();
		return false;
	}

	OutPoint = TargetPath->Points[PointIndex];
	return true;
}

bool UTourAuthoringWidgetBase::SetPoint(int32 PointIndex, const FTourPoint& InPoint)
{
	if (WarnIfNoTargetPath(TEXT("SetPoint")))
	{
		return false;
	}

	if (!TargetPath->Points.IsValidIndex(PointIndex))
	{
		return false;
	}

	TargetPath->Points[PointIndex] = InPoint;
	CommitPathEdit();
	return true;
}

int32 UTourAuthoringWidgetBase::GetPointCount() const
{
	return IsValid(TargetPath) ? TargetPath->Points.Num() : 0;
}

UTourSequencePreset* UTourAuthoringWidgetBase::DuplicatePreset(const UTourSequencePreset* Source)
{
	if (Source == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("DuplicatePreset: null source."));
		return nullptr;
	}

	UTourSequencePreset* Copy = NewObject<UTourSequencePreset>(this, NAME_None, RF_Transient);
	check(Copy != nullptr);

	Copy->Steps           = Source->Steps;
	Copy->ReferencedPaths = Source->ReferencedPaths;
	Copy->bLoopTour       = Source->bLoopTour;
	Copy->GlobalTimeScale = Source->GlobalTimeScale;
	Copy->TourTitle       = Source->TourTitle;
	Copy->BakedSequence   = Source->BakedSequence;
	Copy->PlaybackBackend = Source->PlaybackBackend;
	// A duplicate is a different tour, so it gets a different identity.
	Copy->PresetId        = FGuid::NewGuid();
	Copy->SchemaVersion   = UTourSequencePreset::CurrentSchemaVersion;

	return Copy;
}

UTourSequencePreset* UTourAuthoringWidgetBase::CreatePresetFromTargetPath(FName PathName, const FText& TourTitle)
{
	if (WarnIfNoTargetPath(TEXT("CreatePresetFromTargetPath")))
	{
		return nullptr;
	}

	if (!TargetPath->IsTraversable())
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("CreatePresetFromTargetPath: '%s' has fewer than two points."), *TargetPath->GetName());
		return nullptr;
	}

	if (PathName == NAME_None)
	{
		PathName = TargetPath->GetFName();
	}
	TargetPath->Tags.AddUnique(PathName);

	UTourSequencePreset* Preset = NewObject<UTourSequencePreset>(this, NAME_None, RF_Transient);
	check(Preset != nullptr);

	FTourStep Step;
	Step.StepType      = ETourStepType::SplineMove;
	Step.Label         = TourTitle.IsEmpty() ? FText::FromName(PathName) : TourTitle;
	Step.SplinePathRef = PathName;
	Step.StartInputKey = 0.0f;
	// EndInputKey <= StartInputKey means "the whole spline", which is what a one-step tour wants.
	Step.EndInputKey   = 0.0f;
	// Zero duration on a spline step means "derive it from the authored cm/s speed".
	Step.Duration      = 0.0f;

	Preset->Steps.Add(Step);
	Preset->TourTitle = TourTitle;

	return Preset;
}

bool UTourAuthoringWidgetBase::SaveCurrentTourToSlot(const FString& SlotName)
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	if (Subsystem == nullptr)
	{
		return false;
	}

	const UTourSequencePreset* Preset = Subsystem->GetLoadedTour();
	if (Preset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("SaveCurrentTourToSlot('%s'): no tour is loaded."), *SlotName);
		return false;
	}

	// The loaded tour's soft path references may be empty for a runtime-authored tour, so the
	// target path's own geometry is written explicitly. Without this a slot saved in a packaged
	// build would come back with steps but no curves for them to follow.
	TArray<FTourPathData> Paths;
	TArray<FName> PathNames;

	if (IsValid(TargetPath))
	{
		Paths.Add(TargetPath->BuildPathData());
		PathNames.Add(TargetPath->Tags.Num() > 0 ? TargetPath->Tags[0] : TargetPath->GetFName());
	}

	return UTourPersistenceLibrary::SaveTourDataToSlot(
		Preset->Steps, Paths, PathNames, Preset->TourTitle,
		Preset->bLoopTour, Preset->GlobalTimeScale, SlotName, /*UserIndex*/ 0);
}

bool UTourAuthoringWidgetBase::LoadTourFromSlot(const FString& SlotName)
{
	UTourSubsystem* Subsystem = GetTourSubsystem();
	if (Subsystem == nullptr)
	{
		return false;
	}

	UTourSequencePreset* Preset = UTourPersistenceLibrary::LoadTourPresetFromSlot(this, SlotName, /*UserIndex*/ 0);
	if (Preset == nullptr)
	{
		return false;
	}

	return Subsystem->LoadTour(Preset);
}

TArray<FString> UTourAuthoringWidgetBase::EnumerateSlots() const
{
	return UTourPersistenceLibrary::EnumerateTourSlots();
}

bool UTourAuthoringWidgetBase::DeleteSlot(const FString& SlotName)
{
	return UTourPersistenceLibrary::DeleteTourSlot(SlotName, /*UserIndex*/ 0);
}
