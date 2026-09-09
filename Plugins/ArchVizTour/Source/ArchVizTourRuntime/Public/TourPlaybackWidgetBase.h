// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TourTypes.h"

#include "TourPlaybackWidgetBase.generated.h"

class UTourSequencePreset;
class UTourSubsystem;

/**
 * Base class for a tour playback widget.
 *
 * No visual hierarchy is shipped on purpose: a hard-coded widget is the one thing every
 * project throws away. Reparent an existing UMG widget to this class and its buttons can be
 * wired with a single node each, while the events below replace any polling in Tick.
 *
 * Subscribes to the subsystem in NativeConstruct and unsubscribes in NativeDestruct, so a
 * widget removed from the viewport never keeps a stale binding alive.
 */
UCLASS(Abstract, BlueprintType, Blueprintable, meta = (DisplayName = "Tour Playback Widget Base"))
class ARCHVIZTOURRUNTIME_API UTourPlaybackWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	// --- UUserWidget ------------------------------------------------------
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ---------------------------------------------------------------------
	// Events. Implement the ones the widget cares about; unimplemented events cost nothing.
	// ---------------------------------------------------------------------

	/** The tour's state changed. Use it to swap a play icon for a pause icon. */
	UFUNCTION(BlueprintImplementableEvent, Category = "ArchViz Tour|Events")
	void OnStateChanged(ETourState OldState, ETourState NewState);

	/** The current step changed. Use it to highlight a step in a list. */
	UFUNCTION(BlueprintImplementableEvent, Category = "ArchViz Tour|Events")
	void OnStepChanged(int32 StepIndex, const FText& Label);

	/** Playback advanced. Both alphas are 0..1. Fired once per tick while playing. */
	UFUNCTION(BlueprintImplementableEvent, Category = "ArchViz Tour|Events")
	void OnProgress(float TourAlpha, float StepAlpha);

	/** The tour ended. bWasInterrupted is true when it was stopped rather than completed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "ArchViz Tour|Events")
	void OnTourFinished(bool bWasInterrupted);

	/** A Custom step was entered. */
	UFUNCTION(BlueprintImplementableEvent, Category = "ArchViz Tour|Events")
	void OnCustomEvent(FGameplayTag Tag);

	/** A tour was installed. Use it to rebuild a step list. */
	UFUNCTION(BlueprintImplementableEvent, Category = "ArchViz Tour|Events")
	void OnTourLoaded(UTourSequencePreset* Preset);

	// ---------------------------------------------------------------------
	// Transport passthroughs. One node each, so button graphs hold no logic.
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void Play();

	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void Pause();

	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void TogglePause();

	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void Stop();

	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void Next();

	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void Previous();

	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void Restart();

	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void JumpToStep(int32 StepIndex);

	/** Wire this to a slider's OnValueChanged to scrub the tour. Alpha is 0..1. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void ScrubToAlpha(float Alpha);

	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void SetTimeScale(float NewTimeScale);

	/** Play backwards when true. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void SetReversed(bool bReversed);

	/** Install a tour and, when bAutoPlay is set, start it immediately. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Transport")
	void LoadTour(UTourSequencePreset* Preset, bool bAutoPlay = false);

	// ---------------------------------------------------------------------
	// Query passthroughs, so button enable states need no duplicated logic
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	bool IsPlayButtonEnabled() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	bool IsPauseButtonEnabled() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	bool IsStopButtonEnabled() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	bool IsNextButtonEnabled() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	bool IsPreviousButtonEnabled() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	ETourState GetTourState() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	int32 GetCurrentStepIndex() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	int32 GetStepCount() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	FText GetCurrentStepLabel() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	TArray<FText> GetStepLabels() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	float GetTourProgress() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	float GetStepProgress() const;

	/**
	 * Elapsed and total tour time, pre-formatted for display.
	 * @param Format  Text format taking {0} elapsed and {1} total, both as m:ss.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	FText GetFormattedTime() const;

	/** The subsystem this widget is bound to. Null before NativeConstruct or outside a world. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	UTourSubsystem* GetTourSubsystem() const;

protected:
	/** Bind to the subsystem's delegates. Idempotent. */
	void BindToSubsystem();

	/** Unbind from the subsystem's delegates. Idempotent and safe after world teardown. */
	void UnbindFromSubsystem();

private:
	// UFUNCTION-marked relays are required: dynamic multicast delegates can only bind to them.

	UFUNCTION()
	void HandleTourStateChanged(ETourState OldState, ETourState NewState);

	UFUNCTION()
	void HandleStepChanged(int32 StepIndex, const FText& Label);

	UFUNCTION()
	void HandleTourProgress(float TourAlpha, float StepAlpha);

	UFUNCTION()
	void HandleTourFinished(bool bWasInterrupted);

	UFUNCTION()
	void HandleCustomEventTag(FGameplayTag Tag);

	UFUNCTION()
	void HandlePresetLoaded(UTourSequencePreset* Preset);

	/** Weak so a widget outliving its world never resurrects the subsystem. */
	TWeakObjectPtr<UTourSubsystem> BoundSubsystem;
};
