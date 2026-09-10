// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/ArchSkyState.h"

#include "Data/ArchTimeCalendar.h"
#include "Util/ArchSkyLog.h"

const FPrimaryAssetType UArchSkyStatePreset::PrimaryAssetType = FPrimaryAssetType(TEXT("ArchSkyStatePreset"));

// ---------------------------------------------------------------------------------------
// FArchSkyState
// ---------------------------------------------------------------------------------------

FDateTime FArchSkyState::ToLocalDateTime() const
{
	return ArchTimeCalendar::MakeDateTime(Year, DayOfYear, TimeOfDayHours);
}

void FArchSkyState::Sanitise()
{
	// ARCH NOTE: this is a clamp-and-log, never a check(). Every field here can be reached
	// from a UI slider, a console command, a replicated packet or a JSON save file, and
	// asserting on user input would turn a typo into a crash during a client presentation.
	Year = FMath::Clamp(Year, 1900, 2200);
	DayOfYear = FMath::Clamp(DayOfYear, 1, ArchTimeCalendar::DaysInYear(Year));
	TimeOfDayHours = ArchTimeCalendar::WrapHours(TimeOfDayHours);

	// Keep the north offset in [0, 360) so the UI dial and the compass rose agree.
	NorthOffsetDegrees = FMath::Fmod(NorthOffsetDegrees, 360.f);
	if (NorthOffsetDegrees < 0.f)
	{
		NorthOffsetDegrees += 360.f;
	}

	TimeFlowRate = FMath::Clamp(TimeFlowRate, -600.f, 600.f);
	WeatherBlendAlpha = FMath::Clamp(WeatherBlendAlpha, 0.f, 1.f);

	Location.LatitudeDegrees = FMath::Clamp(Location.LatitudeDegrees, -90.0, 90.0);
	Location.LongitudeDegrees = FMath::Clamp(Location.LongitudeDegrees, -180.0, 180.0);
	Location.TimezoneOffsetHours = FMath::Clamp(Location.TimezoneOffsetHours, -12.f, 14.f);
	Location.ElevationMeters = FMath::Clamp(Location.ElevationMeters, -500.f, 9000.f);

	// A blend with no destination is not a blend.
	if (WeatherPresetB.IsNone())
	{
		WeatherBlendAlpha = 0.f;
	}
}

bool FArchSkyState::IsNearlyEqual(const FArchSkyState& Other, float Tolerance) const
{
	return Year == Other.Year
		&& DayOfYear == Other.DayOfYear
		&& bAutoAdvanceDate == Other.bAutoAdvanceDate
		&& WeatherPresetA == Other.WeatherPresetA
		&& WeatherPresetB == Other.WeatherPresetB
		&& Location == Other.Location
		&& FMath::IsNearlyEqual(TimeOfDayHours, Other.TimeOfDayHours, Tolerance)
		&& FMath::IsNearlyEqual(NorthOffsetDegrees, Other.NorthOffsetDegrees, Tolerance)
		&& FMath::IsNearlyEqual(TimeFlowRate, Other.TimeFlowRate, Tolerance)
		&& FMath::IsNearlyEqual(WeatherBlendAlpha, Other.WeatherBlendAlpha, Tolerance);
}

// ---------------------------------------------------------------------------------------
// FArchSkyReplicatedState
// ---------------------------------------------------------------------------------------

FArchSkyReplicatedState FArchSkyReplicatedState::FromSkyState(const FArchSkyState& State, uint8 InDiscontinuityCounter)
{
	FArchSkyReplicatedState Replicated;

	// 24 h * 1000 = 24000, comfortably inside uint16.
	Replicated.QuantisedTimeOfDay = static_cast<uint16>(FMath::Clamp(
		FMath::RoundToInt(ArchTimeCalendar::WrapHours(State.TimeOfDayHours) * TimeQuantisationScale),
		0, 23999));

	Replicated.DayOfYear = static_cast<uint16>(FMath::Clamp(State.DayOfYear, 1, 366));
	Replicated.Year = static_cast<uint16>(FMath::Clamp(State.Year, 1900, 2200));

	Replicated.QuantisedTimeFlowRate = static_cast<int16>(FMath::Clamp(
		FMath::RoundToInt(State.TimeFlowRate * FlowRateQuantisationScale),
		static_cast<int32>(MIN_int16), static_cast<int32>(MAX_int16)));

	Replicated.QuantisedWeatherAlpha = static_cast<uint8>(FMath::Clamp(
		FMath::RoundToInt(State.WeatherBlendAlpha * 255.f), 0, 255));

	Replicated.WeatherPresetA = State.WeatherPresetA;
	Replicated.WeatherPresetB = State.WeatherPresetB;
	Replicated.DiscontinuityCounter = InDiscontinuityCounter;

	return Replicated;
}

void FArchSkyReplicatedState::ApplyToSkyState(FArchSkyState& OutState) const
{
	OutState.TimeOfDayHours = GetTimeOfDayHours();
	OutState.DayOfYear = static_cast<int32>(DayOfYear);
	OutState.Year = static_cast<int32>(Year);
	OutState.TimeFlowRate = GetTimeFlowRate();
	OutState.WeatherBlendAlpha = static_cast<float>(QuantisedWeatherAlpha) / 255.f;
	OutState.WeatherPresetA = WeatherPresetA;
	OutState.WeatherPresetB = WeatherPresetB;

	OutState.Sanitise();
}

float FArchSkyReplicatedState::GetTimeOfDayHours() const
{
	return static_cast<float>(QuantisedTimeOfDay) / TimeQuantisationScale;
}

float FArchSkyReplicatedState::GetTimeFlowRate() const
{
	return static_cast<float>(QuantisedTimeFlowRate) / FlowRateQuantisationScale;
}

bool FArchSkyReplicatedState::operator==(const FArchSkyReplicatedState& Other) const
{
	return QuantisedTimeOfDay == Other.QuantisedTimeOfDay
		&& DayOfYear == Other.DayOfYear
		&& Year == Other.Year
		&& QuantisedTimeFlowRate == Other.QuantisedTimeFlowRate
		&& QuantisedWeatherAlpha == Other.QuantisedWeatherAlpha
		&& WeatherPresetA == Other.WeatherPresetA
		&& WeatherPresetB == Other.WeatherPresetB
		&& DiscontinuityCounter == Other.DiscontinuityCounter;
}

// ---------------------------------------------------------------------------------------
// UArchSkyStatePreset
// ---------------------------------------------------------------------------------------

FPrimaryAssetId UArchSkyStatePreset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}
