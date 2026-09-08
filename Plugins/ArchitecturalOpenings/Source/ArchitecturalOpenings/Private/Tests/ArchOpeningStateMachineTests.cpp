// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ArchOpeningComponent.h"
#include "Tests/ArchOpeningTestSupport.h"
#include "UObject/GCObjectScopeGuard.h"

namespace ArchOpeningStateTests
{
	/**
	 * Builds a bare opening with no world and no assigned parts.
	 *
	 * With no calibration the pose application is a no-op, which is exactly what these tests want:
	 * they exercise the state machine and the timing, and the transform maths is covered separately
	 * by the solver tests.
	 */
	UArchOpeningComponent* MakeOpening(float OpenDuration = 1.0f, float CloseDuration = 1.0f)
	{
		UArchOpeningComponent* Opening = NewObject<UArchOpeningComponent>(GetTransientPackage());
		Opening->Timing.TimingMode = EArchOpeningTimingMode::Duration;
		Opening->Timing.OpenDuration = OpenDuration;
		Opening->Timing.CloseDuration = CloseDuration;
		Opening->Timing.OpeningEasing = EArchOpeningEasing::Linear;
		Opening->Timing.ClosingEasing = EArchOpeningEasing::Linear;
		Opening->Timing.DelayBeforeOpening = 0.0f;
		Opening->Timing.DelayBeforeClosing = 0.0f;
		Opening->Obstruction.Policy = EArchOpeningObstructionPolicy::Ignore;
		Opening->Interaction.Mode = EArchOpeningInteractionMode::ClickOnly;
		return Opening;
	}

	void Advance(UArchOpeningComponent* Opening, float Seconds, float Step = 1.0f / 60.0f)
	{
		float Remaining = Seconds;
		while (Remaining > 0.0f)
		{
			const float Delta = FMath::Min(Step, Remaining);
			Opening->TickComponent(Delta, LEVELTICK_All, nullptr);
			Remaining -= Delta;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningBasicCycleTest,
	"ArchitecturalOpenings.StateMachine.OpenCloseCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningBasicCycleTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningStateTests;

	UArchOpeningComponent* Opening = MakeOpening(1.0f, 1.0f);
	UArchOpeningEventCounter* Counter = NewObject<UArchOpeningEventCounter>(GetTransientPackage());
	FGCObjectScopeGuard OpeningGuard(Opening);
	FGCObjectScopeGuard CounterGuard(Counter);
	Counter->BindTo(Opening);

	TestTrue(TEXT("Starts closed"), Opening->GetOpeningState() == EArchOpeningState::Closed);
	TestEqual(TEXT("Starts at zero openness"), Opening->GetOpenness(), 0.0f);

	Opening->Open();
	TestTrue(TEXT("Open enters the Opening state"), Opening->GetOpeningState() == EArchOpeningState::Opening);
	TestEqual(TEXT("Opening Started fired once"), Counter->OpeningStarted, 1);

	// Repeated identical commands must be inert.
	Opening->Open();
	Opening->Open();
	TestEqual(TEXT("Repeated Open does not re-fire Opening Started"), Counter->OpeningStarted, 1);

	Advance(Opening, 0.5f);
	TestTrue(TEXT("Halfway through, openness is around half"),
		FMath::IsNearlyEqual(Opening->GetOpenness(), 0.5f, 0.05f));
	TestEqual(TEXT("Fully Opened has not fired early"), Counter->FullyOpened, 0);

	Advance(Opening, 0.6f);
	TestTrue(TEXT("Reaches the Open state"), Opening->GetOpeningState() == EArchOpeningState::Open);
	TestEqual(TEXT("Openness lands exactly on 1"), Opening->GetOpenness(), 1.0f);
	TestEqual(TEXT("Fully Opened fired once"), Counter->FullyOpened, 1);

	// Ticking on past the endpoint must not re-fire anything.
	Advance(Opening, 1.0f);
	TestEqual(TEXT("Fully Opened still fired only once"), Counter->FullyOpened, 1);

	Opening->Close();
	TestEqual(TEXT("Closing Started fired once"), Counter->ClosingStarted, 1);

	Advance(Opening, 1.2f);
	TestTrue(TEXT("Reaches the Closed state"), Opening->GetOpeningState() == EArchOpeningState::Closed);
	TestEqual(TEXT("Openness lands exactly on 0"), Opening->GetOpenness(), 0.0f);
	TestEqual(TEXT("Fully Closed fired once"), Counter->FullyClosed, 1);

	// Repeated cycles must be exactly repeatable: openness returns to exactly 0 and exactly 1.
	for (int32 Cycle = 0; Cycle < 20; ++Cycle)
	{
		Opening->Open();
		Advance(Opening, 1.2f);
		TestEqual(TEXT("Cycle reaches exactly 1"), Opening->GetOpenness(), 1.0f);

		Opening->Close();
		Advance(Opening, 1.2f);
		TestEqual(TEXT("Cycle returns to exactly 0"), Opening->GetOpenness(), 0.0f);
	}

	TestEqual(TEXT("Every cycle fired exactly one Fully Opened"), Counter->FullyOpened, 21);
	TestEqual(TEXT("Every cycle fired exactly one Fully Closed"), Counter->FullyClosed, 21);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningReversalTest,
	"ArchitecturalOpenings.StateMachine.MidMotionReversal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningReversalTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningStateTests;

	UArchOpeningComponent* Opening = MakeOpening(2.0f, 2.0f);
	FGCObjectScopeGuard OpeningGuard(Opening);

	Opening->Open();
	Advance(Opening, 0.6f);

	const float BeforeReversal = Opening->GetOpenness();
	TestTrue(TEXT("Some travel happened before the reversal"), BeforeReversal > 0.1f && BeforeReversal < 0.5f);

	Opening->Close();

	// Position continuity: the reversal must not move the leaf at all in the moment it is issued.
	TestEqual(TEXT("Reversal does not snap the pose"), Opening->GetOpenness(), BeforeReversal);
	TestTrue(TEXT("Reversal enters the Closing state"), Opening->GetOpeningState() == EArchOpeningState::Closing);

	// One tick later it must be moving the other way, and never past the endpoint.
	Advance(Opening, 1.0f / 60.0f);
	TestTrue(TEXT("Reversal moves back toward closed"), Opening->GetOpenness() < BeforeReversal);

	// A short remaining distance must take a correspondingly short time, not a full close duration.
	// From ~0.3 openness with a 2 s full close, the remaining travel should take ~0.6 s.
	Advance(Opening, 0.9f);
	TestTrue(TEXT("Short remaining travel finishes in proportional time"), Opening->GetOpeningState() == EArchOpeningState::Closed);

	// Reversing repeatedly in quick succession must stay inside the range and end coherently.
	Opening->Open();
	Advance(Opening, 0.3f);
	Opening->Close();
	Advance(Opening, 0.1f);
	Opening->Open();
	Advance(Opening, 0.1f);
	Opening->Close();

	for (int32 Step = 0; Step < 300; ++Step)
	{
		Advance(Opening, 1.0f / 60.0f);
		TestTrue(TEXT("Openness never leaves 0..1 during rapid reversals"),
			Opening->GetOpenness() >= 0.0f && Opening->GetOpenness() <= 1.0f);
	}

	TestTrue(TEXT("Rapid reversals settle closed"), Opening->GetOpeningState() == EArchOpeningState::Closed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningTimingTest,
	"ArchitecturalOpenings.StateMachine.TimingModesAndDelays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningTimingTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningStateTests;

	// Speed mode: 90 degrees at 90 deg/s must take about a second.
	{
		UArchOpeningComponent* Opening = MakeOpening();
		FGCObjectScopeGuard Guard(Opening);
		Opening->MotionType = EArchOpeningMotionType::Hinged;
		Opening->Hinged.OpenAngle = 90.0f;
		Opening->Timing.TimingMode = EArchOpeningTimingMode::Speed;
		Opening->Timing.OpenSpeed = 90.0f;
		Opening->Timing.CloseSpeed = 45.0f;

		Opening->Open();
		Advance(Opening, 0.9f);
		TestTrue(TEXT("Speed mode has not finished early"), Opening->GetOpeningState() == EArchOpeningState::Opening);

		Advance(Opening, 0.2f);
		TestTrue(TEXT("90 degrees at 90 deg/s takes about one second"), Opening->GetOpeningState() == EArchOpeningState::Open);

		// Closing at half the speed must take about twice as long.
		Opening->Close();
		Advance(Opening, 1.5f);
		TestTrue(TEXT("Half speed close is still running at 1.5 s"), Opening->GetOpeningState() == EArchOpeningState::Closing);

		Advance(Opening, 0.7f);
		TestTrue(TEXT("Half speed close finishes by 2.2 s"), Opening->GetOpeningState() == EArchOpeningState::Closed);
	}

	// Zero and negative durations must be handled safely rather than dividing by zero.
	{
		UArchOpeningComponent* Opening = MakeOpening(0.0f, -5.0f);
		FGCObjectScopeGuard Guard(Opening);

		Opening->Open();
		Advance(Opening, 1.0f / 60.0f);
		TestTrue(TEXT("Zero open duration snaps open"), Opening->GetOpeningState() == EArchOpeningState::Open);
		TestEqual(TEXT("Zero open duration lands on 1"), Opening->GetOpenness(), 1.0f);

		Opening->Close();
		Advance(Opening, 1.0f / 60.0f);
		TestTrue(TEXT("Negative close duration snaps closed"), Opening->GetOpeningState() == EArchOpeningState::Closed);
		TestEqual(TEXT("Negative close duration lands on 0"), Opening->GetOpenness(), 0.0f);
	}

	// Zero speed must not hang the transition.
	{
		UArchOpeningComponent* Opening = MakeOpening();
		FGCObjectScopeGuard Guard(Opening);
		Opening->Timing.TimingMode = EArchOpeningTimingMode::Speed;
		Opening->Timing.OpenSpeed = 0.0f;

		Opening->Open();
		Advance(Opening, 1.0f / 60.0f);
		TestTrue(TEXT("Zero speed snaps rather than stalling"), Opening->GetOpeningState() == EArchOpeningState::Open);
	}

	// Delays must run before the transition and must not leave a stale pending action behind when
	// the command is reversed during the delay.
	{
		UArchOpeningComponent* Opening = MakeOpening(1.0f, 1.0f);
		Opening->Timing.DelayBeforeOpening = 0.5f;

		UArchOpeningEventCounter* Counter = NewObject<UArchOpeningEventCounter>(GetTransientPackage());
		FGCObjectScopeGuard Guard(Opening);
		FGCObjectScopeGuard CounterGuard(Counter);
		Counter->BindTo(Opening);

		Opening->Open();
		TestTrue(TEXT("Open with a delay enters Opening Delay"), Opening->GetOpeningState() == EArchOpeningState::OpeningDelay);

		Advance(Opening, 0.3f);
		TestTrue(TEXT("Still delayed at 0.3 s"), Opening->GetOpeningState() == EArchOpeningState::OpeningDelay);
		TestEqual(TEXT("Nothing has started moving yet"), Counter->OpeningStarted, 0);

		// Cancel during the delay. Nothing may fire afterwards.
		Opening->Close();
		Advance(Opening, 2.0f);

		TestEqual(TEXT("Cancelled delay never started opening"), Counter->OpeningStarted, 0);
		TestTrue(TEXT("Cancelled delay settles closed"), Opening->GetOpeningState() == EArchOpeningState::Closed);
	}

	// Auto-close must fire once, after the configured delay.
	{
		UArchOpeningComponent* Opening = MakeOpening(0.2f, 0.2f);
		Opening->Timing.bAutoClose = true;
		Opening->Timing.AutoCloseDelay = 1.0f;

		UArchOpeningEventCounter* Counter = NewObject<UArchOpeningEventCounter>(GetTransientPackage());
		FGCObjectScopeGuard Guard(Opening);
		FGCObjectScopeGuard CounterGuard(Counter);
		Counter->BindTo(Opening);

		Opening->Open();
		Advance(Opening, 0.3f);
		TestTrue(TEXT("Auto-close waits while open"), Opening->GetOpeningState() == EArchOpeningState::Open);

		Advance(Opening, 0.8f);
		TestTrue(TEXT("Auto-close is still waiting before its delay elapses"), Opening->GetOpeningState() == EArchOpeningState::Open);

		Advance(Opening, 0.4f);
		TestEqual(TEXT("Auto-close started exactly one close"), Counter->ClosingStarted, 1);

		Advance(Opening, 0.4f);
		TestTrue(TEXT("Auto-close completes"), Opening->GetOpeningState() == EArchOpeningState::Closed);
		TestEqual(TEXT("Auto-close fired Fully Closed once"), Counter->FullyClosed, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningSetOpennessTest,
	"ArchitecturalOpenings.StateMachine.SetOpennessAndStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningSetOpennessTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningStateTests;

	UArchOpeningComponent* Opening = MakeOpening(1.0f, 1.0f);
	UArchOpeningEventCounter* Counter = NewObject<UArchOpeningEventCounter>(GetTransientPackage());
	FGCObjectScopeGuard OpeningGuard(Opening);
	FGCObjectScopeGuard CounterGuard(Counter);
	Counter->BindTo(Opening);

	// Immediate: no animation, no transition events.
	Opening->SetOpennessImmediate(0.42f);
	TestTrue(TEXT("Immediate set applies at once"), FMath::IsNearlyEqual(Opening->GetOpenness(), 0.42f));
	TestEqual(TEXT("Immediate set fires no opening event"), Counter->OpeningStarted, 0);
	TestEqual(TEXT("Immediate set fires no fully-opened event"), Counter->FullyOpened, 0);

	// Out of range input is clamped, not extrapolated.
	Opening->SetOpennessImmediate(5.0f);
	TestEqual(TEXT("Immediate set clamps above one"), Opening->GetOpenness(), 1.0f);
	Opening->SetOpennessImmediate(-3.0f);
	TestEqual(TEXT("Immediate set clamps below zero"), Opening->GetOpenness(), 0.0f);

	// Animated: travels there and reports a motion stop, but not a fully-opened endpoint event.
	Counter->Reset();
	Opening->SetOpennessAnimated(0.5f);
	TestTrue(TEXT("Animated set starts opening"), Opening->GetOpeningState() == EArchOpeningState::Opening);

	Advance(Opening, 0.8f);
	TestTrue(TEXT("Animated set reaches its target"), FMath::IsNearlyEqual(Opening->GetOpenness(), 0.5f, 0.01f));
	TestEqual(TEXT("Animated set to a partial pose is not a full open"), Counter->FullyOpened, 0);
	TestEqual(TEXT("Animated set reports one motion stop"), Counter->MotionStopped, 1);

	// Stop mid-motion holds the pose and reports exactly one motion stop.
	Counter->Reset();
	Opening->Open();
	Advance(Opening, 0.2f);
	const float Held = Opening->GetOpenness();

	Opening->Stop();
	TestEqual(TEXT("Stop holds the current pose"), Opening->GetOpenness(), Held);
	TestEqual(TEXT("Stop reports one motion stop"), Counter->MotionStopped, 1);
	TestEqual(TEXT("Stop does not fake an arrival at fully open"), Counter->FullyOpened, 0);

	Advance(Opening, 1.0f);
	TestEqual(TEXT("Stopped opening stays put"), Opening->GetOpenness(), Held);

	// A redundant Stop must not report a second motion stop.
	Opening->Stop();
	TestEqual(TEXT("Redundant Stop is inert"), Counter->MotionStopped, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningHandleSequenceTest,
	"ArchitecturalOpenings.StateMachine.HandleSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningHandleSequenceTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningStateTests;

	// A handle group with no valid meshes must not stall the sequence: the leaf has to move anyway.
	{
		UArchOpeningComponent* Opening = MakeOpening(0.5f, 0.5f);
		FGCObjectScopeGuard Guard(Opening);

		FArchOpeningHandleGroup Group;
		Group.GroupName = TEXT("Lever");
		Group.RotationAngle = -40.0f;
		Group.DelayBeforeLeafMovement = 0.3f;
		Opening->HandleGroups.Add(Group);

		Opening->Open();
		TestTrue(TEXT("A handle group with no meshes does not delay the leaf"), Opening->GetOpeningState() == EArchOpeningState::Opening);
	}

	// A zero rotation angle also must not delay the leaf: there is nothing to actuate.
	{
		UArchOpeningComponent* Opening = MakeOpening(0.5f, 0.5f);
		FGCObjectScopeGuard Guard(Opening);

		FArchOpeningHandleGroup Group;
		Group.RotationAngle = 0.0f;
		Group.DelayBeforeLeafMovement = 0.3f;
		Opening->HandleGroups.Add(Group);

		Opening->Open();
		TestTrue(TEXT("A zero-angle handle group does not delay the leaf"), Opening->GetOpeningState() == EArchOpeningState::Opening);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
