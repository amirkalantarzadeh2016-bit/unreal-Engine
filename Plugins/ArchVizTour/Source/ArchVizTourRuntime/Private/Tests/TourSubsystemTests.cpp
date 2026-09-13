// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TourPath.h"
#include "TourSequencePreset.h"
#include "TourSubsystem.h"
#include "TourTypes.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ArchVizTourSubsystemTests
{
	/**
	 * A world plus its engine world context, torn down deterministically.
	 *
	 * World subsystems only exist on an initialised world, so the transport tests need a real
	 * one. Dwell steps are used throughout because they resolve without any level actors, which
	 * keeps these tests about the transport rather than about actor lookup.
	 */
	struct FScopedTestWorld
	{
		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false, TEXT("ArchVizTourTestWorld"));
			if (World != nullptr && GEngine != nullptr)
			{
				WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
				WorldContext->SetCurrentWorld(World);
			}
		}

		~FScopedTestWorld()
		{
			// The context has to go first: destroying the world it points at leaves the engine
			// holding a dangling context.
			if (WorldContext != nullptr && GEngine != nullptr)
			{
				GEngine->DestroyWorldContext(World);
			}

			if (World != nullptr)
			{
				World->DestroyWorld(/*bInformEngineOfWorld*/ false);
			}
		}

		FScopedTestWorld(const FScopedTestWorld&) = delete;
		FScopedTestWorld& operator=(const FScopedTestWorld&) = delete;

		UTourSubsystem* GetSubsystem() const
		{
			return World != nullptr ? World->GetSubsystem<UTourSubsystem>() : nullptr;
		}

		UWorld* World = nullptr;
		FWorldContext* WorldContext = nullptr;
	};

	/**
	 * Spawn a straight four-point path of TotalLengthCm, tagged so a step can resolve it.
	 *
	 * Linear points make the curve exactly the straight line through them, which gives the
	 * arc-length and duration assertions an analytic answer to compare against.
	 */
	static ATourPath* SpawnStraightPath(UWorld* World, FName Tag, float TotalLengthCm, float SpeedCmPerSecond)
	{
		check(World != nullptr);

		FTourPathData PathData;
		PathData.DefaultSpeed = SpeedCmPerSecond;

		constexpr int32 PointCount = 4;
		for (int32 Index = 0; Index < PointCount; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(PointCount - 1);

			FTourPoint Point;
			Point.Location  = FVector(TotalLengthCm * Alpha, 0.0, 0.0);
			Point.PointType = ESplinePointType::Linear;
			Point.Speed     = SpeedCmPerSecond;
			PathData.Points.Add(Point);
		}

		ATourPath* Path = World->SpawnActor<ATourPath>(ATourPath::StaticClass(), FTransform::Identity);
		if (Path == nullptr)
		{
			return nullptr;
		}

		Path->Tags.AddUnique(Tag);
		Path->ApplyPathData(PathData, /*bApplyTransform*/ false);
		return Path;
	}

	/** A tour of Dwell steps with distinct labels and durations. */
	static UTourSequencePreset* MakeDwellTour(UObject* Outer, int32 StepCount)
	{
		UTourSequencePreset* Preset = NewObject<UTourSequencePreset>(Outer, NAME_None, RF_Transient);
		check(Preset != nullptr);

		for (int32 Index = 0; Index < StepCount; ++Index)
		{
			FTourStep Step;
			Step.StepType  = ETourStepType::Dwell;
			Step.Label     = FText::FromString(FString::Printf(TEXT("Step %d"), Index));
			Step.Duration  = 2.0f + static_cast<float>(Index);
			Step.BlendTime = 0.0f;
			Preset->Steps.Add(Step);
		}

		return Preset;
	}
}

// ---------------------------------------------------------------------------
// Step index bounds
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourStepIndexBoundsTest,
	"ArchVizTour.Subsystem.StepIndexBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourStepIndexBoundsTest::RunTest(const FString& Parameters)
{
	using namespace ArchVizTourSubsystemTests;

	FScopedTestWorld TestWorld;
	UTourSubsystem* Subsystem = TestWorld.GetSubsystem();

	if (!TestNotNull(TEXT("Tour subsystem on the test world"), Subsystem))
	{
		return false;
	}

	// --- No tour loaded: every query must answer safely ---------------------
	TestEqual(TEXT("An empty subsystem reports zero steps"), Subsystem->GetStepCount(), 0);
	TestEqual(TEXT("An empty subsystem is Idle"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Idle));
	TestEqual(TEXT("An empty subsystem reports no progress"), Subsystem->GetTourProgress(), 0.0f);
	TestEqual(TEXT("An empty subsystem reports no step progress"), Subsystem->GetStepProgress(), 0.0f);
	TestTrue(TEXT("An empty subsystem has an empty current label"), Subsystem->GetCurrentStepLabel().IsEmpty());
	TestEqual(TEXT("An empty subsystem lists no labels"), Subsystem->GetStepLabels().Num(), 0);
	TestFalse(TEXT("Play is disabled with no tour"), Subsystem->IsPlayButtonEnabled());
	TestFalse(TEXT("Next is disabled with no tour"), Subsystem->IsNextButtonEnabled());
	TestFalse(TEXT("Previous is disabled with no tour"), Subsystem->IsPreviousButtonEnabled());

	// These must be no-ops rather than crashes.
	Subsystem->JumpToStep(0);
	Subsystem->JumpToStep(-5);
	Subsystem->NextStep();
	Subsystem->PreviousStep();
	Subsystem->PlayTour();
	Subsystem->ScrubToAlpha(0.5f);

	// --- An empty preset must be refused ------------------------------------
	UTourSequencePreset* EmptyPreset = NewObject<UTourSequencePreset>(GetTransientPackage(), NAME_None, RF_Transient);
	TestFalse(TEXT("A preset with no steps is refused"), Subsystem->LoadTour(EmptyPreset));
	TestEqual(TEXT("A refused preset leaves the step count at zero"), Subsystem->GetStepCount(), 0);

	// --- A real tour ---------------------------------------------------------
	constexpr int32 StepCount = 4;
	UTourSequencePreset* Preset = MakeDwellTour(GetTransientPackage(), StepCount);

	if (!TestTrue(TEXT("Loading a four-step tour succeeds"), Subsystem->LoadTour(Preset)))
	{
		return false;
	}

	TestEqual(TEXT("Step count matches the preset"), Subsystem->GetStepCount(), StepCount);
	TestEqual(TEXT("Loading starts on step 0"), Subsystem->GetCurrentStepIndex(), 0);
	TestEqual(TEXT("Every step is labelled"), Subsystem->GetStepLabels().Num(), StepCount);

	Subsystem->PlayTour();
	TestEqual(TEXT("Playing a tour with no blend enters Playing"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Playing));

	// Out-of-range jumps clamp rather than corrupting the index.
	Subsystem->JumpToStep(99);
	TestEqual(TEXT("Jumping past the end clamps to the last step"), Subsystem->GetCurrentStepIndex(), StepCount - 1);

	Subsystem->JumpToStep(-7);
	TestEqual(TEXT("Jumping before the start clamps to step 0"), Subsystem->GetCurrentStepIndex(), 0);

	Subsystem->JumpToStep(2);
	TestEqual(TEXT("Jumping in range lands exactly"), Subsystem->GetCurrentStepIndex(), 2);
	TestEqual(TEXT("Jumping restarts the step"), Subsystem->GetStepProgress(), 0.0f);

	// Scrubbing must land inside the tour at both extremes.
	Subsystem->ScrubToAlpha(-1.0f);
	TestEqual(TEXT("Scrubbing below 0 lands on the first step"), Subsystem->GetCurrentStepIndex(), 0);
	TestEqual(TEXT("Scrubbing below 0 reports no progress"), Subsystem->GetTourProgress(), 0.0f, 1.e-3f);

	Subsystem->ScrubToAlpha(2.0f);
	TestEqual(TEXT("Scrubbing above 1 lands on the last step"), Subsystem->GetCurrentStepIndex(), StepCount - 1);
	TestTrue(TEXT("Scrubbing above 1 reports full progress"), Subsystem->GetTourProgress() > 0.99f);

	// Time scale must reject values that would stall or reverse playback.
	Subsystem->SetTimeScale(2.0f);
	TestEqual(TEXT("A positive time scale is accepted"), Subsystem->GetTimeScale(), 2.0f, 1.e-3f);
	Subsystem->SetTimeScale(0.0f);
	TestEqual(TEXT("A zero time scale is rejected"), Subsystem->GetTimeScale(), 2.0f, 1.e-3f);
	Subsystem->SetTimeScale(-1.0f);
	TestEqual(TEXT("A negative time scale is rejected"), Subsystem->GetTimeScale(), 2.0f, 1.e-3f);

	Subsystem->SetPlaybackDirection(-1.0f);
	TestEqual(TEXT("Playback direction can be reversed"), Subsystem->GetPlaybackDirection(), -1.0f, 1.e-3f);
	Subsystem->SetPlaybackDirection(0.0f);
	TestEqual(TEXT("A zero direction is rejected"), Subsystem->GetPlaybackDirection(), -1.0f, 1.e-3f);
	Subsystem->SetPlaybackDirection(1.0f);

	Subsystem->StopTour();
	TestEqual(TEXT("Stopping returns to Idle"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Idle));

	return true;
}

// ---------------------------------------------------------------------------
// Transport behaviour at the tour boundaries
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourStepNavigationBoundaryTest,
	"ArchVizTour.Subsystem.NextPreviousAtTourBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourStepNavigationBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace ArchVizTourSubsystemTests;

	FScopedTestWorld TestWorld;
	UTourSubsystem* Subsystem = TestWorld.GetSubsystem();

	if (!TestNotNull(TEXT("Tour subsystem on the test world"), Subsystem))
	{
		return false;
	}

	constexpr int32 StepCount = 3;

	// --- Non-looping tour ---------------------------------------------------
	{
		UTourSequencePreset* Preset = MakeDwellTour(GetTransientPackage(), StepCount);
		Preset->bLoopTour = false;

		TestTrue(TEXT("Loading the linear tour succeeds"), Subsystem->LoadTour(Preset));
		Subsystem->PlayTour();

		TestEqual(TEXT("Playback starts on step 0"), Subsystem->GetCurrentStepIndex(), 0);
		TestTrue(TEXT("Previous is offered on the first step, because it restarts it"), Subsystem->IsPreviousButtonEnabled());

		// Previous on the first step restarts it rather than wrapping to the end.
		Subsystem->PreviousStep();
		TestEqual(TEXT("Previous on the first step stays on step 0"), Subsystem->GetCurrentStepIndex(), 0);
		TestEqual(TEXT("Previous on the first step keeps playing"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Playing));

		Subsystem->NextStep();
		TestEqual(TEXT("Next advances one step"), Subsystem->GetCurrentStepIndex(), 1);

		Subsystem->NextStep();
		TestEqual(TEXT("Next advances to the last step"), Subsystem->GetCurrentStepIndex(), StepCount - 1);
		TestFalse(TEXT("Next is disabled on the last step of a linear tour"), Subsystem->IsNextButtonEnabled());

		// Next at the end completes the tour rather than doing nothing.
		Subsystem->NextStep();
		TestEqual(TEXT("Next at the end finishes the tour"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Finished));
		TestEqual(TEXT("A finished tour parks on the last step"), Subsystem->GetCurrentStepIndex(), StepCount - 1);

		// Playing a finished tour restarts it from the top.
		Subsystem->PlayTour();
		TestEqual(TEXT("Playing a finished tour restarts at step 0"), Subsystem->GetCurrentStepIndex(), 0);
		TestEqual(TEXT("Playing a finished tour resumes Playing"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Playing));

		// Jumping on a *finished* tour must honour the requested index. Routing the jump through
		// PlayTour used to reset it to the top, because PlayTour treats Finished as "replay".
		Subsystem->JumpToStep(StepCount - 1);
		Subsystem->NextStep();
		TestEqual(TEXT("The tour finished again"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Finished));

		Subsystem->JumpToStep(1);
		TestEqual(TEXT("Jumping on a finished tour lands on the requested step, not step 0"), Subsystem->GetCurrentStepIndex(), 1);
		TestEqual(TEXT("Jumping on a finished tour resumes playback"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Playing));

		Subsystem->StopTour();
		Subsystem->JumpToStep(2);
		TestEqual(TEXT("Jumping on an idle tour lands on the requested step"), Subsystem->GetCurrentStepIndex(), 2);
		TestEqual(TEXT("Jumping on an idle tour starts playback"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Playing));

		Subsystem->StopTour();
	}

	// --- Looping tour --------------------------------------------------------
	{
		UTourSequencePreset* Preset = MakeDwellTour(GetTransientPackage(), StepCount);
		Preset->bLoopTour = true;

		TestTrue(TEXT("Loading the looping tour succeeds"), Subsystem->LoadTour(Preset));
		Subsystem->PlayTour();

		TestTrue(TEXT("Next is always available on a looping tour"), Subsystem->IsNextButtonEnabled());

		Subsystem->JumpToStep(StepCount - 1);
		TestEqual(TEXT("Jumped to the last step"), Subsystem->GetCurrentStepIndex(), StepCount - 1);

		Subsystem->NextStep();
		TestEqual(TEXT("Next past the end of a looping tour wraps to step 0"), Subsystem->GetCurrentStepIndex(), 0);
		TestEqual(TEXT("A looping tour keeps playing across the wrap"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Playing));

		Subsystem->PreviousStep();
		TestEqual(TEXT("Previous before the start of a looping tour wraps to the last step"), Subsystem->GetCurrentStepIndex(), StepCount - 1);

		Subsystem->StopTour();
	}

	// --- Single-step tour ----------------------------------------------------
	{
		UTourSequencePreset* Preset = MakeDwellTour(GetTransientPackage(), 1);
		Preset->bLoopTour = false;

		TestTrue(TEXT("Loading the single-step tour succeeds"), Subsystem->LoadTour(Preset));
		Subsystem->PlayTour();

		TestEqual(TEXT("A single-step tour starts on step 0"), Subsystem->GetCurrentStepIndex(), 0);
		TestFalse(TEXT("Next is disabled on a single-step linear tour"), Subsystem->IsNextButtonEnabled());

		Subsystem->PreviousStep();
		TestEqual(TEXT("Previous on a single-step tour stays put"), Subsystem->GetCurrentStepIndex(), 0);

		Subsystem->NextStep();
		TestEqual(TEXT("Next on a single-step tour finishes it"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Finished));

		Subsystem->StopTour();
	}

	return true;
}

// ---------------------------------------------------------------------------
// Speed-derived step lengths, progress and resolved camera state
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourSplineStepResolutionTest,
	"ArchVizTour.Subsystem.SplineStepDurationAndCameraState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourSplineStepResolutionTest::RunTest(const FString& Parameters)
{
	using namespace ArchVizTourSubsystemTests;

	FScopedTestWorld TestWorld;
	UTourSubsystem* Subsystem = TestWorld.GetSubsystem();

	if (!TestNotNull(TEXT("Tour subsystem on the test world"), Subsystem))
	{
		return false;
	}

	constexpr float PathLength = 900.0f;   // centimetres
	constexpr float PathSpeed  = 300.0f;   // centimetres per second -> 3 s to traverse
	constexpr float DwellTime  = 2.0f;     // seconds

	ATourPath* Path = SpawnStraightPath(TestWorld.World, FName(TEXT("TestPath")), PathLength, PathSpeed);
	if (!TestNotNull(TEXT("Spawned tour path"), Path))
	{
		return false;
	}

	TestEqual(TEXT("The spawned path measures its authored length"), Path->GetPathLength(), PathLength, 1.0f);
	TestTrue(TEXT("The spawned path is traversable"), Path->IsTraversable());
	TestEqual(TEXT("The path reports its authored speed"), Path->GetSpeedAtDistance(PathLength * 0.5f), PathSpeed, 0.1f);

	UTourSequencePreset* Preset = NewObject<UTourSequencePreset>(GetTransientPackage(), NAME_None, RF_Transient);

	// Duration 0 on a spline step means "derive the length from the authored cm/s speed", which
	// is how the quickstart authors one. A tour built this way used to report zero total length,
	// which made the render subsystem refuse it.
	FTourStep MoveStep;
	MoveStep.StepType      = ETourStepType::SplineMove;
	MoveStep.Label         = FText::FromString(TEXT("Approach"));
	MoveStep.Duration      = 0.0f;
	MoveStep.BlendTime     = 0.0f;
	MoveStep.SplinePathRef = FName(TEXT("TestPath"));
	Preset->Steps.Add(MoveStep);

	FTourStep HoldStep;
	HoldStep.StepType  = ETourStepType::Dwell;
	HoldStep.Label     = FText::FromString(TEXT("Hold"));
	HoldStep.Duration  = DwellTime;
	HoldStep.BlendTime = 0.0f;
	Preset->Steps.Add(HoldStep);

	if (!TestTrue(TEXT("Loading the spline tour succeeds"), Subsystem->LoadTour(Preset)))
	{
		return false;
	}

	// --- Duration derived from speed ---------------------------------------
	float Elapsed = 0.0f;
	float Total = 0.0f;
	Subsystem->GetTourTimes(Elapsed, Total);

	const float ExpectedTotal = (PathLength / PathSpeed) + DwellTime;
	TestEqual(TEXT("The tour's resolved length is the traversal time plus the dwell"), Total, ExpectedTotal, 0.05f);

	// --- Arc-length traversal and camera resolution -------------------------
	Subsystem->PlayTour();

	FTourCameraState State;
	if (!TestTrue(TEXT("The camera state resolves at the start of the tour"), Subsystem->GetCurrentCameraState(State)))
	{
		return false;
	}
	TestEqual(TEXT("The tour starts at the beginning of the path"), State.Location.X, 0.0, 1.0);

	// Half of the *move* portion, expressed as a fraction of the whole tour.
	const float HalfwayAlpha = (PathLength / PathSpeed) * 0.5f / ExpectedTotal;
	Subsystem->ScrubToAlpha(HalfwayAlpha);

	TestTrue(TEXT("The camera state resolves mid-move"), Subsystem->GetCurrentCameraState(State));
	TestEqual(TEXT("Half the move time is half the distance, because traversal is arc-length parameterised"),
		State.Location.X, static_cast<double>(PathLength) * 0.5, 5.0);

	// --- A Dwell step reports the pose the previous step left on screen ------
	Subsystem->ScrubToAlpha(1.0f);
	TestEqual(TEXT("Scrubbing to the end lands on the dwell step"), Subsystem->GetCurrentStepIndex(), 1);

	TestTrue(TEXT("The camera state still resolves during a dwell"), Subsystem->GetCurrentCameraState(State));
	TestEqual(TEXT("A dwell holds the end of the preceding spline move, not the origin"),
		State.Location.X, static_cast<double>(PathLength), 5.0);

	// --- Progress is monotonic and spans the full range ----------------------
	float PreviousProgress = -1.0f;
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const float Alpha = static_cast<float>(Step) / 20.0f;
		Subsystem->ScrubToAlpha(Alpha);

		const float Progress = Subsystem->GetTourProgress();
		if (Progress < PreviousProgress - 1.e-3f)
		{
			AddError(FString::Printf(
				TEXT("Tour progress went backwards while scrubbing forwards: %.4f after %.4f at alpha %.2f."),
				Progress, PreviousProgress, Alpha));
			break;
		}
		PreviousProgress = Progress;
	}

	Subsystem->ScrubToAlpha(0.0f);
	TestEqual(TEXT("Scrubbing to 0 reports no progress"), Subsystem->GetTourProgress(), 0.0f, 1.e-3f);
	Subsystem->ScrubToAlpha(1.0f);
	TestTrue(TEXT("Scrubbing to 1 reports full progress"), Subsystem->GetTourProgress() > 0.98f);

	Subsystem->StopTour();
	return true;
}

// ---------------------------------------------------------------------------
// Pause and resume
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourPauseStateTest,
	"ArchVizTour.Subsystem.PauseAndResume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourPauseStateTest::RunTest(const FString& Parameters)
{
	using namespace ArchVizTourSubsystemTests;

	FScopedTestWorld TestWorld;
	UTourSubsystem* Subsystem = TestWorld.GetSubsystem();

	if (!TestNotNull(TEXT("Tour subsystem on the test world"), Subsystem))
	{
		return false;
	}

	UTourSequencePreset* Preset = MakeDwellTour(GetTransientPackage(), 3);
	TestTrue(TEXT("Loading the tour succeeds"), Subsystem->LoadTour(Preset));

	// Pausing an idle tour must not invent a Paused state out of nothing.
	Subsystem->SetPaused(true);
	TestEqual(TEXT("Pausing an idle tour leaves it Idle"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Idle));

	Subsystem->PlayTour();
	TestEqual(TEXT("The tour is playing"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Playing));

	Subsystem->TogglePause();
	TestEqual(TEXT("Toggling pauses"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Paused));

	Subsystem->TogglePause();
	TestEqual(TEXT("Toggling again resumes"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Playing));

	Subsystem->SetPaused(true);
	// Play on a paused tour resumes rather than restarting from the current step.
	Subsystem->JumpToStep(2);
	const int32 IndexWhilePaused = Subsystem->GetCurrentStepIndex();
	Subsystem->PlayTour();
	TestEqual(TEXT("Play on a paused tour resumes in place"), Subsystem->GetCurrentStepIndex(), IndexWhilePaused);
	TestEqual(TEXT("Play on a paused tour returns to Playing"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Playing));

	Subsystem->RestartTour();
	TestEqual(TEXT("Restart returns to step 0"), Subsystem->GetCurrentStepIndex(), 0);
	TestEqual(TEXT("Restart reports no progress"), Subsystem->GetStepProgress(), 0.0f, 1.e-3f);

	Subsystem->StopTour();
	TestEqual(TEXT("Stopping returns to Idle"), static_cast<int32>(Subsystem->GetState()), static_cast<int32>(ETourState::Idle));

	// Clearing the tour must leave the subsystem queryable.
	TestFalse(TEXT("Loading a null tour reports failure"), Subsystem->LoadTour(nullptr));
	TestEqual(TEXT("Clearing the tour empties the step list"), Subsystem->GetStepCount(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
