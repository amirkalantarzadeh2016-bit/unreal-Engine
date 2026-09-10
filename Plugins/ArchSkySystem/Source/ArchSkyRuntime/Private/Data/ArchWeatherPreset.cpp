// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/ArchWeatherPreset.h"

#include "Templates/Function.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchWeather"

const FPrimaryAssetType UArchWeatherPreset::PrimaryAssetType = FPrimaryAssetType(TEXT("ArchWeatherPreset"));

// ---------------------------------------------------------------------------------------
// FArchWeatherParams
// ---------------------------------------------------------------------------------------

namespace ArchWeatherBlendDetail
{
	/** Shorthand for a scalar lerp; Alpha is pre-clamped by the caller. */
	FORCEINLINE float L(float A, float B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}

	/**
	 * Interpolates a compass bearing the short way round, so blending 350 deg to 10 deg
	 * passes through 0 rather than sweeping backwards through 180.
	 */
	float LerpBearingDegrees(float A, float B, float Alpha)
	{
		float Delta = FMath::Fmod(B - A + 540.f, 360.f) - 180.f;
		float Result = FMath::Fmod(A + Delta * Alpha, 360.f);
		if (Result < 0.f)
		{
			Result += 360.f;
		}
		return Result;
	}
}

FArchWeatherParams FArchWeatherParams::Blend(const FArchWeatherParams& A, const FArchWeatherParams& B, float Alpha)
{
	using namespace ArchWeatherBlendDetail;

	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	// Fast paths: a transition spends most of its life at one end or the other, and the
	// blend is evaluated every frame while a transition runs.
	if (Alpha <= 0.f)
	{
		return A;
	}
	if (Alpha >= 1.f)
	{
		return B;
	}

	FArchWeatherParams Result;

	// --- Clouds ---
	Result.CloudCoverage = L(A.CloudCoverage, B.CloudCoverage, Alpha);
	Result.CloudDensity = L(A.CloudDensity, B.CloudDensity, Alpha);
	Result.CloudAltitudeKm = L(A.CloudAltitudeKm, B.CloudAltitudeKm, Alpha);
	Result.CloudLayerThicknessKm = L(A.CloudLayerThicknessKm, B.CloudLayerThicknessKm, Alpha);
	Result.CloudErosion = L(A.CloudErosion, B.CloudErosion, Alpha);
	Result.CloudWindSpeed = L(A.CloudWindSpeed, B.CloudWindSpeed, Alpha);
	Result.CloudWindDirectionDeg = LerpBearingDegrees(A.CloudWindDirectionDeg, B.CloudWindDirectionDeg, Alpha);

	// --- Atmosphere ---
	Result.RayleighScatteringScale = L(A.RayleighScatteringScale, B.RayleighScatteringScale, Alpha);
	Result.MieScatteringScale = L(A.MieScatteringScale, B.MieScatteringScale, Alpha);
	Result.MieAnisotropy = L(A.MieAnisotropy, B.MieAnisotropy, Alpha);
	Result.AerosolTurbidity = L(A.AerosolTurbidity, B.AerosolTurbidity, Alpha);

	// Colours are lerped componentwise in LINEAR space. FLinearColor already is linear, so
	// this is the physically correct blend - converting to sRGB first would darken the
	// midpoint of every transition.
	Result.SkyLuminanceTint = FMath::Lerp(A.SkyLuminanceTint, B.SkyLuminanceTint, Alpha);

	// --- Fog ---
	Result.FogDensity = L(A.FogDensity, B.FogDensity, Alpha);
	Result.FogHeightFalloff = L(A.FogHeightFalloff, B.FogHeightFalloff, Alpha);
	Result.FogInscatteringColor = FMath::Lerp(A.FogInscatteringColor, B.FogInscatteringColor, Alpha);
	Result.FogStartDistance = L(A.FogStartDistance, B.FogStartDistance, Alpha);
	Result.VolumetricFogExtinctionScale = L(A.VolumetricFogExtinctionScale, B.VolumetricFogExtinctionScale, Alpha);

	// --- Light response ---
	Result.SunIntensityMultiplier = L(A.SunIntensityMultiplier, B.SunIntensityMultiplier, Alpha);
	Result.SunLightColorTintStrength = L(A.SunLightColorTintStrength, B.SunLightColorTintStrength, Alpha);
	Result.SkyLightIntensityMultiplier = L(A.SkyLightIntensityMultiplier, B.SkyLightIntensityMultiplier, Alpha);
	Result.ShadowSoftnessMultiplier = L(A.ShadowSoftnessMultiplier, B.ShadowSoftnessMultiplier, Alpha);
	Result.DiffuseToDirectRatio = L(A.DiffuseToDirectRatio, B.DiffuseToDirectRatio, Alpha);

	// --- Precipitation ---
	Result.PrecipIntensity = L(A.PrecipIntensity, B.PrecipIntensity, Alpha);
	Result.PuddleWetness = L(A.PuddleWetness, B.PuddleWetness, Alpha);
	Result.SurfaceSnowCoverage = L(A.SurfaceSnowCoverage, B.SurfaceSnowCoverage, Alpha);
	Result.WindStrength = L(A.WindStrength, B.WindStrength, Alpha);
	Result.WindTurbulence = L(A.WindTurbulence, B.WindTurbulence, Alpha);

	// --- Audio ---
	Result.AmbientVolume = L(A.AmbientVolume, B.AmbientVolume, Alpha);
	Result.ThunderFrequencyPerMinute = L(A.ThunderFrequencyPerMinute, B.ThunderFrequencyPerMinute, Alpha);

	// --- Post ---
	Result.ExposureCompensation = L(A.ExposureCompensation, B.ExposureCompensation, Alpha);
	Result.BloomMultiplier = L(A.BloomMultiplier, B.BloomMultiplier, Alpha);

	// --- Discrete: select, never interpolate. See the note on Blend() in the header. ---
	const bool bTakeFromB = Alpha >= 0.5f;
	Result.PrecipType = bTakeFromB ? B.PrecipType : A.PrecipType;
	Result.AmbientLoop = bTakeFromB ? B.AmbientLoop : A.AmbientLoop;
	Result.ThunderCue = bTakeFromB ? B.ThunderCue : A.ThunderCue;

	return Result;
}

bool FArchWeatherParams::IsNearlyEqual(const FArchWeatherParams& Other, float Tolerance) const
{
	return GetSignificantDelta(Other) <= Tolerance
		&& PrecipType == Other.PrecipType
		&& AmbientLoop == Other.AmbientLoop
		&& ThunderCue == Other.ThunderCue;
}

float FArchWeatherParams::GetSignificantDelta(const FArchWeatherParams& Other) const
{
	// Normalised so that each term is roughly "fraction of its useful range", letting a
	// single scalar threshold in the Director mean the same thing for every field.
	float Delta = 0.f;

	auto Accumulate = [&Delta](float A, float B, float Range)
	{
		Delta = FMath::Max(Delta, FMath::Abs(A - B) / Range);
	};

	Accumulate(CloudCoverage, Other.CloudCoverage, 1.f);
	Accumulate(CloudDensity, Other.CloudDensity, 1.f);
	Accumulate(CloudAltitudeKm, Other.CloudAltitudeKm, 12.f);
	Accumulate(CloudLayerThicknessKm, Other.CloudLayerThicknessKm, 12.f);
	Accumulate(CloudErosion, Other.CloudErosion, 1.f);
	Accumulate(RayleighScatteringScale, Other.RayleighScatteringScale, 3.f);
	Accumulate(MieScatteringScale, Other.MieScatteringScale, 10.f);
	Accumulate(MieAnisotropy, Other.MieAnisotropy, 1.f);
	Accumulate(AerosolTurbidity, Other.AerosolTurbidity, 10.f);
	Accumulate(FogDensity, Other.FogDensity, 0.05f);
	Accumulate(FogHeightFalloff, Other.FogHeightFalloff, 1.f);
	Accumulate(FogStartDistance, Other.FogStartDistance, 20000.f);
	Accumulate(VolumetricFogExtinctionScale, Other.VolumetricFogExtinctionScale, 4.f);
	Accumulate(SunIntensityMultiplier, Other.SunIntensityMultiplier, 1.5f);
	Accumulate(SkyLightIntensityMultiplier, Other.SkyLightIntensityMultiplier, 3.f);
	Accumulate(ShadowSoftnessMultiplier, Other.ShadowSoftnessMultiplier, 20.f);
	Accumulate(DiffuseToDirectRatio, Other.DiffuseToDirectRatio, 1.f);
	Accumulate(PrecipIntensity, Other.PrecipIntensity, 1.f);
	Accumulate(PuddleWetness, Other.PuddleWetness, 1.f);
	Accumulate(SurfaceSnowCoverage, Other.SurfaceSnowCoverage, 1.f);

	// Colours: the largest single-channel difference.
	auto AccumulateColor = [&Delta](const FLinearColor& A, const FLinearColor& B)
	{
		Delta = FMath::Max(Delta, FMath::Max3(
			FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G), FMath::Abs(A.B - B.B)));
	};

	AccumulateColor(SkyLuminanceTint, Other.SkyLuminanceTint);
	AccumulateColor(FogInscatteringColor, Other.FogInscatteringColor);

	return Delta;
}

// ---------------------------------------------------------------------------------------
// UArchWeatherPreset
// ---------------------------------------------------------------------------------------

UArchWeatherPreset::UArchWeatherPreset()
{
	DisplayName = LOCTEXT("DefaultWeatherName", "New Weather Preset");
}

FPrimaryAssetId UArchWeatherPreset::GetPrimaryAssetId() const
{
	// Keyed on PresetId rather than the asset name so that renaming the .uasset does not
	// invalidate saved presets or console commands. Falls back to the object name when the
	// author has not filled the id in yet.
	const FName Id = PresetId.IsNone() ? GetFName() : PresetId;
	return FPrimaryAssetId(PrimaryAssetType, Id);
}

#if WITH_EDITOR

void UArchWeatherPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Preset ids are used as console arguments and save-game keys, so silently accepting
	// whitespace or mixed case would produce lookups that fail for no visible reason.
	if (!PresetId.IsNone())
	{
		const FString Trimmed = PresetId.ToString().TrimStartAndEnd();
		if (Trimmed != PresetId.ToString())
		{
			PresetId = FName(*Trimmed);
			UE_LOG(LogArchSky, Log, TEXT("Trimmed whitespace from weather PresetId on '%s'."), *GetName());
		}
	}
}

EDataValidationResult UArchWeatherPreset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (PresetId.IsNone())
	{
		Context.AddError(LOCTEXT("MissingPresetId",
			"PresetId is empty. Blueprint, console commands and save games all address weather by this id."));
		Result = EDataValidationResult::Invalid;
	}

	if (DisplayName.IsEmpty())
	{
		Context.AddWarning(LOCTEXT("MissingDisplayName",
			"DisplayName is empty; the weather tile in the UI will have no label."));
	}

	if (Params.PrecipType != EArchPrecipType::None && Params.PrecipIntensity <= 0.f)
	{
		Context.AddWarning(LOCTEXT("PrecipTypeWithoutIntensity",
			"A precipitation type is set but PrecipIntensity is zero, so no precipitation will appear."));
	}

	if (Params.PrecipType == EArchPrecipType::Snow && Params.PuddleWetness > 0.5f)
	{
		Context.AddWarning(LOCTEXT("SnowWithPuddles",
			"Snow with high PuddleWetness reads as rain on the ground. Prefer SurfaceSnowCoverage."));
	}

	return Result;
}

#endif // WITH_EDITOR

// ---------------------------------------------------------------------------------------
// Built-in preset library
// ---------------------------------------------------------------------------------------

namespace ArchBuiltInWeather
{
	/**
	 * ARCH NOTE: these twelve parameter sets are compiled in rather than shipped only as
	 * .uasset files. Two reasons. First, a .uasset cannot be authored from source control
	 * as text, so a plugin delivered as code alone would otherwise have an empty weather
	 * library until someone hand-built twelve assets. Second, they double as the reference
	 * values an artist starts from - Content/Data/README.md documents how to create the
	 * matching assets, and any asset with the same PresetId simply overrides its built-in.
	 *
	 * The numbers are anchored to real observations: clear-sky turbidity ~2.5, urban haze
	 * 5-6, a Middle-Eastern dust storm 10+; overcast raises the diffuse fraction to ~1.0
	 * and drops direct sun to near zero, which is why an overcast scene has soft, almost
	 * shadowless lighting.
	 */
	struct FBuiltInEntry
	{
		const TCHAR* Id;
		FArchWeatherParams Params;
	};

	static TArray<FBuiltInEntry> BuildLibrary()
	{
		TArray<FBuiltInEntry> Library;

		auto Add = [&Library](const TCHAR* Id, TFunctionRef<void(FArchWeatherParams&)> Configure)
		{
			FBuiltInEntry Entry;
			Entry.Id = Id;
			Configure(Entry.Params);
			Library.Add(MoveTemp(Entry));
		};

		Add(TEXT("Clear"), [](FArchWeatherParams& P)
		{
			P.CloudCoverage = 0.03f; P.CloudDensity = 0.05f; P.CloudErosion = 0.5f;
			P.CloudAltitudeKm = 6.f; P.CloudLayerThicknessKm = 2.f;
			P.CloudWindSpeed = 4.f;
			P.RayleighScatteringScale = 1.f; P.MieScatteringScale = 1.f; P.AerosolTurbidity = 2.5f;
			P.FogDensity = 0.008f; P.FogHeightFalloff = 0.25f;
			P.SunIntensityMultiplier = 1.f; P.SkyLightIntensityMultiplier = 1.f;
			P.ShadowSoftnessMultiplier = 1.f; P.DiffuseToDirectRatio = 0.13f;
			P.WindStrength = 2.f; P.AmbientVolume = 0.25f;
		});

		Add(TEXT("ClearHot"), [](FArchWeatherParams& P)
		{
			// A high-summer Iranian plateau day: dry, hazy near the horizon, brutally bright.
			P.CloudCoverage = 0.02f; P.CloudDensity = 0.04f; P.CloudAltitudeKm = 7.f;
			P.CloudLayerThicknessKm = 1.5f; P.CloudWindSpeed = 3.f;
			P.RayleighScatteringScale = 0.9f; P.MieScatteringScale = 2.2f; P.AerosolTurbidity = 4.5f;
			P.SkyLuminanceTint = FLinearColor(1.f, 0.98f, 0.92f, 1.f);
			P.FogDensity = 0.012f; P.FogHeightFalloff = 0.35f;
			P.FogInscatteringColor = FLinearColor(0.62f, 0.60f, 0.52f, 1.f);
			P.SunIntensityMultiplier = 1.05f; P.SkyLightIntensityMultiplier = 1.1f;
			P.ShadowSoftnessMultiplier = 1.2f; P.DiffuseToDirectRatio = 0.18f;
			P.WindStrength = 3.f; P.ExposureCompensation = -0.2f; P.BloomMultiplier = 1.2f;
			P.AmbientVolume = 0.2f;
		});

		Add(TEXT("PartlyCloudy"), [](FArchWeatherParams& P)
		{
			P.CloudCoverage = 0.35f; P.CloudDensity = 0.25f; P.CloudErosion = 0.55f;
			P.CloudAltitudeKm = 4.f; P.CloudLayerThicknessKm = 3.5f; P.CloudWindSpeed = 8.f;
			P.RayleighScatteringScale = 1.f; P.MieScatteringScale = 1.4f; P.AerosolTurbidity = 3.2f;
			P.FogDensity = 0.012f;
			P.SunIntensityMultiplier = 0.92f; P.SkyLightIntensityMultiplier = 1.15f;
			P.ShadowSoftnessMultiplier = 1.5f; P.DiffuseToDirectRatio = 0.3f;
			P.WindStrength = 4.f; P.AmbientVolume = 0.3f;
		});

		Add(TEXT("Overcast"), [](FArchWeatherParams& P)
		{
			// The reference condition for a "worst case daylight" study: no direct sun,
			// almost all illuminance arriving as uniform diffuse sky.
			P.CloudCoverage = 0.98f; P.CloudDensity = 0.62f; P.CloudErosion = 0.25f;
			P.CloudAltitudeKm = 2.f; P.CloudLayerThicknessKm = 5.f; P.CloudWindSpeed = 10.f;
			P.RayleighScatteringScale = 0.8f; P.MieScatteringScale = 3.5f; P.AerosolTurbidity = 5.f;
			P.SkyLuminanceTint = FLinearColor(0.88f, 0.90f, 0.94f, 1.f);
			P.FogDensity = 0.025f; P.FogHeightFalloff = 0.18f;
			P.FogInscatteringColor = FLinearColor(0.52f, 0.55f, 0.60f, 1.f);
			P.SunIntensityMultiplier = 0.18f; P.SkyLightIntensityMultiplier = 1.8f;
			P.ShadowSoftnessMultiplier = 12.f; P.DiffuseToDirectRatio = 0.95f;
			P.WindStrength = 5.f; P.ExposureCompensation = 0.3f; P.BloomMultiplier = 0.7f;
			P.AmbientVolume = 0.3f;
		});

		Add(TEXT("LightRain"), [](FArchWeatherParams& P)
		{
			P.CloudCoverage = 0.85f; P.CloudDensity = 0.55f; P.CloudErosion = 0.3f;
			P.CloudAltitudeKm = 1.8f; P.CloudLayerThicknessKm = 4.5f; P.CloudWindSpeed = 12.f;
			P.RayleighScatteringScale = 0.85f; P.MieScatteringScale = 3.f; P.AerosolTurbidity = 4.5f;
			P.FogDensity = 0.03f; P.FogHeightFalloff = 0.2f;
			P.FogInscatteringColor = FLinearColor(0.48f, 0.52f, 0.58f, 1.f);
			P.VolumetricFogExtinctionScale = 1.4f;
			P.SunIntensityMultiplier = 0.25f; P.SkyLightIntensityMultiplier = 1.5f;
			P.ShadowSoftnessMultiplier = 9.f; P.DiffuseToDirectRatio = 0.88f;
			P.PrecipType = EArchPrecipType::Rain; P.PrecipIntensity = 0.3f;
			P.PuddleWetness = 0.45f; P.WindStrength = 6.f; P.WindTurbulence = 0.3f;
			P.AmbientVolume = 0.5f; P.ExposureCompensation = 0.2f;
		});

		Add(TEXT("HeavyRain"), [](FArchWeatherParams& P)
		{
			P.CloudCoverage = 1.f; P.CloudDensity = 0.8f; P.CloudErosion = 0.2f;
			P.CloudAltitudeKm = 1.2f; P.CloudLayerThicknessKm = 6.5f; P.CloudWindSpeed = 20.f;
			P.RayleighScatteringScale = 0.7f; P.MieScatteringScale = 4.5f; P.AerosolTurbidity = 6.f;
			P.SkyLuminanceTint = FLinearColor(0.72f, 0.76f, 0.82f, 1.f);
			P.FogDensity = 0.055f; P.FogHeightFalloff = 0.15f;
			P.FogInscatteringColor = FLinearColor(0.38f, 0.42f, 0.48f, 1.f);
			P.VolumetricFogExtinctionScale = 2.2f;
			P.SunIntensityMultiplier = 0.1f; P.SkyLightIntensityMultiplier = 1.2f;
			P.ShadowSoftnessMultiplier = 16.f; P.DiffuseToDirectRatio = 0.98f;
			P.PrecipType = EArchPrecipType::Rain; P.PrecipIntensity = 0.85f;
			P.PuddleWetness = 0.95f; P.WindStrength = 14.f; P.WindTurbulence = 0.55f;
			P.AmbientVolume = 0.85f; P.ExposureCompensation = 0.5f; P.BloomMultiplier = 0.6f;
		});

		Add(TEXT("Thunderstorm"), [](FArchWeatherParams& P)
		{
			P.CloudCoverage = 1.f; P.CloudDensity = 0.92f; P.CloudErosion = 0.35f;
			P.CloudAltitudeKm = 1.f; P.CloudLayerThicknessKm = 9.f; P.CloudWindSpeed = 28.f;
			P.RayleighScatteringScale = 0.6f; P.MieScatteringScale = 5.5f; P.AerosolTurbidity = 7.f;
			P.SkyLuminanceTint = FLinearColor(0.62f, 0.65f, 0.72f, 1.f);
			P.FogDensity = 0.065f; P.FogHeightFalloff = 0.14f;
			P.FogInscatteringColor = FLinearColor(0.30f, 0.33f, 0.40f, 1.f);
			P.VolumetricFogExtinctionScale = 2.6f;
			P.SunIntensityMultiplier = 0.06f; P.SkyLightIntensityMultiplier = 1.f;
			P.ShadowSoftnessMultiplier = 20.f; P.DiffuseToDirectRatio = 1.f;
			P.PrecipType = EArchPrecipType::Rain; P.PrecipIntensity = 1.f;
			P.PuddleWetness = 1.f; P.WindStrength = 22.f; P.WindTurbulence = 0.85f;
			P.AmbientVolume = 1.f; P.ThunderFrequencyPerMinute = 6.f;
			P.ExposureCompensation = 0.7f; P.BloomMultiplier = 0.5f;
		});

		Add(TEXT("Fog"), [](FArchWeatherParams& P)
		{
			// A Rasht / Caspian-coast morning: thick ground fog under a thin overcast.
			P.CloudCoverage = 0.7f; P.CloudDensity = 0.4f; P.CloudAltitudeKm = 0.8f;
			P.CloudLayerThicknessKm = 2.f; P.CloudWindSpeed = 2.f;
			P.RayleighScatteringScale = 0.9f; P.MieScatteringScale = 6.f; P.AerosolTurbidity = 8.f;
			P.SkyLuminanceTint = FLinearColor(0.90f, 0.92f, 0.94f, 1.f);
			P.FogDensity = 0.14f; P.FogHeightFalloff = 0.06f;
			P.FogInscatteringColor = FLinearColor(0.72f, 0.75f, 0.78f, 1.f);
			P.FogStartDistance = 200.f; P.VolumetricFogExtinctionScale = 3.2f;
			P.SunIntensityMultiplier = 0.12f; P.SkyLightIntensityMultiplier = 1.6f;
			P.ShadowSoftnessMultiplier = 18.f; P.DiffuseToDirectRatio = 0.97f;
			P.WindStrength = 1.f; P.AmbientVolume = 0.2f;
			P.ExposureCompensation = 0.4f; P.BloomMultiplier = 1.4f;
		});

		Add(TEXT("Haze"), [](FArchWeatherParams& P)
		{
			// Tehran inversion smog: sun visible but flat, horizon washed out.
			P.CloudCoverage = 0.12f; P.CloudDensity = 0.1f; P.CloudAltitudeKm = 5.f;
			P.CloudLayerThicknessKm = 2.f; P.CloudWindSpeed = 2.f;
			P.RayleighScatteringScale = 0.8f; P.MieScatteringScale = 5.f; P.AerosolTurbidity = 7.5f;
			P.SkyLuminanceTint = FLinearColor(0.95f, 0.93f, 0.86f, 1.f);
			P.FogDensity = 0.045f; P.FogHeightFalloff = 0.09f;
			P.FogInscatteringColor = FLinearColor(0.66f, 0.63f, 0.55f, 1.f);
			P.VolumetricFogExtinctionScale = 1.6f;
			P.SunIntensityMultiplier = 0.7f; P.SunLightColorTintStrength = 0.35f;
			P.SkyLightIntensityMultiplier = 1.25f;
			P.ShadowSoftnessMultiplier = 4.f; P.DiffuseToDirectRatio = 0.5f;
			P.WindStrength = 1.5f; P.AmbientVolume = 0.25f; P.BloomMultiplier = 1.5f;
		});

		Add(TEXT("DustStorm"), [](FArchWeatherParams& P)
		{
			// Sistan "120-day wind" conditions. Turbidity is the defining term.
			P.CloudCoverage = 0.25f; P.CloudDensity = 0.3f; P.CloudAltitudeKm = 3.f;
			P.CloudLayerThicknessKm = 4.f; P.CloudWindSpeed = 30.f;
			P.RayleighScatteringScale = 0.45f; P.MieScatteringScale = 9.f; P.AerosolTurbidity = 12.f;
			P.MieAnisotropy = 0.88f;
			P.SkyLuminanceTint = FLinearColor(1.f, 0.78f, 0.48f, 1.f);
			P.FogDensity = 0.11f; P.FogHeightFalloff = 0.1f;
			P.FogInscatteringColor = FLinearColor(0.78f, 0.56f, 0.30f, 1.f);
			P.VolumetricFogExtinctionScale = 2.8f;
			P.SunIntensityMultiplier = 0.3f; P.SunLightColorTintStrength = 0.85f;
			P.SkyLightIntensityMultiplier = 0.9f;
			P.ShadowSoftnessMultiplier = 14.f; P.DiffuseToDirectRatio = 0.8f;
			P.PrecipType = EArchPrecipType::Dust; P.PrecipIntensity = 0.8f;
			P.WindStrength = 26.f; P.WindTurbulence = 0.75f;
			P.AmbientVolume = 0.9f; P.ExposureCompensation = -0.3f; P.BloomMultiplier = 1.6f;
		});

		Add(TEXT("LightSnow"), [](FArchWeatherParams& P)
		{
			P.CloudCoverage = 0.9f; P.CloudDensity = 0.5f; P.CloudErosion = 0.25f;
			P.CloudAltitudeKm = 1.6f; P.CloudLayerThicknessKm = 4.f; P.CloudWindSpeed = 8.f;
			P.RayleighScatteringScale = 0.9f; P.MieScatteringScale = 3.2f; P.AerosolTurbidity = 4.f;
			P.SkyLuminanceTint = FLinearColor(0.92f, 0.94f, 1.f, 1.f);
			P.FogDensity = 0.035f; P.FogHeightFalloff = 0.16f;
			P.FogInscatteringColor = FLinearColor(0.68f, 0.72f, 0.80f, 1.f);
			P.SunIntensityMultiplier = 0.28f; P.SkyLightIntensityMultiplier = 1.7f;
			P.ShadowSoftnessMultiplier = 10.f; P.DiffuseToDirectRatio = 0.9f;
			P.PrecipType = EArchPrecipType::Snow; P.PrecipIntensity = 0.35f;
			P.SurfaceSnowCoverage = 0.5f; P.WindStrength = 4.f; P.WindTurbulence = 0.35f;
			P.AmbientVolume = 0.2f; P.ExposureCompensation = 0.3f;
		});

		Add(TEXT("Blizzard"), [](FArchWeatherParams& P)
		{
			P.CloudCoverage = 1.f; P.CloudDensity = 0.85f; P.CloudErosion = 0.2f;
			P.CloudAltitudeKm = 0.9f; P.CloudLayerThicknessKm = 7.f; P.CloudWindSpeed = 32.f;
			P.RayleighScatteringScale = 0.75f; P.MieScatteringScale = 6.5f; P.AerosolTurbidity = 8.f;
			P.SkyLuminanceTint = FLinearColor(0.86f, 0.89f, 0.96f, 1.f);
			P.FogDensity = 0.12f; P.FogHeightFalloff = 0.08f;
			P.FogInscatteringColor = FLinearColor(0.74f, 0.78f, 0.86f, 1.f);
			P.VolumetricFogExtinctionScale = 3.f;
			P.SunIntensityMultiplier = 0.08f; P.SkyLightIntensityMultiplier = 1.3f;
			P.ShadowSoftnessMultiplier = 20.f; P.DiffuseToDirectRatio = 1.f;
			P.PrecipType = EArchPrecipType::Snow; P.PrecipIntensity = 1.f;
			P.SurfaceSnowCoverage = 1.f; P.WindStrength = 28.f; P.WindTurbulence = 0.9f;
			P.AmbientVolume = 1.f; P.ExposureCompensation = 0.6f; P.BloomMultiplier = 0.8f;
		});

		return Library;
	}

	/** Built once on first use; the library is immutable afterwards. */
	static const TArray<FBuiltInEntry>& Get()
	{
		static const TArray<FBuiltInEntry> Library = BuildLibrary();
		return Library;
	}
}

FArchWeatherParams UArchWeatherLibrary::BlendWeatherParams(const FArchWeatherParams& A, const FArchWeatherParams& B, float Alpha)
{
	return FArchWeatherParams::Blend(A, B, Alpha);
}

FText UArchWeatherLibrary::GetPrecipTypeDisplayName(EArchPrecipType PrecipType)
{
	switch (PrecipType)
	{
	case EArchPrecipType::None: return LOCTEXT("Precip_None", "None");
	case EArchPrecipType::Rain: return LOCTEXT("Precip_Rain", "Rain");
	case EArchPrecipType::Snow: return LOCTEXT("Precip_Snow", "Snow");
	case EArchPrecipType::Hail: return LOCTEXT("Precip_Hail", "Hail");
	case EArchPrecipType::Dust: return LOCTEXT("Precip_Dust", "Dust");
	default:                    return FText::GetEmpty();
	}
}

FArchWeatherParams UArchWeatherLibrary::GetBuiltInWeatherParams(FName PresetId, bool& bOutFound)
{
	for (const ArchBuiltInWeather::FBuiltInEntry& Entry : ArchBuiltInWeather::Get())
	{
		if (PresetId == FName(Entry.Id))
		{
			bOutFound = true;
			return Entry.Params;
		}
	}

	bOutFound = false;
	return FArchWeatherParams();
}

TArray<FName> UArchWeatherLibrary::GetBuiltInWeatherPresetIds()
{
	TArray<FName> Ids;
	Ids.Reserve(ArchBuiltInWeather::Get().Num());

	for (const ArchBuiltInWeather::FBuiltInEntry& Entry : ArchBuiltInWeather::Get())
	{
		Ids.Add(FName(Entry.Id));
	}

	return Ids;
}

#undef LOCTEXT_NAMESPACE
