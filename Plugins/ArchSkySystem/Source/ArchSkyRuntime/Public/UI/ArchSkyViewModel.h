// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/ArchSkyPlaybackTypes.h"
#include "Core/ArchSkyState.h"
#include "Data/ArchLocationPreset.h"
#include "Data/ArchSkySaveGame.h"
#include "Math/ArchMoonMath.h"
#include "Math/ArchSolarMath.h"
#include "UObject/Object.h"
#include "Util/ArchJalaliCalendar.h"

#include "ArchSkyViewModel.generated.h"

class UArchSkyPlaybackSubsystem;
class UArchSkySubsystem;

/** Fired once per coalesced update. The widget rebinds everything from this one event. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArchViewModelUpdated);

/** Fired when the list of saved presets changes, so the preset panel can rebuild. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArchPresetListChanged);

/** One row of the weather tile list. */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchWeatherTileInfo
{
	GENERATED_BODY()

	/** Preset identifier to pass back to the ViewModel's commands. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI")
	FName PresetId;

	/** Localised label for the tile. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI")
	FText DisplayName;

	/** Optional thumbnail. Soft, so building the list loads no textures. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	/** True for the preset currently in force, so the tile can draw its selected state. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI")
	bool bIsActive = false;
};

/**
 * The V-M of the plugin's MVVM. Non-negotiable layer, per the architecture.
 *
 * WHY THIS EXISTS
 * ---------------
 * A UMG widget bound directly to UArchSkySubsystem would have to do three things badly:
 * format numbers into text on every tick, re-read unchanged values through Blueprint
 * property bindings (which the engine evaluates every frame, per binding), and manage its
 * own delegate lifetimes against a subsystem that outlives it.
 *
 * The ViewModel does the formatting exactly once per change, exposes the result as plain
 * BlueprintReadOnly properties, and raises ONE event. A widget therefore binds one
 * delegate and reads fields - no per-frame property bindings anywhere.
 *
 * THREADING: game thread only.
 */
UCLASS(BlueprintType, DisplayName = "ArchSky View Model")
class ARCHSKYRUNTIME_API UArchSkyViewModel : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Binds to a world's subsystem and performs the first refresh.
	 * Safe to call twice; the second call rebinds.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI", meta = (WorldContext = "WorldContextObject"))
	void Initialise(const UObject* WorldContextObject);

	/** Unbinds every delegate. Call from NativeDestruct. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI")
	void Shutdown();

	/** True once Initialise has found a subsystem. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|UI")
	bool IsInitialised() const { return SkySubsystem.IsValid(); }

	// -----------------------------------------------------------------------------------
	// Presentation state. All read-only: the widget displays these, it never writes them.
	// -----------------------------------------------------------------------------------

	/** "06:42" or "6:42 AM", per bUse24HourClock. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	FText TimeText;

	/** Localised date in the currently selected calendar. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	FText DateText;

	/** The same date in the OTHER calendar, for the dual-calendar readout. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	FText SecondaryDateText;

	/** Localised season name. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	FText SeasonText;

	/** Localised photographic phase, e.g. "Golden Hour (Morning)". */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	FText TimePhaseText;

	/** Raw local decimal hours, for the time slider's value. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	float TimeOfDayHours = 12.f;

	/** Raw day of year, for the date slider's value. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	int32 DayOfYear = 172;

	/** True while time is not advancing, for the play/pause button's icon. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	bool bIsTimePaused = true;

	/** Current simulated hours per real second, for the speed selector's highlight. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	float TimeFlowRate = 0.f;

	/** "06:42", or a localised "No sunrise" on a polar day. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Solar")
	FText SunriseText;

	/** "18:14", or a localised "No sunset". */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Solar")
	FText SunsetText;

	/** "11h 32m". */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Solar")
	FText DayLengthText;

	/** Solar noon as a clock string. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Solar")
	FText SolarNoonText;

	/** "142.6 deg" - the sun's compass bearing. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Analysis")
	FText SunAzimuthText;

	/** "57.3 deg" - the sun's altitude above the horizon. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Analysis")
	FText SunAltitudeText;

	/** "1.67 x height" - the shadow-length multiplier an architect dimensions from. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Analysis")
	FText ShadowLengthText;

	/** Solar altitude at noon on this date, the key shadow-study number. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Analysis")
	FText SolarNoonAltitudeText;

	/** Localised moon phase name. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Analysis")
	FText MoonPhaseText;

	/** "73%" - illuminated fraction of the lunar disc. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Analysis")
	FText MoonIlluminationText;

	/** 0..1 illuminated fraction, for driving a moon-phase icon's material. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Analysis")
	float MoonIllumination01 = 0.f;

	/** Rotation, in degrees, to apply to a moon-phase icon so the crescent points correctly. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Analysis")
	float MoonIconRotationDegrees = 0.f;

	/** Localised city name, or "Custom" when the coordinates match no preset. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Location")
	FText LocationText;

	/** "35.689 N, 51.389 E". */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Location")
	FText CoordinatesText;

	/** "UTC+03:30". */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Location")
	FText TimezoneText;

	/** Plan-north offset in degrees, for the compass dial's value. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Location")
	float NorthOffsetDegrees = 0.f;

	/** "True north is 30 deg clockwise of plan north." */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Location")
	FText NorthOffsetText;

	/** Localised name of the weather currently in force. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Weather")
	FText WeatherText;

	/** 0..1 progress of a running weather transition; 1 when none is active. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Weather")
	float WeatherTransitionProgress = 1.f;

	// --- Playback transport ------------------------------------------------------------

	/**
	 * Minutes from midnight, 0 .. 1440. Bind the timeline slider's value to this and route
	 * its OnValueChanged to CommandSetSimTime for the bidirectional binding the brief asks
	 * for. It is a derived view of the one authoritative clock, not a second copy of it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	float CurrentSimTime = 720.f;

	/** CurrentSimTime as a 0..1 fraction, for a normalised slider. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	float CurrentSimTime01 = 0.5f;

	/** True while the clock is advancing. Drives the play/pause button's icon. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	bool bIsPlaying = false;

	/** The transport clock as "HH:MM". */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	FText PlaybackTimeText;

	/** The simulated date, which advances when playback runs past midnight. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	FText PlaybackDateText;

	/** Speed label for beside the slider, e.g. "Hour per second (x3600)". */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	FText SpeedPresetText;

	/** The raw multiplier, for a numeric entry box. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	float SpeedMultiplier = 60.f;

	/** Which named preset is selected, or Custom. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	EArchPlaybackSpeedPreset SpeedPreset = EArchPlaybackSpeedPreset::Fast;

	/** True when reaching the loop end restarts instead of stopping. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	bool bLoopEnabled = false;

	/** Loop window in minutes from midnight. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	float LoopStart = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	float LoopEnd = 1440.f;

	/** The loop window as 0..1 fractions, for drawing the window onto the slider track. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	float LoopStart01 = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	float LoopEnd01 = 1.f;

	/** "06:00 - 18:00", for the loop readout. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	FText LoopRangeText;

	/** Minutes per press of the step buttons. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	float StepSize = 15.f;

	/** "15 min", for the step buttons' labels or tooltips. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Playback")
	FText StepSizeText;

	/** Which calendar the primary date readout uses. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	EArchCalendarType CalendarType = EArchCalendarType::Gregorian;

	/** 24-hour vs AM/PM. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|UI|Time")
	bool bUse24HourClock = true;

	// -----------------------------------------------------------------------------------
	// Commands. The widget calls these; they forward to the subsystem.
	// -----------------------------------------------------------------------------------

	/** Sets the time of day. Routed through a server RPC on a client when permitted. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetTimeOfDay(float Hours);

	/** Sets the day of year. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetDayOfYear(int32 Day);

	/** Sets the date from the currently selected calendar's year/month/day. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetDate(int32 Year, int32 Month, int32 Day);

	/** Play / pause. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandToggleTimePause();

	/** One of the 1x / 10x / 60x / 600x speed presets, expressed in hours per second. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetTimeFlowRate(float HoursPerSecond);

	/** Selects a city by id. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetLocationPreset(FName CityId);

	/** Sets latitude, longitude and timezone from manual entry. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetManualLocation(float Latitude, float Longitude, float TimezoneHours, float ElevationMeters);

	/** Sets the plan's north offset from the compass dial. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetNorthOffset(float Degrees);

	/** Starts a weather transition. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetWeather(FName PresetId, float TransitionSeconds);

	/** Jumps to a named solar instant. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandJumpToSunrise();

	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandJumpToSolarNoon();

	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandJumpToSunset();

	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandJumpToGoldenHour(bool bEvening);

	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandJumpToBlueHour(bool bEvening);

	/** Selects a solstice or equinox. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetSolstice(EArchSolsticePreset Preset);

	/** Switches the date readout between Gregorian and Jalali. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetCalendarType(EArchCalendarType NewCalendarType);

	/** Switches between 24-hour and AM/PM. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	void CommandSetUse24HourClock(bool bIn24Hour);

	// --- Playback transport commands ---------------------------------------------------

	/** Starts playback at the current speed. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandPlay();

	/** Stops the clock where it is. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandPause();

	/** Stops the clock and rewinds to the loop start, or to midnight. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandStop();

	/** Play if paused, pause if playing. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandTogglePlayback();

	/** Seeks the simulation. The scrubber's OnValueChanged target. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandSetSimTime(float NewSimTimeMinutes);

	/** Seeks from a normalised 0..1 slider value. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandSetSimTimeNormalised(float Value01);

	/** Call when the user grabs the scrubber; suspends playback for the drag. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandBeginScrub();

	/** Call when the user releases the scrubber; playback resumes from the new position. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandEndScrub();

	/** Adds StepSize minutes. Pauses continuous playback first. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandStepForward();

	/** Subtracts StepSize minutes. Pauses continuous playback first. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandStepBackward();

	/** Sets the minutes per step. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandSetStepSize(float NewStepSize);

	/** Selects a named speed preset. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandSetSpeedPreset(EArchPlaybackSpeedPreset NewPreset);

	/** Sets an arbitrary speed multiplier. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandSetSpeedMultiplier(float NewSpeedMultiplier);

	/** Turns looping on or off. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	void CommandSetLoopEnabled(bool bEnabled);

	/** Sets the loop window in minutes. Returns false if the window is empty or inverted. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands|Playback")
	bool CommandSetLoopRange(float NewLoopStart, float NewLoopEnd);

	/** Every selectable speed preset with its label, for building a dropdown. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|UI|Lists")
	void GetSpeedPresetOptions(TArray<EArchPlaybackSpeedPreset>& OutPresets, TArray<FText>& OutLabels) const;

	/** Saves the current state as a named runtime preset. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	bool CommandSavePreset(const FString& PresetName, const FString& Notes);

	/** Applies a saved runtime preset. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	bool CommandLoadPreset(const FString& PresetName);

	/** Deletes a saved runtime preset. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|UI|Commands")
	bool CommandDeletePreset(const FString& PresetName);

	// -----------------------------------------------------------------------------------
	// List providers for the panels
	// -----------------------------------------------------------------------------------

	/** Every weather preset, ready to build tiles from. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|UI|Lists")
	TArray<FArchWeatherTileInfo> GetWeatherTiles() const;

	/** Cities matching a search string. Empty search returns everything. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|UI|Lists")
	TArray<FArchLocationEntry> GetLocationOptions(const FString& SearchText) const;

	/** Saved runtime presets, newest first. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|UI|Lists")
	TArray<FArchSkyNamedPreset> GetSavedPresets() const;

	/** Month names for the calendar picker, in the currently selected calendar. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|UI|Lists")
	TArray<FText> GetMonthNames() const;

	/** Year/month/day of the current date in the currently selected calendar. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|UI|Lists")
	void GetCurrentDateParts(int32& OutYear, int32& OutMonth, int32& OutDay) const;

	/**
	 * Normalised positions (0..1 across the 24 h axis) of sunrise, solar noon and sunset,
	 * so the time slider can draw its tick marks without redoing the astronomy.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|UI|Lists")
	void GetTimeSliderTicks(float& OutSunrise01, float& OutSolarNoon01, float& OutSunset01, bool& bOutValid) const;

	// -----------------------------------------------------------------------------------
	// Events
	// -----------------------------------------------------------------------------------

	/** The single event a widget binds to. Raised at most once per frame. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|UI|Events")
	FOnArchViewModelUpdated OnViewModelUpdated;

	/** Raised when a preset is saved or deleted. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|UI|Events")
	FOnArchPresetListChanged OnPresetListChanged;

	/** Save-game slot the preset commands read and write. Empty means the plugin default. */
	UPROPERTY(BlueprintReadWrite, Category = "ArchSky|UI")
	FString PresetSlotName;

private:
	/** Bound to UArchSkySubsystem::OnSkyStateChanged. */
	UFUNCTION()
	void HandleSkyStateChanged(const FArchSkyState& NewState);

	/** Bound to UArchSkySubsystem::OnTimePhaseChanged. */
	UFUNCTION()
	void HandleTimePhaseChanged(EArchTimePhase OldPhase, EArchTimePhase NewPhase);

	/** Bound to the weather transition delegates. */
	UFUNCTION()
	void HandleWeatherTransition(FName FromPresetId, FName ToPresetId);

	/** Recomputes every presentation field and raises OnViewModelUpdated. */
	void RefreshAllFields();

	/**
	 * Routes a mutation through the server when this is a client with control rights,
	 * and applies it locally otherwise. Returns true if the caller should also apply
	 * locally (single player, listen server, or predicted client input).
	 */
	bool ShouldApplyLocally() const;

	/** The Director, looked up lazily, used only to reach its server RPCs. */
	class AArchSkyDirector* FindDirector() const;

	/** Bound to UArchSkyPlaybackSubsystem::OnPlaybackStateChanged. */
	UFUNCTION()
	void HandlePlaybackStateChanged(bool bNowPlaying);

	/** Bound to UArchSkyPlaybackSubsystem::OnPlaybackSpeedChanged. */
	UFUNCTION()
	void HandlePlaybackSpeedChanged(float NewSpeedMultiplier, EArchPlaybackSpeedPreset NewPreset);

	/** Bound to UArchSkyPlaybackSubsystem::OnPlaybackReachedEnd. */
	UFUNCTION()
	void HandlePlaybackReachedEnd();

	/** The subsystem this ViewModel presents. */
	TWeakObjectPtr<UArchSkySubsystem> SkySubsystem;

	/** The transport this ViewModel drives. */
	TWeakObjectPtr<UArchSkyPlaybackSubsystem> PlaybackSubsystem;

	/** Cached Director, for the RPC path only. */
	TWeakObjectPtr<class AArchSkyDirector> CachedDirector;

	/** Set while a refresh is running, so a re-entrant broadcast cannot recurse. */
	bool bRefreshing = false;
};
