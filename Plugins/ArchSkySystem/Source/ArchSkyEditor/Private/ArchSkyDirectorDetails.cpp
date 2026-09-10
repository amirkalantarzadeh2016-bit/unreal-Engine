// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchSkyDirectorDetails.h"

#include "ArchSkySceneValidator.h"
#include "Core/ArchSkyDirector.h"
#include "Core/ArchSkySubsystem.h"
#include "Data/ArchTimeCalendar.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Math/ArchSolarTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ArchSkyDirectorDetails"

TSharedRef<IDetailCustomization> FArchSkyDirectorDetails::MakeInstance()
{
	return MakeShared<FArchSkyDirectorDetails>();
}

UArchSkySubsystem* FArchSkyDirectorDetails::GetSubsystem() const
{
	const AArchSkyDirector* Director = EditedDirector.Get();
	return Director ? Director->GetSkySubsystem() : nullptr;
}

void FArchSkyDirectorDetails::ApplyAndRefresh() const
{
#if WITH_EDITOR
	if (AArchSkyDirector* Director = EditedDirector.Get())
	{
		Director->RefreshEditorPreview();
	}

	// Without this the viewport only repaints on the next mouse move, which makes a
	// dragged slider look like it is lagging half a second behind the pointer.
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(/*bInvalidateHitProxies*/ false);
	}
#endif
}

// ---------------------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------------------

float FArchSkyDirectorDetails::GetTimeOfDay() const
{
	const UArchSkySubsystem* Subsystem = GetSubsystem();
	return Subsystem ? Subsystem->GetSkyState().TimeOfDayHours : 12.f;
}

void FArchSkyDirectorDetails::SetTimeOfDay(float NewValue)
{
	if (UArchSkySubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->SetTimeOfDay(NewValue);
		ApplyAndRefresh();
	}
}

int32 FArchSkyDirectorDetails::GetDayOfYear() const
{
	const UArchSkySubsystem* Subsystem = GetSubsystem();
	return Subsystem ? Subsystem->GetSkyState().DayOfYear : 172;
}

void FArchSkyDirectorDetails::SetDayOfYear(int32 NewValue)
{
	if (UArchSkySubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->SetDayOfYear(NewValue);
		ApplyAndRefresh();
	}
}

float FArchSkyDirectorDetails::GetNorthOffset() const
{
	const UArchSkySubsystem* Subsystem = GetSubsystem();
	return Subsystem ? Subsystem->GetSkyState().NorthOffsetDegrees : 0.f;
}

void FArchSkyDirectorDetails::SetNorthOffset(float NewValue)
{
	if (UArchSkySubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->SetNorthOffset(NewValue);
		ApplyAndRefresh();
	}
}

FText FArchSkyDirectorDetails::GetSummaryText() const
{
	const UArchSkySubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return LOCTEXT("NoSubsystem", "No ArchSky subsystem for this world.");
	}

	const FArchSolarPosition& Sun = Subsystem->GetSolarPosition();
	const FArchSolarDayInfo& DayInfo = Subsystem->GetSolarDayInfo();

	FFormatNamedArguments Args;
	Args.Add(TEXT("Date"), Subsystem->GetFormattedDateString(EArchCalendarType::Gregorian));
	Args.Add(TEXT("Jalali"), Subsystem->GetFormattedDateString(EArchCalendarType::Jalali));
	Args.Add(TEXT("Time"), Subsystem->GetFormattedTimeString(true));
	Args.Add(TEXT("Azimuth"), FText::AsNumber(Sun.AzimuthDegrees, &FNumberFormattingOptions::DefaultWithGrouping()));
	Args.Add(TEXT("Altitude"), FText::AsNumber(Sun.TrueAltitudeDegrees, &FNumberFormattingOptions::DefaultWithGrouping()));
	Args.Add(TEXT("NoonAltitude"), FText::AsNumber(DayInfo.MaxAltitudeDegrees, &FNumberFormattingOptions::DefaultWithGrouping()));

	return FText::Format(LOCTEXT("SunStudySummary",
		"{Date}  ({Jalali})  at  {Time}\nSun azimuth {Azimuth}°, altitude {Altitude}°   |   Solar noon altitude {NoonAltitude}°"),
		Args);
}

FText FArchSkyDirectorDetails::GetSunTimesText() const
{
	const UArchSkySubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return FText::GetEmpty();
	}

	const FArchSolarDayInfo& DayInfo = Subsystem->GetSolarDayInfo();

	if (DayInfo.bPolarDay)
	{
		return LOCTEXT("PolarDaySummary", "Midnight sun - the sun does not set on this date.");
	}
	if (DayInfo.bPolarNight)
	{
		return LOCTEXT("PolarNightSummary", "Polar night - the sun does not rise on this date.");
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("Sunrise"), ArchTimeCalendar::FormatTimeOfDay(DayInfo.SunriseHours, true));
	Args.Add(TEXT("Noon"), ArchTimeCalendar::FormatTimeOfDay(DayInfo.SolarNoonHours, true));
	Args.Add(TEXT("Sunset"), ArchTimeCalendar::FormatTimeOfDay(DayInfo.SunsetHours, true));
	Args.Add(TEXT("Length"), ArchTimeCalendar::FormatDuration(DayInfo.DayLengthHours));

	return FText::Format(LOCTEXT("SunTimesSummary",
		"Sunrise {Sunrise}   Solar noon {Noon}   Sunset {Sunset}   Day length {Length}"), Args);
}

// ---------------------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------------------

FReply FArchSkyDirectorDetails::OnJumpToSunrise()
{
	if (UArchSkySubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->JumpToSunrise();
		ApplyAndRefresh();
	}
	return FReply::Handled();
}

FReply FArchSkyDirectorDetails::OnJumpToSolarNoon()
{
	if (UArchSkySubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->JumpToSolarNoon();
		ApplyAndRefresh();
	}
	return FReply::Handled();
}

FReply FArchSkyDirectorDetails::OnJumpToSunset()
{
	if (UArchSkySubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->JumpToSunset();
		ApplyAndRefresh();
	}
	return FReply::Handled();
}

FReply FArchSkyDirectorDetails::OnSetSolstice(int32 PresetIndex)
{
	if (UArchSkySubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->SetSolsticePreset(static_cast<EArchSolsticePreset>(PresetIndex));
		ApplyAndRefresh();
	}
	return FReply::Handled();
}

FReply FArchSkyDirectorDetails::OnValidateScene()
{
	if (const AArchSkyDirector* Director = EditedDirector.Get())
	{
		UArchSkySceneValidator::ValidateAndReport(Director->GetWorld());
	}
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------

void FArchSkyDirectorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjects;
	DetailBuilder.GetObjectsBeingCustomized(SelectedObjects);

	// A multi-selection has no single subsystem to scrub, and driving several Directors
	// from one slider would be meaningless: they all share one subsystem per world anyway.
	if (SelectedObjects.Num() != 1)
	{
		return;
	}

	EditedDirector = Cast<AArchSkyDirector>(SelectedObjects[0].Get());
	if (!EditedDirector.IsValid())
	{
		return;
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("Sun Study"),
		LOCTEXT("SunStudyCategory", "Sun Study"),
		ECategoryPriority::Important);

	// --- Live summary ---
	Category.AddCustomRow(LOCTEXT("SummaryRow", "Summary"))
		.WholeRowContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 4.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return GetSummaryText(); })
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 2.f, 0.f, 6.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return GetSunTimesText(); })
				.AutoWrapText(true)
			]
		];

	// --- Time of day ---
	Category.AddCustomRow(LOCTEXT("TimeOfDayRow", "Time of Day"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TimeOfDayLabel", "Time of Day"))
			.ToolTipText(LOCTEXT("TimeOfDayTooltip",
				"Local wall-clock time in decimal hours. Drag to scrub the sun; the viewport updates live without entering Play."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this]() { return GetTimeOfDay() / 24.f; })
				.OnValueChanged_Lambda([this](float NewValue) { SetTimeOfDay(NewValue * 24.f); })
				.ToolTipText(LOCTEXT("TimeSliderTooltip", "0 = midnight, 12 = noon, 24 = midnight."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SNumericEntryBox<float>)
				.AllowSpin(true)
				.MinValue(0.f)
				.MaxValue(24.f)
				.MinSliderValue(0.f)
				.MaxSliderValue(24.f)
				.Value_Lambda([this]() { return TOptional<float>(GetTimeOfDay()); })
				.OnValueChanged_Lambda([this](float NewValue) { SetTimeOfDay(NewValue); })
				.MinDesiredValueWidth(60.f)
			]
		];

	// --- Day of year ---
	Category.AddCustomRow(LOCTEXT("DayOfYearRow", "Day of Year"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DayOfYearLabel", "Day of Year"))
			.ToolTipText(LOCTEXT("DayOfYearTooltip",
				"1 to 365 or 366. Drag to move through the seasons and watch the sun's arc rise and fall."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this]() { return static_cast<float>(GetDayOfYear() - 1) / 365.f; })
				.OnValueChanged_Lambda([this](float NewValue)
				{
					SetDayOfYear(1 + FMath::RoundToInt32(NewValue * 365.f));
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SNumericEntryBox<int32>)
				.AllowSpin(true)
				.MinValue(1)
				.MaxValue(366)
				.MinSliderValue(1)
				.MaxSliderValue(366)
				.Value_Lambda([this]() { return TOptional<int32>(GetDayOfYear()); })
				.OnValueChanged_Lambda([this](int32 NewValue) { SetDayOfYear(NewValue); })
				.MinDesiredValueWidth(60.f)
			]
		];

	// --- North offset ---
	Category.AddCustomRow(LOCTEXT("NorthOffsetRow", "Plan North Offset"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NorthOffsetLabel", "Plan North Offset"))
			.ToolTipText(LOCTEXT("NorthOffsetTooltip",
				"Scene yaw, in degrees, at which TRUE north lies. 0 means the plan is modelled with north along +X.\n"
				"The red arrow in the viewport is true north; the blue one is the plan's north. Shadow studies are "
				"wrong until these match the site plan."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this]() { return GetNorthOffset() / 360.f; })
				.OnValueChanged_Lambda([this](float NewValue) { SetNorthOffset(NewValue * 360.f); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SNumericEntryBox<float>)
				.AllowSpin(true)
				.MinValue(0.f)
				.MaxValue(360.f)
				.MinSliderValue(0.f)
				.MaxSliderValue(360.f)
				.Value_Lambda([this]() { return TOptional<float>(GetNorthOffset()); })
				.OnValueChanged_Lambda([this](float NewValue) { SetNorthOffset(NewValue); })
				.MinDesiredValueWidth(60.f)
			]
		];

	// --- Jump buttons ---
	Category.AddCustomRow(LOCTEXT("JumpRow", "Jump To"))
		.WholeRowContent()
		[
			SNew(SUniformGridPanel)
			.SlotPadding(FMargin(2.f))
			+ SUniformGridPanel::Slot(0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("JumpSunrise", "Sunrise"))
				.ToolTipText(LOCTEXT("JumpSunriseTip", "Jump to the instant the sun's upper limb clears the horizon."))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { return OnJumpToSunrise(); })
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("JumpNoon", "Solar Noon"))
				.ToolTipText(LOCTEXT("JumpNoonTip", "Jump to the sun's meridian crossing - its highest point of the day."))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { return OnJumpToSolarNoon(); })
			]
			+ SUniformGridPanel::Slot(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("JumpSunset", "Sunset"))
				.ToolTipText(LOCTEXT("JumpSunsetTip", "Jump to sunset."))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { return OnJumpToSunset(); })
			]
		];

	// --- Solstice / equinox buttons, labelled the way architects say them ---
	Category.AddCustomRow(LOCTEXT("SolsticeRow", "Key Dates"))
		.WholeRowContent()
		[
			SNew(SUniformGridPanel)
			.SlotPadding(FMargin(2.f))
			+ SUniformGridPanel::Slot(0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("SpringEquinoxButton", "Spring Equinox — Mar 20"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { return OnSetSolstice(static_cast<int32>(EArchSolsticePreset::SpringEquinox)); })
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("SummerSolsticeButton", "Summer Solstice — Jun 21"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { return OnSetSolstice(static_cast<int32>(EArchSolsticePreset::SummerSolstice)); })
			]
			+ SUniformGridPanel::Slot(0, 1)
			[
				SNew(SButton)
				.Text(LOCTEXT("AutumnEquinoxButton", "Autumn Equinox — Sep 22"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { return OnSetSolstice(static_cast<int32>(EArchSolsticePreset::AutumnEquinox)); })
			]
			+ SUniformGridPanel::Slot(1, 1)
			[
				SNew(SButton)
				.Text(LOCTEXT("WinterSolsticeButton", "Winter Solstice — Dec 21"))
				.ToolTipText(LOCTEXT("WinterSolsticeTip",
					"The worst-case overshadowing date in the northern hemisphere, and the one most planning "
					"authorities require a study for."))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { return OnSetSolstice(static_cast<int32>(EArchSolsticePreset::WinterSolstice)); })
			]
		];

	// --- Validation ---
	Category.AddCustomRow(LOCTEXT("ValidateRow", "Validate"))
		.WholeRowContent()
		[
			SNew(SButton)
			.Text(LOCTEXT("ValidateSceneButton", "Validate Scene Setup"))
			.ToolTipText(LOCTEXT("ValidateSceneTip",
				"Checks this level for duplicate lights, non-movable lights, a missing atmosphere, a wrong "
				"AtmosphereSunLightIndex and other setup mistakes. Results appear in the ArchSky message log."))
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([this]() { return OnValidateScene(); })
		];
}

#undef LOCTEXT_NAMESPACE
