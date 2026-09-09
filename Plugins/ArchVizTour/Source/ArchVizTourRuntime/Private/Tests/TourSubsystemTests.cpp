// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
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
