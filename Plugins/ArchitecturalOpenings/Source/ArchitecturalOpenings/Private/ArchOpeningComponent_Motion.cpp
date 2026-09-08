// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningComponent.h"

#include "ArchOpeningEasing.h"
#include "ArchOpeningLog.h"
#include "ArchOpeningSolver.h"

#include "Curves/CurveFloat.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ArchOpenings"

namespace ArchOpeningMotion
{
	/** Below this a duration is treated as "snap", which keeps division by zero out of the solver. */
	static constexpr float MinDuration = 1.0e-4f;

	/** Openness difference below which a retarget is considered already satisfied. */
	static constexpr float OpennessEpsilon = 1.0e-4f;
}

// -------------------------------------------------------------------------------------------
// Timing resolution
// -------------------------------------------------------------------------------------------

float UArchOpeningComponent::ResolveTransitionDuration(bool bOpening) const
{
	if (Timing.TimingMode == EArchOpeningTimingMode::Duration)
	{
		return FMath::Max(bOpening ? Timing.OpenDuration : Timing.CloseDuration, 0.0f);
	}

	// Speed mode. The speed defines the NOMINAL full-travel time: total travel divided by speed.
	// With easing enabled the instantaneous velocity still varies across the transition; what the
	// speed guarantees is the time a complete open or close takes.
	const float Travel = FArchOpeningSolver::ComputeNominalTravel(MotionType, Hinged, Sliding);
	const float Speed = FMath::Max(bOpening ? Timing.OpenSpeed : Timing.CloseSpeed, 0.0f);

	if (Speed <= ArchOpeningMotion::MinDuration)
	{
		return 0.0f;	// Treated as instantaneous.
	}

	return Travel / Speed;
}

EArchOpeningEasing UArchOpeningComponent::ResolveEasing(bool bOpening) const
{
	return bOpening ? Timing.OpeningEasing : Timing.ClosingEasing;
}

const UCurveFloat* UArchOpeningComponent::ResolveCurve(bool bOpening) const
{
	return bOpening ? Timing.OpeningCurve : Timing.ClosingCurve;
}

bool UArchOpeningComponent::ResolveCurveValidity(bool bOpening) const
{
	return bOpening ? bOpeningCurveValid : bClosingCurveValid;
}

// -------------------------------------------------------------------------------------------
// Commands
// -------------------------------------------------------------------------------------------

bool UArchOpeningComponent::IsMoving() const
{
	return State == EArchOpeningState::Opening || State == EArchOpeningState::Closing;
}

void UArchOpeningComponent::Open(EArchOpeningCommandSource Source)
{
	// Proximity must not fight a manual close made from inside the trigger.
	if (Source == EArchOpeningCommandSource::Proximity && bProximityReopenSuppressed)
	{
		return;
	}

	// Idempotent: an Open while already open or already opening changes nothing, which is what
	// stops repeated commands from re-firing events or restarting the start sound.
	const bool bAlreadyHeadingOpen =
		State == EArchOpeningState::Open ||
		State == EArchOpeningState::Opening ||
		State == EArchOpeningState::OpeningDelay ||
		(State == EArchOpeningState::HandlePreparation && bTransitionOpening);

	if (bAlreadyHeadingOpen)
	{
		// Still cancel a pending auto-close: the user asked for it to stay open.
		if (State == EArchOpeningState::Open && Source != EArchOpeningCommandSource::AutoClose)
		{
			CancelDeferredClose();
			if (Timing.bAutoClose)
			{
				RequestDeferredClose(Timing.AutoCloseDelay, EArchOpeningCommandSource::AutoClose);
			}
			UpdateTickEnabled();
		}
		return;
	}

	LastCommandSource = Source;
	CancelDeferredClose();

	// An explicit open clears an obstruction hold; the leaf is leaving the obstruction anyway.
	bResumeClosingAfterObstruction = false;
	ObstructionReopenTarget = -1.0f;
	CurrentObstructor = nullptr;

	if (Timing.DelayBeforeOpening > 0.0f)
	{
		bTransitionOpening = true;
		StateTimer = Timing.DelayBeforeOpening;
		EnterState(EArchOpeningState::OpeningDelay);
		UpdateTickEnabled();
		return;
	}

	BeginTransition(1.0f, /*bAllowHandleSequence*/ true, Source);
}

void UArchOpeningComponent::Close(EArchOpeningCommandSource Source)
{
	const bool bAlreadyHeadingClosed =
		State == EArchOpeningState::Closed ||
		State == EArchOpeningState::Closing ||
		State == EArchOpeningState::ClosingDelay ||
		(State == EArchOpeningState::HandlePreparation && !bTransitionOpening);

	// A manual close from inside the proximity trigger suppresses proximity re-opening until the
	// trigger empties. Recorded even for a redundant command so the intent is not lost.
	const bool bManual =
		Source == EArchOpeningCommandSource::Click || Source == EArchOpeningCommandSource::Script;

	if (bManual && GetOccupantCount() > 0)
	{
		bProximityReopenSuppressed = true;
	}

	if (bAlreadyHeadingClosed)
	{
		return;
	}

	LastCommandSource = Source;
	CancelDeferredClose();

	bResumeClosingAfterObstruction = false;
	ObstructionReopenTarget = -1.0f;
	CurrentObstructor = nullptr;

	if (Timing.DelayBeforeClosing > 0.0f)
	{
		bTransitionOpening = false;
		StateTimer = Timing.DelayBeforeClosing;
		EnterState(EArchOpeningState::ClosingDelay);
		UpdateTickEnabled();
		return;
	}

	BeginTransition(0.0f, /*bAllowHandleSequence*/ true, Source);
}

void UArchOpeningComponent::Toggle(EArchOpeningCommandSource Source)
{
	const bool bHeadingOpen =
		State == EArchOpeningState::Open ||
		State == EArchOpeningState::Opening ||
		State == EArchOpeningState::OpeningDelay ||
		(State == EArchOpeningState::HandlePreparation && bTransitionOpening);

	if (bHeadingOpen)
	{
		Close(Source);
	}
	else
	{
		Open(Source);
	}
}

void UArchOpeningComponent::Stop()
{
	const bool bWasActive = (State != EArchOpeningState::Closed && State != EArchOpeningState::Open);

	CancelDeferredClose();
	StateTimer = 0.0f;
	ObstructionReopenTarget = -1.0f;
	bResumeClosingAfterObstruction = false;
	CurrentObstructor = nullptr;

	StopMovementLoop();

	// Settle into the idle state matching the pose we stopped at. Any openness above zero counts as
	// Open; only an exactly closed leaf is Closed.
	EnterState(Openness <= ArchOpeningMotion::OpennessEpsilon
		? EArchOpeningState::Closed
		: EArchOpeningState::Open);

	if (bWasActive)
	{
		OnMotionStopped.Broadcast(this);
	}

	UpdateTickEnabled();
}

void UArchOpeningComponent::SetOpennessImmediate(float NewOpenness)
{
	CancelDeferredClose();

	Openness = FMath::Clamp(NewOpenness, 0.0f, 1.0f);
	TransitionProgress = Openness;
	TransitionStartOpenness = Openness;
	TransitionEndOpenness = Openness;
	StateTimer = 0.0f;
	ObstructionReopenTarget = -1.0f;
	bResumeClosingAfterObstruction = false;

	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		const bool bActuated =
			(Group.ReturnBehavior == EArchOpeningHandleReturn::RemainActuatedWhileOpen) && (Openness > 0.0f);
		Group.CurrentAlpha = bActuated ? 1.0f : 0.0f;
		Group.TargetAlpha = Group.CurrentAlpha;
	}

	StopMovementLoop();
	ApplyPose();

	// No transition events and no sounds: this is an authoring / teleport operation.
	State = (Openness <= ArchOpeningMotion::OpennessEpsilon)
		? EArchOpeningState::Closed
		: EArchOpeningState::Open;

	UpdateTickEnabled();
}

void UArchOpeningComponent::SetOpennessAnimated(float TargetOpenness)
{
	const float Target = FMath::Clamp(TargetOpenness, 0.0f, 1.0f);

	if (FMath::IsNearlyEqual(Target, Openness, ArchOpeningMotion::OpennessEpsilon))
	{
		return;
	}

	CancelDeferredClose();
	BeginTransition(Target, /*bAllowHandleSequence*/ false, EArchOpeningCommandSource::Script);
}

// -------------------------------------------------------------------------------------------
// Transitions
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::BeginTransition(float TargetOpenness, bool bAllowHandleSequence, EArchOpeningCommandSource Source)
{
	const float Target = FMath::Clamp(TargetOpenness, 0.0f, 1.0f);
	const bool bOpening = Target > Openness;

	bTransitionOpening = bOpening;
	TransitionStartOpenness = Openness;
	TransitionEndOpenness = Target;
	LastCommandSource = Source;

	if (FMath::IsNearlyEqual(Target, Openness, ArchOpeningMotion::OpennessEpsilon))
	{
		// Nothing to travel: settle immediately rather than starting a degenerate transition.
		Openness = Target;
		EnterState(Target >= 1.0f - ArchOpeningMotion::OpennessEpsilon
			? EArchOpeningState::Open
			: (Target <= ArchOpeningMotion::OpennessEpsilon ? EArchOpeningState::Closed : EArchOpeningState::Open));
		UpdateTickEnabled();
		return;
	}

	// Re-anchor the transition on the pose we are already in.
	//
	// The transition always runs from the CURRENT openness to the target, with its progress reset
	// to 0 and its duration scaled by the fraction of full travel it actually covers. Two things
	// follow, and they are the whole reason for doing it this way:
	//
	//  - Position is continuous. Lerp(Start, End, Ease(0)) == Start == the current pose, so a
	//    reversal mid-motion cannot snap and cannot reset to an endpoint.
	//  - A short remaining distance takes a correspondingly short time instead of restarting a
	//    full-length transition.
	//
	// Velocity is NOT continuous across a reversal: the new transition re-enters its easing at
	// t = 0, so an eased profile decelerates to a stop and accelerates back the other way. For a
	// door that reads correctly; it is a deliberate choice, not an oversight.
	TransitionProgress = 0.0f;

	const float PreparationTime = bAllowHandleSequence ? ComputeHandlePreparationTime(bOpening) : 0.0f;

	if (bAllowHandleSequence)
	{
		// Actuate the handles that participate in this direction.
		SetHandleTargets(/*bActuated*/ true, /*bOnlyGroupsThatActuateOnClosing*/ !bOpening);

		if (AnyHandleActuatesFor(bOpening))
		{
			PlayOneShot(Audio.HandleActuationSound);
		}
	}

	if (PreparationTime > 0.0f)
	{
		StateTimer = PreparationTime;
		EnterState(EArchOpeningState::HandlePreparation);
		UpdateTickEnabled();
		return;
	}

	EnterState(bOpening ? EArchOpeningState::Opening : EArchOpeningState::Closing);
	UpdateTickEnabled();
}

void UArchOpeningComponent::EnterState(EArchOpeningState NewState, bool bArrivedByMotion)
{
	if (State == NewState)
	{
		return;
	}

	const EArchOpeningState PreviousState = State;
	State = NewState;

	switch (NewState)
	{
	case EArchOpeningState::Opening:
		// Handles that only actuate to release the latch return as soon as the leaf starts moving.
		for (FArchOpeningHandleGroup& Group : HandleGroups)
		{
			if (Group.ReturnBehavior == EArchOpeningHandleReturn::ReturnAfterActuation)
			{
				Group.TargetAlpha = 0.0f;
			}
		}
		PlayOneShot(Audio.OpenStartSound);
		StartMovementLoop();
		OnOpeningStarted.Broadcast(this);
		break;

	case EArchOpeningState::Closing:
		for (FArchOpeningHandleGroup& Group : HandleGroups)
		{
			if (Group.ReturnBehavior == EArchOpeningHandleReturn::ReturnAfterActuation)
			{
				Group.TargetAlpha = 0.0f;
			}
		}
		PlayOneShot(Audio.CloseStartSound);
		StartMovementLoop();
		OnClosingStarted.Broadcast(this);
		break;

	case EArchOpeningState::Open:
		StopMovementLoop();
		// Handles that stay actuated while the opening is not closed hold their pose here.
		for (FArchOpeningHandleGroup& Group : HandleGroups)
		{
			Group.TargetAlpha =
				(Group.ReturnBehavior == EArchOpeningHandleReturn::RemainActuatedWhileOpen) ? 1.0f : 0.0f;
		}

		// Endpoint audio and the Fully Opened event fire only when the leaf genuinely travelled all
		// the way here, never on a Stop() that happens to land near the end.
		if (bArrivedByMotion && PreviousState == EArchOpeningState::Opening && Openness >= 1.0f - ArchOpeningMotion::OpennessEpsilon)
		{
			PlayOneShot(Audio.EndStopSound);
			OnFullyOpened.Broadcast(this);

			if (Timing.bAutoClose)
			{
				RequestDeferredClose(Timing.AutoCloseDelay, EArchOpeningCommandSource::AutoClose);
			}
		}
		break;

	case EArchOpeningState::Closed:
		StopMovementLoop();
		for (FArchOpeningHandleGroup& Group : HandleGroups)
		{
			Group.TargetAlpha = 0.0f;
		}

		// Latch audio plays only when the leaf genuinely arrived at closed under its own motion.
		if (bArrivedByMotion && PreviousState == EArchOpeningState::Closing)
		{
			PlayOneShot(Audio.LatchSound);
			OnFullyClosed.Broadcast(this);
		}
		break;

	case EArchOpeningState::Obstructed:
	case EArchOpeningState::OpeningDelay:
	case EArchOpeningState::ClosingDelay:
	case EArchOpeningState::HandlePreparation:
		// The leaf is not travelling in any of these states, so the movement loop must not play.
		// A direction reversal that goes straight from Opening to Closing keeps the loop running,
		// because the leaf never actually stops moving there.
		StopMovementLoop();
		break;

	default:
		break;
	}
}

// -------------------------------------------------------------------------------------------
// Tick
// -------------------------------------------------------------------------------------------

bool UArchOpeningComponent::NeedsTick() const
{
	switch (State)
	{
	case EArchOpeningState::Opening:
	case EArchOpeningState::Closing:
	case EArchOpeningState::OpeningDelay:
	case EArchOpeningState::ClosingDelay:
	case EArchOpeningState::HandlePreparation:
	case EArchOpeningState::Obstructed:
		return true;
	default:
		break;
	}

	return PendingCloseTimer >= 0.0f || AnyHandleStillAnimating();
}

void UArchOpeningComponent::UpdateTickEnabled()
{
	SetComponentTickEnabled(NeedsTick());
}

void UArchOpeningComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (DeltaTime <= 0.0f)
	{
		return;
	}

	TickDelays(DeltaTime);
	TickObstruction(DeltaTime);
	TickTransition(DeltaTime);
	TickHandles(DeltaTime);

	ApplyPose();
	UpdateTickEnabled();
}

void UArchOpeningComponent::TickDelays(float DeltaTime)
{
	// Delays live in the state machine as plain accumulators rather than world timers. There is no
	// callback to become stale, so cancelling or reversing mid-delay cannot leave a pending fire.
	if (PendingCloseTimer >= 0.0f)
	{
		PendingCloseTimer -= DeltaTime;
		if (PendingCloseTimer <= 0.0f)
		{
			const EArchOpeningCommandSource Source = PendingCloseSource;
			CancelDeferredClose();
			Close(Source);
			return;
		}
	}

	switch (State)
	{
	case EArchOpeningState::OpeningDelay:
		StateTimer -= DeltaTime;
		if (StateTimer <= 0.0f)
		{
			StateTimer = 0.0f;
			BeginTransition(1.0f, /*bAllowHandleSequence*/ true, LastCommandSource);
		}
		break;

	case EArchOpeningState::ClosingDelay:
		StateTimer -= DeltaTime;
		if (StateTimer <= 0.0f)
		{
			StateTimer = 0.0f;
			BeginTransition(0.0f, /*bAllowHandleSequence*/ true, LastCommandSource);
		}
		break;

	case EArchOpeningState::HandlePreparation:
		StateTimer -= DeltaTime;
		if (StateTimer <= 0.0f)
		{
			StateTimer = 0.0f;
			EnterState(bTransitionOpening ? EArchOpeningState::Opening : EArchOpeningState::Closing);
		}
		break;

	default:
		break;
	}
}

void UArchOpeningComponent::TickTransition(float DeltaTime)
{
	if (State != EArchOpeningState::Opening && State != EArchOpeningState::Closing)
	{
		return;
	}

	const bool bOpening = (State == EArchOpeningState::Opening);
	const float FullDuration = ResolveTransitionDuration(bOpening);

	// The transition may cover only part of the full travel (a reversal, a partial SetOpenness).
	// Scaling the duration by that fraction is what stops a tiny remaining distance from taking a
	// full-length transition.
	const float Span = FMath::Abs(TransitionEndOpenness - TransitionStartOpenness);
	const float ScaledDuration = FullDuration * FMath::Max(Span, 0.0f);

	if (ScaledDuration <= ArchOpeningMotion::MinDuration)
	{
		TransitionProgress = 1.0f;
	}
	else
	{
		TransitionProgress = FMath::Clamp(TransitionProgress + DeltaTime / ScaledDuration, 0.0f, 1.0f);
	}

	const float Eased = FArchOpeningEasing::Evaluate(
		ResolveEasing(bOpening), ResolveCurve(bOpening), ResolveCurveValidity(bOpening), TransitionProgress);

	Openness = FMath::Clamp(
		FMath::Lerp(TransitionStartOpenness, TransitionEndOpenness, Eased), 0.0f, 1.0f);

	if (TransitionProgress >= 1.0f)
	{
		Openness = TransitionEndOpenness;

		if (Openness >= 1.0f - ArchOpeningMotion::OpennessEpsilon)
		{
			Openness = 1.0f;
			EnterState(EArchOpeningState::Open, /*bArrivedByMotion*/ true);
		}
		else if (Openness <= ArchOpeningMotion::OpennessEpsilon)
		{
			Openness = 0.0f;
			EnterState(EArchOpeningState::Closed, /*bArrivedByMotion*/ true);
		}
		else
		{
			// Arrived at an intermediate target from SetOpennessAnimated. Not an endpoint, so no
			// latch or end-stop sound and no Fully Opened / Fully Closed event.
			EnterState(EArchOpeningState::Open, /*bArrivedByMotion*/ false);
			OnMotionStopped.Broadcast(this);
		}
	}
}

void UArchOpeningComponent::TickHandles(float DeltaTime)
{
	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		if (FMath::IsNearlyEqual(Group.CurrentAlpha, Group.TargetAlpha, KINDA_SMALL_NUMBER))
		{
			Group.CurrentAlpha = Group.TargetAlpha;
			continue;
		}

		const bool bActuating = Group.TargetAlpha > Group.CurrentAlpha;
		const float Duration = bActuating ? Group.ActuationDuration : Group.ReturnDuration;

		if (Duration <= ArchOpeningMotion::MinDuration)
		{
			Group.CurrentAlpha = Group.TargetAlpha;
			continue;
		}

		// Constant rate toward the target. An interrupted actuation simply reverses from wherever it
		// had reached, which is why a cancelled sequence recovers without a snap.
		const float Step = DeltaTime / Duration;
		Group.CurrentAlpha = bActuating
			? FMath::Min(Group.CurrentAlpha + Step, Group.TargetAlpha)
			: FMath::Max(Group.CurrentAlpha - Step, Group.TargetAlpha);
	}
}

float UArchOpeningComponent::ComputeHandlePreparationTime(bool bOpening) const
{
	float Longest = 0.0f;

	for (const FArchOpeningHandleGroup& Group : HandleGroups)
	{
		if (!bOpening && !Group.bActuateOnClosing)
		{
			continue;
		}

		const bool bHasParts = Group.Parts.ContainsByPredicate(
			[](const FArchOpeningPartRef& Part) { return Part.IsValidPart(); });

		if (!bHasParts || FMath::IsNearlyZero(Group.RotationAngle))
		{
			continue;
		}

		Longest = FMath::Max(Longest, FMath::Max(Group.DelayBeforeLeafMovement, 0.0f));
	}

	return Longest;
}

void UArchOpeningComponent::SetHandleTargets(bool bActuated, bool bOnlyGroupsThatActuateOnClosing)
{
	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		if (bOnlyGroupsThatActuateOnClosing && !Group.bActuateOnClosing)
		{
			Group.TargetAlpha = 0.0f;
			continue;
		}

		Group.TargetAlpha = bActuated ? 1.0f : 0.0f;
	}
}

bool UArchOpeningComponent::AnyHandleActuatesFor(bool bOpening) const
{
	for (const FArchOpeningHandleGroup& Group : HandleGroups)
	{
		if (!bOpening && !Group.bActuateOnClosing)
		{
			continue;
		}

		const bool bHasParts = Group.Parts.ContainsByPredicate(
			[](const FArchOpeningPartRef& Part) { return Part.IsValidPart(); });

		if (bHasParts && !FMath::IsNearlyZero(Group.RotationAngle))
		{
			return true;
		}
	}

	return false;
}

bool UArchOpeningComponent::AnyHandleStillAnimating() const
{
	for (const FArchOpeningHandleGroup& Group : HandleGroups)
	{
		if (!FMath::IsNearlyEqual(Group.CurrentAlpha, Group.TargetAlpha, KINDA_SMALL_NUMBER))
		{
			return true;
		}
	}

	return false;
}

// -------------------------------------------------------------------------------------------
// Deferred close arbitration
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::RequestDeferredClose(float Delay, EArchOpeningCommandSource Source)
{
	const float Clamped = FMath::Max(Delay, 0.0f);

	// One timer, shared by auto-close and proximity close. Whichever wants to close sooner wins, so
	// the two features can never run competing schedules against each other.
	if (PendingCloseTimer < 0.0f || Clamped < PendingCloseTimer)
	{
		PendingCloseTimer = Clamped;
		PendingCloseSource = Source;
	}

	UpdateTickEnabled();
}

void UArchOpeningComponent::CancelDeferredClose()
{
	PendingCloseTimer = -1.0f;
	PendingCloseSource = EArchOpeningCommandSource::Script;
}

#undef LOCTEXT_NAMESPACE
