// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ArchSkyWidgetBase.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Data/ArchTimeCalendar.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchSkyWidget"

UArchSkyWidgetBase::UArchSkyWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Focusability is set in NativeOnInitialized via SetIsFocusable(): the bIsFocusable
	// member was deprecated in favour of the accessor, and touching it here would emit a
	// deprecation warning.
}

// ---------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------

void UArchSkyWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// The panel is a control surface, not a HUD: it must be focusable so the whole thing
	// is reachable from a gamepad during a presentation with no mouse.
	SetIsFocusable(true);

	// Outered to the widget so the ViewModel's lifetime is exactly the widget's, and so
	// GetWorldFromContextObject can walk up to a world if anything needs it.
	ViewModel = NewObject<UArchSkyViewModel>(this, UArchSkyViewModel::StaticClass(), TEXT("ArchSkyViewModel"));
	if (!ViewModel)
	{
		UE_LOG(LogArchSky, Error, TEXT("Failed to create the ArchSky ViewModel; the panel will be inert."));
		return;
	}

	ViewModel->Initialise(this);
	ViewModel->OnViewModelUpdated.AddDynamic(this, &UArchSkyWidgetBase::HandleViewModelUpdated);
	ViewModel->OnPresetListChanged.AddDynamic(this, &UArchSkyWidgetBase::HandlePresetListChanged);

	// Every control is optional, so each hook-up is individually guarded.
	if (TimeOfDaySlider)
	{
		TimeOfDaySlider->OnValueChanged.AddDynamic(this, &UArchSkyWidgetBase::HandleTimeSliderChanged);
		TimeOfDaySlider->SetToolTipText(LOCTEXT("TimeSliderTooltip",
			"Time of day. Drag to scrub the sun across the sky; tick marks show sunrise, solar noon and sunset."));
	}

	if (DayOfYearSlider)
	{
		DayOfYearSlider->OnValueChanged.AddDynamic(this, &UArchSkyWidgetBase::HandleDaySliderChanged);
		DayOfYearSlider->SetToolTipText(LOCTEXT("DaySliderTooltip",
			"Day of the year. Drag to move through the seasons and watch the sun's arc change height."));
	}

	if (NorthOffsetDial)
	{
		NorthOffsetDial->OnValueChanged.AddDynamic(this, &UArchSkyWidgetBase::HandleNorthDialChanged);
		NorthOffsetDial->SetToolTipText(LOCTEXT("NorthDialTooltip",
			"Rotation of true north relative to the floor plan. Set this to match the site plan's north arrow - "
			"shadow studies are wrong until it is correct."));
	}

	if (PlayPauseButton)
	{
		PlayPauseButton->OnClicked.AddDynamic(this, &UArchSkyWidgetBase::HandlePlayPauseClicked);
		PlayPauseButton->SetToolTipText(LOCTEXT("PlayPauseTooltip", "Start or stop the passage of time."));
	}

	if (CalendarToggle)
	{
		CalendarToggle->OnCheckStateChanged.AddDynamic(this, &UArchSkyWidgetBase::HandleCalendarToggled);
		CalendarToggle->SetToolTipText(LOCTEXT("CalendarToggleTooltip",
			"Switch the date between the Gregorian and Jalali (Solar Hijri) calendars."));
	}

	if (LocationCombo)
	{
		LocationCombo->OnSelectionChanged.AddDynamic(this, &UArchSkyWidgetBase::HandleLocationSelected);
		LocationCombo->SetToolTipText(LOCTEXT("LocationComboTooltip",
			"Site location. Latitude and timezone determine the sun's path and the length of the day."));
	}

	if (LocationSearchBox)
	{
		LocationSearchBox->OnTextChanged.AddDynamic(this, &UArchSkyWidgetBase::HandleLocationSearchChanged);
		LocationSearchBox->SetToolTipText(LOCTEXT("LocationSearchTooltip", "Type to filter the city list."));
		LocationSearchBox->SetHintText(LOCTEXT("LocationSearchHint", "Search cities..."));
	}

	RebuildLocationOptions();
}

void UArchSkyWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	// The ViewModel may have been initialised before a subsystem existed (a widget created
	// during level transition). Re-initialising is cheap and idempotent.
	if (ViewModel && !ViewModel->IsInitialised())
	{
		ViewModel->Initialise(this);
	}

	HandleViewModelUpdated();
	SetKeyboardFocus();
}

void UArchSkyWidgetBase::NativeDestruct()
{
	// ARCH NOTE: teardown is explicit and total. The subsystem outlives this widget, so a
	// delegate left bound here is a dangling call into a destroyed UObject the first time
	// the sun moves after the panel closes.
	FlushPendingSliderPush();

	if (ViewModel)
	{
		ViewModel->OnViewModelUpdated.RemoveDynamic(this, &UArchSkyWidgetBase::HandleViewModelUpdated);
		ViewModel->OnPresetListChanged.RemoveDynamic(this, &UArchSkyWidgetBase::HandlePresetListChanged);
		ViewModel->Shutdown();
	}

	if (TimeOfDaySlider)
	{
		TimeOfDaySlider->OnValueChanged.RemoveDynamic(this, &UArchSkyWidgetBase::HandleTimeSliderChanged);
	}
	if (DayOfYearSlider)
	{
		DayOfYearSlider->OnValueChanged.RemoveDynamic(this, &UArchSkyWidgetBase::HandleDaySliderChanged);
	}
	if (NorthOffsetDial)
	{
		NorthOffsetDial->OnValueChanged.RemoveDynamic(this, &UArchSkyWidgetBase::HandleNorthDialChanged);
	}
	if (PlayPauseButton)
	{
		PlayPauseButton->OnClicked.RemoveDynamic(this, &UArchSkyWidgetBase::HandlePlayPauseClicked);
	}
	if (CalendarToggle)
	{
		CalendarToggle->OnCheckStateChanged.RemoveDynamic(this, &UArchSkyWidgetBase::HandleCalendarToggled);
	}
	if (LocationCombo)
	{
		LocationCombo->OnSelectionChanged.RemoveDynamic(this, &UArchSkyWidgetBase::HandleLocationSelected);
	}
	if (LocationSearchBox)
	{
		LocationSearchBox->OnTextChanged.RemoveDynamic(this, &UArchSkyWidgetBase::HandleLocationSearchChanged);
	}

	Super::NativeDestruct();
}

void UArchSkyWidgetBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// The only per-frame work in the whole UI layer: draining the slider throttle.
	if (!bTimePushPending && !bDayPushPending && !bNorthPushPending)
	{
		return;
	}

	SecondsSinceSliderPush += InDeltaTime;
	if (SecondsSinceSliderPush >= SliderPushInterval)
	{
		FlushPendingSliderPush();
	}
}

// ---------------------------------------------------------------------------------------
// Slider throttling
// ---------------------------------------------------------------------------------------

void UArchSkyWidgetBase::FlushPendingSliderPush()
{
	if (!ViewModel)
	{
		bTimePushPending = bDayPushPending = bNorthPushPending = false;
		return;
	}

	if (bTimePushPending)
	{
		ViewModel->CommandSetTimeOfDay(PendingTimeOfDayHours);
		bTimePushPending = false;
	}

	if (bDayPushPending)
	{
		ViewModel->CommandSetDayOfYear(FMath::RoundToInt32(PendingDayOfYear));
		bDayPushPending = false;
	}

	if (bNorthPushPending)
	{
		ViewModel->CommandSetNorthOffset(PendingNorthOffsetDegrees);
		bNorthPushPending = false;
	}

	SecondsSinceSliderPush = 0.f;
}

void UArchSkyWidgetBase::HandleTimeSliderChanged(float Value)
{
	if (bSuppressSliderCallbacks || !ViewModel)
	{
		return;
	}

	const float Hours = FMath::Clamp(Value, 0.f, 1.f) * 24.f;

	// Two gates: a value delta and a time interval. The delta alone would still let a
	// fast drag through at full frame rate; the interval alone would drop a slow,
	// deliberate nudge. Together they coalesce a drag without ever losing the final value,
	// because NativeTick flushes whatever is still pending.
	if (FMath::Abs(Hours - PendingTimeOfDayHours) < TimeSliderMinDelta && bTimePushPending)
	{
		return;
	}

	PendingTimeOfDayHours = Hours;
	bTimePushPending = true;
}

void UArchSkyWidgetBase::HandleDaySliderChanged(float Value)
{
	if (bSuppressSliderCallbacks || !ViewModel)
	{
		return;
	}

	// 1..365 or 1..366; the subsystem clamps to the year's real length.
	PendingDayOfYear = 1.f + FMath::Clamp(Value, 0.f, 1.f) * 365.f;
	bDayPushPending = true;
}

void UArchSkyWidgetBase::HandleNorthDialChanged(float Value)
{
	if (bSuppressSliderCallbacks || !ViewModel)
	{
		return;
	}

	PendingNorthOffsetDegrees = FMath::Clamp(Value, 0.f, 1.f) * 360.f;
	bNorthPushPending = true;
}

// ---------------------------------------------------------------------------------------
// Other controls
// ---------------------------------------------------------------------------------------

void UArchSkyWidgetBase::HandlePlayPauseClicked()
{
	if (ViewModel)
	{
		ViewModel->CommandToggleTimePause();
	}
}

void UArchSkyWidgetBase::HandleCalendarToggled(bool bIsChecked)
{
	if (ViewModel)
	{
		ViewModel->CommandSetCalendarType(bIsChecked ? EArchCalendarType::Jalali : EArchCalendarType::Gregorian);
		RebuildLocationOptions();
	}
}

void UArchSkyWidgetBase::HandleLocationSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// Ignore the selection the combo box raises while we are repopulating it.
	if (SelectionType == ESelectInfo::Direct || !ViewModel || !LocationCombo)
	{
		return;
	}

	const int32 Index = LocationCombo->FindOptionIndex(SelectedItem);
	if (LocationComboIds.IsValidIndex(Index))
	{
		ViewModel->CommandSetLocationPreset(LocationComboIds[Index]);
	}
}

void UArchSkyWidgetBase::HandleLocationSearchChanged(const FText& SearchText)
{
	RebuildLocationOptions();
}

void UArchSkyWidgetBase::RebuildLocationOptions()
{
	if (!LocationCombo || !ViewModel)
	{
		return;
	}

	const FString SearchText = LocationSearchBox ? LocationSearchBox->GetText().ToString() : FString();
	const TArray<FArchLocationEntry> Options = ViewModel->GetLocationOptions(SearchText);

	LocationCombo->ClearOptions();
	LocationComboIds.Reset(Options.Num());

	for (const FArchLocationEntry& Entry : Options)
	{
		// The parallel id array means selection never has to re-search by display string,
		// which would break the moment two cities shared a localised name.
		LocationComboIds.Add(Entry.CityId);
		LocationCombo->AddOption(Entry.DisplayName.ToString());
	}
}

// ---------------------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------------------

void UArchSkyWidgetBase::HandleViewModelUpdated()
{
	if (!ViewModel)
	{
		return;
	}

	// Writing a slider's value raises its OnValueChanged, which would push straight back
	// into the subsystem and fight the user's drag. Suppress for the duration.
	TGuardValue<bool> SuppressGuard(bSuppressSliderCallbacks, true);

	if (TimeOfDaySlider)
	{
		TimeOfDaySlider->SetValue(ViewModel->TimeOfDayHours / 24.f);
	}

	if (DayOfYearSlider)
	{
		DayOfYearSlider->SetValue(static_cast<float>(ViewModel->DayOfYear - 1) / 365.f);
	}

	if (NorthOffsetDial)
	{
		NorthOffsetDial->SetValue(ViewModel->NorthOffsetDegrees / 360.f);
	}

	if (TimeLabel)
	{
		TimeLabel->SetText(ViewModel->TimeText);
	}

	if (DateLabel)
	{
		DateLabel->SetText(ViewModel->DateText);
	}

	if (SunTimesLabel)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Sunrise"), ViewModel->SunriseText);
		Args.Add(TEXT("Sunset"), ViewModel->SunsetText);
		Args.Add(TEXT("DayLength"), ViewModel->DayLengthText);

		SunTimesLabel->SetText(FText::Format(
			LOCTEXT("SunTimesFormat", "Sunrise {Sunrise}  /  Sunset {Sunset}  /  Day length {DayLength}"), Args));
	}

	if (AnalysisLabel)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Azimuth"), ViewModel->SunAzimuthText);
		Args.Add(TEXT("Altitude"), ViewModel->SunAltitudeText);
		Args.Add(TEXT("Shadow"), ViewModel->ShadowLengthText);
		Args.Add(TEXT("Phase"), ViewModel->TimePhaseText);

		AnalysisLabel->SetText(FText::Format(
			LOCTEXT("AnalysisFormat", "Azimuth {Azimuth}  /  Altitude {Altitude}  /  Shadow {Shadow}  /  {Phase}"), Args));
	}

	BP_OnSkyViewUpdated();
}

void UArchSkyWidgetBase::HandlePresetListChanged()
{
	BP_OnPresetListChanged();
}

// ---------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------

FReply UArchSkyWidgetBase::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!ViewModel)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	const FKey Key = InKeyEvent.GetKey();

	// Keyboard and gamepad parity: an architect presenting from a couch with a controller
	// gets the same scrub control as one at a desk.
	const bool bNudgeBack = (Key == EKeys::Left) || (Key == EKeys::Gamepad_DPad_Left);
	const bool bNudgeForward = (Key == EKeys::Right) || (Key == EKeys::Gamepad_DPad_Right);

	if (bNudgeBack || bNudgeForward)
	{
		const float Step = bNudgeForward ? KeyboardTimeStepHours : -KeyboardTimeStepHours;
		ViewModel->CommandSetTimeOfDay(ArchTimeCalendar::WrapHours(ViewModel->TimeOfDayHours + Step));
		return FReply::Handled();
	}

	if (Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		ViewModel->CommandToggleTimePause();
		return FReply::Handled();
	}

	// Jump shortcuts, matching the buttons an architect uses most.
	if (Key == EKeys::One)
	{
		ViewModel->CommandJumpToSunrise();
		return FReply::Handled();
	}
	if (Key == EKeys::Two)
	{
		ViewModel->CommandJumpToSolarNoon();
		return FReply::Handled();
	}
	if (Key == EKeys::Three)
	{
		ViewModel->CommandJumpToSunset();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UArchSkyWidgetBase::TogglePanelVisibility()
{
	if (IsPanelOpen())
	{
		// Flush before hiding so a half-finished drag is not silently discarded.
		FlushPendingSliderPush();
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	SetVisibility(ESlateVisibility::Visible);
	HandleViewModelUpdated();
	SetKeyboardFocus();
}

bool UArchSkyWidgetBase::IsPanelOpen() const
{
	const ESlateVisibility Current = GetVisibility();
	return Current == ESlateVisibility::Visible || Current == ESlateVisibility::SelfHitTestInvisible;
}

#undef LOCTEXT_NAMESPACE
