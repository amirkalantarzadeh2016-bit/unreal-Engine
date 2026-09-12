// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ArchOpeningPieceSetComponent.h"
#include "UObject/GCObjectScopeGuard.h"

namespace ArchOpeningPieceSetTests
{
	/** One synthetic piece: a triangle count and a bounding size. */
	struct FPieceSpec
	{
		int32 TriangleCount;
		FVector Size;
	};

	/** A piece set with N pieces of the given triangle counts and sizes, all stationary. */
	UArchOpeningPieceSetComponent* MakePieceSet(const TArray<FPieceSpec>& Specs)
	{
		UArchOpeningPieceSetComponent* Set = NewObject<UArchOpeningPieceSetComponent>(GetTransientPackage());
		Set->InitializeDefaultGroups();

		for (int32 Index = 0; Index < Specs.Num(); ++Index)
		{
			FArchOpeningPieceRecord& Piece = Set->Pieces.AddDefaulted_GetRef();
			Piece.Key = 1000 + Index * 7;	// Arbitrary but distinct, as real triangle ids would be.
			Piece.TriangleCount = Specs[Index].TriangleCount;
			Piece.VertexCount = Specs[Index].TriangleCount * 3;
			Piece.LocalBounds = FBox(FVector::ZeroVector, Specs[Index].Size);
			Piece.GroupIndex = 0;
		}

		return Set;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningPieceSetGroupTest,
	"ArchitecturalOpenings.Extraction.Groups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningPieceSetGroupTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningPieceSetTests;

	UArchOpeningPieceSetComponent* Set = MakePieceSet({
		{100, FVector(90, 5, 200)},		// leaf slab
		{76,  FVector(4, 2, 180)},		// moulding
		{76,  FVector(4, 2, 180)},		// identical moulding
		{76,  FVector(2, 4, 180)},		// same moulding rotated 90 degrees
		{12,  FVector(4, 2, 180)},		// same size, different triangle count
		{500, FVector(100, 12, 210)}	// frame
	});
	FGCObjectScopeGuard Guard(Set);

	// Defaults: a stationary bucket plus one movable leaf, with the movable one active.
	TestEqual(TEXT("Two default groups"), Set->Groups.Num(), 2);
	TestTrue(TEXT("Group 0 is stationary"), Set->Groups[0].Role == EArchOpeningGroupRole::Stationary);
	TestTrue(TEXT("Group 1 is movable"), Set->Groups[1].Role == EArchOpeningGroupRole::Movable);
	TestEqual(TEXT("The movable group starts active"), Set->ActiveGroupIndex, 1);
	TestEqual(TEXT("Every piece starts stationary"), Set->CountPiecesInGroup(0), 6);

	// Group names must stay unique, because they end up on asset names. Removing a group and
	// adding one used to mint a duplicate.
	const int32 SecondLeaf = Set->AddMovableGroup();
	TestEqual(TEXT("Adding a group appends it"), SecondLeaf, 2);

	Set->RemoveGroup(1);
	const int32 ReplacementLeaf = Set->AddMovableGroup();

	TestTrue(TEXT("A re-added group does not collide with an existing name"),
		Set->Groups[ReplacementLeaf].GroupName != Set->Groups[1].GroupName);

	{
		TSet<FName> Names;
		for (const FArchOpeningExtractionGroup& Group : Set->Groups)
		{
			TestFalse(TEXT("No two groups share a name"), Names.Contains(Group.GroupName));
			Names.Add(Group.GroupName);
		}
	}

	// An explicit rename must also be de-duplicated.
	Set->RenameGroup(2, Set->Groups[1].GroupName);
	TestTrue(TEXT("A rename onto a taken name is made unique"),
		Set->Groups[2].GroupName != Set->Groups[1].GroupName);

	// Removing a group returns its pieces to stationary and shifts the later ones down without
	// re-pointing anything at the wrong group.
	Set->AssignPieceToGroup(0, 1);
	Set->AssignPieceToGroup(1, 2);

	const FName SurvivingName = Set->Groups[2].GroupName;
	Set->RemoveGroup(1);

	TestEqual(TEXT("The removed group's piece falls back to stationary"), Set->Pieces[0].GroupIndex, 0);
	TestEqual(TEXT("A later group's piece shifts down with it"), Set->Pieces[1].GroupIndex, 1);
	TestTrue(TEXT("The shifted piece still points at the same group"),
		Set->Groups[Set->Pieces[1].GroupIndex].GroupName == SurvivingName);

	// Group 0 is the fallback bucket and must not be removable.
	TestFalse(TEXT("The stationary group cannot be removed"), Set->RemoveGroup(0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningPieceSetSelectionTest,
	"ArchitecturalOpenings.Extraction.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningPieceSetSelectionTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningPieceSetTests;

	UArchOpeningPieceSetComponent* Set = MakePieceSet({
		{100, FVector(90, 5, 200)},		// 0: leaf slab
		{76,  FVector(4, 2, 180)},		// 1: moulding
		{76,  FVector(4, 2, 180)},		// 2: identical moulding
		{76,  FVector(2, 4, 180)},		// 3: same moulding, rotated
		{12,  FVector(4, 2, 180)},		// 4: same size, different triangle count
		{500, FVector(100, 12, 210)}	// 5: frame
	});
	FGCObjectScopeGuard Guard(Set);

	// Select Similar is the answer to "twelve identical mouldings", so it has to catch the rotated
	// copy and reject the piece that merely happens to share a bounding size.
	Set->SetSelection(1);
	const int32 Matched = Set->SelectSimilarTo(1, /*SizeTolerance*/ 0.5f);

	TestTrue(TEXT("Similar finds the identical moulding"), Set->SelectedPieces.Contains(2));
	TestTrue(TEXT("Similar finds the rotated moulding"), Set->SelectedPieces.Contains(3));
	TestFalse(TEXT("Similar rejects a same-sized piece with a different triangle count"),
		Set->SelectedPieces.Contains(4));
	TestFalse(TEXT("Similar rejects the leaf slab"), Set->SelectedPieces.Contains(0));
	TestFalse(TEXT("Similar rejects the frame"), Set->SelectedPieces.Contains(5));
	TestEqual(TEXT("Three mouldings matched"), Matched, 3);

	// Assigning the ticked set moves exactly those pieces.
	Set->AssignSelectionToGroup(1);
	TestEqual(TEXT("The ticked mouldings moved to the leaf group"), Set->CountPiecesInGroup(1), 3);
	TestEqual(TEXT("Everything else stayed stationary"), Set->CountPiecesInGroup(0), 3);

	// Invert, all, none.
	Set->InvertSelection();
	TestEqual(TEXT("Invert flips the ticked set"), Set->SelectedPieces.Num(), 3);
	TestFalse(TEXT("Invert untickes a previously ticked piece"), Set->SelectedPieces.Contains(1));

	Set->SelectAll();
	TestEqual(TEXT("Select all ticks every piece"), Set->SelectedPieces.Num(), 6);

	Set->ClearSelection();
	TestEqual(TEXT("Clear empties the ticked set"), Set->SelectedPieces.Num(), 0);

	Set->ToggleSelection(4);
	TestTrue(TEXT("Toggle ticks"), Set->SelectedPieces.Contains(4));
	Set->ToggleSelection(4);
	TestFalse(TEXT("Toggle unticks"), Set->SelectedPieces.Contains(4));

	// Out-of-range indices must be ignored rather than corrupting the set.
	Set->AssignPieceToGroup(99, 1);
	Set->AssignPieceToGroup(0, 99);
	Set->ToggleSelection(-1);
	TestEqual(TEXT("An out-of-range assignment changes nothing"), Set->CountPiecesInGroup(1), 3);
	TestEqual(TEXT("An out-of-range toggle ticks nothing"), Set->SelectedPieces.Num(), 0);

	// Piece keys are the identity saved profiles match on, so they must be distinct and findable.
	TestEqual(TEXT("A piece is found by its key"), Set->FindPieceByKey(Set->Pieces[3].Key), 3);
	TestEqual(TEXT("An unknown key reports not found"), Set->FindPieceByKey(-12345), INDEX_NONE);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
