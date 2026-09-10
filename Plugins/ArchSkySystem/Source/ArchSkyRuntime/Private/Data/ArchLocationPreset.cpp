// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/ArchLocationPreset.h"

#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchLocations"

const FPrimaryAssetType UArchLocationPreset::PrimaryAssetType = FPrimaryAssetType(TEXT("ArchLocationPreset"));

const FArchLocationEntry* UArchLocationPreset::FindLocation(FName CityId) const
{
	return Locations.FindByPredicate(
		[CityId](const FArchLocationEntry& Entry) { return Entry.CityId == CityId; });
}

FPrimaryAssetId UArchLocationPreset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR

EDataValidationResult UArchLocationPreset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FName> SeenIds;
	for (const FArchLocationEntry& Entry : Locations)
	{
		if (Entry.CityId.IsNone())
		{
			Context.AddError(LOCTEXT("LocationMissingId", "A location entry has an empty CityId."));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		bool bAlreadyPresent = false;
		SeenIds.Add(Entry.CityId, &bAlreadyPresent);
		if (bAlreadyPresent)
		{
			Context.AddError(FText::Format(
				LOCTEXT("LocationDuplicateId", "Duplicate CityId '{0}'. Lookups will return whichever entry comes first."),
				FText::FromName(Entry.CityId)));
			Result = EDataValidationResult::Invalid;
		}

		if (FMath::Abs(Entry.Location.LatitudeDegrees) > 90.0)
		{
			Context.AddError(FText::Format(
				LOCTEXT("LocationBadLatitude", "City '{0}' has a latitude outside [-90, 90]."),
				FText::FromName(Entry.CityId)));
			Result = EDataValidationResult::Invalid;
		}

		// A timezone more than an hour away from the longitude's natural offset is usually
		// a typo, but it is legal (China, Spain, India), so warn rather than fail.
		const double NaturalOffset = Entry.Location.LongitudeDegrees / 15.0;
		if (FMath::Abs(NaturalOffset - static_cast<double>(Entry.Location.TimezoneOffsetHours)) > 2.0)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("LocationTimezoneSuspect",
					"City '{0}' has a timezone more than two hours from its longitude. Check for a sign error."),
				FText::FromName(Entry.CityId)));
		}
	}

	return Result;
}

#endif // WITH_EDITOR

// ---------------------------------------------------------------------------------------
// Built-in city library
// ---------------------------------------------------------------------------------------

namespace ArchBuiltInLocations
{
	/**
	 * Coordinates are city-centre positions; elevations are the commonly quoted airport or
	 * city-centre figures. Iranian cities lead the list because that is this plugin's
	 * primary audience, followed by regional and global reference cities used for
	 * comparison studies.
	 *
	 * Iran has not observed DST since 2022, so every Iranian entry leaves bObserveDST off.
	 */
	static TArray<FArchLocationEntry> BuildLibrary()
	{
		TArray<FArchLocationEntry> Library;

		auto Add = [&Library](const TCHAR* CityId, FText Display, FText Country,
			double Latitude, double Longitude, float TimezoneHours, float ElevationMeters,
			EArchClimateHint Climate, const TCHAR* DefaultWeather, bool bDst = false)
		{
			FArchLocationEntry Entry;
			Entry.CityId = FName(CityId);
			Entry.DisplayName = MoveTemp(Display);
			Entry.CountryName = MoveTemp(Country);
			Entry.Location = FArchGeoLocation(Latitude, Longitude, TimezoneHours, ElevationMeters);
			Entry.Location.bObserveDST = bDst;
			Entry.ClimateHint = Climate;
			Entry.DefaultWeatherPresetId = FName(DefaultWeather);
			Library.Add(MoveTemp(Entry));
		};

		const FText Iran = LOCTEXT("Country_Iran", "Iran");
		const FText Uae = LOCTEXT("Country_Uae", "United Arab Emirates");
		const FText Turkiye = LOCTEXT("Country_Turkiye", "Turkiye");
		const FText UnitedKingdom = LOCTEXT("Country_Uk", "United Kingdom");
		const FText UnitedStates = LOCTEXT("Country_Usa", "United States");
		const FText Japan = LOCTEXT("Country_Japan", "Japan");
		const FText Australia = LOCTEXT("Country_Australia", "Australia");
		const FText SingaporeCountry = LOCTEXT("Country_Singapore", "Singapore");

		// --- Iran ---
		Add(TEXT("Tehran"),      LOCTEXT("City_Tehran", "Tehran"),           Iran,  35.6892,  51.3890, 3.5f, 1200.f, EArchClimateHint::SemiAridContinental, TEXT("Haze"));
		Add(TEXT("Isfahan"),     LOCTEXT("City_Isfahan", "Isfahan"),         Iran,  32.6539,  51.6660, 3.5f, 1590.f, EArchClimateHint::AridDesert,          TEXT("Clear"));
		Add(TEXT("Shiraz"),      LOCTEXT("City_Shiraz", "Shiraz"),           Iran,  29.5918,  52.5837, 3.5f, 1500.f, EArchClimateHint::Mediterranean,       TEXT("Clear"));
		Add(TEXT("Tabriz"),      LOCTEXT("City_Tabriz", "Tabriz"),           Iran,  38.0800,  46.2919, 3.5f, 1350.f, EArchClimateHint::Continental,         TEXT("PartlyCloudy"));
		Add(TEXT("Mashhad"),     LOCTEXT("City_Mashhad", "Mashhad"),         Iran,  36.2605,  59.6168, 3.5f,  995.f, EArchClimateHint::SemiAridContinental, TEXT("Clear"));
		Add(TEXT("Yazd"),        LOCTEXT("City_Yazd", "Yazd"),               Iran,  31.8974,  54.3569, 3.5f, 1216.f, EArchClimateHint::AridDesert,          TEXT("ClearHot"));
		Add(TEXT("BandarAbbas"), LOCTEXT("City_BandarAbbas", "Bandar Abbas"),Iran,  27.1865,  56.2808, 3.5f,    9.f, EArchClimateHint::AridDesert,          TEXT("Haze"));
		Add(TEXT("Rasht"),       LOCTEXT("City_Rasht", "Rasht"),             Iran,  37.2808,  49.5832, 3.5f,   -8.f, EArchClimateHint::HumidSubtropical,    TEXT("LightRain"));
		Add(TEXT("Kish"),        LOCTEXT("City_Kish", "Kish"),               Iran,  26.5578,  53.9807, 3.5f,   30.f, EArchClimateHint::AridDesert,          TEXT("Clear"));

		// --- Regional and global reference cities ---
		Add(TEXT("Dubai"),     LOCTEXT("City_Dubai", "Dubai"),         Uae,              25.2048,  55.2708, 4.0f,   5.f, EArchClimateHint::AridDesert,       TEXT("ClearHot"));
		Add(TEXT("Istanbul"),  LOCTEXT("City_Istanbul", "Istanbul"),   Turkiye,          41.0082,  28.9784, 3.0f,  40.f, EArchClimateHint::Mediterranean,    TEXT("PartlyCloudy"));
		Add(TEXT("London"),    LOCTEXT("City_London", "London"),       UnitedKingdom,    51.5072,  -0.1276, 0.0f,  11.f, EArchClimateHint::TemperateOceanic, TEXT("Overcast"),    true);
		Add(TEXT("NewYork"),   LOCTEXT("City_NewYork", "New York"),    UnitedStates,     40.7128, -74.0060, -5.0f, 10.f, EArchClimateHint::Continental,      TEXT("PartlyCloudy"), true);
		Add(TEXT("Tokyo"),     LOCTEXT("City_Tokyo", "Tokyo"),         Japan,            35.6762, 139.6503, 9.0f,  40.f, EArchClimateHint::HumidSubtropical, TEXT("PartlyCloudy"));
		Add(TEXT("Sydney"),    LOCTEXT("City_Sydney", "Sydney"),       Australia,       -33.8688, 151.2093, 10.0f, 58.f, EArchClimateHint::TemperateOceanic, TEXT("Clear"),        true);
		Add(TEXT("Singapore"), LOCTEXT("City_Singapore", "Singapore"), SingaporeCountry,  1.3521, 103.8198, 8.0f,  15.f, EArchClimateHint::Tropical,         TEXT("PartlyCloudy"));

		return Library;
	}

	static const TArray<FArchLocationEntry>& Get()
	{
		// ARCH NOTE: constructed on first use rather than as a namespace-scope global.
		// The entries hold FText, and FText construction before the localisation manager is
		// initialised produces entries with no display string in a packaged build.
		static const TArray<FArchLocationEntry> Library = BuildLibrary();
		return Library;
	}
}

FArchLocationEntry UArchLocationLibrary::GetBuiltInLocation(FName CityId, bool& bOutFound)
{
	for (const FArchLocationEntry& Entry : ArchBuiltInLocations::Get())
	{
		if (Entry.CityId == CityId)
		{
			bOutFound = true;
			return Entry;
		}
	}

	bOutFound = false;
	return FArchLocationEntry();
}

TArray<FArchLocationEntry> UArchLocationLibrary::GetBuiltInLocations()
{
	return ArchBuiltInLocations::Get();
}

FText UArchLocationLibrary::GetClimateHintDisplayName(EArchClimateHint ClimateHint)
{
	switch (ClimateHint)
	{
	case EArchClimateHint::AridDesert:          return LOCTEXT("Climate_Arid", "Arid / Desert");
	case EArchClimateHint::Mediterranean:       return LOCTEXT("Climate_Mediterranean", "Mediterranean");
	case EArchClimateHint::SemiAridContinental: return LOCTEXT("Climate_SemiArid", "Semi-Arid Continental");
	case EArchClimateHint::HumidSubtropical:    return LOCTEXT("Climate_HumidSubtropical", "Humid Subtropical");
	case EArchClimateHint::TemperateOceanic:    return LOCTEXT("Climate_Oceanic", "Temperate Oceanic");
	case EArchClimateHint::Continental:         return LOCTEXT("Climate_Continental", "Continental");
	case EArchClimateHint::Tropical:            return LOCTEXT("Climate_Tropical", "Tropical");
	default:                                    return FText::GetEmpty();
	}
}

TArray<FArchLocationEntry> UArchLocationLibrary::SearchBuiltInLocations(const FString& SearchText)
{
	const TArray<FArchLocationEntry>& Library = ArchBuiltInLocations::Get();

	if (SearchText.IsEmpty())
	{
		return Library;
	}

	TArray<FArchLocationEntry> Matches;
	Matches.Reserve(Library.Num());

	for (const FArchLocationEntry& Entry : Library)
	{
		// FText::ToString() gives the localised text, so a Persian build searches Persian.
		const bool bMatches =
			Entry.DisplayName.ToString().Contains(SearchText, ESearchCase::IgnoreCase)
			|| Entry.CountryName.ToString().Contains(SearchText, ESearchCase::IgnoreCase)
			|| Entry.CityId.ToString().Contains(SearchText, ESearchCase::IgnoreCase);

		if (bMatches)
		{
			Matches.Add(Entry);
		}
	}

	return Matches;
}

#undef LOCTEXT_NAMESPACE
