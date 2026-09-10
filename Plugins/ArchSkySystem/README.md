# ArchSky System

Physically accurate sun, moon, sky, cloud and weather control for architectural
visualisation in Unreal Engine 5.4+.

Built for one job: an architect walks a client through a building, and at any moment can
change the **time, date, city, plan orientation and weather** and get a shadow study that
is correct enough to defend. Solar positions come from the NOAA Solar Position Algorithm
(±0.01° for 1901–2099), so they can be checked against
[NOAA's own calculator](https://gml.noaa.gov/grad/solcalc/) — and they have been, in the
automation tests.

Dates work in both the **Gregorian** and **Jalali (Solar Hijri)** calendars.

---

## Quick start — five steps to a working sky

1. **Enable the plugin.** Copy `ArchSkySystem` into your project's `Plugins/` folder and
   restart. *Edit → Plugins → Architecture → ArchSky System* should show it enabled.
2. **Add the Director.** *Tools → ArchSky → Add Sky Director to Level.* It brings its own
   sun, moon, sky light, atmosphere, volumetric clouds and height fog — delete any you
   already have, or tick **Use Existing Scene Actors** on the Director to drive them instead.
3. **Set your site.** With the Director selected, use the **Sun Study** category at the top
   of the Details panel: drag *Time of Day* and *Day of Year* and watch the shadows move
   **without pressing Play**.
4. **Set plan north.** Drag *Plan North Offset* until the red arrow in the viewport points
   the way the site plan's north arrow does. The blue arrow is the plan's own north.
   **A shadow study is wrong until this is right** — architectural plans are almost never
   modelled north-aligned.
5. **Check the setup.** *Tools → ArchSky → Validate Scene Setup.* It reports duplicate
   lights, non-movable lights, a missing atmosphere, a wrong `AtmosphereSunLightIndex`, a
   static sky light and more — each with the fix.

You now have a correct sky. Everything below is about driving it at runtime.

---

## Architecture

```
 ┌──────────────────────────────────────────────────────────────────────────┐
 │  LAYER A — PURE MATH          no UObject, no UWorld, any thread          │
 │                                                                          │
 │   ArchSolarMath    NOAA solar position, refraction, day info             │
 │   ArchMoonMath     Meeus ch. 47 (truncated), phase, illumination         │
 │   ArchJalaliCalendar / ArchTimeCalendar    calendar conversion           │
 │                                                                          │
 │   SolarToUnrealLightRotation()  ← the alt/az → engine boundary           │
 └──────────────────────────────────────────────────────────────────────────┘
                                     ▲
                                     │  calls (pure, cached)
                                     │
 ┌──────────────────────────────────────────────────────────────────────────┐
 │  LAYER C — STATE + API        UArchSkySubsystem (UWorldSubsystem)        │
 │                                                                          │
 │   FArchSkyState  ── the ONE source of truth                              │
 │   Setters (validated) · Getters (derived, cached) · 7 delegates          │
 │                                                                          │
 │   Holds NO pointer to any scene component.                               │
 └──────────────────────────────────────────────────────────────────────────┘
                    │                                    ▲
   OnSkyStateChanged│ (broadcast)          Command*()    │ (calls)
                    ▼                                    │
 ┌───────────────────────────────────┐   ┌───────────────────────────────────┐
 │  LAYER B — SCENE                  │   │  UI — MVVM                        │
 │  AArchSkyDirector                 │   │                                   │
 │                                   │   │  UArchSkyViewModel                │
 │  handler → sets DIRTY FLAGS only  │   │   formats once per change,        │
 │  Tick    → applies, throttled:    │   │   raises ONE event                │
 │    Lights        every frame      │   │             │                     │
 │    SkyLight      thresholds+floor │   │             ▼                     │
 │    Atmosphere    on weather only  │   │  UArchSkyWidgetBase               │
 │    Fog           on weather only  │   │   binds ViewModel, throttles      │
 │    Clouds        on an interval   │   │   sliders, unbinds on destruct    │
 │    MPC           batched, 1×/frame│   │                                   │
 │                                   │   │  The widget can never reach a     │
 │  Owns its components, or adopts   │   │  light: there is no path to one.  │
 │  the level's existing ones.       │   │                                   │
 └───────────────────────────────────┘   └───────────────────────────────────┘
                    │
                    ▼
   Sun · Moon · SkyLight · SkyAtmosphere · VolumetricCloud · HeightFog · MPC_ArchSky
```

### Layer responsibilities

| Layer | Owns | Must never |
|---|---|---|
| **A — Math** | Astronomy and calendars. Pure functions in `double`. | Touch a `UObject`, a `UWorld`, or the game thread's state. |
| **C — Subsystem** | `FArchSkyState`, validation, derived values, delegates. | Hold a pointer into a scene component, or apply anything to the scene. |
| **B — Director** | Every scene component and every write to them. Throttling. | Decide *what* the state should be, or advance time. |
| **UI — ViewModel** | Formatting and command routing. | Touch a light, or be bound to per-frame. |
| **UI — Widget** | Visuals, input, focus. | Talk to the subsystem directly, or leave a delegate bound. |
| **Editor** | Visualiser, Sun Study panel, validator, toolbar. | Exist in a packaged build (it is an `Editor`-type module). |

The decoupling is not decoration. Because the subsystem holds no scene references, it is
fully functional in a level with **no Director at all** — which is what makes the
automation tests and headless shadow-study export possible.

---

## Driving it at runtime

Blueprint: `Get ArchSky Subsystem` → everything in [API.md](API.md).

Console (development builds):

```
ArchSky.SetTime 14.5
ArchSky.SetDay 355
ArchSky.SetLocation Isfahan
ArchSky.SetWeather DustStorm 5
ArchSky.SetNorthOffset 27.5
ArchSky.TimeScale 0.5
ArchSky.LogState

ArchSky.Debug.ShowState 1        on-screen state HUD
ArchSky.Debug.DrawSunPath 1      sun path in the world
ArchSky.Debug.LogSolarMath 1     full solution, once a second

ArchSky.Perf.SkyRecaptureThreshold 2.5   (also available in Shipping)
stat ArchSky
```

Editor-only viewport CVars: `ArchSky.Editor.SunPathRadius`,
`ArchSky.Editor.ShowAnalemma`, `ArchSky.Editor.ShowReferenceArcs`.

---

## Sign conventions — read this once

Getting these wrong is the classic failure mode of a sky system, so they are stated
explicitly and derived in full in the comment block above
`SolarToUnrealLightRotation()` in `ArchSolarMath.cpp`.

- **Azimuth** is a compass bearing: `0` = true north, increasing **clockwise**
  (east = 90, south = 180, west = 270).
- **Scene axes**: `+X` = true north, `+Y` = east, `+Z` = up. Unreal's yaw rotates `+X`
  toward `+Y`, which is north → east, i.e. clockwise — so **Unreal yaw and compass azimuth
  are the same number**. That equality is why this axis convention was chosen.
- **`NorthOffsetDegrees`** is the *scene-space yaw at which true north lies*. If the plan
  is modelled "up the page" along `+X` but true north is really 30° clockwise of that,
  set `30`.
- **Light rotation** is `Pitch = -Altitude`, `Yaw = Azimuth + NorthOffset + 180`. The
  `+180` is because a light's forward vector is the direction light *travels* — from the
  sun toward the scene.
- **Geometric vs apparent altitude.** `TrueAltitudeDegrees` (no refraction) aims the light
  and computes shadows, because a rendered shadow is cast along the geometric direction.
  `AltitudeDegrees` (with refraction) is what an observer would measure with a clinometer.

---

## Verifying the astronomy

```
Automation RunTests ArchSky
```

or *Tools → Test Automation → Automation → ArchSky*. The suite covers:

- Tehran at both solstices (77.8° and 30.9° solar-noon altitude)
- The equator at the equinox (90°)
- Tromsø polar day **and** polar night
- Sunrise/sunset symmetry about solar noon, across five months
- Agreement between the two independent solar code paths at the horizon crossing
- Each NOAA pipeline step against published constants
- Lunar longitude at a reference new moon and full moon, the synodic period, and a
  year of invariants
- Every axis of the coordinate conversion, including that the light's forward vector is
  the exact negation of the direction-to-body at 200+ angle combinations
- Jalali ↔ Gregorian round trips for **every day from 1900 to 2100**, plus Nowruz anchors

Angular tolerance is ±0.5°.

---

## Building the content assets

The C++ is complete. Three kinds of asset cannot be authored as text and must be made
once, by hand. **The plugin works without all of them** — only the MPC changes what you
can do, and only for materials.

### 1. `MPC_ArchSky` (recommended)

Follow **[MATERIALS.md](MATERIALS.md)**. It lists every parameter with its exact name,
range, default and a usage example. Assign it in
*Project Settings → Plugins → ArchSky → Sky Parameter Collection*.

Without it, lighting, atmosphere, clouds and fog all still work; only *materials* lose the
ability to react to wetness, snow, wind and sun direction.

### 2. The UI widgets

The panel is a Blueprint subclass of `UArchSkyWidgetBase`. Full recipe in
[`Content/UI/README.md`](Content/UI/README.md). The short version:

1. Create a Widget Blueprint with parent class **`ArchSkyWidgetBase`**, named `WBP_ArchSkyPanel`.
2. Lay out the panel. Name any control you want auto-bound exactly as the C++ property is
   named — `TimeOfDaySlider`, `DayOfYearSlider`, `NorthOffsetDial`, `TimeLabel`,
   `DateLabel`, `SunTimesLabel`, `AnalysisLabel`, `PlayPauseButton`, `CalendarToggle`,
   `LocationCombo`, `LocationSearchBox`. **All are optional**, so a cut-down panel is valid.
3. For everything else, implement the **`On Sky View Updated`** event and push
   `ViewModel` fields into your widgets there.
   **Do not use UMG property bindings** — they evaluate every frame for every bound widget,
   which is precisely what the ViewModel exists to avoid.
4. Add the panel to the viewport from your Pawn or HUD, and bind a key to
   `Toggle Panel Visibility`.

Recommended panel sections, in order of value to an architect: time, date (with the
calendar toggle), **north offset dial**, location, weather tiles, the analysis readout,
and saved presets.

### 3. Weather and location assets (optional)

Twelve weather presets and sixteen cities are compiled in and work with no content at all.
Create assets only to override or extend them — see
[`Content/Data/README.md`](Content/Data/README.md). An asset whose `PresetId` or `CityId`
matches a built-in **replaces** it.

---

## Localisation

Every user-facing string is an `FText` built with `LOCTEXT`/`NSLOCTEXT`. Nothing is
hard-coded. Namespaces to add to the localisation dashboard:

| Namespace | Contents |
|---|---|
| `ArchJalaliCalendar` | Jalali month names, Jalali date format |
| `ArchTimeCalendar` | Gregorian months, seasons, time phases, clock and duration formats |
| `ArchMoonMath` | The eight moon-phase names |
| `ArchLocations` | City names, country names, climate hints |
| `ArchWeather` | Precipitation type names |
| `ArchSkySubsystem` | The Gregorian date format |
| `ArchSkyViewModel` | Every readout format string |
| `ArchSkyWidget` | Tooltips and composite readout formats |
| `ArchSkyDirectorDetails`, `ArchSkySceneValidator`, `FArchSkyEditorModule` | Editor-only |

Persian month names are also available in Persian script directly from
`ArchJalaliCalendar::GetJalaliMonthNamePersian()`, independent of the active culture, so a
dual-script readout works even in an English build.

**RTL:** Slate handles bidirectional text and mirrors layout automatically when the active
culture is RTL. Two things are on you: build the panel with `Left`/`Right` alignment
rather than fixed `Padding` offsets, and set the panel's `Flow Direction Preference` to
`Inherit`. The compass dial deliberately does **not** mirror — a compass rose is a
geographic instrument, not a text layout.

---

## Networking

Off by default; with `bEnableReplication` false, every code path behaves exactly as it
does in single player.

Turn it on in *Project Settings → Plugins → ArchSky*:

- The server owns time. The Director replicates a **quantised** `FArchSkyReplicatedState`
  at 2 Hz — clients interpolate between updates rather than receiving per-frame data.
- `OnRep_SkyState` **reconciles smoothly** (absorbing 35% of the error per update) unless
  the server signalled a discontinuity, in which case it snaps. Interpolating through a
  deliberate jump would sweep the sun across the sky.
- Location and plan north are **not** replicated — they are level authoring, identical on
  every machine.
- `bAllowClientTimeControl` gates the four validated server RPCs. Leave it **off** so a
  client cannot hijack a presentation; turn it on for a collaborative review.

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| The sun does not move | The directional light is not **Movable**. Run *Validate Scene Setup*. |
| Shadows point the wrong way | `NorthOffsetDegrees` does not match the site plan. Compare the red and blue viewport arrows. |
| Black sky | Two lights both claim `AtmosphereSunLightIndex 0`, or the sun light has *Atmosphere Sun Light* off. |
| Ambient light updates in steps | Lower `SkyRecaptureAltitudeThreshold` — see [PERFORMANCE.md](PERFORMANCE.md). |
| Materials ignore the weather | `MPC_ArchSky` is not created or not assigned. See [MATERIALS.md](MATERIALS.md). |
| Nothing happens at all | No Director in the level. `ArchSky.Debug.ShowState 1` says so in red. |
| Indoor light does not follow the sun | Lumen is off. ArchSky assumes a dynamic GI path. |
| Jalali date is off by one day | You are comparing against a Birashk-cycle converter. This plugin uses the leap-year breaks table, which matches the observed Iranian calendar. |

---

## Documents

- **[MATERIALS.md](MATERIALS.md)** — the `MPC_ArchSky` contract and the two cloud paths
- **[API.md](API.md)** — every Blueprint-exposed function, with examples
- **[PERFORMANCE.md](PERFORMANCE.md)** — every throttle, its cost, and profiles for
  Desktop / VR / low-end

Architectural trade-offs are recorded inline as `// ARCH NOTE:` comments, each stating the
alternative that was rejected and why.
