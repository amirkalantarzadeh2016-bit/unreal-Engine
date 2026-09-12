// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningPieceSetComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace ArchOpeningPiecePalette
{
	/** Stationary is always this neutral grey; movable groups cycle through the rest. */
	static const FLinearColor StationaryColor(0.55f, 0.55f, 0.58f);

	static const FLinearColor MovableColors[] =
	{
		FLinearColor(0.15f, 0.85f, 0.35f),	// green
		FLinearColor(0.25f, 0.55f, 1.00f),	// blue
		FLinearColor(1.00f, 0.60f, 0.10f),	// orange
		FLinearColor(0.75f, 0.35f, 1.00f),	// purple
		FLinearColor(0.10f, 0.85f, 0.85f),	// cyan
		FLinearColor(1.00f, 0.85f, 0.15f),	// yellow
		FLinearColor(1.00f, 0.35f, 0.65f),	// pink
		FLinearColor(0.60f, 0.80f, 0.20f)	// lime
	};

	static constexpr int32 NumMovableColors = UE_ARRAY_COUNT(MovableColors);
}

UArchOpeningPieceSetComponent::UArchOpeningPieceSetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Editor-only is expressed through the IsEditorOnly() override rather than by writing the
	// engine's own bIsEditorOnly field, which is not part of the public surface.
}

// -------------------------------------------------------------------------------------------
// Groups
// -------------------------------------------------------------------------------------------

void UArchOpeningPieceSetComponent::InitializeDefaultGroups()
{
	Groups.Reset();

	// Group 0 is always the stationary bucket. Every piece starts there, so the classification is
	// complete from the moment analysis finishes and there is no "unassigned" state to forget about.
	FArchOpeningExtractionGroup& Stationary = Groups.AddDefaulted_GetRef();
	Stationary.GroupName = TEXT("Stationary Frame");
	Stationary.Role = EArchOpeningGroupRole::Stationary;
	Stationary.DisplayColor = ArchOpeningPiecePalette::StationaryColor;

	AddMovableGroup();

	ActiveGroupIndex = 1;
}

int32 UArchOpeningPieceSetComponent::AddMovableGroup()
{
	const int32 MovableCount = Groups.FilterByPredicate(
		[](const FArchOpeningExtractionGroup& Group) { return Group.Role == EArchOpeningGroupRole::Movable; }).Num();

	FArchOpeningExtractionGroup& Group = Groups.AddDefaulted_GetRef();
	Group.Role = EArchOpeningGroupRole::Movable;
	Group.DisplayColor = ArchOpeningPiecePalette::MovableColors[MovableCount % ArchOpeningPiecePalette::NumMovableColors];

	// Counting the movable groups is not enough on its own: removing "Movable Leaf 1" and adding a
	// group would otherwise mint a second "Movable Leaf 2". Names end up on asset names, so they
	// have to be unique.
	Group.GroupName = MakeUniqueGroupName(FName(*FString::Printf(TEXT("Movable Leaf %d"), MovableCount + 1)),
		Groups.Num() - 1);

	return Groups.Num() - 1;
}

FName UArchOpeningPieceSetComponent::MakeUniqueGroupName(FName Desired, int32 IgnoreGroupIndex) const
{
	auto IsTaken = [this, IgnoreGroupIndex](FName Candidate)
	{
		for (int32 Index = 0; Index < Groups.Num(); ++Index)
		{
			if (Index != IgnoreGroupIndex && Groups[Index].GroupName == Candidate)
			{
				return true;
			}
		}
		return false;
	};

	if (Desired.IsNone())
	{
		Desired = TEXT("Group");
	}

	if (!IsTaken(Desired))
	{
		return Desired;
	}

	const FString Base = Desired.ToString();
	for (int32 Suffix = 2; Suffix < 1000; ++Suffix)
	{
		const FName Candidate(*FString::Printf(TEXT("%s %d"), *Base, Suffix));
		if (!IsTaken(Candidate))
		{
			return Candidate;
		}
	}

	return Desired;
}

void UArchOpeningPieceSetComponent::RenameGroup(int32 GroupIndex, FName NewName)
{
	if (Groups.IsValidIndex(GroupIndex))
	{
		Groups[GroupIndex].GroupName = MakeUniqueGroupName(NewName, GroupIndex);
	}
}

bool UArchOpeningPieceSetComponent::RemoveGroup(int32 GroupIndex)
{
	// Group 0 is the fallback every removed group's pieces land in, so it cannot itself be removed.
	if (GroupIndex <= 0 || !Groups.IsValidIndex(GroupIndex))
	{
		return false;
	}

	Groups.RemoveAt(GroupIndex);

	// Re-point every assignment: pieces in the removed group fall back to stationary, and pieces in
	// later groups shift down with them.
	for (FArchOpeningPieceRecord& Piece : Pieces)
	{
		if (Piece.GroupIndex == GroupIndex)
		{
			Piece.GroupIndex = 0;
		}
		else if (Piece.GroupIndex > GroupIndex)
		{
			--Piece.GroupIndex;
		}
	}

	ActiveGroupIndex = FMath::Clamp(ActiveGroupIndex, 0, Groups.Num() - 1);
	return true;
}

int32 UArchOpeningPieceSetComponent::CountPiecesInGroup(int32 GroupIndex) const
{
	int32 Count = 0;
	for (const FArchOpeningPieceRecord& Piece : Pieces)
	{
		if (Piece.GroupIndex == GroupIndex)
		{
			++Count;
		}
	}
	return Count;
}

FLinearColor UArchOpeningPieceSetComponent::GetGroupColor(int32 GroupIndex) const
{
	return Groups.IsValidIndex(GroupIndex) ? Groups[GroupIndex].DisplayColor : FLinearColor::White;
}

FLinearColor UArchOpeningPieceSetComponent::GetPieceDrawColor(int32 PieceIndex) const
{
	if (!Pieces.IsValidIndex(PieceIndex))
	{
		return FLinearColor::White;
	}

	FLinearColor Color = GetGroupColor(Pieces[PieceIndex].GroupIndex);

	// Hover wins over selection: it is the thing the artist is pointing at right now.
	if (PieceIndex == HoveredPieceIndex)
	{
		return FLinearColor::White;
	}

	if (SelectedPieces.Contains(PieceIndex))
	{
		Color = Color * 1.9f;
		Color.A = 1.0f;
	}

	return Color;
}

// -------------------------------------------------------------------------------------------
// Assignment and selection
// -------------------------------------------------------------------------------------------

void UArchOpeningPieceSetComponent::AssignPieceToGroup(int32 PieceIndex, int32 GroupIndex)
{
	if (Pieces.IsValidIndex(PieceIndex) && Groups.IsValidIndex(GroupIndex))
	{
		Pieces[PieceIndex].GroupIndex = GroupIndex;
	}
}

void UArchOpeningPieceSetComponent::AssignSelectionToGroup(int32 GroupIndex)
{
	if (!Groups.IsValidIndex(GroupIndex))
	{
		return;
	}

	for (int32 PieceIndex : SelectedPieces)
	{
		if (Pieces.IsValidIndex(PieceIndex))
		{
			Pieces[PieceIndex].GroupIndex = GroupIndex;
		}
	}
}

void UArchOpeningPieceSetComponent::SetSelection(int32 PieceIndex)
{
	SelectedPieces.Reset();
	if (Pieces.IsValidIndex(PieceIndex))
	{
		SelectedPieces.Add(PieceIndex);
	}
}

void UArchOpeningPieceSetComponent::ToggleSelection(int32 PieceIndex)
{
	if (!Pieces.IsValidIndex(PieceIndex))
	{
		return;
	}

	if (SelectedPieces.Contains(PieceIndex))
	{
		SelectedPieces.Remove(PieceIndex);
	}
	else
	{
		SelectedPieces.Add(PieceIndex);
	}
}

void UArchOpeningPieceSetComponent::SelectAll()
{
	SelectedPieces.Reset();
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		SelectedPieces.Add(Index);
	}
}

void UArchOpeningPieceSetComponent::ClearSelection()
{
	SelectedPieces.Reset();
}

void UArchOpeningPieceSetComponent::InvertSelection()
{
	TSet<int32> Inverted;
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		if (!SelectedPieces.Contains(Index))
		{
			Inverted.Add(Index);
		}
	}
	SelectedPieces = MoveTemp(Inverted);
}

int32 UArchOpeningPieceSetComponent::SelectSimilarTo(int32 ReferencePieceIndex, float SizeTolerance)
{
	if (!Pieces.IsValidIndex(ReferencePieceIndex))
	{
		return 0;
	}

	const FArchOpeningPieceRecord& Reference = Pieces[ReferencePieceIndex];
	const FVector ReferenceSize = Reference.LocalBounds.IsValid ? Reference.LocalBounds.GetSize() : FVector::ZeroVector;
	const double Tolerance = FMath::Max(SizeTolerance, 0.0f);

	int32 Matched = 0;

	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		const FArchOpeningPieceRecord& Piece = Pieces[Index];

		// Triangle count alone is a weak match - two unrelated boxes both have twelve - so the
		// bounding size has to agree as well before two pieces count as the same part.
		if (Piece.TriangleCount != Reference.TriangleCount)
		{
			continue;
		}

		const FVector Size = Piece.LocalBounds.IsValid ? Piece.LocalBounds.GetSize() : FVector::ZeroVector;

		// Compare sorted extents so the same moulding rotated 90 degrees still matches.
		FVector SortedReference = ReferenceSize;
		FVector SortedSize = Size;
		auto SortDescending = [](FVector& V)
		{
			double A = V.X, B = V.Y, C = V.Z;
			if (A < B) { Swap(A, B); }
			if (B < C) { Swap(B, C); }
			if (A < B) { Swap(A, B); }
			V = FVector(A, B, C);
		};
		SortDescending(SortedReference);
		SortDescending(SortedSize);

		if (FMath::Abs(SortedSize.X - SortedReference.X) <= Tolerance &&
			FMath::Abs(SortedSize.Y - SortedReference.Y) <= Tolerance &&
			FMath::Abs(SortedSize.Z - SortedReference.Z) <= Tolerance)
		{
			SelectedPieces.Add(Index);
			++Matched;
		}
	}

	return Matched;
}

int32 UArchOpeningPieceSetComponent::FindPieceByKey(int32 Key) const
{
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		if (Pieces[Index].Key == Key)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FTransform UArchOpeningPieceSetComponent::GetSourceWorldTransform() const
{
	// Piece bounds are in the source mesh's local space, so they have to be drawn through the
	// placed component's transform to land on the geometry the artist is looking at.
	return ::IsValid(SourceComponent) ? SourceComponent->GetComponentTransform() : GetComponentTransform();
}
