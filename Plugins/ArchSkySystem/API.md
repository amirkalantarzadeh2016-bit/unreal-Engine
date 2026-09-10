# API.md — every Blueprint-exposed function

Everything below is callable from Blueprint. The entry point is always the subsystem:

```
Get ArchSky Subsystem  (World Context)  →  ArchSky Subsystem
```

**Always check the result for validity.** It returns null during world teardown and in
world types the subsystem does not serve.

---

## `UArchSkySubsystem` — setters

Every setter validates and clamps. None can put the state into an illegal shape, and none
asserts on bad input — an out-of-range value is clamped and logged to `LogArchSky`.

| Function | Parameters | Notes |
|---|---|---|
| `SetTimeOfDay` | `float Hours` | Wraps into `[0,24)`. Does **not** roll the date. |
| `AddTimeOfDay` | `float DeltaHours` | Rolls the date when `bAutoAdvanceDate` is set. Handles multi-day deltas and negative values. |
| `SetDayOfYear` | `int32 Day` | Clamped to the current year's length, so 366 in a non-leap year becomes 365. |
| `SetDateFromGregorian` | `int32 Year, Month, Day` | Out-of-range month/day are clamped. |
| `SetDateFromJalali` | `int32 JalaliYear, JalaliMonth, JalaliDay` | An **invalid** date (e.g. 30 Esfand in a non-leap year) is rejected with a warning and nothing changes. |
| `SetTimeFlowRate` | `float HoursPerSecond` | `0` pauses. Negative runs time backwards. Clamped to ±600. |
| `PauseTime` | — | Remembers the rate for `ResumeTime`. |
| `ResumeTime` | — | Restores the remembered rate, or `1.0` if that was zero. |
| `ToggleTimePause` | — | |
| `SetLocation` | `FArchGeoLocation` | |
| `SetLocationPreset` | `FName CityId` → `bool` | Returns false and warns on an unknown id; the location is unchanged. |
| `SetNorthOffset` | `float Degrees` | Scene yaw at which **true north** lies. |
| `SetWeatherPreset` | `FName PresetId, float TransitionSeconds = 3` → `bool` | `0` seconds applies instantly. Retargeting mid-transition starts from what is currently on screen. |
| `SetWeatherBlend` | `FName A, FName B, float Alpha` → `bool` | An authored static blend; cancels any running transition. |
| `JumpToSunrise` / `JumpToSunset` | — | No-ops with a log line on a polar day or night. |
| `JumpToSolarNoon` | — | Always valid — solar noon exists even on a polar night. |
| `JumpToGoldenHour` | `bool bEvening = true` | |
| `JumpToBlueHour` | `bool bEvening = true` | |
| `SetSolsticePreset` | `EArchSolsticePreset` | `SpringEquinox`, `SummerSolstice`, `AutumnEquinox`, `WinterSolstice`. |
| `ApplyStatePreset` | `UArchSkyStatePreset*` | Null is ignored with a warning. Honours the preset's `bApply*` switches. |
| `ApplySkyState` | `FArchSkyState` | Wholesale replacement. Used by save-load and replication. |

### Example — a winter-solstice shadow study

```
Get ArchSky Subsystem
├─ Set Location Preset        (CityId = "Tehran")
├─ Set North Offset           (Degrees = 27.5)          ← from the site plan
├─ Set Solstice Preset        (Preset = Winter Solstice)
├─ Set Weather Preset         (PresetId = "Clear", TransitionSeconds = 0)
├─ Jump To Solar Noon
└─ Get Shadow Length Multiplier  →  print                ← the number to dimension from
```

### Example — an animated day sweep

```
Set Time Of Day    (Hours = 6.0)
Set Time Flow Rate (Hours Per Second = 0.5)     ← a 24 h day in 48 real seconds
```

Bind `OnTimePhaseChanged` to caption the golden hour as it happens.

---

## `UArchSkySubsystem` — getters

All `BlueprintPure`. Derived values are cached and recomputed only when the state actually
changes, so calling these in a UI tick is cheap — but the ViewModel is still the right
place to read them from.

| Function | Returns | Notes |
|---|---|---|
| `GetSkyState` | `FArchSkyState` | The authoritative state. |
| `GetSolarPosition` | `FArchSolarPosition` | Azimuth, altitude (apparent **and** geometric), declination, equation of time, hour angle, refraction. |
| `GetLunarPosition` | `FArchLunarPosition` | Includes illuminated fraction, phase, age, distance and bright-limb angle. |
| `GetSolarDayInfo` | `FArchSolarDayInfo` | Sunrise, sunset, solar noon, day length, civil twilight, max altitude, polar-day/night flags. |
| `GetFormattedTimeString` | `FText` | `bool b24Hour`. |
| `GetFormattedDateString` | `FText` | `EArchCalendarType`: `Gregorian` or `Jalali`. |
| `GetJalaliDate` | `FArchJalaliDate` | |
| `GetSeason` | `EArchSeason` | Derived from day-of-year **and hemisphere**; never stored. |
| `GetSeasonBlend01` | `float` | Continuous 0…1, `0` = midwinter. For materials. |
| `GetTimePhase` | `EArchTimePhase` | Nine phases from `Night` to `Day`. |
| `IsGoldenHour` / `IsBlueHour` | `bool` | |
| `GetShadowLengthMultiplier` | `float` | `cot(altitude)`. **Uses geometric altitude**, which is what casts the shadow. |
| `GetCurrentWeatherBlended` | `FArchWeatherParams` | With any running transition applied. |
| `GetAvailableWeatherPresetIds` | `TArray<FName>` | Built-ins then asset-only presets. |
| `GetWeatherPresetDisplayName` | `FText` | Falls back to the id for built-ins. |
| `GetAvailableLocations` | `TArray<FArchLocationEntry>` | Asset entries override built-ins in place. |
| `IsWeatherTransitionActive` | `bool` | |
| `GetWeatherTransitionRemainingSeconds` | `float` | |
| `IsTimePaused` | `bool` | |
| `HasSceneDirector` | `bool` | False means nothing will be visible. |
| `GetSolarPositionAtHour` | `FArchSolarPosition` | Any time today, **without** changing the state. |
| `GetSolarPositionAtDayAndHour` | `FArchSolarPosition` | Any day and time. This is what draws sun-path arcs. |

### Example — an hourly shadow table

```
For Loop (6 → 18)
└─ Get Solar Position At Hour (Local Hours = index)
   ├─ break out Altitude
   └─ Shadow Length Multiplier (Altitude) → append to a string
```

Because these are pure queries, the sky never moves while the table is built.

---

## `UArchSkySubsystem` — delegates

Bind with *Assign* / *Bind Event to…*.

| Delegate | Signature | Fires |
|---|---|---|
| `OnSkyStateChanged` | `(const FArchSkyState&)` | After **every** state change, including once per tick while time flows. |
| `OnTimePhaseChanged` | `(EArchTimePhase Old, EArchTimePhase New)` | Only on a real phase change. |
| `OnWeatherTransitionStarted` | `(FName From, FName To)` | |
| `OnWeatherTransitionCompleted` | `(FName From, FName To)` | Also fires for an instant (0 s) change. |
| `OnDayRolled` | `(int32 NewDayOfYear)` | When time crosses midnight. |
| `OnSunrise` / `OnSunset` | `(float LocalHours)` | When the sun's upper limb crosses the horizon. Fires when *scrubbing* too, not only when time flows. |

> `OnSkyStateChanged` fires every frame while time is flowing. Do not do expensive work in
> it — that is exactly the mistake the Director avoids by only setting dirty flags.

---

## `UArchSkyViewModel` — the UI layer

Read the `BlueprintReadOnly` fields, call the `Command*` functions, bind
`OnViewModelUpdated`. Never bind UI directly to the subsystem.

**Commands:** `CommandSetTimeOfDay`, `CommandSetDayOfYear`, `CommandSetDate`,
`CommandToggleTimePause`, `CommandSetTimeFlowRate`, `CommandSetLocationPreset`,
`CommandSetManualLocation`, `CommandSetNorthOffset`, `CommandSetWeather`,
`CommandJumpToSunrise`, `CommandJumpToSolarNoon`, `CommandJumpToSunset`,
`CommandJumpToGoldenHour`, `CommandJumpToBlueHour`, `CommandSetSolstice`,
`CommandSetCalendarType`, `CommandSetUse24HourClock`, `CommandSavePreset`,
`CommandLoadPreset`, `CommandDeletePreset`.

Commands automatically route through a server RPC on a client when replication is on;
callers need no networking code.

**Formatted fields:** `TimeText`, `DateText`, `SecondaryDateText`, `SeasonText`,
`TimePhaseText`, `SunriseText`, `SunsetText`, `DayLengthText`, `SolarNoonText`,
`SunAzimuthText`, `SunAltitudeText`, `ShadowLengthText`, `SolarNoonAltitudeText`,
`MoonPhaseText`, `MoonIlluminationText`, `LocationText`, `CoordinatesText`,
`TimezoneText`, `NorthOffsetText`, `WeatherText`.

**Raw fields for controls:** `TimeOfDayHours`, `DayOfYear`, `NorthOffsetDegrees`,
`TimeFlowRate`, `bIsTimePaused`, `MoonIllumination01`, `MoonIconRotationDegrees`,
`WeatherTransitionProgress`, `CalendarType`, `bUse24HourClock`.

**Lists:** `GetWeatherTiles`, `GetLocationOptions(Search)`, `GetSavedPresets`,
`GetMonthNames`, `GetCurrentDateParts`, `GetTimeSliderTicks`.

`GetTimeSliderTicks` returns normalised 0…1 positions for the sunrise, solar-noon and
sunset tick marks, plus a `bValid` flag that is false on a polar day — so a slider can
draw its ticks without redoing any astronomy.

---

## Pure math libraries

Callable with no subsystem, from any thread.

### `UArchSolarMathLibrary`

| Function | Notes |
|---|---|
| `CalculateSolarPosition(Location, LocalTime)` | Full NOAA solution. |
| `CalculateSolarDayInfo(Location, LocalDate)` | Sunrise/sunset/twilight for a day. |
| `SolarToUnrealLightRotation(Azimuth, Altitude, NorthOffset)` | Directional-light rotation. |
| `SolarToUnrealDirectionToBody(Azimuth, Altitude, NorthOffset)` | Unit vector **towards** the body. |
| `ShadowLengthMultiplier(Altitude, MaxMultiplier)` | `cot(altitude)`, clamped. |
| `EquationOfTimeMinutes(UTC)` | |
| `ToJulianDay(UTC)` | Returns `double` — a `float` would quantise a Julian Day to about six hours. |

### `UArchMoonMathLibrary`

`CalculateMoonPosition(Location, LocalTime)`, `GetMoonPhaseDisplayName(Phase)`.

### `UArchJalaliCalendarLibrary`

`GregorianToJalali`, `JalaliToGregorian` (returns `bool`), `DateTimeToJalali`,
`IsJalaliLeapYear`, `DaysInJalaliMonth`, `GetJalaliMonthName(Month, bPersianScript)`,
`FormatJalaliDate`.

### `UArchTimeCalendarLibrary`

`IsLeapYear`, `DayOfYearToMonthDay`, `MonthDayToDayOfYear`, `GetSolsticeDayOfYear`,
`FormatTimeOfDay`, `FormatDuration`, `GetSeasonDisplayName`, `GetTimePhaseDisplayName`.

### `UArchWeatherLibrary`

`BlendWeatherParams(A, B, Alpha)`, `GetPrecipTypeDisplayName`, `GetBuiltInWeatherParams`,
`GetBuiltInWeatherPresetIds`.

### `UArchLocationLibrary`

`GetBuiltInLocation`, `GetBuiltInLocations`, `GetClimateHintDisplayName`,
`SearchBuiltInLocations`.

### `UArchSkyPresetLibrary`

`SaveCurrentStateAsPreset(WorldContext, Name, Notes, Slot)`,
`LoadPresetByName`, `GetSavedPresets`, `DeletePreset`,
`ExportStateToJson`, `ImportStateFromJson`.

An empty slot name means the plugin default (`ArchSkyPresets`).

---

## `AArchSkyDirector`

Mostly configuration. The functions worth calling:

| Function | Notes |
|---|---|
| `GetSkySubsystem` | |
| `MarkAllDirty` | Forces a full re-apply next tick. Rarely needed. |
| `GetSunLightRotation` | The rotation currently applied to the sun light. |
| `GetDirectionToSun` | World-space unit vector towards the sun. |
| `Server_RequestSetTimeOfDay` etc. | Client → server RPCs. The ViewModel calls these for you. |

---

## Thread safety

| Layer | Threading |
|---|---|
| `ArchSolarMath`, `ArchMoonMath`, `ArchJalaliCalendar`, `ArchTimeCalendar` (non-`FText`) | **Any thread.** Pure functions, no state, no UObject access. |
| `FArchWeatherParams::Blend` | **Any thread.** |
| Anything returning `FText` | **Game thread** — the localisation manager is not thread-safe. |
| `UArchSkySubsystem`, `AArchSkyDirector`, `UArchSkyViewModel`, `UArchSkyWidgetBase` | **Game thread only.** |
