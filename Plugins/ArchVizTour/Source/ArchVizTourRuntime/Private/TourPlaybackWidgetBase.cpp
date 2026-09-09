// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourPlaybackWidgetBase.h"

#include "ArchVizTourLog.h"
#include "TourSequencePreset.h"
#include "TourSubsystem.h"

#define LOCTEXT_NAMESPACE "ArchVizTour"

namespace ArchVizTour::WidgetPrivate
{
	/** Format seconds as m:ss, which is what a tour-length readout wants. */
	static FText FormatSeconds(float Seconds)
	{
		const int32 TotalSeconds = FMath::Max(FMath::FloorToInt(Seconds), 0);
		const int32 Minutes = TotalSeconds / 60;
		const int32 RemainingSeconds = TotalSeconds % 60;

		return FText::FromString(FString::Printf(TEXT("%d:%02d"), Minutes, RemainingSeconds));
	}
}

void UTourPlaybackWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	BindToSubsystem();

	// A widget can be constructed after the tour was already loaded and started, so the current
	// state is pushed once on construction rather than waiting for the next change.
	if (const UTourSubsystem* Subsystem = BoundSubsystem.Get())
	{
		OnTourLoaded(Subsystem->GetLoadedTour());
		OnStateChanged(ETourState::Idle, Subsystem->GetState());
		OnStepChanged(Subsystem->GetCurrentStepIndex(), Subsystem->GetCurrentStepLabel());
		OnProgress(Subsystem->GetTourProgress(), Subsystem->GetStepProgress());
	}
}

void UTourPlaybackWidgetBase::NativeDestruct()
{
	UnbindFromSubsystem();

	Super::NativeDestruct();
}

void UTourPlaybackWidgetBase::BindToSubsystem()
{
	UTourSubsystem* Subsystem = UTourSubsystem::Get(this);
	if (Subsystem == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour playback widget '%s' could not find a tour subsystem; transport buttons will do nothing."),
			*GetName());
		return;
	}

	if (BoundSubsystem.Get() == Subsystem)
	{
		return;
	}

	UnbindFromSubsystem();

	Subsystem->OnTourStateChanged.AddDynamic(this, &UTourPlaybackWidgetBase::HandleTourStateChanged);
	Subsystem->OnStepChanged.AddDynamic(this, &UTourPlaybackWidgetBase::HandleStepChanged);
	Subsystem->OnTourProgress.AddDynamic(this, &UTourPlaybackWidgetBase::HandleTourProgress);
	Subsystem->OnTourFinished.AddDynamic(this, &UTourPlaybackWidgetBase::HandleTourFinished);
	Subsystem->OnCustomEventTag.AddDynamic(this, &UTourPlaybackWidgetBase::HandleCustomEventTag);
	Subsystem->OnPresetLoaded.AddDynamic(this, &UTourPlaybackWidgetBase::HandlePresetLoaded);

	BoundSubsystem = Subsystem;
}

void UTourPlaybackWidgetBase::UnbindFromSubsystem()
{
	UTourSubsystem* Subsystem = BoundSubsystem.Get();
	if (Subsystem == nullptr)
	{
		// The subsystem is already gone with its world; its delegates went with it.
		BoundSubsystem.Reset();
		return;
	}

	Subsystem->OnTourStateChanged.RemoveDynamic(this, &UTourPlaybackWidgetBase::HandleTourStateChanged);
	Subsystem->OnStepChanged.RemoveDynamic(this, &UTourPlaybackWidgetBase::HandleStepChanged);
	Subsystem->OnTourProgress.RemoveDynamic(this, &UTourPlaybackWidgetBase::HandleTourProgress);
	Subsystem->OnTourFinished.RemoveDynamic(this, &UTourPlaybackWidgetBase::HandleTourFinished);
	Subsystem->OnCustomEventTag.RemoveDynamic(this, &UTourPlaybackWidgetBase::HandleCustomEventTag);
	Subsystem->OnPresetLoaded.RemoveDynamic(this, &UTourPlaybackWidgetBase::HandlePresetLoaded);

	BoundSubsystem.Reset();
}

// ---------------------------------------------------------------------------
// Delegate relays
// ---------------------------------------------------------------------------

void UTourPlaybackWidgetBase::HandleTourStateChanged(ETourState OldState, ETourState NewState)
{
	OnStateChanged(OldState, NewState);
}

void UTourPlaybackWidgetBase::HandleStepChanged(int32 StepIndex, const FText& Label)
{
	OnStepChanged(StepIndex, Label);
}

void UTourPlaybackWidgetBase::HandleTourProgress(float TourAlpha, float StepAlpha)
{
	OnProgress(TourAlpha, StepAlpha);
}

void UTourPlaybackWidgetBase::HandleTourFinished(bool bWasInterrupted)
{
	OnTourFinished(bWasInterrupted);
}

void UTourPlaybackWidgetBase::HandleCustomEventTag(FGameplayTag Tag)
{
	OnCustomEvent(Tag);
}

void UTourPlaybackWidgetBase::HandlePresetLoaded(UTourSequencePreset* Preset)
{
	OnTourLoaded(Preset);
}

// ---------------------------------------------------------------------------
// Transport passthroughs
// ---------------------------------------------------------------------------

UTourSubsystem* UTourPlaybackWidgetBase::GetTourSubsystem() const
{
	if (UTourSubsystem* Bound = BoundSubsystem.Get())
	{
		return Bound;
	}

	// Falling back to a live lookup keeps the query helpers usable before NativeConstruct, which
	// is where a designer preview evaluates them.
	return UTourSubsystem::Get(this);
}

void UTourPlaybackWidgetBase::Play()
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->PlayTour();
	}
}

void UTourPlaybackWidgetBase::Pause()
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->SetPaused(true);
	}
}

void UTourPlaybackWidgetBase::TogglePause()
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->TogglePause();
	}
}

void UTourPlaybackWidgetBase::Stop()
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->StopTour();
	}
}

void UTourPlaybackWidgetBase::Next()
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->NextStep();
	}
}

void UTourPlaybackWidgetBase::Previous()
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->PreviousStep();
	}
}

void UTourPlaybackWidgetBase::Restart()
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->RestartTour();
	}
}

void UTourPlaybackWidgetBase::JumpToStep(int32 StepIndex)
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->JumpToStep(StepIndex);
	}
}

void UTourPlaybackWidgetBase::ScrubToAlpha(float Alpha)
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->ScrubToAlpha(Alpha);
	}
}

void UTourPlaybackWidgetBase::SetTimeScale(float NewTimeScale)
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->SetTimeScale(NewTimeScale);
	}
}

void UTourPlaybackWidgetBase::SetReversed(bool bReversed)
{
	if (UTourSubsystem* Subsystem = GetTourSubsystem())
	{
		Subsystem->SetPlaybackDirection(bReversed ? -1.0f : 1.0f);
	}
}

void UTourPlaybackWidgetBase::LoadTour(UTourSequencePreset* Preset, bool bAutoPlay)
{
	UTourSubsystem* Subsystem = GetTourSubsystem();
	if (Subsystem == nullptr)
	{
		return;
	}

	if (Subsystem->LoadTour(Preset) && bAutoPlay)
	{
		Subsystem->PlayTour();
	}
}

// ---------------------------------------------------------------------------
// Query passthroughs
// ---------------------------------------------------------------------------

bool UTourPlaybackWidgetBase::IsPlayButtonEnabled() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr && Subsystem->IsPlayButtonEnabled();
}

bool UTourPlaybackWidgetBase::IsPauseButtonEnabled() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr && Subsystem->IsPauseButtonEnabled();
}

bool UTourPlaybackWidgetBase::IsStopButtonEnabled() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr && Subsystem->IsStopButtonEnabled();
}

bool UTourPlaybackWidgetBase::IsNextButtonEnabled() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr && Subsystem->IsNextButtonEnabled();
}

bool UTourPlaybackWidgetBase::IsPreviousButtonEnabled() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr && Subsystem->IsPreviousButtonEnabled();
}

ETourState UTourPlaybackWidgetBase::GetTourState() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr ? Subsystem->GetState() : ETourState::Idle;
}

int32 UTourPlaybackWidgetBase::GetCurrentStepIndex() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr ? Subsystem->GetCurrentStepIndex() : INDEX_NONE;
}

int32 UTourPlaybackWidgetBase::GetStepCount() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr ? Subsystem->GetStepCount() : 0;
}

FText UTourPlaybackWidgetBase::GetCurrentStepLabel() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr ? Subsystem->GetCurrentStepLabel() : FText::GetEmpty();
}

TArray<FText> UTourPlaybackWidgetBase::GetStepLabels() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr ? Subsystem->GetStepLabels() : TArray<FText>();
}

float UTourPlaybackWidgetBase::GetTourProgress() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr ? Subsystem->GetTourProgress() : 0.0f;
}

float UTourPlaybackWidgetBase::GetStepProgress() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	return Subsystem != nullptr ? Subsystem->GetStepProgress() : 0.0f;
}

FText UTourPlaybackWidgetBase::GetFormattedTime() const
{
	const UTourSubsystem* Subsystem = GetTourSubsystem();
	if (Subsystem == nullptr)
	{
		return FText::GetEmpty();
	}

	float Elapsed = 0.0f;
	float Total = 0.0f;
	Subsystem->GetTourTimes(Elapsed, Total);

	return FText::Format(
		LOCTEXT("TourTimeReadout", "{0} / {1}"),
		ArchVizTour::WidgetPrivate::FormatSeconds(Elapsed),
		ArchVizTour::WidgetPrivate::FormatSeconds(Total));
}

#undef LOCTEXT_NAMESPACE
