// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/ArchSkyState.h"
#include "Data/ArchLocationPreset.h"
#include "Data/ArchWeatherPreset.h"
#include "Math/ArchMoonMath.h"
#include "Math/ArchSolarMath.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Util/ArchJalaliCalendar.h"

#include "ArchSkySubsystem.generated.h"

class UArchSkyStatePreset;

/** Broadcast whenever any field of the sky state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnArchSkyStateChanged, const FArchSkyState&, NewState);

/** Broadcast when the sun crosses one of the photographic phase boundaries. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnArchTimePhaseChanged, EArchTimePhase, OldPhase, EArchTimePhase, NewPhase);

/** Broadcast at the start and end of a weather transition. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnArchWeatherChanged, FName, FromPresetId, FName, ToPresetId);

/** Broadcast when time flows past midnight and the date advances. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnArchDayRolled, int32, NewDayOfYear);

/** Broadcast when the sun crosses the horizon. Carries the local decimal hour of the event. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnArchSunriseSunset, float, LocalHours);

/**
 * The public API of the ArchSky System. Everything - UI, Blueprint, console, save games -
 * talks to the sky through this object, and nothing else.
 *
 * ARCHITECTURE
 * ------------
 * The subsystem owns STATE and MATH. It does not own, reference, or know about a single
 * scene component. When the state changes it broadcasts OnSkyStateChanged; AArchSkyDirector
 * listens and is solely responsible for pushing that state into lights, atmosphere, fog and
 * materials.
 *
 * That decoupling is deliberate and is the reason the widget can never "touch a light":
 * there is no path from here to a UDirectionalLightComponent to follow. It also means the
 * subsystem is fully functional in a level with no Director at all, which is what makes
 * headless shadow-study export and the automation tests possible.
 *
 * THREADING: game thread only. The math it calls is thread-safe, but the state, the
 * delegates and the cached derived values are not.
 */
UCLASS(DisplayName = "ArchSky Subsystem")
class ARCHSKYRUNTIME_API UArchSkySubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// -----------------------------------------------------------------------------------
	// Lifecycle
	// -----------------------------------------------------------------------------------

	//~ Begin USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	//~ End FTickableGameObject

	/**
	 * Convenience accessor. Returns nullptr when WorldContext is invalid or the world has
	 * no subsystem (for example during shutdown). ALWAYS null-check the result.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky", meta = (WorldContext = "WorldContextObject", DisplayName = "Get ArchSky Subsystem"))
	static UArchSkySubsystem* Get(const UObject* WorldContextObject);

	// -----------------------------------------------------------------------------------
	// Setters. Every one validates and clamps; none can put the state into an illegal shape.
	// -----------------------------------------------------------------------------------

	/** Sets the local wall-clock time. Values outside [0,24) wrap; the date does NOT roll. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Time")
	void SetTimeOfDay(float Hours);

	/** Adds to the local time. Rolls the date when bAutoAdvanceDate is set. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Time")
	void AddTimeOfDay(float DeltaHours);

	/** Sets the day of the year, clamped to the length of the current year. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Time")
	void SetDayOfYear(int32 Day);

	/** Sets the date from a Gregorian year/month/day. Out-of-range values are clamped. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Time")
	void SetDateFromGregorian(int32 InYear, int32 Month, int32 Day);

	/** Sets the date from a Jalali (Solar Hijri) year/month/day. Invalid dates are rejected with a warning. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Time")
	void SetDateFromJalali(int32 JalaliYear, int32 JalaliMonth, int32 JalaliDay);

	/** Sets how many simulated hours pass per real second. 0 pauses. Negative runs backwards. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Time")
	void SetTimeFlowRate(float HoursPerSecond);

	/** Stops time, remembering the previous flow rate for ResumeTime. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Time")
	void PauseTime();

	/** Restores the flow rate captured by the last PauseTime. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Time")
	void ResumeTime();

	/** Pauses if running, resumes if paused. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Time")
	void ToggleTimePause();

	/** True when time is not advancing. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Time")
	bool IsTimePaused() const;

	/** Sets the observer position directly. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Location")
	void SetLocation(const FArchGeoLocation& NewLocation);

	/** Selects a city from the location library. Warns and does nothing if the id is unknown. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Location")
	bool SetLocationPreset(FName CityId);

	/** Sets the scene-space yaw at which true north lies. See FArchSkyState::NorthOffsetDegrees. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Location")
	void SetNorthOffset(float Degrees);

	/**
	 * Starts a timed transition to a weather preset.
	 *
	 * @param PresetId            Destination preset. Unknown ids warn and do nothing.
	 * @param TransitionSeconds   Duration. 0 applies instantly.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Weather")
	bool SetWeatherPreset(FName PresetId, float TransitionSeconds = 3.f);

	/** Sets an explicit static blend between two presets, cancelling any running transition. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Weather")
	bool SetWeatherBlend(FName A, FName B, float Alpha);

	/** Jumps to the instant the sun's upper limb clears the horizon. No-op on a polar day. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Jump")
	void JumpToSunrise();

	/** Jumps to the sun's meridian crossing - the highest point of the day. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Jump")
	void JumpToSolarNoon();

	/** Jumps to sunset. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Jump")
	void JumpToSunset();

	/**
	 * Jumps to the middle of the golden hour.
	 * @param bEvening  False for the morning golden hour, true for the evening one.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Jump")
	void JumpToGoldenHour(bool bEvening = true);

	/** Jumps to the middle of the blue hour (sun around -5 degrees). */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Jump")
	void JumpToBlueHour(bool bEvening = true);

	/** Selects the nominal date of a solstice or equinox in the current year. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Jump")
	void SetSolsticePreset(EArchSolsticePreset Preset);

	/** Applies a saved state preset. Null is ignored with a warning. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Presets")
	void ApplyStatePreset(const UArchSkyStatePreset* Preset);

	/** Replaces the entire state at once. Used by save-game load and by replication. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Presets")
	void ApplySkyState(const FArchSkyState& NewState);

	// -----------------------------------------------------------------------------------
	// Getters
	// -----------------------------------------------------------------------------------

	/** The authoritative state. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|State")
	const FArchSkyState& GetSkyState() const { return SkyState; }

	/** Current solar position. Cached; recomputed only when the state actually changed. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|State")
	const FArchSolarPosition& GetSolarPosition() const { return CachedSolarPosition; }

	/** Current lunar position. Cached alongside the solar position. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|State")
	const FArchLunarPosition& GetLunarPosition() const { return CachedLunarPosition; }

	/** Sunrise / sunset / twilight for the current date and location. Cached per day. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|State")
	const FArchSolarDayInfo& GetSolarDayInfo() const { return CachedSolarDayInfo; }

	/** Local clock string, e.g. "06:42" or "6:42 AM". */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Format")
	FText GetFormattedTimeString(bool b24Hour = true) const;

	/** Local date string in the requested calendar. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Format")
	FText GetFormattedDateString(EArchCalendarType CalendarType = EArchCalendarType::Gregorian) const;

	/** The current date expressed in the Jalali calendar. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Format")
	FArchJalaliDate GetJalaliDate() const;

	/** Season, derived from day-of-year and hemisphere. Never stored. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|State")
	EArchSeason GetSeason() const;

	/** Continuous 0..1 seasonal position for materials. 0 = midwinter, 1 = midsummer. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|State")
	float GetSeasonBlend01() const;

	/** Photographic phase of the day, derived from the sun's altitude. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|State")
	EArchTimePhase GetTimePhase() const { return CachedTimePhase; }

	/** True during either golden hour (sun between 0.833 and 6 degrees). */
	UFUNCTION(BlueprintPure, Category = "ArchSky|State")
	bool IsGoldenHour() const;

	/** True during either blue hour (sun between -6 and -4 degrees). */
	UFUNCTION(BlueprintPure, Category = "ArchSky|State")
	bool IsBlueHour() const;

	/** cot(solar altitude): how many times its own height an object's shadow is. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Analysis")
	float GetShadowLengthMultiplier() const;

	/** The weather parameters in force right now, with any running transition applied. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Weather")
	const FArchWeatherParams& GetCurrentWeatherBlended() const { return CachedWeatherParams; }

	/** Every weather preset id available, assets and built-ins merged, in display order. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Weather")
	TArray<FName> GetAvailableWeatherPresetIds() const;

	/** Localised display name for a weather preset id. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Weather")
	FText GetWeatherPresetDisplayName(FName PresetId) const;

	/** Every city available, assets and built-ins merged. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Location")
	TArray<FArchLocationEntry> GetAvailableLocations() const;

	/** True while a timed weather transition is running. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Weather")
	bool IsWeatherTransitionActive() const { return WeatherTransitionDuration > 0.f; }

	/** Seconds remaining in the running weather transition; 0 when none is active. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Weather")
	float GetWeatherTransitionRemainingSeconds() const;

	/**
	 * Solar position at an arbitrary time today, without disturbing the current state.
	 * This is what the editor visualiser uses to draw sun-path arcs.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Analysis")
	FArchSolarPosition GetSolarPositionAtHour(float LocalHours) const;

	/** Solar position on an arbitrary day and hour, without disturbing the current state. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Analysis")
	FArchSolarPosition GetSolarPositionAtDayAndHour(int32 InDayOfYear, float LocalHours) const;

	// -----------------------------------------------------------------------------------
	// Delegates
	// -----------------------------------------------------------------------------------

	/** Fired after every state change, including once per tick while time is flowing. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Events")
	FOnArchSkyStateChanged OnSkyStateChanged;

	/** Fired only when the photographic phase actually changes. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Events")
	FOnArchTimePhaseChanged OnTimePhaseChanged;

	/** Fired when a timed weather transition begins. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Events")
	FOnArchWeatherChanged OnWeatherTransitionStarted;

	/** Fired when a timed weather transition reaches its destination. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Events")
	FOnArchWeatherChanged OnWeatherTransitionCompleted;

	/** Fired when time flows past midnight and the date advances. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Events")
	FOnArchDayRolled OnDayRolled;

	/** Fired when the sun's upper limb crosses the horizon upwards. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Events")
	FOnArchSunriseSunset OnSunrise;

	/** Fired when the sun's upper limb crosses the horizon downwards. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Events")
	FOnArchSunriseSunset OnSunset;

	// -----------------------------------------------------------------------------------
	// Networking support (called by AArchSkyDirector; not intended for Blueprint)
	// -----------------------------------------------------------------------------------

	/** True when this world's sky is driven by a remote server rather than locally. */
	bool IsClientDriven() const;

	/** Applies a replicated packet. Snaps on a discontinuity, otherwise reconciles smoothly. */
	void ApplyReplicatedState(const FArchSkyReplicatedState& Replicated);

	/** Packs the current state for replication. */
	FArchSkyReplicatedState MakeReplicatedState() const;

	/** Incremented whenever a setter causes a jump clients must not interpolate through. */
	uint8 GetDiscontinuityCounter() const { return DiscontinuityCounter; }

	/** Called by the Director once it has registered, so the subsystem stops warning about it. */
	void NotifyDirectorRegistered(bool bRegistered);

	/**
	 * True when a Director has claimed this world. The subsystem works without one - the
	 * state and the math are complete on their own - but nothing will be visible.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|State")
	bool HasSceneDirector() const { return bDirectorRegistered; }

private:
	/** Recomputes cached derived values and broadcasts OnSkyStateChanged. */
	void RefreshDerivedState(bool bForceFullRefresh);

	/** Recomputes only the per-day cache (sunrise, sunset, twilight). */
	void RefreshDayInfo();

	/** Resolves a preset id to parameters: asset first, then built-in, then a warning. */
	bool ResolveWeatherParams(FName PresetId, FArchWeatherParams& OutParams) const;

	/** Loads the weather-preset and location assets the Asset Manager knows about. */
	void DiscoverPresetAssets();

	/** Applies the project defaults from UArchSkySettings. */
	void ApplyProjectDefaults();

	/** Advances time by DeltaSeconds of real time, rolling the date if configured. */
	void AdvanceTime(float DeltaSeconds);

	/** Advances a running weather transition, completing and broadcasting when it lands. */
	void AdvanceWeatherTransition(float DeltaSeconds);

	/** Fires OnSunrise / OnSunset / OnTimePhaseChanged if the relevant boundary was crossed. */
	void DetectAndBroadcastTransitions();

	/** Marks the state as changed by something a client must not interpolate through. */
	void MarkDiscontinuity();

	/** True when a setter should be refused because this is a client without control rights. */
	bool ShouldRejectLocalMutation(const TCHAR* SetterName) const;

	/** The one authoritative state. */
	UPROPERTY(Transient)
	FArchSkyState SkyState;

	/** Derived, recomputed on change. Never serialised. */
	UPROPERTY(Transient)
	FArchSolarPosition CachedSolarPosition;

	UPROPERTY(Transient)
	FArchLunarPosition CachedLunarPosition;

	UPROPERTY(Transient)
	FArchSolarDayInfo CachedSolarDayInfo;

	UPROPERTY(Transient)
	FArchWeatherParams CachedWeatherParams;

	/** Weather endpoints resolved once per transition rather than once per frame. */
	FArchWeatherParams ResolvedWeatherA;
	FArchWeatherParams ResolvedWeatherB;

	/** Weather preset assets discovered at Initialize, keyed by PresetId. */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UArchWeatherPreset>> WeatherPresetAssets;

	/** Optional curated city library from the project settings. */
	UPROPERTY(Transient)
	TObjectPtr<UArchLocationPreset> LocationLibraryAsset;

	/** Photographic phase last broadcast, so OnTimePhaseChanged fires only on a real change. */
	EArchTimePhase CachedTimePhase = EArchTimePhase::Day;

	/** Altitude from the previous refresh, used to tell rising from setting. */
	double PreviousSolarAltitude = 0.0;

	/** Above-horizon flag from the previous refresh, for the sunrise/sunset edges. */
	bool bPreviousAboveHorizon = false;

	/** Day-of-year at the last refresh, for the day-rolled edge. */
	int32 PreviousDayOfYear = -1;

	/** Flow rate captured by PauseTime, restored by ResumeTime. */
	float FlowRateBeforePause = 1.f;

	/** Total and remaining seconds of the running weather transition. 0 = none. */
	float WeatherTransitionDuration = 0.f;
	float WeatherTransitionElapsed = 0.f;

	/** Seconds since the last full solar/lunar recomputation, for SolarUpdateInterval. */
	float SecondsSinceSolarUpdate = 0.f;

	/** Bumped on every discontinuous change so clients snap rather than interpolate. */
	uint8 DiscontinuityCounter = 0;

	/** Set while ApplyReplicatedState is running, to suppress the client-rejection warning. */
	bool bApplyingReplicatedState = false;

	/** True once a Director has claimed this world. */
	bool bDirectorRegistered = false;

	/** True once the initial defaults have been applied. */
	bool bInitialised = false;
};
