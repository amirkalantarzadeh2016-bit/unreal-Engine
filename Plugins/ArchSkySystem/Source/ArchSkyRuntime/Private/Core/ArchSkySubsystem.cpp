// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/ArchSkySubsystem.h"

#include "Data/ArchSkySettings.h"
#include "Data/ArchTimeCalendar.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchSkySubsystem"

// ---------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------

bool UArchSkySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}

	// ARCH NOTE: we deliberately exist in Editor worlds as well as Game and PIE. The
	// Director's editor-time property scrubbing queries this subsystem, which is what lets
	// an architect drag the time slider in the details panel and watch the shadows move
	// WITHOUT entering PIE. Ticking is separately disabled in editor worlds, so no time
	// actually flows there.
	switch (World->WorldType)
	{
	case EWorldType::Game:
	case EWorldType::PIE:
	case EWorldType::Editor:
	case EWorldType::EditorPreview:
	case EWorldType::GamePreview:
		return true;
	default:
		return false;
	}
}

void UArchSkySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	DiscoverPresetAssets();
	ApplyProjectDefaults();

	bInitialised = true;

	UE_LOG(LogArchSky, Log, TEXT("ArchSky subsystem initialised for world '%s' (%d weather preset assets)."),
		*GetNameSafe(GetWorld()), WeatherPresetAssets.Num());
}

void UArchSkySubsystem::Deinitialize()
{
	// Clearing the delegates here rather than relying on GC guarantees that a Director or
	// widget torn down after us cannot be re-entered during world teardown.
	OnSkyStateChanged.Clear();
	OnTimePhaseChanged.Clear();
	OnWeatherTransitionStarted.Clear();
	OnWeatherTransitionCompleted.Clear();
	OnDayRolled.Clear();
	OnSunrise.Clear();
	OnSunset.Clear();

	WeatherPresetAssets.Empty();
	LocationLibraryAsset = nullptr;
	bInitialised = false;

	Super::Deinitialize();
}

void UArchSkySubsystem::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ArchSky_SubsystemTick);

	const bool bTimeFlowing = !FMath::IsNearlyZero(SkyState.TimeFlowRate);
	const bool bTransitionRunning = WeatherTransitionDuration > 0.f;

	if (!bTimeFlowing && !bTransitionRunning)
	{
		// IsTickable already filters this, but a state change between the two calls is
		// possible, and doing nothing is cheaper than trusting the filter.
		return;
	}

	if (bTimeFlowing)
	{
		AdvanceTime(DeltaTime);
	}

	if (bTransitionRunning)
	{
		AdvanceWeatherTransition(DeltaTime);
	}

	// SolarUpdateInterval lets a low-end target recompute the ephemeris at, say, 10 Hz
	// while lights still interpolate every frame. Default 0 = every frame.
	const UArchSkySettings* Settings = UArchSkySettings::Get();
	const float SolarInterval = Settings ? Settings->SolarUpdateInterval : 0.f;

	SecondsSinceSolarUpdate += DeltaTime;
	const bool bRecomputeSolar = (SolarInterval <= 0.f) || (SecondsSinceSolarUpdate >= SolarInterval);
	if (bRecomputeSolar)
	{
		SecondsSinceSolarUpdate = 0.f;
	}

	RefreshDerivedState(bRecomputeSolar);
}

bool UArchSkySubsystem::IsTickable() const
{
	if (IsTemplate() || !bInitialised)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World || World->WorldType == EWorldType::Editor || World->WorldType == EWorldType::EditorPreview)
	{
		// Editor worlds hold state for property scrubbing but never advance time on their own.
		return false;
	}

	// The whole point of the early-out: a paused sky with no transition costs nothing.
	return !FMath::IsNearlyZero(SkyState.TimeFlowRate) || WeatherTransitionDuration > 0.f;
}

TStatId UArchSkySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UArchSkySubsystem, STATGROUP_Tickables);
}

UWorld* UArchSkySubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

UArchSkySubsystem* UArchSkySubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UArchSkySubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------------------
// Defaults and asset discovery
// ---------------------------------------------------------------------------------------

void UArchSkySubsystem::DiscoverPresetAssets()
{
	const UArchSkySettings* Settings = UArchSkySettings::Get();
	if (!Settings)
	{
		return;
	}

	// The optional curated city library is a direct soft reference, so it needs no scan.
	if (!Settings->LocationLibrary.IsNull())
	{
		LocationLibraryAsset = Settings->LocationLibrary.LoadSynchronous();
		if (!LocationLibraryAsset)
		{
			UE_LOG(LogArchSky, Warning, TEXT("Configured location library '%s' failed to load; using the built-in city list."),
				*Settings->LocationLibrary.ToString());
		}
	}

	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (!AssetManager)
	{
		// Perfectly normal in a commandlet or a bare automation run.
		UE_LOG(LogArchSky, Verbose, TEXT("No AssetManager available; using built-in weather presets only."));
		return;
	}

	TArray<FString> ScanPaths;
	ScanPaths.Reserve(Settings->PresetScanPaths.Num());
	for (const FDirectoryPath& Directory : Settings->PresetScanPaths)
	{
		if (!Directory.Path.IsEmpty())
		{
			ScanPaths.Add(Directory.Path);
		}
	}

	if (ScanPaths.IsEmpty())
	{
		return;
	}

	AssetManager->ScanPathsForPrimaryAssets(
		UArchWeatherPreset::PrimaryAssetType, ScanPaths, UArchWeatherPreset::StaticClass(),
		/*bHasBlueprintClasses*/ false, /*bIsEditorOnly*/ false, /*bForceSynchronousScan*/ true);

	TArray<FPrimaryAssetId> PresetIds;
	AssetManager->GetPrimaryAssetIdList(UArchWeatherPreset::PrimaryAssetType, PresetIds);

	// ARCH NOTE: this is a synchronous load, which we would normally refuse to do at world
	// init. It is justified here because a weather preset is a few hundred bytes of floats
	// and the sky must be correct on the very first rendered frame - an async load would
	// show one frame of default weather. Everything heavy inside a preset (thumbnail,
	// ambient loop, thunder cue) is behind TSoftObjectPtr and is NOT pulled in by this.
	for (const FPrimaryAssetId& PresetId : PresetIds)
	{
		const FSoftObjectPath AssetPath = AssetManager->GetPrimaryAssetPath(PresetId);
		UArchWeatherPreset* Preset = Cast<UArchWeatherPreset>(AssetPath.TryLoad());

		if (!IsValid(Preset))
		{
			UE_LOG(LogArchSky, Warning, TEXT("Weather preset '%s' could not be loaded."), *AssetPath.ToString());
			continue;
		}

		const FName Id = Preset->PresetId.IsNone() ? Preset->GetFName() : Preset->PresetId;
		if (WeatherPresetAssets.Contains(Id))
		{
			UE_LOG(LogArchSky, Warning,
				TEXT("Duplicate weather PresetId '%s'; keeping the first and ignoring '%s'."),
				*Id.ToString(), *Preset->GetPathName());
			continue;
		}

		WeatherPresetAssets.Add(Id, Preset);
	}
}

void UArchSkySubsystem::ApplyProjectDefaults()
{
	const UArchSkySettings* Settings = UArchSkySettings::Get();

	if (Settings)
	{
		SkyState.Year = Settings->DefaultYear;
		SkyState.DayOfYear = Settings->DefaultDayOfYear;
		SkyState.TimeOfDayHours = Settings->DefaultTimeOfDayHours;
		SkyState.WeatherPresetA = Settings->DefaultWeatherPresetId;
		SkyState.WeatherPresetB = NAME_None;
		SkyState.WeatherBlendAlpha = 0.f;

		// Resolution order: curated asset library, then the compiled-in city list, then
		// the explicitly configured fallback. The asset wins so a project can retune a
		// city's coordinates without touching code.
		bool bResolvedLocation = false;

		if (LocationLibraryAsset)
		{
			if (const FArchLocationEntry* AssetEntry = LocationLibraryAsset->FindLocation(Settings->DefaultLocationId))
			{
				SkyState.Location = AssetEntry->Location;
				bResolvedLocation = true;
			}
		}

		if (!bResolvedLocation)
		{
			bool bFoundBuiltIn = false;
			const FArchLocationEntry BuiltIn = UArchLocationLibrary::GetBuiltInLocation(Settings->DefaultLocationId, bFoundBuiltIn);
			if (bFoundBuiltIn)
			{
				SkyState.Location = BuiltIn.Location;
				bResolvedLocation = true;
			}
		}

		if (!bResolvedLocation)
		{
			UE_LOG(LogArchSky, Warning,
				TEXT("Default location '%s' is not in the library; falling back to the configured FallbackLocation."),
				*Settings->DefaultLocationId.ToString());
			SkyState.Location = Settings->FallbackLocation;
		}
	}

	SkyState.Sanitise();

	// Resolve weather once so the first frame has real parameters rather than struct defaults.
	if (!ResolveWeatherParams(SkyState.WeatherPresetA, ResolvedWeatherA))
	{
		UE_LOG(LogArchSky, Warning, TEXT("Default weather preset '%s' is unknown; using built-in 'Clear'."),
			*SkyState.WeatherPresetA.ToString());
		SkyState.WeatherPresetA = TEXT("Clear");
		ResolveWeatherParams(SkyState.WeatherPresetA, ResolvedWeatherA);
	}
	ResolvedWeatherB = ResolvedWeatherA;

	PreviousDayOfYear = SkyState.DayOfYear;
	RefreshDayInfo();
	RefreshDerivedState(/*bForceFullRefresh*/ true);

	// Seed the edge detectors so the first tick does not fire a spurious sunrise.
	bPreviousAboveHorizon = CachedSolarPosition.bIsAboveHorizon;
	PreviousSolarAltitude = CachedSolarPosition.TrueAltitudeDegrees;
	CachedTimePhase = ArchSolarMath::ClassifyTimePhase(CachedSolarPosition.AltitudeDegrees, true);
}

// ---------------------------------------------------------------------------------------
// Derived state
// ---------------------------------------------------------------------------------------

void UArchSkySubsystem::RefreshDayInfo()
{
	CachedSolarDayInfo = ArchSolarMath::CalculateSolarDayInfo(SkyState.Location, SkyState.ToLocalDateTime());
}

void UArchSkySubsystem::RefreshDerivedState(bool bForceFullRefresh)
{
	if (bForceFullRefresh)
	{
		const FDateTime LocalTime = SkyState.ToLocalDateTime();
		CachedSolarPosition = ArchSolarMath::CalculateSolarPosition(SkyState.Location, LocalTime);
		CachedLunarPosition = ArchMoonMath::CalculateMoonPosition(SkyState.Location, LocalTime);
	}

	// Weather: skip the blend entirely at the endpoints, which is where it spends most of
	// its life. FArchWeatherParams::Blend already fast-paths this, but avoiding the copy
	// matters when this runs every frame.
	if (SkyState.WeatherPresetB.IsNone() || SkyState.WeatherBlendAlpha <= 0.f)
	{
		CachedWeatherParams = ResolvedWeatherA;
	}
	else if (SkyState.WeatherBlendAlpha >= 1.f)
	{
		CachedWeatherParams = ResolvedWeatherB;
	}
	else
	{
		CachedWeatherParams = FArchWeatherParams::Blend(ResolvedWeatherA, ResolvedWeatherB, SkyState.WeatherBlendAlpha);
	}

	DetectAndBroadcastTransitions();

	OnSkyStateChanged.Broadcast(SkyState);
}

void UArchSkySubsystem::DetectAndBroadcastTransitions()
{
	// --- Day rolled ---
	if (PreviousDayOfYear >= 0 && SkyState.DayOfYear != PreviousDayOfYear)
	{
		PreviousDayOfYear = SkyState.DayOfYear;
		RefreshDayInfo();
		OnDayRolled.Broadcast(SkyState.DayOfYear);
	}

	// --- Horizon crossings ---
	const bool bAboveHorizon = CachedSolarPosition.bIsAboveHorizon;
	if (bAboveHorizon != bPreviousAboveHorizon)
	{
		if (bAboveHorizon)
		{
			OnSunrise.Broadcast(SkyState.TimeOfDayHours);
		}
		else
		{
			OnSunset.Broadcast(SkyState.TimeOfDayHours);
		}
		bPreviousAboveHorizon = bAboveHorizon;
	}

	// --- Photographic phase ---
	// "Rising" is decided by comparing altitudes across the refresh rather than by the
	// hour angle, so it stays correct when the user scrubs the time slider backwards.
	const bool bIsRising = CachedSolarPosition.TrueAltitudeDegrees >= PreviousSolarAltitude;
	PreviousSolarAltitude = CachedSolarPosition.TrueAltitudeDegrees;

	const EArchTimePhase NewPhase = ArchSolarMath::ClassifyTimePhase(CachedSolarPosition.AltitudeDegrees, bIsRising);
	if (NewPhase != CachedTimePhase)
	{
		const EArchTimePhase OldPhase = CachedTimePhase;
		CachedTimePhase = NewPhase;
		OnTimePhaseChanged.Broadcast(OldPhase, NewPhase);
	}
}

// ---------------------------------------------------------------------------------------
// Time advancement
// ---------------------------------------------------------------------------------------

void UArchSkySubsystem::AdvanceTime(float DeltaSeconds)
{
	const float DeltaHours = SkyState.TimeFlowRate * DeltaSeconds;
	const float RawTime = SkyState.TimeOfDayHours + DeltaHours;

	if (SkyState.bAutoAdvanceDate)
	{
		// Handles multi-day jumps from an extreme flow rate and a long hitch, and runs
		// backwards correctly for a negative flow rate.
		const int32 DayDelta = FMath::FloorToInt32(RawTime / 24.f);
		if (DayDelta != 0)
		{
			int32 NewDayOfYear = SkyState.DayOfYear + DayDelta;

			// Roll across the year boundary in either direction.
			while (NewDayOfYear > ArchTimeCalendar::DaysInYear(SkyState.Year))
			{
				NewDayOfYear -= ArchTimeCalendar::DaysInYear(SkyState.Year);
				SkyState.Year += 1;
			}
			while (NewDayOfYear < 1)
			{
				SkyState.Year -= 1;
				NewDayOfYear += ArchTimeCalendar::DaysInYear(SkyState.Year);
			}

			SkyState.DayOfYear = NewDayOfYear;
		}
	}

	SkyState.TimeOfDayHours = ArchTimeCalendar::WrapHours(RawTime);
	SkyState.Sanitise();
}

void UArchSkySubsystem::AdvanceWeatherTransition(float DeltaSeconds)
{
	WeatherTransitionElapsed += DeltaSeconds;

	if (WeatherTransitionElapsed >= WeatherTransitionDuration)
	{
		// Land exactly on the destination and collapse the blend so subsequent frames take
		// the cheap endpoint path in RefreshDerivedState.
		const FName From = SkyState.WeatherPresetA;
		const FName To = SkyState.WeatherPresetB;

		SkyState.WeatherPresetA = To;
		SkyState.WeatherPresetB = NAME_None;
		SkyState.WeatherBlendAlpha = 0.f;
		ResolvedWeatherA = ResolvedWeatherB;

		WeatherTransitionDuration = 0.f;
		WeatherTransitionElapsed = 0.f;

		OnWeatherTransitionCompleted.Broadcast(From, To);
		return;
	}

	// Smoothstep rather than linear: a linear weather blend has a visible "kink" at both
	// ends where the rate of change starts and stops abruptly.
	const float Linear = FMath::Clamp(WeatherTransitionElapsed / WeatherTransitionDuration, 0.f, 1.f);
	SkyState.WeatherBlendAlpha = FMath::SmoothStep(0.f, 1.f, Linear);
}

// ---------------------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------------------

bool UArchSkySubsystem::ShouldRejectLocalMutation(const TCHAR* SetterName) const
{
	if (bApplyingReplicatedState)
	{
		return false;
	}

	const UArchSkySettings* Settings = UArchSkySettings::Get();
	if (!Settings || !Settings->bEnableReplication || Settings->bAllowClientTimeControl)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogArchSky, Warning,
			TEXT("%s was called on a client while bAllowClientTimeControl is false. The server owns the sky; ignoring."),
			SetterName);
		return true;
	}

	return false;
}

void UArchSkySubsystem::MarkDiscontinuity()
{
	// Wraps naturally at 255; clients compare for inequality, never ordering.
	++DiscontinuityCounter;
}

void UArchSkySubsystem::SetTimeOfDay(float Hours)
{
	if (ShouldRejectLocalMutation(TEXT("SetTimeOfDay")))
	{
		return;
	}

	SkyState.TimeOfDayHours = ArchTimeCalendar::WrapHours(Hours);
	SkyState.Sanitise();
	MarkDiscontinuity();
	RefreshDerivedState(/*bForceFullRefresh*/ true);
}

void UArchSkySubsystem::AddTimeOfDay(float DeltaHours)
{
	if (ShouldRejectLocalMutation(TEXT("AddTimeOfDay")))
	{
		return;
	}

	// Reuse the flow-rate path so date rolling behaves identically whether time is
	// nudged by a button or advanced by the clock.
	const float SavedFlowRate = SkyState.TimeFlowRate;
	SkyState.TimeFlowRate = 1.f;
	AdvanceTime(DeltaHours);
	SkyState.TimeFlowRate = SavedFlowRate;

	MarkDiscontinuity();
	RefreshDerivedState(/*bForceFullRefresh*/ true);
}

void UArchSkySubsystem::SetDayOfYear(int32 Day)
{
	if (ShouldRejectLocalMutation(TEXT("SetDayOfYear")))
	{
		return;
	}

	SkyState.DayOfYear = FMath::Clamp(Day, 1, ArchTimeCalendar::DaysInYear(SkyState.Year));
	SkyState.Sanitise();
	MarkDiscontinuity();
	RefreshDayInfo();
	PreviousDayOfYear = SkyState.DayOfYear;
	RefreshDerivedState(/*bForceFullRefresh*/ true);
}

void UArchSkySubsystem::SetDateFromGregorian(int32 InYear, int32 Month, int32 Day)
{
	if (ShouldRejectLocalMutation(TEXT("SetDateFromGregorian")))
	{
		return;
	}

	SkyState.Year = FMath::Clamp(InYear, 1900, 2200);
	SkyState.DayOfYear = ArchTimeCalendar::MonthDayToDayOfYear(SkyState.Year, Month, Day);
	SkyState.Sanitise();
	MarkDiscontinuity();
	RefreshDayInfo();
	PreviousDayOfYear = SkyState.DayOfYear;
	RefreshDerivedState(/*bForceFullRefresh*/ true);
}

void UArchSkySubsystem::SetDateFromJalali(int32 JalaliYear, int32 JalaliMonth, int32 JalaliDay)
{
	if (ShouldRejectLocalMutation(TEXT("SetDateFromJalali")))
	{
		return;
	}

	const FArchJalaliDate JalaliDate(JalaliYear, JalaliMonth, JalaliDay);

	int32 GregorianYear = 0;
	int32 GregorianMonth = 0;
	int32 GregorianDay = 0;

	if (!ArchJalaliCalendar::JalaliToGregorian(JalaliDate, GregorianYear, GregorianMonth, GregorianDay))
	{
		// Validated-and-logged, never checked: this comes straight from a text box.
		UE_LOG(LogArchSky, Warning, TEXT("Ignoring invalid Jalali date %d/%d/%d."),
			JalaliYear, JalaliMonth, JalaliDay);
		return;
	}

	SetDateFromGregorian(GregorianYear, GregorianMonth, GregorianDay);
}

void UArchSkySubsystem::SetTimeFlowRate(float HoursPerSecond)
{
	if (ShouldRejectLocalMutation(TEXT("SetTimeFlowRate")))
	{
		return;
	}

	SkyState.TimeFlowRate = FMath::Clamp(HoursPerSecond, -600.f, 600.f);
	if (!FMath::IsNearlyZero(SkyState.TimeFlowRate))
	{
		FlowRateBeforePause = SkyState.TimeFlowRate;
	}

	MarkDiscontinuity();
	RefreshDerivedState(/*bForceFullRefresh*/ false);
}

void UArchSkySubsystem::PauseTime()
{
	if (FMath::IsNearlyZero(SkyState.TimeFlowRate))
	{
		return;
	}

	FlowRateBeforePause = SkyState.TimeFlowRate;
	SetTimeFlowRate(0.f);
}

void UArchSkySubsystem::ResumeTime()
{
	// A resume that restored 0 would be a silent no-op, so fall back to a sane rate.
	SetTimeFlowRate(FMath::IsNearlyZero(FlowRateBeforePause) ? 1.f : FlowRateBeforePause);
}

void UArchSkySubsystem::ToggleTimePause()
{
	IsTimePaused() ? ResumeTime() : PauseTime();
}

bool UArchSkySubsystem::IsTimePaused() const
{
	return FMath::IsNearlyZero(SkyState.TimeFlowRate);
}

void UArchSkySubsystem::SetLocation(const FArchGeoLocation& NewLocation)
{
	if (ShouldRejectLocalMutation(TEXT("SetLocation")))
	{
		return;
	}

	SkyState.Location = NewLocation;
	SkyState.Sanitise();
	MarkDiscontinuity();
	RefreshDayInfo();
	RefreshDerivedState(/*bForceFullRefresh*/ true);
}

bool UArchSkySubsystem::SetLocationPreset(FName CityId)
{
	if (ShouldRejectLocalMutation(TEXT("SetLocationPreset")))
	{
		return false;
	}

	// Asset library first so a project can override a built-in city's coordinates.
	if (LocationLibraryAsset)
	{
		if (const FArchLocationEntry* Entry = LocationLibraryAsset->FindLocation(CityId))
		{
			SetLocation(Entry->Location);
			return true;
		}
	}

	bool bFound = false;
	const FArchLocationEntry BuiltIn = UArchLocationLibrary::GetBuiltInLocation(CityId, bFound);
	if (!bFound)
	{
		UE_LOG(LogArchSky, Warning, TEXT("Unknown location preset '%s'. The location is unchanged."), *CityId.ToString());
		return false;
	}

	SetLocation(BuiltIn.Location);
	return true;
}

void UArchSkySubsystem::SetNorthOffset(float Degrees)
{
	if (ShouldRejectLocalMutation(TEXT("SetNorthOffset")))
	{
		return;
	}

	SkyState.NorthOffsetDegrees = Degrees;
	SkyState.Sanitise();
	MarkDiscontinuity();

	// The north offset does not change any astronomical quantity - only how the scene is
	// oriented under the sky - so there is nothing to recompute, only to re-broadcast.
	RefreshDerivedState(/*bForceFullRefresh*/ false);
}

bool UArchSkySubsystem::SetWeatherPreset(FName PresetId, float TransitionSeconds)
{
	if (ShouldRejectLocalMutation(TEXT("SetWeatherPreset")))
	{
		return false;
	}

	FArchWeatherParams Destination;
	if (!ResolveWeatherParams(PresetId, Destination))
	{
		UE_LOG(LogArchSky, Warning, TEXT("Unknown weather preset '%s'. The weather is unchanged."), *PresetId.ToString());
		return false;
	}

	if (PresetId == SkyState.WeatherPresetA && SkyState.WeatherPresetB.IsNone())
	{
		// Already there, and no transition to interrupt.
		return true;
	}

	const FName From = SkyState.WeatherPresetB.IsNone() ? SkyState.WeatherPresetA : SkyState.WeatherPresetB;

	// Interrupting a running transition: freeze the CURRENT blended parameters as the new
	// source so the second transition starts from what is on screen, not from where the
	// first one began. Without this, retargeting mid-blend visibly snaps backwards.
	ResolvedWeatherA = CachedWeatherParams;
	ResolvedWeatherB = Destination;

	SkyState.WeatherPresetA = From;
	SkyState.WeatherPresetB = PresetId;
	SkyState.WeatherBlendAlpha = 0.f;

	TransitionSeconds = FMath::Max(TransitionSeconds, 0.f);
	if (TransitionSeconds <= 0.f)
	{
		SkyState.WeatherPresetA = PresetId;
		SkyState.WeatherPresetB = NAME_None;
		SkyState.WeatherBlendAlpha = 0.f;
		ResolvedWeatherA = Destination;
		ResolvedWeatherB = Destination;
		WeatherTransitionDuration = 0.f;
		WeatherTransitionElapsed = 0.f;

		MarkDiscontinuity();
		RefreshDerivedState(/*bForceFullRefresh*/ false);
		OnWeatherTransitionCompleted.Broadcast(From, PresetId);
		return true;
	}

	WeatherTransitionDuration = TransitionSeconds;
	WeatherTransitionElapsed = 0.f;

	OnWeatherTransitionStarted.Broadcast(From, PresetId);
	RefreshDerivedState(/*bForceFullRefresh*/ false);
	return true;
}

bool UArchSkySubsystem::SetWeatherBlend(FName A, FName B, float Alpha)
{
	if (ShouldRejectLocalMutation(TEXT("SetWeatherBlend")))
	{
		return false;
	}

	FArchWeatherParams ParamsA;
	if (!ResolveWeatherParams(A, ParamsA))
	{
		UE_LOG(LogArchSky, Warning, TEXT("Unknown weather preset '%s' in SetWeatherBlend."), *A.ToString());
		return false;
	}

	FArchWeatherParams ParamsB = ParamsA;
	if (!B.IsNone() && !ResolveWeatherParams(B, ParamsB))
	{
		UE_LOG(LogArchSky, Warning, TEXT("Unknown weather preset '%s' in SetWeatherBlend."), *B.ToString());
		return false;
	}

	// An explicit blend is an authored state, not an animation: cancel any transition.
	WeatherTransitionDuration = 0.f;
	WeatherTransitionElapsed = 0.f;

	ResolvedWeatherA = ParamsA;
	ResolvedWeatherB = ParamsB;

	SkyState.WeatherPresetA = A;
	SkyState.WeatherPresetB = B;
	SkyState.WeatherBlendAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	SkyState.Sanitise();

	MarkDiscontinuity();
	RefreshDerivedState(/*bForceFullRefresh*/ false);
	return true;
}

bool UArchSkySubsystem::ResolveWeatherParams(FName PresetId, FArchWeatherParams& OutParams) const
{
	if (PresetId.IsNone())
	{
		return false;
	}

	// Assets win over built-ins, so a project can retune "Overcast" without touching code.
	if (const TObjectPtr<UArchWeatherPreset>* Found = WeatherPresetAssets.Find(PresetId))
	{
		if (IsValid(*Found))
		{
			OutParams = (*Found)->Params;
			return true;
		}
	}

	bool bFoundBuiltIn = false;
	const FArchWeatherParams BuiltIn = UArchWeatherLibrary::GetBuiltInWeatherParams(PresetId, bFoundBuiltIn);
	if (bFoundBuiltIn)
	{
		OutParams = BuiltIn;
		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------------------
// Jumps
// ---------------------------------------------------------------------------------------

void UArchSkySubsystem::JumpToSunrise()
{
	if (CachedSolarDayInfo.bPolarDay || CachedSolarDayInfo.bPolarNight)
	{
		UE_LOG(LogArchSky, Log, TEXT("JumpToSunrise: no sunrise occurs at this latitude on day %d."), SkyState.DayOfYear);
		return;
	}
	SetTimeOfDay(CachedSolarDayInfo.SunriseHours);
}

void UArchSkySubsystem::JumpToSolarNoon()
{
	// Solar noon always exists, even on a polar night.
	SetTimeOfDay(CachedSolarDayInfo.SolarNoonHours);
}

void UArchSkySubsystem::JumpToSunset()
{
	if (CachedSolarDayInfo.bPolarDay || CachedSolarDayInfo.bPolarNight)
	{
		UE_LOG(LogArchSky, Log, TEXT("JumpToSunset: no sunset occurs at this latitude on day %d."), SkyState.DayOfYear);
		return;
	}
	SetTimeOfDay(CachedSolarDayInfo.SunsetHours);
}

void UArchSkySubsystem::JumpToGoldenHour(bool bEvening)
{
	if (CachedSolarDayInfo.bPolarDay || CachedSolarDayInfo.bPolarNight)
	{
		UE_LOG(LogArchSky, Log, TEXT("JumpToGoldenHour: the sun does not cross the horizon on day %d."), SkyState.DayOfYear);
		return;
	}

	// The golden hour runs from the horizon to +6 degrees. Rather than solving for the
	// 6-degree crossing analytically we take the midpoint of the horizon crossing and a
	// point a fixed fraction of the half-day later - accurate to a couple of minutes and
	// immune to the polar edge cases the analytic solve would need extra branches for.
	const float HalfDay = CachedSolarDayInfo.DayLengthHours * 0.5f;
	const float GoldenSpan = FMath::Min(1.f, HalfDay * 0.35f);

	SetTimeOfDay(bEvening
		? CachedSolarDayInfo.SunsetHours - GoldenSpan * 0.5f
		: CachedSolarDayInfo.SunriseHours + GoldenSpan * 0.5f);
}

void UArchSkySubsystem::JumpToBlueHour(bool bEvening)
{
	// The blue hour sits between civil twilight and the horizon; its midpoint is the
	// midpoint of those two reported instants, which is exact by construction.
	const float Target = bEvening
		? (CachedSolarDayInfo.SunsetHours + CachedSolarDayInfo.CivilTwilightEndHours) * 0.5f
		: (CachedSolarDayInfo.SunriseHours + CachedSolarDayInfo.CivilTwilightStartHours) * 0.5f;

	SetTimeOfDay(Target);
}

void UArchSkySubsystem::SetSolsticePreset(EArchSolsticePreset Preset)
{
	SetDayOfYear(ArchTimeCalendar::GetSolsticeDayOfYear(SkyState.Year, Preset));
}

void UArchSkySubsystem::ApplyStatePreset(const UArchSkyStatePreset* Preset)
{
	if (!IsValid(Preset))
	{
		UE_LOG(LogArchSky, Warning, TEXT("ApplyStatePreset was given a null preset; ignoring."));
		return;
	}

	FArchSkyState NewState = SkyState;

	NewState.Year = Preset->State.Year;
	NewState.DayOfYear = Preset->State.DayOfYear;
	NewState.TimeOfDayHours = Preset->State.TimeOfDayHours;
	NewState.TimeFlowRate = Preset->State.TimeFlowRate;
	NewState.bAutoAdvanceDate = Preset->State.bAutoAdvanceDate;

	if (Preset->bApplyLocation)
	{
		NewState.Location = Preset->State.Location;
	}
	if (Preset->bApplyNorthOffset)
	{
		NewState.NorthOffsetDegrees = Preset->State.NorthOffsetDegrees;
	}
	if (Preset->bApplyWeather)
	{
		NewState.WeatherPresetA = Preset->State.WeatherPresetA;
		NewState.WeatherPresetB = Preset->State.WeatherPresetB;
		NewState.WeatherBlendAlpha = Preset->State.WeatherBlendAlpha;
	}

	ApplySkyState(NewState);
}

void UArchSkySubsystem::ApplySkyState(const FArchSkyState& NewState)
{
	if (ShouldRejectLocalMutation(TEXT("ApplySkyState")))
	{
		return;
	}

	SkyState = NewState;
	SkyState.Sanitise();

	// A wholesale state replacement invalidates every cached endpoint.
	WeatherTransitionDuration = 0.f;
	WeatherTransitionElapsed = 0.f;

	if (!ResolveWeatherParams(SkyState.WeatherPresetA, ResolvedWeatherA))
	{
		UE_LOG(LogArchSky, Warning, TEXT("State references unknown weather preset '%s'; falling back to 'Clear'."),
			*SkyState.WeatherPresetA.ToString());
		SkyState.WeatherPresetA = TEXT("Clear");
		ResolveWeatherParams(SkyState.WeatherPresetA, ResolvedWeatherA);
	}

	if (!SkyState.WeatherPresetB.IsNone() && !ResolveWeatherParams(SkyState.WeatherPresetB, ResolvedWeatherB))
	{
		SkyState.WeatherPresetB = NAME_None;
		SkyState.WeatherBlendAlpha = 0.f;
		ResolvedWeatherB = ResolvedWeatherA;
	}

	MarkDiscontinuity();
	RefreshDayInfo();
	PreviousDayOfYear = SkyState.DayOfYear;
	RefreshDerivedState(/*bForceFullRefresh*/ true);
}

// ---------------------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------------------

FText UArchSkySubsystem::GetFormattedTimeString(bool b24Hour) const
{
	return ArchTimeCalendar::FormatTimeOfDay(SkyState.TimeOfDayHours, b24Hour);
}

FArchJalaliDate UArchSkySubsystem::GetJalaliDate() const
{
	int32 Month = 1;
	int32 Day = 1;
	ArchTimeCalendar::DayOfYearToMonthDay(SkyState.Year, SkyState.DayOfYear, Month, Day);

	return ArchJalaliCalendar::GregorianToJalali(SkyState.Year, Month, Day);
}

FText UArchSkySubsystem::GetFormattedDateString(EArchCalendarType CalendarType) const
{
	if (CalendarType == EArchCalendarType::Jalali)
	{
		return UArchJalaliCalendarLibrary::FormatJalaliDate(GetJalaliDate(), /*bPersianScript*/ false);
	}

	int32 Month = 1;
	int32 Day = 1;
	ArchTimeCalendar::DayOfYearToMonthDay(SkyState.Year, SkyState.DayOfYear, Month, Day);

	FNumberFormattingOptions NoGrouping;
	NoGrouping.UseGrouping = false;

	FFormatNamedArguments Args;
	Args.Add(TEXT("Day"), FText::AsNumber(Day, &NoGrouping));
	Args.Add(TEXT("Month"), ArchTimeCalendar::GetGregorianMonthName(Month));
	Args.Add(TEXT("Year"), FText::AsNumber(SkyState.Year, &NoGrouping));

	return FText::Format(LOCTEXT("GregorianDateFormat", "{Day} {Month} {Year}"), Args);
}

EArchSeason UArchSkySubsystem::GetSeason() const
{
	return ArchTimeCalendar::GetSeason(SkyState.Year, SkyState.DayOfYear, SkyState.Location.IsSouthernHemisphere());
}

float UArchSkySubsystem::GetSeasonBlend01() const
{
	return ArchTimeCalendar::GetSeasonBlend01(SkyState.Year, SkyState.DayOfYear, SkyState.Location.IsSouthernHemisphere());
}

bool UArchSkySubsystem::IsGoldenHour() const
{
	return CachedTimePhase == EArchTimePhase::GoldenHourMorning
		|| CachedTimePhase == EArchTimePhase::GoldenHourEvening;
}

bool UArchSkySubsystem::IsBlueHour() const
{
	return CachedTimePhase == EArchTimePhase::CivilTwilight;
}

float UArchSkySubsystem::GetShadowLengthMultiplier() const
{
	// Geometric altitude, not apparent: an engine shadow is cast along the true direction.
	return static_cast<float>(ArchSolarMath::ShadowLengthMultiplier(CachedSolarPosition.TrueAltitudeDegrees));
}

TArray<FName> UArchSkySubsystem::GetAvailableWeatherPresetIds() const
{
	// Built-ins first, in their authored order, then any asset-only presets appended.
	TArray<FName> Ids = UArchWeatherLibrary::GetBuiltInWeatherPresetIds();

	TArray<FName> AssetIds;
	WeatherPresetAssets.GenerateKeyArray(AssetIds);

	// Stable, locale-independent ordering for the ones we did not author.
	AssetIds.Sort(FNameLexicalLess());

	for (const FName& AssetId : AssetIds)
	{
		Ids.AddUnique(AssetId);
	}

	return Ids;
}

FText UArchSkySubsystem::GetWeatherPresetDisplayName(FName PresetId) const
{
	if (const TObjectPtr<UArchWeatherPreset>* Found = WeatherPresetAssets.Find(PresetId))
	{
		if (IsValid(*Found) && !(*Found)->DisplayName.IsEmpty())
		{
			return (*Found)->DisplayName;
		}
	}

	// LOCALISATION: built-in preset ids have no authored FText, so we surface the id.
	// A project that wants translated names ships preset assets with DisplayName filled in.
	return FText::FromName(PresetId);
}

TArray<FArchLocationEntry> UArchSkySubsystem::GetAvailableLocations() const
{
	TArray<FArchLocationEntry> Locations = UArchLocationLibrary::GetBuiltInLocations();

	if (LocationLibraryAsset)
	{
		for (const FArchLocationEntry& Entry : LocationLibraryAsset->Locations)
		{
			const int32 ExistingIndex = Locations.IndexOfByPredicate(
				[&Entry](const FArchLocationEntry& Candidate) { return Candidate.CityId == Entry.CityId; });

			if (ExistingIndex != INDEX_NONE)
			{
				// Asset entries override built-ins of the same id, in place, so the
				// dropdown order stays stable when a project retunes one city.
				Locations[ExistingIndex] = Entry;
			}
			else
			{
				Locations.Add(Entry);
			}
		}
	}

	return Locations;
}

float UArchSkySubsystem::GetWeatherTransitionRemainingSeconds() const
{
	return (WeatherTransitionDuration > 0.f)
		? FMath::Max(0.f, WeatherTransitionDuration - WeatherTransitionElapsed)
		: 0.f;
}

FArchSolarPosition UArchSkySubsystem::GetSolarPositionAtHour(float LocalHours) const
{
	return GetSolarPositionAtDayAndHour(SkyState.DayOfYear, LocalHours);
}

FArchSolarPosition UArchSkySubsystem::GetSolarPositionAtDayAndHour(int32 InDayOfYear, float LocalHours) const
{
	const FDateTime SampleTime = ArchTimeCalendar::MakeDateTime(
		SkyState.Year,
		FMath::Clamp(InDayOfYear, 1, ArchTimeCalendar::DaysInYear(SkyState.Year)),
		LocalHours);

	return ArchSolarMath::CalculateSolarPosition(SkyState.Location, SampleTime);
}

// ---------------------------------------------------------------------------------------
// Networking
// ---------------------------------------------------------------------------------------

bool UArchSkySubsystem::IsClientDriven() const
{
	const UArchSkySettings* Settings = UArchSkySettings::Get();
	if (!Settings || !Settings->bEnableReplication)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_Client;
}

FArchSkyReplicatedState UArchSkySubsystem::MakeReplicatedState() const
{
	return FArchSkyReplicatedState::FromSkyState(SkyState, DiscontinuityCounter);
}

void UArchSkySubsystem::ApplyReplicatedState(const FArchSkyReplicatedState& Replicated)
{
	TGuardValue<bool> ApplyGuard(bApplyingReplicatedState, true);

	const bool bDiscontinuous = (Replicated.DiscontinuityCounter != DiscontinuityCounter);
	DiscontinuityCounter = Replicated.DiscontinuityCounter;

	const int32 PreviousDay = SkyState.DayOfYear;
	const FArchGeoLocation PreservedLocation = SkyState.Location;
	const float PreservedNorthOffset = SkyState.NorthOffsetDegrees;

	if (bDiscontinuous)
	{
		// The server jumped: snap, because interpolating through a jump would sweep the
		// sun across the sky and produce a shadow study nobody asked for.
		Replicated.ApplyToSkyState(SkyState);
	}
	else
	{
		// Ordinary drift: reconcile the clock rather than overwrite it, so a client whose
		// prediction is a few hundred milliseconds ahead eases back instead of stuttering.
		const float ClientTimeOfDay = SkyState.TimeOfDayHours;

		FArchSkyState Target = SkyState;
		Replicated.ApplyToSkyState(Target);

		// Signed shortest-path difference around the 24 h wrap, in hours.
		const float Delta = FMath::Fmod(Target.TimeOfDayHours - ClientTimeOfDay + 36.f, 24.f) - 12.f;

		// How much of the remaining error to absorb per update. At the default 2 Hz this
		// converges in about a second, which is below the threshold of noticing.
		constexpr float ReconcileRate = 0.35f;

		// A large disagreement is a missed packet, not drift, so take the server's value.
		constexpr float SnapThresholdHours = 0.25f;

		SkyState = Target;
		if (FMath::Abs(Delta) < SnapThresholdHours)
		{
			SkyState.TimeOfDayHours = ArchTimeCalendar::WrapHours(ClientTimeOfDay + Delta * ReconcileRate);
		}
	}

	// Location and plan north are never replicated - they are level authoring, identical on
	// every machine - so restore whatever this client already had.
	SkyState.Location = PreservedLocation;
	SkyState.NorthOffsetDegrees = PreservedNorthOffset;
	SkyState.Sanitise();

	if (!ResolveWeatherParams(SkyState.WeatherPresetA, ResolvedWeatherA))
	{
		ResolvedWeatherA = FArchWeatherParams();
	}
	if (!SkyState.WeatherPresetB.IsNone() && !ResolveWeatherParams(SkyState.WeatherPresetB, ResolvedWeatherB))
	{
		ResolvedWeatherB = ResolvedWeatherA;
	}

	if (SkyState.DayOfYear != PreviousDay)
	{
		RefreshDayInfo();
	}

	RefreshDerivedState(/*bForceFullRefresh*/ true);
}

void UArchSkySubsystem::NotifyDirectorRegistered(bool bRegistered)
{
	bDirectorRegistered = bRegistered;
}

#undef LOCTEXT_NAMESPACE
