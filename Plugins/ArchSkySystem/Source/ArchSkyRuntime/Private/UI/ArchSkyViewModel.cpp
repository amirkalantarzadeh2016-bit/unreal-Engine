// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ArchSkyViewModel.h"

#include "Core/ArchSkyDirector.h"
#include "Core/ArchSkySubsystem.h"
#include "Data/ArchSkySettings.h"
#include "Data/ArchTimeCalendar.h"
#include "Data/ArchWeatherPreset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchSkyViewModel"

namespace ArchViewModelDetail
{
	/** Formats an angle with one decimal place and a degree suffix. */
	FText FormatDegrees(double Degrees, int32 FractionDigits = 1)
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = FractionDigits;
		Options.MaximumFractionalDigits = FractionDigits;

		return FText::Format(LOCTEXT("DegreesFormat", "{0}°"), FText::AsNumber(Degrees, &Options));
	}

	/** Formats a UTC offset as "UTC+03:30". */
	FText FormatTimezone(float OffsetHours)
	{
		const bool bNegative = OffsetHours < 0.f;
		const float Absolute = FMath::Abs(OffsetHours);

		const int32 Hours = FMath::FloorToInt32(Absolute);
		const int32 Minutes = FMath::RoundToInt32((Absolute - static_cast<float>(Hours)) * 60.f);

		FNumberFormattingOptions TwoDigits;
		TwoDigits.MinimumIntegralDigits = 2;
		TwoDigits.UseGrouping = false;

		FFormatNamedArguments Args;
		Args.Add(TEXT("Sign"), bNegative ? LOCTEXT("TimezoneMinus", "-") : LOCTEXT("TimezonePlus", "+"));
		Args.Add(TEXT("Hours"), FText::AsNumber(Hours, &TwoDigits));
		Args.Add(TEXT("Minutes"), FText::AsNumber(Minutes, &TwoDigits));

		return FText::Format(LOCTEXT("TimezoneFormat", "UTC{Sign}{Hours}:{Minutes}"), Args);
	}

	/** Formats a signed latitude/longitude pair with hemisphere letters. */
	FText FormatCoordinates(double Latitude, double Longitude)
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = 3;
		Options.MaximumFractionalDigits = 3;

		FFormatNamedArguments Args;
		Args.Add(TEXT("Lat"), FText::AsNumber(FMath::Abs(Latitude), &Options));
		Args.Add(TEXT("LatHemisphere"), (Latitude >= 0.0) ? LOCTEXT("North", "N") : LOCTEXT("South", "S"));
		Args.Add(TEXT("Lon"), FText::AsNumber(FMath::Abs(Longitude), &Options));
		Args.Add(TEXT("LonHemisphere"), (Longitude >= 0.0) ? LOCTEXT("East", "E") : LOCTEXT("West", "W"));

		return FText::Format(
			LOCTEXT("CoordinatesFormat", "{Lat}° {LatHemisphere}, {Lon}° {LonHemisphere}"), Args);
	}

	/** Formats a 0..1 fraction as a whole-number percentage. */
	FText FormatPercent(double Fraction01)
	{
		FNumberFormattingOptions Options;
		Options.MaximumFractionalDigits = 0;
		return FText::AsPercent(Fraction01, &Options);
	}
}

// ---------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------

void UArchSkyViewModel::Initialise(const UObject* WorldContextObject)
{
	// Rebinding is legal; make sure we never end up double-bound.
	Shutdown();

	UArchSkySubsystem* Subsystem = UArchSkySubsystem::Get(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogArchSky, Warning,
			TEXT("UArchSkyViewModel::Initialise found no ArchSky subsystem; the panel will show stale defaults."));
		return;
	}

	SkySubsystem = Subsystem;

	Subsystem->OnSkyStateChanged.AddDynamic(this, &UArchSkyViewModel::HandleSkyStateChanged);
	Subsystem->OnTimePhaseChanged.AddDynamic(this, &UArchSkyViewModel::HandleTimePhaseChanged);
	Subsystem->OnWeatherTransitionStarted.AddDynamic(this, &UArchSkyViewModel::HandleWeatherTransition);
	Subsystem->OnWeatherTransitionCompleted.AddDynamic(this, &UArchSkyViewModel::HandleWeatherTransition);

	if (const UArchSkySettings* Settings = UArchSkySettings::Get())
	{
		CalendarType = Settings->DefaultCalendarType;
		bUse24HourClock = Settings->bDefaultTo24HourClock;
	}

	RefreshAllFields();
}

void UArchSkyViewModel::Shutdown()
{
	if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
	{
		Subsystem->OnSkyStateChanged.RemoveDynamic(this, &UArchSkyViewModel::HandleSkyStateChanged);
		Subsystem->OnTimePhaseChanged.RemoveDynamic(this, &UArchSkyViewModel::HandleTimePhaseChanged);
		Subsystem->OnWeatherTransitionStarted.RemoveDynamic(this, &UArchSkyViewModel::HandleWeatherTransition);
		Subsystem->OnWeatherTransitionCompleted.RemoveDynamic(this, &UArchSkyViewModel::HandleWeatherTransition);
	}

	SkySubsystem.Reset();
	CachedDirector.Reset();
}

void UArchSkyViewModel::HandleSkyStateChanged(const FArchSkyState& NewState)
{
	RefreshAllFields();
}

void UArchSkyViewModel::HandleTimePhaseChanged(EArchTimePhase OldPhase, EArchTimePhase NewPhase)
{
	// The phase name is refreshed by the state change that caused it, so there is nothing
	// extra to do here. The binding exists so a widget can also react to the phase change
	// on its own (a flash, a sound) without subscribing to the subsystem directly.
}

void UArchSkyViewModel::HandleWeatherTransition(FName FromPresetId, FName ToPresetId)
{
	RefreshAllFields();
}

// ---------------------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------------------

void UArchSkyViewModel::RefreshAllFields()
{
	UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	if (!Subsystem || bRefreshing)
	{
		return;
	}

	// A command called from inside an OnViewModelUpdated handler would otherwise recurse.
	TGuardValue<bool> RefreshGuard(bRefreshing, true);

	using namespace ArchViewModelDetail;

	const FArchSkyState& State = Subsystem->GetSkyState();
	const FArchSolarPosition& Sun = Subsystem->GetSolarPosition();
	const FArchLunarPosition& Moon = Subsystem->GetLunarPosition();
	const FArchSolarDayInfo& DayInfo = Subsystem->GetSolarDayInfo();

	// --- Time ---
	TimeOfDayHours = State.TimeOfDayHours;
	DayOfYear = State.DayOfYear;
	TimeFlowRate = State.TimeFlowRate;
	bIsTimePaused = Subsystem->IsTimePaused();

	TimeText = Subsystem->GetFormattedTimeString(bUse24HourClock);
	DateText = Subsystem->GetFormattedDateString(CalendarType);

	// The secondary readout always shows the other calendar, which is how an Iranian
	// architect cross-checks a date against a Gregorian client brief at a glance.
	SecondaryDateText = Subsystem->GetFormattedDateString(
		(CalendarType == EArchCalendarType::Gregorian) ? EArchCalendarType::Jalali : EArchCalendarType::Gregorian);

	SeasonText = ArchTimeCalendar::GetSeasonDisplayName(Subsystem->GetSeason());
	TimePhaseText = ArchTimeCalendar::GetTimePhaseDisplayName(Subsystem->GetTimePhase());

	// --- Solar day ---
	if (DayInfo.bPolarDay)
	{
		SunriseText = LOCTEXT("PolarDayNoSunrise", "Midnight sun");
		SunsetText = LOCTEXT("PolarDayNoSunset", "Midnight sun");
		DayLengthText = ArchTimeCalendar::FormatDuration(24.f);
	}
	else if (DayInfo.bPolarNight)
	{
		SunriseText = LOCTEXT("PolarNightNoSunrise", "Polar night");
		SunsetText = LOCTEXT("PolarNightNoSunset", "Polar night");
		DayLengthText = ArchTimeCalendar::FormatDuration(0.f);
	}
	else
	{
		SunriseText = ArchTimeCalendar::FormatTimeOfDay(DayInfo.SunriseHours, bUse24HourClock);
		SunsetText = ArchTimeCalendar::FormatTimeOfDay(DayInfo.SunsetHours, bUse24HourClock);
		DayLengthText = ArchTimeCalendar::FormatDuration(DayInfo.DayLengthHours);
	}

	SolarNoonText = ArchTimeCalendar::FormatTimeOfDay(DayInfo.SolarNoonHours, bUse24HourClock);

	// --- Analysis ---
	SunAzimuthText = FormatDegrees(Sun.AzimuthDegrees);
	SunAltitudeText = FormatDegrees(Sun.TrueAltitudeDegrees);
	SolarNoonAltitudeText = FormatDegrees(DayInfo.MaxAltitudeDegrees);

	if (Sun.TrueAltitudeDegrees <= 0.0)
	{
		// cot() of a negative altitude is meaningless; say so rather than printing "100.0x".
		ShadowLengthText = LOCTEXT("ShadowBelowHorizon", "Sun below horizon");
	}
	else
	{
		FNumberFormattingOptions ShadowOptions;
		ShadowOptions.MinimumFractionalDigits = 2;
		ShadowOptions.MaximumFractionalDigits = 2;

		ShadowLengthText = FText::Format(
			LOCTEXT("ShadowLengthFormat", "{0} × height"),
			FText::AsNumber(Subsystem->GetShadowLengthMultiplier(), &ShadowOptions));
	}

	MoonPhaseText = ArchMoonMath::GetMoonPhaseDisplayName(Moon.Phase);
	MoonIllumination01 = static_cast<float>(Moon.IlluminatedFraction);
	MoonIlluminationText = FormatPercent(Moon.IlluminatedFraction);
	MoonIconRotationDegrees = static_cast<float>(Moon.BrightLimbAngleDegrees);

	// --- Location ---
	CoordinatesText = FormatCoordinates(State.Location.LatitudeDegrees, State.Location.LongitudeDegrees);
	TimezoneText = FormatTimezone(State.Location.TimezoneOffsetHours);

	// Match against the library so the dropdown shows a city name rather than raw numbers.
	LocationText = LOCTEXT("CustomLocation", "Custom location");
	for (const FArchLocationEntry& Entry : Subsystem->GetAvailableLocations())
	{
		if (FMath::IsNearlyEqual(Entry.Location.LatitudeDegrees, State.Location.LatitudeDegrees, 1.e-3)
			&& FMath::IsNearlyEqual(Entry.Location.LongitudeDegrees, State.Location.LongitudeDegrees, 1.e-3))
		{
			LocationText = Entry.DisplayName;
			break;
		}
	}

	NorthOffsetDegrees = State.NorthOffsetDegrees;
	if (FMath::IsNearlyZero(NorthOffsetDegrees, 0.05f))
	{
		NorthOffsetText = LOCTEXT("NorthAligned", "Plan north is true north");
	}
	else
	{
		NorthOffsetText = FText::Format(
			LOCTEXT("NorthOffsetFormat", "True north is {0} clockwise of plan north"),
			FormatDegrees(NorthOffsetDegrees));
	}

	// --- Weather ---
	const FName ActiveWeatherId = State.WeatherPresetB.IsNone() ? State.WeatherPresetA : State.WeatherPresetB;
	WeatherText = Subsystem->GetWeatherPresetDisplayName(ActiveWeatherId);

	WeatherTransitionProgress = Subsystem->IsWeatherTransitionActive() ? State.WeatherBlendAlpha : 1.f;

	OnViewModelUpdated.Broadcast();
}

// ---------------------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------------------

AArchSkyDirector* UArchSkyViewModel::FindDirector() const
{
	if (AArchSkyDirector* Cached = CachedDirector.Get())
	{
		return Cached;
	}

	const UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	UWorld* World = Subsystem ? Subsystem->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	// ARCH NOTE: one iteration, cached in a weak pointer, and only ever reached on the
	// networked path - a single-player build never calls this. It is not on any hot path.
	for (TActorIterator<AArchSkyDirector> It(World); It; ++It)
	{
		const_cast<UArchSkyViewModel*>(this)->CachedDirector = *It;
		return *It;
	}

	return nullptr;
}

bool UArchSkyViewModel::ShouldApplyLocally() const
{
	const UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	if (!Subsystem)
	{
		return false;
	}

	// Not a client, or replication is off entirely: apply directly.
	if (!Subsystem->IsClientDriven())
	{
		return true;
	}

	const UArchSkySettings* Settings = UArchSkySettings::Get();
	if (!Settings || !Settings->bAllowClientTimeControl)
	{
		// The client is a spectator of the presentation. The subsystem would refuse the
		// write anyway; refusing here keeps the warning to one line instead of one per field.
		return false;
	}

	// Client with control rights: apply locally as prediction AND send the RPC, so the
	// architect's own slider stays responsive while the server distributes the change.
	return true;
}

void UArchSkyViewModel::CommandSetTimeOfDay(float Hours)
{
	if (AArchSkyDirector* Director = SkySubsystem.IsValid() && SkySubsystem->IsClientDriven() ? FindDirector() : nullptr)
	{
		Director->Server_RequestSetTimeOfDay(Hours);
	}

	if (ShouldApplyLocally())
	{
		if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
		{
			Subsystem->SetTimeOfDay(Hours);
		}
	}
}

void UArchSkyViewModel::CommandSetDayOfYear(int32 Day)
{
	if (AArchSkyDirector* Director = SkySubsystem.IsValid() && SkySubsystem->IsClientDriven() ? FindDirector() : nullptr)
	{
		Director->Server_RequestSetDayOfYear(Day);
	}

	if (ShouldApplyLocally())
	{
		if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
		{
			Subsystem->SetDayOfYear(Day);
		}
	}
}

void UArchSkyViewModel::CommandSetDate(int32 Year, int32 Month, int32 Day)
{
	UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	if (!Subsystem || !ShouldApplyLocally())
	{
		return;
	}

	// The picker hands us numbers in whichever calendar it is showing.
	if (CalendarType == EArchCalendarType::Jalali)
	{
		Subsystem->SetDateFromJalali(Year, Month, Day);
	}
	else
	{
		Subsystem->SetDateFromGregorian(Year, Month, Day);
	}
}

void UArchSkyViewModel::CommandToggleTimePause()
{
	UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	if (!Subsystem)
	{
		return;
	}

	const float RequestedRate = Subsystem->IsTimePaused() ? 1.f : 0.f;

	if (SkySubsystem->IsClientDriven())
	{
		if (AArchSkyDirector* Director = FindDirector())
		{
			Director->Server_RequestSetTimeFlowRate(RequestedRate);
		}
	}

	if (ShouldApplyLocally())
	{
		Subsystem->ToggleTimePause();
	}
}

void UArchSkyViewModel::CommandSetTimeFlowRate(float HoursPerSecond)
{
	if (SkySubsystem.IsValid() && SkySubsystem->IsClientDriven())
	{
		if (AArchSkyDirector* Director = FindDirector())
		{
			Director->Server_RequestSetTimeFlowRate(HoursPerSecond);
		}
	}

	if (ShouldApplyLocally())
	{
		if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
		{
			Subsystem->SetTimeFlowRate(HoursPerSecond);
		}
	}
}

void UArchSkyViewModel::CommandSetLocationPreset(FName CityId)
{
	if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
	{
		if (ShouldApplyLocally())
		{
			Subsystem->SetLocationPreset(CityId);
		}
	}
}

void UArchSkyViewModel::CommandSetManualLocation(float Latitude, float Longitude, float TimezoneHours, float ElevationMeters)
{
	UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	if (!Subsystem || !ShouldApplyLocally())
	{
		return;
	}

	FArchGeoLocation NewLocation = Subsystem->GetSkyState().Location;
	NewLocation.LatitudeDegrees = Latitude;
	NewLocation.LongitudeDegrees = Longitude;
	NewLocation.TimezoneOffsetHours = TimezoneHours;
	NewLocation.ElevationMeters = ElevationMeters;

	// SetLocation clamps, so a text box containing "999" produces a legal latitude and a
	// warning rather than an assert.
	Subsystem->SetLocation(NewLocation);
}

void UArchSkyViewModel::CommandSetNorthOffset(float Degrees)
{
	if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
	{
		if (ShouldApplyLocally())
		{
			Subsystem->SetNorthOffset(Degrees);
		}
	}
}

void UArchSkyViewModel::CommandSetWeather(FName PresetId, float TransitionSeconds)
{
	if (SkySubsystem.IsValid() && SkySubsystem->IsClientDriven())
	{
		if (AArchSkyDirector* Director = FindDirector())
		{
			Director->Server_RequestSetWeather(PresetId, TransitionSeconds);
		}
	}

	if (ShouldApplyLocally())
	{
		if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
		{
			Subsystem->SetWeatherPreset(PresetId, TransitionSeconds);
		}
	}
}

void UArchSkyViewModel::CommandJumpToSunrise()
{
	if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
	{
		CommandSetTimeOfDay(Subsystem->GetSolarDayInfo().SunriseHours);
	}
}

void UArchSkyViewModel::CommandJumpToSolarNoon()
{
	if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
	{
		CommandSetTimeOfDay(Subsystem->GetSolarDayInfo().SolarNoonHours);
	}
}

void UArchSkyViewModel::CommandJumpToSunset()
{
	if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
	{
		CommandSetTimeOfDay(Subsystem->GetSolarDayInfo().SunsetHours);
	}
}

void UArchSkyViewModel::CommandJumpToGoldenHour(bool bEvening)
{
	if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
	{
		if (ShouldApplyLocally())
		{
			Subsystem->JumpToGoldenHour(bEvening);
		}
	}
}

void UArchSkyViewModel::CommandJumpToBlueHour(bool bEvening)
{
	if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
	{
		if (ShouldApplyLocally())
		{
			Subsystem->JumpToBlueHour(bEvening);
		}
	}
}

void UArchSkyViewModel::CommandSetSolstice(EArchSolsticePreset Preset)
{
	if (UArchSkySubsystem* Subsystem = SkySubsystem.Get())
	{
		if (ShouldApplyLocally())
		{
			Subsystem->SetSolsticePreset(Preset);
		}
	}
}

void UArchSkyViewModel::CommandSetCalendarType(EArchCalendarType NewCalendarType)
{
	if (CalendarType == NewCalendarType)
	{
		return;
	}

	CalendarType = NewCalendarType;
	RefreshAllFields();
}

void UArchSkyViewModel::CommandSetUse24HourClock(bool bIn24Hour)
{
	if (bUse24HourClock == bIn24Hour)
	{
		return;
	}

	bUse24HourClock = bIn24Hour;
	RefreshAllFields();
}

bool UArchSkyViewModel::CommandSavePreset(const FString& PresetName, const FString& Notes)
{
	// ARCH NOTE: the world context is taken from the subsystem, not from `this`. A
	// ViewModel is a plain UObject whose outer may be the transient package (a widget can
	// legitimately construct one with NewObject<>(GetTransientPackage())), in which case
	// GetWorldFromContextObject would return null and the save would silently fail.
	const UObject* WorldContext = SkySubsystem.IsValid() ? Cast<UObject>(SkySubsystem->GetWorld()) : nullptr;

	const bool bSaved = UArchSkyPresetLibrary::SaveCurrentStateAsPreset(WorldContext, PresetName, Notes, PresetSlotName);
	if (bSaved)
	{
		OnPresetListChanged.Broadcast();
	}
	return bSaved;
}

bool UArchSkyViewModel::CommandLoadPreset(const FString& PresetName)
{
	if (!ShouldApplyLocally())
	{
		return false;
	}

	const UObject* WorldContext = SkySubsystem.IsValid() ? Cast<UObject>(SkySubsystem->GetWorld()) : nullptr;
	return UArchSkyPresetLibrary::LoadPresetByName(WorldContext, PresetName, PresetSlotName);
}

bool UArchSkyViewModel::CommandDeletePreset(const FString& PresetName)
{
	const bool bDeleted = UArchSkyPresetLibrary::DeletePreset(PresetName, PresetSlotName);
	if (bDeleted)
	{
		OnPresetListChanged.Broadcast();
	}
	return bDeleted;
}

// ---------------------------------------------------------------------------------------
// List providers
// ---------------------------------------------------------------------------------------

TArray<FArchWeatherTileInfo> UArchSkyViewModel::GetWeatherTiles() const
{
	TArray<FArchWeatherTileInfo> Tiles;

	const UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	if (!Subsystem)
	{
		return Tiles;
	}

	const FArchSkyState& State = Subsystem->GetSkyState();
	const FName ActiveId = State.WeatherPresetB.IsNone() ? State.WeatherPresetA : State.WeatherPresetB;

	const TArray<FName> Ids = Subsystem->GetAvailableWeatherPresetIds();
	Tiles.Reserve(Ids.Num());

	for (const FName& Id : Ids)
	{
		FArchWeatherTileInfo Tile;
		Tile.PresetId = Id;
		Tile.DisplayName = Subsystem->GetWeatherPresetDisplayName(Id);
		Tile.bIsActive = (Id == ActiveId);
		Tiles.Add(MoveTemp(Tile));
	}

	return Tiles;
}

TArray<FArchLocationEntry> UArchSkyViewModel::GetLocationOptions(const FString& SearchText) const
{
	const UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	if (!Subsystem)
	{
		return UArchLocationLibrary::SearchBuiltInLocations(SearchText);
	}

	TArray<FArchLocationEntry> All = Subsystem->GetAvailableLocations();
	if (SearchText.IsEmpty())
	{
		return All;
	}

	// Search the LOCALISED names, so a Persian build searches Persian text.
	All.RemoveAll([&SearchText](const FArchLocationEntry& Entry)
	{
		return !Entry.DisplayName.ToString().Contains(SearchText, ESearchCase::IgnoreCase)
			&& !Entry.CountryName.ToString().Contains(SearchText, ESearchCase::IgnoreCase)
			&& !Entry.CityId.ToString().Contains(SearchText, ESearchCase::IgnoreCase);
	});

	return All;
}

TArray<FArchSkyNamedPreset> UArchSkyViewModel::GetSavedPresets() const
{
	return UArchSkyPresetLibrary::GetSavedPresets(PresetSlotName);
}

TArray<FText> UArchSkyViewModel::GetMonthNames() const
{
	TArray<FText> Names;
	Names.Reserve(12);

	for (int32 Month = 1; Month <= 12; ++Month)
	{
		Names.Add(CalendarType == EArchCalendarType::Jalali
			? ArchJalaliCalendar::GetJalaliMonthName(Month)
			: ArchTimeCalendar::GetGregorianMonthName(Month));
	}

	return Names;
}

void UArchSkyViewModel::GetCurrentDateParts(int32& OutYear, int32& OutMonth, int32& OutDay) const
{
	OutYear = 2026;
	OutMonth = 1;
	OutDay = 1;

	const UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	if (!Subsystem)
	{
		return;
	}

	if (CalendarType == EArchCalendarType::Jalali)
	{
		const FArchJalaliDate JalaliDate = Subsystem->GetJalaliDate();
		OutYear = JalaliDate.Year;
		OutMonth = JalaliDate.Month;
		OutDay = JalaliDate.Day;
		return;
	}

	const FArchSkyState& State = Subsystem->GetSkyState();
	OutYear = State.Year;
	ArchTimeCalendar::DayOfYearToMonthDay(State.Year, State.DayOfYear, OutMonth, OutDay);
}

void UArchSkyViewModel::GetTimeSliderTicks(float& OutSunrise01, float& OutSolarNoon01, float& OutSunset01, bool& bOutValid) const
{
	OutSunrise01 = 0.f;
	OutSolarNoon01 = 0.5f;
	OutSunset01 = 1.f;
	bOutValid = false;

	const UArchSkySubsystem* Subsystem = SkySubsystem.Get();
	if (!Subsystem)
	{
		return;
	}

	const FArchSolarDayInfo& DayInfo = Subsystem->GetSolarDayInfo();

	// Solar noon exists even on a polar day, but sunrise and sunset do not, so the caller
	// is told explicitly rather than being handed two ticks that mean nothing.
	OutSolarNoon01 = FMath::Clamp(DayInfo.SolarNoonHours / 24.f, 0.f, 1.f);

	if (DayInfo.bPolarDay || DayInfo.bPolarNight)
	{
		return;
	}

	OutSunrise01 = FMath::Clamp(DayInfo.SunriseHours / 24.f, 0.f, 1.f);
	OutSunset01 = FMath::Clamp(DayInfo.SunsetHours / 24.f, 0.f, 1.f);
	bOutValid = true;
}

#undef LOCTEXT_NAMESPACE
