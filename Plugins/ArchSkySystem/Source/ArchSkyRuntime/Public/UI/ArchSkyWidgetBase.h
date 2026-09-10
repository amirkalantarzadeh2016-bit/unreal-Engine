// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "UI/ArchSkyViewModel.h"

#include "ArchSkyWidgetBase.generated.h"

class UButton;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class USlider;
class UTextBlock;

/**
 * C++ base for the ArchSky control panel. Subclass this in Blueprint to do the visuals.
 *
 * WHAT THIS CLASS GUARANTEES
 * --------------------------
 *  - It owns the ViewModel's lifetime and unbinds every delegate in NativeDestruct.
 *  - It throttles slider traffic. A UMG slider fires OnValueChanged on every mouse-move,
 *    which at 120 Hz would push 120 state changes, 120 solar solves and 120 broadcasts per
 *    second for a movement the user perceives as one drag. We coalesce to
 *    SliderPushInterval and to a minimum value delta.
 *  - It never touches a light, an atmosphere or a fog component. Every mutation goes
 *    through the ViewModel, which goes through the subsystem.
 *
 * BindWidget properties are all OPTIONAL: a designer can build a cut-down panel with only
 * a time slider and a clock label without the widget failing to compile at load.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "ArchSky Widget Base"))
class ARCHSKYRUNTIME_API UArchSkyWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UArchSkyWidgetBase(const FObjectInitializer& ObjectInitializer);

	//~ Begin UUserWidget
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End UUserWidget

	/** The ViewModel this widget presents. Bind Blueprint UI to this, never to the subsystem. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI")
	TObjectPtr<UArchSkyViewModel> ViewModel;

	/**
	 * Called after the ViewModel raises OnViewModelUpdated.
	 * Override in Blueprint to push the ViewModel's fields into your text blocks. This is
	 * the ONLY place a designer should be reading ViewModel state - property bindings
	 * evaluate every frame and are exactly what this architecture exists to avoid.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "ArchSky|UI", meta = (DisplayName = "On Sky View Updated"))
	void BP_OnSkyViewUpdated();

	/** Called when the saved-preset list changes, so a Blueprint list view can rebuild. */
	UFUNCTION(BlueprintImplementableEvent, Category = "ArchSky|UI", meta = (DisplayName = "On Preset List Changed"))
	void BP_OnPresetListChanged();

	/** Shows or hides the panel and moves keyboard focus accordingly. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI")
	void TogglePanelVisibility();

	/** True while the panel is on screen. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|UI")
	bool IsPanelOpen() const;

	// -----------------------------------------------------------------------------------
	// Optional bound controls. Every one is optional so partial panels are valid.
	// -----------------------------------------------------------------------------------

	/** 0..1 across the 24-hour day. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<USlider> TimeOfDaySlider;

	/** 0..1 across the year. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<USlider> DayOfYearSlider;

	/** 0..1 mapped to 0..360 degrees of plan-north offset. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<USlider> NorthOffsetDial;

	/** Live clock. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TimeLabel;

	/** Date in the selected calendar. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DateLabel;

	/** Sunrise / sunset / day-length readout. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SunTimesLabel;

	/** Azimuth / altitude / shadow-length readout. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AnalysisLabel;

	/** Play / pause. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlayPauseButton;

	/** Gregorian / Jalali toggle. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> CalendarToggle;

	/** Searchable city dropdown. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> LocationCombo;

	/** Free-text city search. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Bound", meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> LocationSearchBox;

	// -----------------------------------------------------------------------------------
	// Throttling
	// -----------------------------------------------------------------------------------

	/**
	 * Minimum seconds between two slider-driven pushes to the subsystem.
	 * 1/30 s is below the threshold at which a dragged slider feels laggy, and cuts the
	 * work by three quarters at 120 fps.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|UI|Performance",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.25", Units = "Seconds"))
	float SliderPushInterval = 0.0333f;

	/** Minimum change in hours before a time-slider move is worth pushing. ~9 seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|UI|Performance",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.25", Units = "Hours"))
	float TimeSliderMinDelta = 0.0025f;

	/** Keyboard/gamepad nudge applied by the left/right keys, in hours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|UI|Input",
		meta = (ClampMin = "0.0", ClampMax = "6.0", UIMin = "0.05", UIMax = "1.0", Units = "Hours"))
	float KeyboardTimeStepHours = 0.25f;

protected:
	/** Slider handlers, wired up in NativeOnInitialized when the control exists. */
	UFUNCTION()
	void HandleTimeSliderChanged(float Value);

	UFUNCTION()
	void HandleDaySliderChanged(float Value);

	UFUNCTION()
	void HandleNorthDialChanged(float Value);

	UFUNCTION()
	void HandlePlayPauseClicked();

	UFUNCTION()
	void HandleCalendarToggled(bool bIsChecked);

	UFUNCTION()
	void HandleLocationSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleLocationSearchChanged(const FText& SearchText);

	/** Raised by the ViewModel; pushes text into the bound labels and calls the BP event. */
	UFUNCTION()
	void HandleViewModelUpdated();

	/** Raised by the ViewModel when the preset list changes. */
	UFUNCTION()
	void HandlePresetListChanged();

	/** Rebuilds the city dropdown from the current search text. */
	void RebuildLocationOptions();

	/** Pushes any pending, throttled slider value to the ViewModel. */
	void FlushPendingSliderPush();

private:
	/** City ids parallel to the combo box's display strings, so selection needs no search. */
	TArray<FName> LocationComboIds;

	/** Value waiting to be pushed, and which control produced it. */
	float PendingTimeOfDayHours = 0.f;
	float PendingDayOfYear = 0.f;
	float PendingNorthOffsetDegrees = 0.f;

	bool bTimePushPending = false;
	bool bDayPushPending = false;
	bool bNorthPushPending = false;

	/** Seconds since the last push, compared against SliderPushInterval. */
	float SecondsSinceSliderPush = 0.f;

	/** Set while HandleViewModelUpdated writes slider values, so we do not echo them back. */
	bool bSuppressSliderCallbacks = false;
};
