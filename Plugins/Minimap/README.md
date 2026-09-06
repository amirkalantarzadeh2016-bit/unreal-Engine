# Minimap Plugin (UE 5.8)

A modular, calibrated world-to-map projection and marker system. Replaces the per-Tick
`WBP_Minimap` graph with a subsystem-driven architecture, while keeping the existing
`WBP_Minimap` and `M_Minimap` assets working unchanged.

---

## 1. Module structure

```
Plugins/Minimap/
├── Minimap.uplugin
└── Source/Minimap/
    ├── Minimap.Build.cs
    ├── Public/
    │   ├── MinimapModule.h              Log category (LogMinimap)
    │   ├── MinimapTypes.h               FMinimapCalibration, FMinimapProjectionContext,
    │   │                                FMinimapMarkerStyle, FMinimapMarkerSnapshot, enums
    │   ├── MinimapFunctionLibrary.h     All projection maths, BlueprintPure, stateless
    │   ├── MinimapSubsystem.h           Registry + single batched update loop
    │   ├── MinimapTrackedComponent.h    Drop on any Actor to appear on the map
    │   ├── MinimapViewComponent.h       Describes one minimap viewport
    │   ├── MinimapBoundsVolume.h        One-click calibration actor
    │   ├── MinimapWidgetBase.h          WBP_Minimap compatibility base
    │   └── MinimapMarkerWidget.h        Pooled marker icon widget
    └── Private/
        ├── *.cpp
        └── Tests/MinimapProjectionTests.cpp
```

### Build.cs dependencies

| Scope | Modules | Why |
|---|---|---|
| Public | `Core`, `CoreUObject`, `Engine`, `UMG` | `UMG` is public because widget classes appear in public headers |
| Private | `Slate`, `SlateCore` | Widget geometry and render transforms, `.cpp` only |

No editor-only modules. `AMinimapBoundsVolume`'s editor helpers are `WITH_EDITOR`-guarded
and use only Engine APIs, so this stays a pure Runtime module and is safe in Shipping.

---

## 2. Data flow

```
AMinimapBoundsVolume ──BeginPlay──> UMinimapSubsystem (calibration + registry)
                                            │
UMinimapTrackedComponent ──BeginPlay──> registry
                                            │  one batched pass @ TickInterval (1/30 s)
                                            ▼
                              per active UMinimapViewComponent
                                  1. RefreshViewState()   -> anchor, ViewYaw, cached sin/cos
                                  2. project every marker -> TArray<FMinimapMarkerSnapshot>
                                  3. OnMinimapViewUpdated broadcast
                                            │
                                            ▼
                              UMinimapWidgetBase -> MID params + pooled marker widgets
```

The subsystem is the **only** ticking object. Markers and widgets are passive.

---

## 3. The maths

**Projection** (`FMinimapProjectionContext::ProjectXY`):

```
Rel   = WorldXY - Anchor
theta = -(MapYaw + ViewYaw)
View  = (Rel.x·cos θ - Rel.y·sin θ,  Rel.x·sin θ + Rel.y·cos θ)

Axes  = bSwapUV ? (View.x, View.y) : (View.y, -View.x)
N.x   = SignU · Axes.x / (EffExtent.x / Zoom)
N.y   = SignV · Axes.y / (EffExtent.y / Zoom)
```

`sin`/`cos` and `1/extent` are baked once per view per pass, so the per-marker cost is
2 subtractions, 6 multiplies and 2 adds — no trigonometry, no division.

**Mode table** — every behaviour is a choice of Anchor and ViewYaw, nothing else:

| Mode | Anchor | ViewYaw |
|---|---|---|
| Background pan (`PlayerX`/`PlayerY`) | `WorldCenter` | `0` |
| Fixed north-up map | `WorldCenter` | `0` |
| Fixed map, rotating | `WorldCenter` | viewer yaw |
| Viewer-centred, north-up | viewer XY | `0` |
| Viewer-centred, rotating *(default)* | viewer XY | viewer yaw |

**Output spaces**

```
Material (legacy M_Minimap):  PlayerX/Y = N · 0.5        →  -0.5 … 0.5
Standard UV:                  UV        = N · 0.5 + 0.5  →   0 … 1
UMG pixels:                   P         = UV · WidgetSize
```

**Clamping** — both scale the *whole* vector, so bearing survives:

```
circular:     if |N| > 1            →  N / |N|
rectangular:  t = 1 / max(|N.x|,|N.y|);  if t < 1  →  N · t
```

Per-axis clamping would pile every off-screen marker into the four corners. The
`RectangularClamp` test asserts collinearity specifically to catch that regression.

**Hysteresis** — enter OOB above 1.0, leave only below 0.98. Both thresholds are per-view
properties. Shape magnitude is defined so 1.0 means "exactly on the boundary" for *both*
shapes, which is what lets the same thresholds apply to either.

**Rotation**

```
MarkerIconAngle  = NormalizeAxis(ActorYaw - ViewYaw)          // degrees, [-180, 180)
MapRotationTurns = Frac((ViewYaw + MapYawOffset) / 360)       // turns, [0, 1)
CompassAngle     = NormalizeAxis(-ViewYaw)                    // degrees
```

---

## 4. Setup: dropping this into a project

1. **Copy** `Plugins/Minimap/` into your project's `Plugins/` folder. Right-click the
   `.uproject` → *Generate Visual Studio project files*, then build. Enable **Minimap**
   in *Edit → Plugins* if it is not already on.

2. **Place the bounds volume.** In the level, add a **Minimap Bounds Volume**. Scale its
   box to cover the playable area (or press **Fit To Level Bounds** in the Details panel).
   Rotate the actor if the map image is not world-axis-aligned. That is the calibration —
   it applies itself on BeginPlay.

3. **Add the view.** On your PlayerController Blueprint, add a **Minimap View** component.
   The controller is the right owner: it survives pawn death, so respawn needs no glue.
   *(If you skip this, `UMinimapWidgetBase` creates one automatically on first construct.)*

4. **Reparent the widget.** Open `WBP_Minimap` → *File → Reparent Blueprint* →
   **MinimapWidgetBase**. The existing `Background` and `North_Container` bind by name.
   Add a **Canvas Panel** named `MarkerCanvas` for markers.

5. **Wire the pop animation.** In the `WBP_Minimap` graph, add the **Handle Pop Effect**
   event, and branch on `bPlayForward` → your existing `PlayPopEffect` / `StopPopEffect`.
   One Branch node; both custom events and `Anim_MapPop` stay exactly as they are.

6. **Delete the old logic.** Remove `Event Tick` and `Event Construct` from `WBP_Minimap`,
   plus `MapActorRef`, `PlayerRef` and `MapMat_Ref`. All of it is now in C++.

7. **Set the axis convention.** On the bounds volume, tick **bSwapUV** to keep your current
   `M_Minimap` working as-is. See §6 — this is the one setting that will look wrong if
   guessed.

8. **Add markers.** Add a **Minimap Tracked** component to any Actor. Set an Icon in
   *Style*. That is the whole setup — it self-registers on BeginPlay.

9. **Set a marker widget class.** On the minimap widget, set *Default Marker Widget Class*
   to a WBP reparented to **MinimapMarkerWidget**, containing an Image named `IconImage`.

---

## 5. Debugging checklist

| Symptom | Check |
|---|---|
| Nothing moves at all | Is there a `AMinimapBoundsVolume` in the level? `LogMinimap` prints the applied calibration on BeginPlay. No line = no calibration, and the subsystem refuses to project. |
| `LogMinimap: Warning: SetCalibration rejected` | Box extent has a zero X or Y, `Zoom <= 0`, or `MaxZ <= MinZ`. The warning names which. |
| Map is mirrored | Toggle `bInvertU` (left/right) or `bInvertV` (top/bottom) on the bounds volume. |
| Map axes are 90° out / markers move perpendicular to the player | This is `bSwapUV`, not an invert. See §6. |
| Map rotates the wrong way relative to the compass | Toggle `bNegateMapRotation` on the view component. Do not edit `M_Minimap`. |
| Map jumps a full turn when facing due north | `MapRotationTurns` is not being read as *turns*. The material's Rotator node expects `[0,1)`, not degrees. |
| Everything squashed on one axis | `bPreserveAspectRatio` is off and the extent is anisotropic. Your legacy setup was 500 × 600 — a ~20% squash. |
| Off-screen markers pile into the corners | Something is clamping per-axis. `ClampNormalizedToShape` is the only correct path. |
| `OnOutOfBoundsChanged` fires constantly | Enter/exit thresholds are equal. Leave a dead band (1.0 / 0.98). |
| Markers never appear | `MaxTrackDistance` too small, height filter too tight, or `OutOfBoundsPolicy == Hide` with the marker off-map. |
| Markers stack in the top-left for one frame | The canvas has no cached geometry yet; raise `FallbackMapSize` to match your map. |
| Marker count caps out | `MaxMarkerWidgets` on the widget, or `MaxMarkersPerView` on the view. Lowest priority is dropped first. |
| Minimap freezes after respawn | Something is holding an explicit view actor. Clear `ExplicitViewActor` to restore auto-resolution. |
| Minimap keeps updating when hidden | The widget must call `SetViewRenderingEnabled(false)`. `NativeDestruct` does this; a widget merely collapsed rather than removed needs it manually. |
| Costs more than expected | Raise `TickInterval`, raise `MoveTolerance`/`AngleTolerance`, set `MaxTrackDistance`, or lower `MaxMarkersPerView`. |

Run the tests: *Window → Test Automation → Minimap*, or
`UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests Minimap;Quit" -unattended -nullrhi`

---

## 6. Assumptions affecting coordinate signs and rotation

Each of these changes what you see on screen. They are all exposed as flags — none is
baked in.

1. **World axes.** `+X` = North, `+Y` = East, `+Z` = Up. Unreal's default.

2. **Normalized space.** `N.y` grows **downward**, so `N → UV → pixels` needs no flip.
   This is why the standard mapping is `N.y = -View.x`: world north (`+X`) must become
   screen up (negative Y).

3. **`bSwapUV` — the one that matters most.** Your `M_Minimap` is fed `PlayerX` from world
   X and `PlayerY` from world Y. The standard convention drives U from world **Y**. That
   is an axis **swap**, which `bInvertU`/`bInvertV` cannot express — they only flip signs.
   The swap is applied to the rotated view vector *before* normalization, so both
   conventions stay exact under arbitrary `MapYaw` and `ViewYaw`.
   *Set `bSwapUV = true` to keep your material. Set it false for the standard convention.*

4. **Aspect ratio.** Your ranges were 1000 cm on X and 1200 cm on Y — anisotropic, so a
   circle in world became a ~20% ellipse on the map. `bPreserveAspectRatio` squares the
   effective extent using `max(X, Y)`, expanding rather than cropping so no playable area
   is hidden. **Turn it off to reproduce the legacy look exactly.**

5. **Map rotation sign.** `MapRotationTurns = Frac((ViewYaw + MapYawOffset)/360)` keeps
   your original `+Yaw` sign. Your compass used `-Yaw`. Those two are only consistent if
   `M_Minimap` negates internally — likely, but not visible from the Blueprint export.
   **If the map and compass turn opposite ways, toggle `bNegateMapRotation`.**

6. **`MapYawOffset` was the unconnected pin.** Your `Add` node feeding `MapRotation` had
   an unwired B input contributing 0. It is now a real property.

7. **Turns, not degrees.** The material `Rotator` node takes turns, so `/360` was right —
   but it needed `Frac` towards negative infinity. `Fmod` returns negative values for
   negative yaw and would make the map jump a full turn crossing due north.

8. **Edge angle** is `atan2(N.x, -N.y)`: 0 = up, clockwise-positive, matching
   `SetRenderTransformAngle` directly.

9. **`MapYaw` comes from the bounds volume's actor yaw**, so rotating the volume rotates
   the map with it. Disable via `bUseActorYawAsMapYaw`.

10. **`PlayerX`/`PlayerY` are absolute, not view-relative.** They describe where the viewer
    is on the *whole* map (anchor = `WorldCenter`, ViewYaw = 0), because the material
    applies rotation itself via `MapRotation`. Markers use the *view-relative* projection.
    Same formula, different anchor.

11. **Component delegates report the primary view only.** A marker has a different position
    in every view. For a non-primary view, bind `OnMinimapViewUpdated`, which carries the
    full per-view snapshot array.

12. **Markers are not replicated.** Each client projects from the actor transforms it
    already receives; replicating map positions would be wasted bandwidth.

---

## 7. UE API notes — verify against your engine build

The following are the points most likely to need a project-specific tweak. Everything else
is long-stable API.

| API | Note |
|---|---|
| `EAllowShrinking::No` | `MinimapSubsystem.cpp`, `RemoveAt`. This enum replaced the old `bool bAllowShrinking` in **UE 5.5**. On 5.4 or earlier use `false`. |
| `UTickableWorldSubsystem` | Available since 4.27. Requires overriding `GetStatId()`; `Initialize`/`Deinitialize` must call `Super`. |
| `FVector2D` is double-precision | UE5 aliases it to `TVector2<double>`. Float conversions are explicitly `static_cast<float>` throughout; if you see narrowing warnings, they are intentional at the API boundary. |
| `UImage::GetBrush()` | Returns `const FSlateBrush&`. Some earlier 5.x used a public `Brush` member directly. |
| `SetDesiredSizeOverride` | On `UImage` in 5.x. |
| `EAutomationTestFlags::EditorContext` | Scoped enum with `ENUM_CLASS_FLAGS` in 5.5+. Earlier versions used the unscoped `EAutomationTestFlags::` namespace form — same spelling, different underlying type. |
| `MarkAsGarbage()` | Used in tests. Replaced `MarkPendingKill()` in UE5. |
| `GetActorBounds` | `FitToLevelBounds` only; editor-only and `WITH_EDITOR`-guarded. |
| `AddChildToCanvas` | Returns `UCanvasPanelSlot*`; null-checked at the call site. |

**Conceptual compile check performed:** all headers forward-declare what they reference;
every out-of-line declaration has a definition (verified by script); `MINIMAP_API` is
applied to every exported class and struct; delegate parameter types are all UHT-legal
(`UObject*`, `USTRUCT` by const-ref, POD); `UFUNCTION`s return by value (const-reference
returns are provided as separate non-`UFUNCTION` C++ accessors, since UHT copies return
values regardless). **The code has not been compiled against an engine build** — no UE
installation is available in this environment — so treat the table above as the list of
things to check first if the build complains.

---

## 8. Test coverage

`Private/Tests/MinimapProjectionTests.cpp`, all under the `Minimap.` prefix:

| Test | Covers |
|---|---|
| `Projection.Center` | Centre → origin, under offset centre and rotation |
| `Projection.CardinalNorthUp` | N/S/E/W mapping and edge angles |
| `Projection.RotatingMap` | View yaw, MapYaw composition, rigid-rotation invariant |
| `Projection.AnchorModes` | Fixed vs viewer-centred, north-up invariance |
| `Bounds.CircularClamp` | Radial clamp, bearing preservation, boundary case |
| `Bounds.RectangularClamp` | Ray-box clamp, **collinearity** (corner-bunching regression), degenerate origin |
| `Bounds.Hysteresis` | Enter/exit thresholds, boundary dithering → ≤1 transition, NaN, inverted thresholds |
| `Calibration.Invalid` | Zero extent, zero zoom, inverted Z, **no NaN propagation**, aspect squaring |
| `Rotation.Wrapping` | Turns in `[0,1)` across ±1080°, negative yaw, offset, negation, compass, icon angles |
| `View.RespawnAndRepossession` | Resolution priority, destroyed-candidate fallthrough, repossession |
| `Compatibility.LegacyMaterialParity` | **Grid sweep vs the original `MapRangeClamped` pair**, and proof the two axis conventions differ |
| `Projection.RoundTrip` | UV round trip, `Rot2D` properties, invert flags, zoom linearity, height ratio |

The projection model was additionally verified against an independent reimplementation:
legacy material parity is exact (maximum absolute error 0 across the sampled grid).
