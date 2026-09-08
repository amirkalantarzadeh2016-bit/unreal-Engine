# 05 — Troubleshooting

**Start here:** select the bounds volume → **Validate Setup** → Output Log, filter
`LogMinimap`. It prints a staged pipeline report where the **first `NO` is the failure
point**, plus a read-back verdict on whether the render target genuinely contains an image.

```
1. Component registered : yes
2. World has a scene    : yes
3. IsVisible()          : NO  <-- CaptureScene() is gated on this and will do NOTHING
7. Capture batches run  : 0   <-- capture NEVER executed
```

---

## The map is black

| Cause | How to confirm | Fix |
|---|---|---|
| Exposure Mode = Manual | Game view looks fine, capture is black | Set **Inherit Scene**. Manual exposure ignores scene lighting, so a dim interior renders to zero. |
| Capture Depth too small | Validate reports it by name | Set **Capture Depth = 0** (automatic). A depth smaller than the camera's drop to the floor culls the whole level. |
| No temporal history | Black or noisy under Lumen | Raise **Warm Up Passes**. A cold one-shot capture has nothing for TAA/Lumen to converge from. |
| Show-only with empty list | Validate reports it | Add inclusion tags, or turn off **Use Show Only List**. |
| Capture component hidden | Stage 3 `IsVisible()` = NO | Fixed in code — `CaptureScene()` is gated on `IsVisible()`, and `bHiddenInGame` makes that false in a game world. The component now forces itself visible and logs if anything re-hides it. |

**Is the render target actually black?** Working markers and a working static map prove
nothing about the capture. `Probe Render Target` reads it back and gives a verdict:

```
Read 1048576 px: mean luminance 0.0000, max 0.0000, 0 non-black (0.0%).
VERDICT: the render target is genuinely BLACK - the problem is in the capture,
         not in the material/widget pipeline.
```

---

## The map repeats / tiles at the edges

The sampler is being asked for a UV outside `[0,1]` and **wraps**. Three defences:

1. **Render target address mode** is `TA_Clamp`. Fixes it outright **if** your material's
   Texture Sample node has **Sampler Source = From texture asset**.
2. **`EdgeMaskPixels`** (default 4) stamps the rim black, so a clamped sample past the edge
   returns black rather than smearing the edge pixel.
3. **`BackgroundApplyMode = CompositedView`** — the guaranteed fix. The plugin composites
   the view itself with a fixed `[0,1]` coordinate span, so tiling is impossible by
   construction and outside the map is solid black.

> **Honest limit:** if your Texture Sample node uses a shared **Wrap** sampler, defences 1
> and 2 cannot help. Shared-sampler addressing is baked into *your material asset* and no
> plugin code can override it from outside. Either change that dropdown, or use
> `CompositedView`, which needs no material at all.

---

## Markers do not line up with the image

| Symptom | Cause |
|---|---|
| Rotated 90° | `bSwapUV` is wrong for your material |
| Mirrored | `bInvertU` / `bInvertV` |
| Squashed on one axis | `bPreserveAspectRatio` off with non-square bounds |
| Map and compass turn opposite ways | Toggle `bNegateMapRotation` — do not edit the material |
| Map jumps a full turn crossing north | Material is reading `MapRotation` as degrees. It is **turns**, `[0,1)`. |

---

## Bounds problems

| Symptom | Fix |
|---|---|
| Map covers far too much empty space | **Fit To Geometry** instead of Fit To All Actors. Raise `GeometryOutlierTrim`. |
| `SetCalibration rejected` | Zero box extent on X or Y, `Zoom <= 0`, or `MaxZ <= MinZ`. The warning names which. |
| "Bounds selection is ambiguous" | Several volumes registered. Set `bPreferredBounds` on one, or give them `BoundsSelectionTag`s and call `SetRequiredBoundsTag`. It refuses to guess rather than pick an arbitrary actor. |
| Volume is pitched or rolled | Only **Yaw** is supported. Validation reports this as an error. |

---

## Performance

| Symptom | Fix |
|---|---|
| Costs more than expected | Raise `TickInterval`; raise `MoveTolerance` / `AngleTolerance`; set `MaxTrackDistance`; lower `MaxMarkersPerView`. |
| Capture is expensive | `RefreshPolicy = Manual` or `CaptureOnceWhenReady`. **Never** enable `ThrottledPeriodic` unless you need it. |
| Many small refreshes | They already coalesce inside `RefreshCoalesceSeconds`. Raise it if edits arrive in bursts. |

---

## Crashes

The plugin contains **no** click, input, mouse, trace or overlap handling — verified by
searching the entire runtime module for `OnClicked`, `OnMouse`, `LineTrace`,
`InputComponent`, `BindAction`, `OnActorClicked` and `OnComponentBeginOverlap`. Its only
`StaticMeshComponent` reference is inside `FitToGeometryBounds`, which is `#if WITH_EDITOR`
and runs only when you press the button.

So a crash while *interacting with a mesh in the level* is very unlikely to originate here.
To find out for certain, collect these three things — **without them, any "fix" is a guess**:

1. **The callstack.** After the crash, Unreal Crash Reporter shows it; copy the whole
   thing. Or open the newest `.log` in `<Project>/Saved/Crashes/…` / `<Project>/Saved/Logs/`
   and copy from `=== Critical error: ===` down.
2. **`LogMinimap` output** from the same session — it says whether the plugin was even
   active when it happened.
3. **Repro steps**: which mesh, what interaction, does it happen in PIE, standalone, or
   both, and **does it still happen with the minimap widget removed from the viewport?**

That last question is the fastest discriminator. If the crash persists with the minimap
gone, it is not this plugin.

**Useful narrowing steps:**

- Build a **Development Editor** target and reproduce with the debugger attached — the
  callstack will name the exact frame.
- Set `SetMinimapEnabled(false)` on the subsystem and retry. If the crash survives, the
  minimap update loop is not involved.
- Check whether the mesh is one of `CaptureExcludedActors` — if a level actor referenced
  there has been deleted, that soft reference is stale (the code null-checks it, but it is
  worth ruling out).
