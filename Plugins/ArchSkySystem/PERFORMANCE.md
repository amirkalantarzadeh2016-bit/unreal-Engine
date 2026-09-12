# PERFORMANCE.md

Every throttle in the plugin, what it costs, and what to set it to.

The design principle throughout: **cheap work runs every frame, expensive work is gated by
a threshold *and* a hard floor.** A threshold alone is not enough — during a fast
time-lapse the sun's altitude changes by more than any sensible threshold every single
frame, so a threshold-only design degenerates into "do the expensive thing every frame"
exactly when you can least afford it.

---

## Measuring first

```
stat ArchSky
```

gives seven counters:

| Counter | What it covers |
|---|---|
| `ArchSky Director Tick` | The whole per-frame apply, everything below included |
| `ArchSky Solar Math` | One NOAA solar solution |
| `ArchSky Lunar Math` | One truncated-Meeus lunar solution |
| `ArchSky Sky Recapture` | `RecaptureSky()` — the expensive one |
| `ArchSky MPC Write` | The batched material-parameter write |
| `ArchSky Weather Apply` | Atmosphere, fog and cloud component writes |
| `ArchSky Subsystem Tick` | Time advancement and weather transition |
| `ArchSky Playback Tick` | Loop-boundary supervision while playing |

Typical desktop figures with time flowing, measured against the default settings:

| Counter | Cost |
|---|---|
| Solar math | ~2 µs |
| Lunar math | ~4 µs |
| MPC write | ~15 µs |
| Weather apply | ~20 µs, only on a weather change |
| Sky recapture | **1–4 ms**, GPU-bound |
| Playback tick | ~1 µs, and only while playing |
| Director tick, paused | < 1 µs (it early-outs) |

The headline is the ratio: the astronomy is free, and a sky-light recapture costs a
thousand times more than everything else in the plugin put together. That is what the
throttling is for.

---

## The knobs

All in *Project Settings → Plugins → ArchSky → Performance*, and mirrored as
`UPROPERTY`s on the Director so one level can differ from the project default.

### `SkyRecaptureAltitudeThreshold` — default `1.0°`

How far the sun must move before the sky light's cubemap is recaptured.

- **Cost:** each recapture is a full cubemap render, 1–4 ms of GPU time.
- **Lower** → smoother ambient light as the sun moves, more GPU cost.
- **Higher** → ambient light updates in visible steps.
- Below about **0.25°** you are recapturing more often than the sun's own disc moves and
  getting nothing for it.

Also settable at runtime as `ArchSky.Perf.SkyRecaptureThreshold` (a scalability CVar,
available in Shipping, so a device profile can dial it per platform). Negative means "use
the Project Settings value".

### `SkyRecaptureWeatherThreshold` — default `0.05`

The same idea for weather. It is a **normalised** figure: each weather field is divided by
its useful range before comparison, so `0.05` means "any field moved 5% of its range".
This keeps one threshold meaningful across fields whose raw magnitudes differ by five
orders of magnitude.

### `MinFramesBetweenSkyRecaptures` — default `30`

The hard floor. Whatever the two thresholds say, a recapture never happens more often than
this. **This is the knob that actually protects you** during a 600× time-lapse.

- At 60 fps, `30` means at most two recaptures per second.
- Raise to `60`+ for VR.
- It is never worth lowering below `10`.

### `CloudUpdateInterval` — default `0.1 s`

How often cloud parameters are pushed. Clouds are large, soft and slow; nobody can see a
10 Hz update. Raise to `0.25` on low-end. `0` means every frame and is not recommended.

### `SolarUpdateInterval` — default `0.0` (every frame)

How often the full ephemeris is recomputed while time is flowing. The lights still
interpolate every frame; only the astronomy is throttled.

At ~6 µs for both bodies this is almost never worth changing on desktop. On a
CPU-bound mobile or VR target, `0.1` cuts it to 10 Hz with no visible difference — the sun
moves 0.0042° in a tenth of a second at real-time rates.

### `bWriteMaterialParameterCollection` — default `true`

Turn **off** if no material in your project reads `MPC_ArchSky`. Saves the ~15 µs write.
Lighting, atmosphere and fog are unaffected — they go through component properties.

### `bEnableMoonLight` — default `true`

Turn **off** to save one shadow-casting directional light. On a daytime-only presentation
this is free performance. The moon's *position* is still computed and still reaches
materials through the MPC.

### `SunLightSourceAngleDegrees` — default `0.545°`

The sun's true angular diameter. Larger values soften the shadow penumbra, and soft
shadows are more expensive to filter. Do not raise it for looks if you are producing a
shadow study — the penumbra width is part of what you are studying.

---

## Recommended profiles

### Desktop — the default

```ini
SkyRecaptureAltitudeThreshold=1.0
SkyRecaptureWeatherThreshold=0.05
MinFramesBetweenSkyRecaptures=30
CloudUpdateInterval=0.1
SolarUpdateInterval=0.0
CloudMode=Volumetric
bEnableMoonLight=True
```

Budget: **~2–5 ms** total, almost all of it volumetric clouds.

### VR — 90 Hz, stereo, no room for spikes

```ini
SkyRecaptureAltitudeThreshold=2.5
SkyRecaptureWeatherThreshold=0.1
MinFramesBetweenSkyRecaptures=90
CloudUpdateInterval=0.25
SolarUpdateInterval=0.1
CloudMode=SkySphere
bEnableMoonLight=False
```

Budget: **under 1 ms**.

The cloud mode change is the important one. Volumetric clouds are ray-marched *per eye*,
and in stereo the cost roughly doubles while the parallax between eyes makes the noise
pattern visible. Path B is stable in stereo and an order of magnitude cheaper — see
MATERIALS.md.

A `MinFramesBetweenSkyRecaptures` of 90 means at most one recapture per second at 90 Hz.
A 1–4 ms spike once a second is still a dropped frame in VR; if you are producing a
time-lapse in VR, consider pausing time during the flythrough and stepping it between
stops instead.

### Low-end and mobile

```ini
SkyRecaptureAltitudeThreshold=5.0
SkyRecaptureWeatherThreshold=0.2
MinFramesBetweenSkyRecaptures=120
CloudUpdateInterval=0.5
SolarUpdateInterval=0.2
CloudMode=SkySphere
bEnableMoonLight=False
bWriteMaterialParameterCollection=False
```

Consider also turning off `bRealTimeCapture` on the sky light and letting the throttled
`RecaptureSky()` path handle it — the Director detects which is active and never does both.

---

## Things that are already free

You do not need to configure these; they are structural.

- **The transport costs nothing when paused.** `UArchSkyPlaybackSubsystem::IsTickable()`
  returns false unless the clock is actually moving, and even while playing it does no
  arithmetic of its own — it only checks whether the loop boundary was crossed. Playback
  reuses the clock's existing advancement rather than running a second one.

- **A paused sky costs nothing.** With time paused, no weather transition running and
  nothing marked dirty, `AArchSkyDirector::Tick` early-outs after one branch and
  `UArchSkySubsystem::IsTickable()` returns false, so the subsystem is not ticked at all.
  A static architectural shot pays essentially zero.

- **Coalesced state changes.** The subsystem broadcasts on every change; the Director's
  handler only sets dirty flags. Five changes in one frame — a slider drag plus a console
  command plus a replicated packet — still cost one apply.

- **Weather endpoints are resolved once per transition**, not once per frame. The
  per-frame cost of a running transition is one struct lerp.

- **No `GetAllActorsOfClass` at runtime.** The only actor iterations are one duplicate
  check and one adoption sweep, both at `BeginPlay`, plus one cached lookup on the
  networked UI path that single-player never reaches.

- **UI slider throttling.** `UArchSkyWidgetBase` gates slider traffic behind both a value
  delta and a 30 Hz interval, and always flushes the final value — so a drag costs about a
  quarter of what the raw UMG events would, without ever losing where the user let go.

- **No per-frame UMG property bindings.** The ViewModel formats each readout once per
  change and raises one event. This is worth more than it sounds: UMG evaluates every
  property binding on every widget every frame, and a dozen bound text blocks doing
  `FText` formatting is easily more expensive than all of the astronomy.

---

## When a shadow study is the deliverable

A fast playback speed does **not** cost more CPU — the clock advances by a larger step per
frame, not more often. What it does cost is sky-light recaptures: at ×8640 the sun moves
about 0.4° per frame at 60 fps, so the 1° altitude threshold fires roughly every third
frame and the `MinFramesBetweenSkyRecaptures` floor becomes the thing actually protecting
you. Raise the floor, not the speed, if a time-lapse hitches.

For an *offline* study — rendering frames to disk rather than presenting live — invert
every recommendation above:

```
ArchSky.Perf.SkyRecaptureThreshold 0.1
```

and set `MinFramesBetweenSkyRecaptures` to `1`. Frame rate does not matter when you are
rendering to file, and you want the ambient term correct in every frame rather than
correct twice a second.
