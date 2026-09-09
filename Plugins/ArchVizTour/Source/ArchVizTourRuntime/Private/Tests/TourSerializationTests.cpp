// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "TourPathPreset.h"
#include "TourSequencePreset.h"
#include "TourTypes.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ArchVizTourSerializationTests
{
	/** A path with every field set to something distinctive, so a dropped field is visible. */
	static FTourPathData MakePopulatedPathData()
	{
		FTourPathData PathData;
		PathData.bClosedLoop  = true;
		PathData.DefaultSpeed = 137.5f;

		PathData.ArcGenerationParams.GeneratorType = ETourGeneratorType::Helix;
		PathData.ArcGenerationParams.Center        = FVector(11.0, 22.0, 33.0);
		PathData.ArcGenerationParams.Radius        = 654.25f;
		PathData.ArcGenerationParams.StartAngleDeg = 17.5f;
		PathData.ArcGenerationParams.SweepAngleDeg = -212.0f;
		PathData.ArcGenerationParams.PointCount    = 9;
		PathData.ArcGenerationParams.UpAxis        = FVector(0.0, 1.0, 0.0);
		PathData.ArcGenerationParams.HeightDelta   = 480.0f;
		PathData.ArcGenerationParams.HelixTurns    = 2.5f;

		PathData.SplineWorldTransform = FTransform(
			FRotator(10.0f, 20.0f, 30.0f), FVector(100.0, 200.0, 300.0), FVector(1.0, 1.0, 1.0));

		for (int32 Index = 0; Index < 4; ++Index)
		{
			FTourPoint Point;
			Point.Location             = FVector(Index * 250.0, Index * -75.0, 160.0 + Index);
			Point.ArriveTangent        = FVector(300.0, Index * 10.0, 0.0);
			Point.LeaveTangent         = FVector(310.0, Index * 11.0, 1.0);
			Point.Rotation             = FRotator(Index * 3.0f, Index * 12.0f, 0.0f);
			Point.bUseExplicitRotation = (Index % 2) == 0;
			Point.FocalLength          = 24.0f + Index * 6.0f;
			Point.Aperture             = 1.8f + Index * 0.4f;
			Point.ManualFocusDistance  = 500.0f * Index;
			Point.bUseLookAtTarget     = (Index == 3);
			Point.LookAtTargetName     = (Index == 3) ? FName(TEXT("HeroBuilding")) : NAME_None;
			Point.Speed                = 90.0f + Index * 15.0f;
			Point.DwellTime            = (Index == 2) ? 2.5f : 0.0f;
			Point.EaseIn               = 0.25f * Index;
			Point.EaseOut              = 0.15f * Index;
			Point.PointType            = ESplinePointType::CurveCustomTangent;
			Point.Label                = FText::FromString(FString::Printf(TEXT("Point %d"), Index));
			PathData.Points.Add(Point);
		}

		return PathData;
	}

	/** A step list exercising both movement models plus the blend fields. */
	static TArray<FTourStep> MakePopulatedSteps()
	{
		TArray<FTourStep> Steps;

		FTourStep SplineStep;
		SplineStep.StepType      = ETourStepType::SplineMove;
		SplineStep.Label         = FText::FromString(TEXT("Approach"));
		SplineStep.Duration      = 12.5f;
		SplineStep.BlendTime     = 1.75f;
		SplineStep.BlendFunction = VTBlend_EaseInOut;
		SplineStep.BlendExp      = 3.0f;
		SplineStep.bLockOutgoing = true;
		SplineStep.SplinePathRef = FName(TEXT("ApproachPath"));
		SplineStep.StartInputKey = 0.5f;
		SplineStep.EndInputKey   = 2.75f;
		SplineStep.bPauseAtEnd   = true;
		Steps.Add(SplineStep);

		FTourStep StaticStep;
		StaticStep.StepType        = ETourStepType::StaticCamera;
		StaticStep.Label           = FText::FromString(TEXT("Atrium hero"));
		StaticStep.Duration        = 6.0f;
		StaticStep.BlendTime       = 0.0f;
		StaticStep.BlendFunction   = VTBlend_Linear;
		StaticStep.StaticCameraRef = FName(TEXT("AtriumCam"));
		Steps.Add(StaticStep);

		FTourStep DwellStep;
		DwellStep.StepType = ETourStepType::Dwell;
		DwellStep.Label    = FText::FromString(TEXT("Hold"));
		DwellStep.Duration = 3.0f;
		Steps.Add(DwellStep);

		return Steps;
	}
}

// ---------------------------------------------------------------------------
// JSON round trip
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourPathJsonRoundTripTest,
	"ArchVizTour.Serialization.TourPathJsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourPathJsonRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace ArchVizTourSerializationTests;

	UTourPathPreset* Source = NewObject<UTourPathPreset>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("Source preset"), Source))
	{
		return false;
	}

	Source->PathData    = MakePopulatedPathData();
	Source->DisplayName = FText::FromString(TEXT("Courtyard Approach"));

	FString Json;
	if (!TestTrue(TEXT("Serialising the path preset to JSON succeeds"), Source->ToJsonString(Json)))
	{
		return false;
	}

	TestTrue(TEXT("The JSON payload carries a schema version"), Json.Contains(TEXT("chemaVersion")));

	UTourPathPreset* Restored = NewObject<UTourPathPreset>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("Restored preset"), Restored))
	{
		return false;
	}

	if (!TestTrue(TEXT("Deserialising the path preset from JSON succeeds"), Restored->FromJsonString(Json)))
	{
		return false;
	}

	TestEqual(TEXT("Point count survives the round trip"), Restored->PathData.Points.Num(), Source->PathData.Points.Num());
	TestEqual(TEXT("Schema version survives the round trip"), Restored->SchemaVersion, Source->SchemaVersion);
	TestEqual(TEXT("Display name survives the round trip"), Restored->DisplayName.ToString(), Source->DisplayName.ToString());
	TestTrue(TEXT("Preset id survives the round trip"), Restored->PresetId == Source->PresetId);

	// The struct comparison covers every point field and the generator parameters at once.
	if (!(Restored->PathData == Source->PathData))
	{
		AddError(TEXT("The round-tripped path data does not compare equal to the original."));

		for (int32 Index = 0; Index < FMath::Min(Restored->PathData.Points.Num(), Source->PathData.Points.Num()); ++Index)
		{
			if (Restored->PathData.Points[Index] != Source->PathData.Points[Index])
			{
				AddError(FString::Printf(TEXT("Point %d differs after the round trip."), Index));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourSequenceJsonRoundTripTest,
	"ArchVizTour.Serialization.TourSequenceJsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourSequenceJsonRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace ArchVizTourSerializationTests;

	UTourSequencePreset* Source = NewObject<UTourSequencePreset>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("Source tour"), Source))
	{
		return false;
	}

	Source->Steps           = MakePopulatedSteps();
	Source->bLoopTour       = true;
	Source->GlobalTimeScale = 1.75f;
	Source->TourTitle       = FText::FromString(TEXT("Riverside Residence"));

	FString Json;
	if (!TestTrue(TEXT("Serialising the tour to JSON succeeds"), Source->ToJsonString(Json)))
	{
		return false;
	}

	UTourSequencePreset* Restored = NewObject<UTourSequencePreset>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestTrue(TEXT("Deserialising the tour from JSON succeeds"), Restored->FromJsonString(Json)))
	{
		return false;
	}

	TestEqual(TEXT("Step count survives the round trip"), Restored->Steps.Num(), Source->Steps.Num());
	TestEqual(TEXT("Loop flag survives the round trip"), Restored->bLoopTour, Source->bLoopTour);
	TestEqual(TEXT("Global time scale survives the round trip"), Restored->GlobalTimeScale, Source->GlobalTimeScale, 1.e-3f);
	TestEqual(TEXT("Tour title survives the round trip"), Restored->TourTitle.ToString(), Source->TourTitle.ToString());
	TestEqual(TEXT("Schema version survives the round trip"), Restored->SchemaVersion, Source->SchemaVersion);

	for (int32 Index = 0; Index < FMath::Min(Restored->Steps.Num(), Source->Steps.Num()); ++Index)
	{
		if (Restored->Steps[Index] != Source->Steps[Index])
		{
			AddError(FString::Printf(TEXT("Step %d differs after the round trip."), Index));
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Schema migration
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourSchemaMigrationTest,
	"ArchVizTour.Serialization.SchemaMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourSchemaMigrationTest::RunTest(const FString& Parameters)
{
	// --- Path schema 1 -> current -------------------------------------------------
	// Schema 1 stored speeds in cm/frame at an assumed 30 fps, and had no explicit-rotation flag.
	{
		FTourPathData Legacy;
		Legacy.DefaultSpeed = 10.0f;

		FTourPoint WithRotation;
		WithRotation.Rotation = FRotator(0.0f, 90.0f, 0.0f);
		WithRotation.Speed    = 5.0f;
		Legacy.Points.Add(WithRotation);

		FTourPoint WithoutRotation;
		WithoutRotation.Rotation = FRotator::ZeroRotator;
		WithoutRotation.Speed    = 0.0f;
		Legacy.Points.Add(WithoutRotation);

		FTourPoint WithLookAt;
		WithLookAt.Rotation         = FRotator(0.0f, 45.0f, 0.0f);
		WithLookAt.bUseLookAtTarget = true;
		WithLookAt.LookAtTargetName = FName(TEXT("Target"));
		WithLookAt.Speed            = 2.0f;
		Legacy.Points.Add(WithLookAt);

		const bool bChanged = UTourPathPreset::MigratePathData(Legacy, /*FromVersion*/ 1);
		TestTrue(TEXT("Migrating from schema 1 reports a change"), bChanged);

		TestEqual(TEXT("Default speed is rescaled from cm/frame to cm/s"), Legacy.DefaultSpeed, 300.0f, 1.e-3f);
		TestEqual(TEXT("Per-point speed is rescaled"), Legacy.Points[0].Speed, 150.0f, 1.e-3f);
		TestEqual(TEXT("A point with no speed inherits the rescaled default"), Legacy.Points[1].Speed, 300.0f, 1.e-3f);

		TestTrue(TEXT("A rotated point opts in to explicit rotation"), Legacy.Points[0].bUseExplicitRotation);
		TestFalse(TEXT("An unrotated point does not opt in"), Legacy.Points[1].bUseExplicitRotation);
		TestFalse(TEXT("A LookAt point does not opt in to explicit rotation"), Legacy.Points[2].bUseExplicitRotation);
	}

	// --- Path schema 2 -> current -------------------------------------------------
	// Schema 2 already had speeds in cm/s, so migration must not rescale them again.
	{
		FTourPathData Legacy;
		Legacy.DefaultSpeed = 200.0f;

		FTourPoint Point;
		Point.Speed    = 175.0f;
		Point.Rotation = FRotator(0.0f, 30.0f, 0.0f);
		Legacy.Points.Add(Point);

		UTourPathPreset::MigratePathData(Legacy, /*FromVersion*/ 2);

		TestEqual(TEXT("Schema 2 speeds are left alone"), Legacy.DefaultSpeed, 200.0f, 1.e-3f);
		TestEqual(TEXT("Schema 2 per-point speeds are left alone"), Legacy.Points[0].Speed, 175.0f, 1.e-3f);
		TestTrue(TEXT("Schema 2 rotation still opts in"), Legacy.Points[0].bUseExplicitRotation);
	}

	// --- Path already current ------------------------------------------------------
	{
		FTourPathData Current;
		Current.DefaultSpeed = 250.0f;
		Current.Points.Add(FTourPoint());

		const bool bChanged = UTourPathPreset::MigratePathData(Current, UTourPathPreset::CurrentSchemaVersion);
		TestFalse(TEXT("Migrating current-schema data changes nothing"), bChanged);
		TestEqual(TEXT("Current-schema speed is untouched"), Current.DefaultSpeed, 250.0f, 1.e-3f);
	}

	// --- Step schema 1 -> current --------------------------------------------------
	// Schema 1 had no blend fields, so every transition was a hard cut; the struct defaults are
	// a one-second cubic blend, which would silently retime every existing tour.
	{
		TArray<FTourStep> LegacySteps;
		LegacySteps.Add(FTourStep());
		LegacySteps.Add(FTourStep());

		const bool bChanged = UTourSequencePreset::MigrateSteps(LegacySteps, /*FromVersion*/ 1);
		TestTrue(TEXT("Migrating steps from schema 1 reports a change"), bChanged);

		for (const FTourStep& Step : LegacySteps)
		{
			TestEqual(TEXT("Legacy steps keep their hard cut"), Step.BlendTime, 0.0f);
			TestEqual(TEXT("Legacy steps use a linear blend function"), static_cast<int32>(Step.BlendFunction.GetValue()), static_cast<int32>(VTBlend_Linear));
		}
	}

	// --- Steps already current -----------------------------------------------------
	{
		TArray<FTourStep> CurrentSteps;
		FTourStep Step;
		Step.BlendTime = 2.0f;
		CurrentSteps.Add(Step);

		const bool bChanged = UTourSequencePreset::MigrateSteps(CurrentSteps, UTourSequencePreset::CurrentSchemaVersion);
		TestFalse(TEXT("Migrating current-schema steps changes nothing"), bChanged);
		TestEqual(TEXT("Current-schema blend time is untouched"), CurrentSteps[0].BlendTime, 2.0f, 1.e-3f);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
