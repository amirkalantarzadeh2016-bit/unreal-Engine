# 03 — Configuration Guide

Every setting, what it does, and what to set it to. Defaults in **bold**.

---

## Calibration — `AMinimapBoundsVolume`

| Setting | Default | Meaning |
|---|---|---|
| `bApplyOnBeginPlay` | **true** | Push this volume's calibration on BeginPlay. |
| `bManualOverride` | **false** | Ignore the box and publish `ManualCalibration` verbatim. |
| `bUseActorYawAsMapYaw` | **true** | Take `MapYaw` from the actor's yaw, so rotating the volume rotates the map. |
| `AdditionalMapYaw` | **0** | Extra yaw for a map texture whose "up" is not the volume's forward. |
| `bPreserveAspectRatio` | **true** | Square the effective extent to `max(X, Y)`. Keeps world distances undistorted. |
| `bCircularMap` | **false** | Radial clamping and `\|N\|` magnitude instead of rectangular. |
| `Zoom` | **1.0** | 1 = whole mapped area visible. |
| `bPreferredBounds` | **false** | Marks this as authoritative when several volumes exist. |
| `BoundsSelectionTag` | **None** | Identifier for tag-based selection. |

### Axis convention — the setting most likely to look wrong

| Setting | Default | Meaning |
|---|---|---|
| `bSwapUV` | **false** | World X → U and Y → V, instead of the standard +Y → right, +X → up. |
| `bInvertU` | **false** | Mirror horizontally. |
| `bInvertV` | **false** | Mirror vertically. |

**Standard** (`bSwapUV=false`): world **+X = North = map up**, **+Y = East = map right**.

**Set `bSwapUV = true`** only to match a pre-authored material that samples U from world X.
That is an axis *swap*, not a sign flip — the invert flags cannot express it.

> **Capture restriction:** `bInvertU` must equal `bInvertV`. If exactly one is set, the
> convention is a *mirror*, which no camera orientation can reproduce, so automatic capture
> refuses to activate and validation reports it. Sign flips are fine in pairs.

### Bounds fitting

| Setting | Default | Meaning |
|---|---|---|
| `FitExcludeTags` / `FitRequireTags` | empty | Tag filters. `FitRequireTags` is an allow-list that overrides everything permissive. |
| `FitExcludeClasses` | empty | Ignore these classes (sky spheres, fog, post-process volumes). |
| `FitMaxActorExtent` | **100000** cm | Skip actors bigger than this. The main guard against one sky sphere inflating the map to kilometres. |
| `bFitIgnoreHiddenActors` | **true** | Skip hidden actors. |
| `FitPadding` | **200** cm | Uniform padding around the fitted result. |
| `MinComponentVolumeCubicMeters` | **0.02** | Geometry fit: ignore components smaller than this. Filters trim, handles, decals. |
| `bGeometryFitRequiresCollision` | **false** | Geometry fit: require collision. |
| `GeometryOutlierTrim` | **0.01** | Geometry fit: fraction of weighted mass trimmed from **each end of each axis**. This is what stops one stray mesh stretching the map. 0 disables. |

---

## Capture — `FMinimapCaptureSettings`

Lives on the preset, or on `CaptureSettingsOverride` per instance.

### Source and resolution

| Setting | Default | Meaning |
|---|---|---|
| `BackgroundSource` | **Static Texture** | `StaticTexture` (legacy) or `SceneCapture`. |
| `CaptureResolution` | **1024** | Longest edge in px, clamped 64–4096. The other edge follows the bounds aspect, so the image is never stretched. |

### Height

| Setting | Default | Meaning |
|---|---|---|
| `HeightMode` | **Auto — Above Bounds** | `AutoAboveBounds` (bounds top + margin), `AutoAboveGeometry` (scans tallest actor per refresh), `ManualWorldHeight`, `RelativeWithinBounds`. |
| `AutoHeightMargin` | **500** cm | Clearance so the camera sits above furniture, not inside it. |
| `ManualCaptureHeight` | **0** | Explicit world Z. |
| `RelativeHeightAlpha` | **1.0** | 0 = bounds floor, 1 = ceiling. |
| `CaptureDepth` | **0** | How far down to render. **0 = automatic.** ⚠️ A value smaller than the camera's drop to the floor culls the whole level and the map goes black. |

### Visibility

| Setting | Default | Meaning |
|---|---|---|
| `bHideLocalPlayerPawn` | **true** | Hide the player from the capture only. |
| `ExclusionTags` | empty | Hide tagged actors from the capture only. One actor iteration per refresh, never per frame. |
| `bUseShowOnlyList` | **false** | Invert to an allow-list. |
| `InclusionTags` | empty | Tags rendered when show-only is on. ⚠️ Empty + show-only = an empty scene. |

### Refresh

| Setting | Default | Meaning |
|---|---|---|
| `RefreshPolicy` | **Capture Once When Ready** | `Manual`, `CaptureOnceWhenReady`, `ThrottledPeriodic`. |
| `RefreshCoalesceSeconds` | **0.15** s | Requests inside this window collapse into one capture. |
| `PeriodicRefreshInterval` | **10** s | Only for `ThrottledPeriodic`. Off by default — a periodic full-scene render is the most expensive thing here. |
| `InitialCaptureDelay` | **0.25** s | Settle delay before the first capture. A convenience, **not** the readiness contract — use `NotifyMinimapContentReady()`. |
| `WarmUpPasses` | **1** | Extra `CaptureScene()` passes. A cold one-shot capture has no temporal history for TAA/Lumen; raise if the map is dark under Lumen. |

### Visual

| Setting | Default | Meaning |
|---|---|---|
| `LightingMode` | **Lit, No Shadows** | `Lit`, `LitNoShadows` (shadows/AO/contact shadows off in the capture only), `UnlitBaseColor` (no lighting at all — flattest, most legible). |
| `ExposureMode` | **Inherit Scene** | ⚠️ `Manual` ignores scene lighting; a dim interior captures black even when the game view looks correct. Leave on Inherit unless you have tuned the bias. |
| `ExposureBias` | **0** | EV offset. Ignored for Inherit Scene. |
| `FixedExposureBrightness` | **1.0** | Pinned brightness for `FixedAutoExposure`. |
| `bUseFlatCaptureLook` | **true** | Disable bloom, vignette, motion blur, DOF, fringe. Does **not** touch exposure. |
| `EdgeMaskPixels` | **4** | Stamp the render target rim with the clear colour, so a clamped sample past the edge returns black rather than smearing the edge pixel. |
| `ClearColor` | **black** | What "outside the map" looks like. |
| `bCaptureAlpha` | **false** | Selects an alpha-capable format. `FinalColorLDR` does not carry meaningful alpha, so uncovered areas remain the clear colour. |

---

## View — `UMinimapViewComponent`

| Setting | Default | Meaning |
|---|---|---|
| `OrientationMode` | **Rotating Map** | `RotatingMap` or `NorthUp`. |
| `AnchorMode` | **Viewer Centered** | `ViewerCentered` or `FixedMapCenter`. |
| `MapYawOffset` | **0** | Added before the material rotation. This is the pin the original Blueprint left unconnected. |
| `bNegateMapRotation` | **false** | Flip map rotation direction without editing the material. Toggle this if map and compass turn opposite ways. |
| `ZoomMultiplier` | **1.0** | Per-view zoom on top of the calibration's. |
| `bPrimaryView` | **true** | This view feeds the tracked components' own delegates. |
| `OutOfBoundsEnterThreshold` | **1.0** | Magnitude at which a marker goes out of bounds. |
| `OutOfBoundsExitThreshold` | **0.98** | Magnitude below which it returns. The dead band stops a marker on the boundary re-broadcasting every update. |
| `AnchorMoveTolerance` | **1.0** cm | Below this the view is not considered moved. |
| `ViewAngleTolerance` | **0.25°** | Below this the view is not considered turned. |
| `MaxMarkersPerView` | **0** | 0 = unlimited. Highest priority survives. |

### Compass float

| Setting | Default | Meaning |
|---|---|---|
| `bSmoothCompass` | **true** | Lag the compass behind the view. |
| `CompassInterpSpeed` | **7.0** | Higher = stiffer, lower = more float. 6–10 reads well; below 3 feels sluggish. |
| `CompassSettleTolerance` | **0.05°** | Snap threshold, so it cannot creep forever. |

### Zoom easing

| Setting | Default | Meaning |
|---|---|---|
| `bSmoothZoom` | **true** | Ease instead of snapping. |
| `ZoomInterpSpeed` | **8.0** | Easing rate. |
| `MinZoomMultiplier` / `MaxZoomMultiplier` | **0.5 / 4.0** | Clamp range. |
| `ZoomStep` | **1.25** | Multiplicative step per Zoom In/Out — 25% per press. |

> The view's tick enables itself only while the compass or zoom is still settling and
> disables once both arrive, so the resting cost is zero.

---

## Marker — `UMinimapTrackedComponent`

| Setting | Default | Meaning |
|---|---|---|
| `Style.Icon` | none | Texture drawn in bounds. |
| `Style.OutOfBoundsIcon` | none | Optional distinct icon (usually an arrow) while clamped. |
| `Style.Tint` / `Style.IconSize` | white / 24×24 | Appearance. |
| `Style.MarkerWidgetClass` | none | Per-marker widget override. |
| `Priority` | **0** | Higher draws on top and survives the budget first. |
| `bUseActorYaw` | **false** | Rotate the icon to the owner's facing. |
| `bMarkerVisible` | **true** | Author-facing switch. |
| `MaxTrackDistance` | **0** | Planar cull distance. 0 = unlimited. |
| `OutOfBoundsPolicy` | **Clamp To Edge** | `ClampToEdge`, `Hide`, `AlwaysShow` (unclamped). |
| `bUseHeightFilter` | **false** | Hide when too far above/below the anchor. |
| `HeightFilterAbove` / `Below` | **300 / 300** cm | Height band. |
| `MoveTolerance` | **2.0** cm | Skip re-projection below this movement. |
| `AngleTolerance` | **0.5°** | Skip re-projection below this rotation. |
| `bAutoRegister` | **true** | Register on BeginPlay. |
| `bAllowIndividualTick` | **false** | Almost never needed — the subsystem batches every marker. |

---

## Widget — `UMinimapWidgetBase`

| Setting | Default | Meaning |
|---|---|---|
| `PlayerXParameterName` | **PlayerX** | Material scalar names, matching the legacy material. |
| `PlayerYParameterName` | **PlayerY** | |
| `MapRotationParameterName` | **MapRotation** | In **turns**, `[0,1)` — the material Rotator node's unit. |
| `MapTextureParameterName` | **MapTexture** | Texture parameter that receives the capture. |
| `BackgroundApplyMode` | **Automatic** | See [02 — Usage](02-Usage.md) Step 5. |
| `MaterialUpdateTolerance` | **0.0005** | Skip scalar writes smaller than this. |
| `CompositedResolution` | **512** | Render target edge for Composited View mode. |
| `DefaultMarkerWidgetClass` | none | Marker widget class. |
| `InitialMarkerPoolSize` | **16** | Pre-warmed widgets. |
| `MaxMarkerWidgets` | **64** | Hard ceiling. 0 = unlimited. |
| `FallbackMapSize` | **256×256** | Used before the canvas has geometry, i.e. the first frame. |
| `bOrbitCardinalIndicators` | **false** | Move indicators around a ring instead of spinning in place. |
| `CardinalRingRadius` | **110** | Ring radius when orbiting. |
| `bKeepCardinalIconsUpright` | **true** | Keep letters readable while orbiting. Turn off for arrows. |

---

## Subsystem

| Function | Default | Meaning |
|---|---|---|
| `SetTickInterval` | **1/30 s** | Seconds between batched updates. |
| `SetMinimapEnabled` | **true** | Master switch. |
| `SetZoom` | — | Calibration zoom. |
| `SetRequiredBoundsTag` | **None** | Selects among several bounds volumes by tag. |

## Preset — `UMinimapPresetAsset`

Carries `CaptureSettings` plus optional calibration defaults, and is **portable**: it holds
no level-specific actor references. Those stay on the bounds volume instance
(`CaptureExcludedActors`, `CaptureIncludedActors`), so dropping a preset into another
project can never drag in a dangling actor reference.

Per-instance override: tick `bOverridePresetCaptureSettings` and edit
`CaptureSettingsOverride`.
