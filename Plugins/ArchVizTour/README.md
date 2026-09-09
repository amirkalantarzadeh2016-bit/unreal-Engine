# ArchViz Tour

Authoring, playback and offline rendering of architectural-visualization camera tours for
Unreal Engine 5.4 and 5.5.

A tour is an ordered list of **steps**. Each step either traverses a sub-range of a spline
(`ATourPath`) or blends to a placed `ACineCameraActor`; both movement models are first class and
mix freely in the same list. One authoritative world subsystem owns playback state, and the UI
binds to its delegates rather than polling it.

---

## Contents

1. [Requirements](#requirements)
2. [Installation](#installation)
3. [Five-minute quickstart](#five-minute-quickstart)
4. [Architecture](#architecture)
5. [Core concepts](#core-concepts)
6. [Render backends](#render-backends)
7. [Blueprint API reference](#blueprint-api-reference)
8. [Project settings](#project-settings)
9. [Automated tests](#automated-tests)
10. [Design notes](#design-notes)

---

## Requirements

| Item | Requirement |
| --- | --- |
| Engine | Unreal Engine 5.4 or 5.5 |
| Project type | C++ project (the plugin ships source, not binaries) |
| Required engine plugins | **Enhanced Input** (on by default in UE5) |
| Optional engine plugins | **Movie Render Pipeline** — enables render Backend A; the plugin builds and runs without it |
| Optional external tool | **ffmpeg** — enables video encoding in render Backend B; without it a frame sequence is written instead |

The plugin uses only engine-shipped modules. It adds no third-party dependencies.

---

## Installation

1. Copy the `ArchVizTour` directory into your project's `Plugins/` directory:

   ```
   <YourProject>/Plugins/ArchVizTour/
   ```

2. Right-click your `.uproject` and choose **Generate Visual Studio project files**
   (or run `GenerateProjectFiles` / `UnrealBuildTool` on Linux and macOS).

3. Build the project. The plugin is `"EnabledByDefault": true`, so no further action is needed.

4. *(Optional, for render Backend A)* Enable **Movie Render Pipeline** under
   **Edit → Plugins → Rendering**, then rebuild. `ArchVizTour.uplugin` lists it as an
   `"Optional"` dependency, so it is picked up automatically when present and skipped when not.

### Verifying the packaged-build constraint

`ArchVizTourEditor` is declared `"Type": "Editor"` in `ArchVizTour.uplugin`, so UnrealBuildTool
never compiles or stages it into a client. The two runtime modules hold no editor dependencies:

* `ArchVizTourRuntime.Build.cs` and `ArchVizTourCapture.Build.cs` name no editor module
  (`UnrealEd`, `PropertyEditor`, `AssetTools`, `Blutility`, `Sequencer`, `DesktopPlatform` all
  appear only in `ArchVizTourEditor.Build.cs`).
* Every use of an editor-only engine API in the runtime modules — `Modify()`,
  `MarkPackageDirty()`, `SetActorLabel()`, `PostEditChangeProperty()`, `PostEditMove()` — is
  inside `#if WITH_EDITOR`.
* The dependency arrow points one way only: `ArchVizTourEditor` depends on
  `ArchVizTourRuntime`; nothing depends on `ArchVizTourEditor`.

To confirm on your own project:

```
RunUAT BuildCookRun -project=<YourProject>.uproject -platform=Win64 ^
                    -clientconfig=Shipping -cook -stage -pak -build -nocompileeditor
```

---

## Five-minute quickstart

### 1. Place a path (30 s)

Drag a **Tour Path** actor into the level from the Place Actors panel. It starts as a usable
two-point straight line, so it is valid immediately.

### 2. Generate an arc (60 s)

With the actor selected, open the **Generation** section of the Details panel:

* **Generator Type** → `Arc`
* **Radius** → `800` (centimetres)
* **Sweep Angle Deg** → `120`
* **Point Count** → `6`
* Press **Generate**.

The tangents are the exact cubic-Bezier circular approximation
(`handle = Radius × 4/3 × tan(θ/4)`), not engine auto-tangents, so a slow pan traces a true
circle rather than a subtly lumpy one.

Useful follow-ups, all in the same panel or as **Call In Editor** buttons: **Snap Points To
Floor**, **Flatten To Height**, **Mirror**, **Reverse**, **Resample Uniform**, **Smooth Tangents**.

Give the actor a tag so steps can find it by name: in the Details panel, **Actor → Tags**, add
`ApproachPath`.

### 3. Create the tour (90 s)

In the Content Browser: **right-click → ArchViz Tour → Tour Sequence Preset**. Open it and fill
in the `Steps` array:

| Field | Step 0 | Step 1 |
| --- | --- | --- |
| Step Type | `Spline Move` | `Static Camera` |
| Label | `Approach` | `Atrium` |
| Duration | `0` (derive from cm/s) | `6` |
| Blend Time | `1.5` | `1.0` |
| Spline Path Ref | `ApproachPath` | — |
| Static Camera Ref | — | `AtriumCam` |

For the second step, place a `CineCameraActor` in the level and tag it `AtriumCam`.

`Duration = 0` on a `Spline Move` means *derive the length from the authored speed*, which is
why the path's `Default Speed` is in cm/s rather than in "units per frame".

### 4. Reparent your widget (60 s)

Open your existing playback UMG widget → **File → Reparent Blueprint** →
`Tour Playback Widget Base`.

Wire the buttons — one node each, no logic:

| Button | Node |
| --- | --- |
| Play | `Play` |
| Pause | `Toggle Pause` |
| Next | `Next` |
| Previous | `Previous` |
| Stop | `Stop` |
| Scrub slider `OnValueChanged` | `Scrub To Alpha` |

Drive the enable states from `Is Play Button Enabled`, `Is Next Button Enabled`, and friends, so
the widget duplicates none of the transport logic. Override the events `On State Changed`,
`On Step Changed`, `On Progress` and `On Tour Finished` for display updates — the base class
binds and unbinds them in `NativeConstruct` / `NativeDestruct`, so nothing polls on Tick.

### 5. Play (30 s)

In your level Blueprint, or wherever the UI is created:

```
Get Tour Subsystem  →  Load Tour (Preset)  →  Play Tour
```

### 6. Render (60 s)

```
Get Tour Render Subsystem  →  Start Render (Tour, Settings)
```

Leave `Settings` unconnected to use the subsystem's defaults, or make a `Tour Render Settings`
object and set resolution, frame rate, output directory and format. Bind `On Render Progress`
and `On Render Completed`.

For Movie Render Pipeline quality, first right-click the Tour Sequence Preset and choose
**Create Level Sequence**; the render backend selector then picks MRP automatically.

---

## Architecture

```
                            ┌─────────────────────────────────────────┐
                            │              YOUR UMG WIDGET            │
                            │      (reparented to the base class)     │
                            └────────────────┬────────────────────────┘
                                             │ 6 events in, ~20 calls out
                            ┌────────────────▼────────────────────────┐
                            │        UTourPlaybackWidgetBase          │
                            │  binds/unbinds every delegate; no Tick  │
                            └────────────────┬────────────────────────┘
                                             │
╔════════════════════════════════════════════▼═══════════════════════════════════════════════╗
║                            ArchVizTourRuntime  (Runtime module)                            ║
║                                                                                            ║
║   ┌──────────────────────────────────────────────────────────────────────────────────┐     ║
║   │                    UTourSubsystem : UTickableWorldSubsystem                       │     ║
║   │   the ONE state machine · Idle/Playing/Paused/Blending/Finished                   │     ║
║   │   time-based accumulation · arc-length traversal · reverse · scrub                │     ║
║   │                                                                                  │     ║
║   │   delegates ▸ OnTourStateChanged  OnStepChanged   OnTourProgress                  │     ║
║   │               OnTourFinished      OnCustomEventTag OnPresetLoaded                 │     ║
║   └───┬──────────────────────────┬────────────────────────────┬─────────────────────┬─┘     ║
║       │ SplineMove step          │ StaticCamera step          │ backend             │       ║
║       ▼                          ▼                            ▼                     ▼       ║
║   ┌────────────┐          ┌──────────────────┐   ┌──────────────────┐   ┌──────────────┐   ║
║   │ ATourPath  │          │ ACineCameraActor │   │ Procedural       │   │ Sequencer    │   ║
║   │ ┌────────┐ │          │  (level-placed,  │   │ (default; ticks  │   │ (ULevelSeq   │   ║
║   │ │Points  │◄┼── auth.  │   found by tag)  │   │  the step list)  │   │  Player)     │   ║
║   │ └───┬────┘ │          └──────────────────┘   └──────────────────┘   └──────────────┘   ║
║   │     │ SyncSplineFromPoints (one-way)                                                    ║
║   │ ┌───▼─────────────────┐                          ┌──────────────────────────────┐      ║
║   │ │ UTourSplineComponent│ ── arc-length table ───► │      ATourCameraRig          │      ║
║   │ └─────────────────────┘                          │  UCineCameraComponent        │      ║
║   │ ┌─────────────────────┐                          │  QInterpTo damping · shake   │      ║
║   │ │ USplineMeshComponent│  pooled rail visual      └──────────────────────────────┘      ║
║   │ └─────────────────────┘                                                                 ║
║   └────────────┘                                                                            ║
║                                                                                            ║
║   DATA                              MATH                          PERSISTENCE               ║
║   ┌────────────────────┐   ┌──────────────────────┐   ┌──────────────────────────────┐     ║
║   │ FTourPoint         │   │ UTourGeometryLibrary │   │ UTourPathPreset  (asset)     │     ║
║   │ FTourStep          │   │  GenerateArc/Helix/  │   │ UTourSequencePreset (asset)  │     ║
║   │ FTourPathData      │   │  Orbit/DollyLine     │   │ UTourSaveGame    (slot)      │     ║
║   │ FTourCameraState   │   │  Resample · Smooth   │   │ UTourPersistenceLibrary      │     ║
║   │ ETourState/StepType│   │  ArcLength · Ease    │   │ JSON via FJsonObjectConverter│     ║
║   └────────────────────┘   └──────────────────────┘   └──────────────────────────────┘     ║
╚════════════════════════════════════════════════════════════════════════════════════════════╝
        ▲                                                              ▲
        │ depends on                                                   │ depends on
╔═══════╧══════════════════════════════╗           ╔═══════════════════╧════════════════════╗
║  ArchVizTourEditor  (Editor module)  ║           ║ ArchVizTourCapture  (Runtime module)   ║
║  ── NEVER in a packaged client ──    ║           ║                                        ║
║                                      ║           ║  UTourRenderSubsystem                  ║
║  FTourPathComponentVisualizer        ║           ║   : UGameInstanceSubsystem             ║
║  FTourPathDetails (generation panel) ║           ║   StartRender / CancelRender            ║
║  UTourPathPresetFactory + 2 more     ║           ║   OnRenderProgress/OnRenderCompleted    ║
║  FAssetTypeActions_Tour* (JSON, bake)║           ║              │                          ║
║  UTourPathPresetThumbnailRenderer    ║           ║      ┌───────▼────────┐                 ║
║  UTourSequenceBuilder ──── bakes ────╫──────────►║      │ITourRenderBackend│ (FGCObject)   ║
║  UTourEditorSettings                 ║ ULevelSeq ║      └───┬────────┬───┘                 ║
║  UTourPresetManagerWidgetBase        ║           ║          │        │                     ║
╚══════════════════════════════════════╝           ║   Backend A     Backend B               ║
                                                   ║   MoviePipeline SceneCapture            ║
                                                   ║   (optional)    + ImageWriteQueue       ║
                                                   ║                 + ffmpeg (optional)     ║
                                                   ╚════════════════════════════════════════╝
```

### Data flow of one frame

```
UWorld::GetDeltaSeconds()                 already carries AWorldSettings time dilation
        │
        ├─ × TimeScale × GlobalTimeScale × PlaybackDirection
        ▼
StepElapsed  ──► ComputeSplineDistance()  walks the step's dwell/ease timeline
        │                                  → distance in cm
        ▼
ATourPath::EvaluateAtDistance(cm)         USplineComponent arc-length table
        │                                  rotation: explicit → LookAt → tangent
        ▼
FTourCameraState { Location, Rotation, FocalLength, Aperture, FocusDistance }
        │
        ▼
ATourCameraRig::ApplyState(State, dt)     optional QInterpTo damping
        │
        ▼
APlayerController view target             set once per step, not per frame
```

---

## Core concepts

### Points are authoritative; the spline is a cache

`ATourPath::Points` is the source of truth. `SyncSplineFromPoints()` pushes it into the
`UTourSplineComponent`, which supplies the arc-length reparameterization table. The flow is
one-directional on purpose — a two-way sync always ends up with two halves that disagree about
which side changed last. `SyncPointsFromSpline()` is the escape hatch for a path edited with the
stock spline tools.

### Rotation resolution order

Per point: **explicit rotation → LookAt target → spline tangent.**

`FTourPoint::bUseExplicitRotation` is what distinguishes an authored `FRotator::ZeroRotator` from
an unauthored one. Without it, every point that happens to face world +X would silently fall
through to the tangent. (This field is an addition to the specified field list; the schema-3
migration sets it from the old implicit rule, so existing data keeps its authored look.)

### Speed is in cm/s and is physically constant

Traversal is arc-length parameterized through `USplineComponent`'s distance table, so equal time
steps are equal *spatial* steps regardless of point spacing or framerate. A step with
`Duration = 0` derives its length from the authored speed; a step with a positive `Duration`
distributes that time across the path in proportion to arc length. Either way the motion is
uniform.

### Dwell and ease

A `Spline Move` step is decomposed into sub-moves separated at every point inside its range that
has a non-zero `DwellTime`. Each sub-move gets its own eased ramp, so the camera settles into a
hold and pulls away from it. `Duration` is the *moving* time; dwells are added on top, so adding
a hold never silently speeds up the rest of the move. With no dwells and no easing the
decomposition collapses to a single linear sub-move — exactly constant cm/s.

Easing is a CSS-style cubic Bezier with control points `(EaseIn, 0)` and `(1 − EaseOut, 1)`,
inverted by Newton-Raphson. `UTourSequenceBuilder` emits the same shape as Sequencer tangent
weights, so procedural playback and a baked sequence accelerate identically.

### Steps are the UI's unit

Next / Previous move between steps, never between individual spline points. `Previous` more than
`RestartStepThreshold` seconds into a step restarts that step, matching every media transport
control. At the last step of a linear tour, `Next` finishes the tour; on a looping tour it wraps.

### Failure is a warning and a skip, never a crash

Missing `ATourPath`, missing `ACineCameraActor`, empty preset, single-point spline, zero-duration
step, absent player controller, world teardown mid-blend — each logs to `LogArchVizTour` and
degrades. `BeginStep` walks over unresolvable steps with a bounded guard; step references are
re-resolved on every entry, so a path that streams in (or is spawned by
`RegisterRuntimePath`) starts working without a reload.

---

## Render backends

Both backends implement `ITourRenderBackend` and are always compiled into the module's
interface; `UTourRenderSubsystem` never branches on which one is present.

### Backend A — Movie Render Pipeline

| | |
| --- | --- |
| **Requires** | The **Movie Render Pipeline** engine plugin, enabled *and packaged* |
| **Also requires** | The tour must have a baked `ULevelSequence` (right-click the preset → **Create Level Sequence**) |
| **Modules** | `MovieRenderPipelineCore`, `MovieRenderPipelineRenderPasses`, `MovieRenderPipelineSettings` |
| **Quality controls** | `SpatialSampleCount`, `TemporalSampleCount`, `EngineWarmUpFrameCount`, `bFlushGrass`, `bUseCinematicQuality` |
| **If unavailable** | `IsAvailable()` returns false with the exact reason. `Backend = Automatic` falls back to Backend B and logs why; `Backend = MoviePipeline` refuses the job rather than silently substituting — a user who asked for MRP quality and got a live capture would ship the wrong thing |

`ArchVizTour.uplugin` lists MovieRenderPipeline as `"Optional": true`, and
`ArchVizTourCapture.Build.cs` probes the engine and project plugin trees for
`MovieRenderPipeline.uplugin` before naming any of its modules. Naming a module from an absent
plugin is a hard UnrealBuildTool error, not a link-time one, so the probe is what lets the plugin
build in a project that has never heard of MRP. The result is published as
`WITH_ARCHVIZTOUR_MRP` (0 or 1), which every Backend A source file keys off.

The pipeline is driven through `UMoviePipeline` directly rather than through an executor:
`UMoviePipelinePIEExecutor` is editor-only and `UMoviePipelineLinearExecutorBase` is abstract, so
neither can be instantiated in a packaged game. `UMoviePipeline::Initialize` installs the
engine-tick hooks the render needs, which is what an executor would have arranged anyway.

Progress reporting is coarse for this backend by design — the pipeline owns its own frame loop
and accumulates an arbitrary number of sub-samples plus warm-up frames per output frame, so
counting engine ticks would report a number unrelated to how much of the render is done. MRP's
own on-screen widget and log report per-frame progress.

### Backend B — SceneCapture + ImageWriteQueue + ffmpeg

| | |
| --- | --- |
| **Requires** | Nothing beyond core engine. Available on every platform, in every configuration |
| **Frame accuracy** | `FApp::SetUseFixedTimeStep(true)` + `FApp::SetFixedDeltaTime(1/FPS)` for the duration of the job, restored afterwards. One game frame is one output frame regardless of how long it took to render |
| **Readback** | Enqueued on the render thread (`RHICmdList.ReadSurfaceData` / `ReadSurfaceFloatData`), handed to `ImageWriteQueue` as `TImagePixelData`. The game thread never blocks on a GPU fence or on file I/O; a bounded backpressure window keeps the queue from growing without limit |
| **Formats** | PNG / JPEG via an `RTF_RGBA8` target, EXR via `RTF_RGBA16f` |
| **Resolution** | Up to 4096 × 4096 |
| **Encoding** | Launches ffmpeg through `FPlatformProcess::CreateProc` with a configurable argument template, parsing `frame=` from stderr for progress |
| **If ffmpeg is missing** | The frame sequence is kept, the job still reports success for the render, and the encoder error is surfaced through `OnRenderCompleted`. The frames are the expensive part and are perfectly usable on their own |
| **Limitation** | `SceneCapture2D` renders the scene only. `bIncludeUI` is not achievable here and logs a warning rather than silently producing frames with no UI |

### Platform notes

| Platform | Backend A | Backend B | ffmpeg |
| --- | --- | --- | --- |
| Windows | Yes, if MRP is packaged | Yes | Bundle it or set the path in project settings |
| Linux | Yes, if MRP is packaged | Yes | Usually on `PATH` |
| macOS | Yes, if MRP is packaged | Yes | Usually via Homebrew |
| Consoles / mobile | Not supported by MRP | Renders frames; writing them depends on the platform's filesystem access | Not available — expect a frame sequence at best |

Both backends auto-hide every `ATourPath` rail mesh that was visible before the render and
restore exactly those afterwards, so a rail the user had deliberately hidden is never switched
back on.

---

## Blueprint API reference

Every function below is callable from Blueprint. Category prefixes are shown without the leading
`ArchViz Tour|`.

### `UTourSubsystem` — world subsystem, the only object the UI needs

| Function | Kind | Category |
| --- | --- | --- |
| `Get Tour Subsystem` | Pure | ArchViz Tour |
| `Load Tour` | Callable | ArchViz Tour |
| `Load Tour Async` | Callable | ArchViz Tour |
| `Get Loaded Tour` | Pure | ArchViz Tour |
| `Play Tour` | Callable | ArchViz Tour |
| `Stop Tour` | Callable | ArchViz Tour |
| `Set Paused` | Callable | ArchViz Tour |
| `Toggle Pause` | Callable | ArchViz Tour |
| `Next Step` | Callable | ArchViz Tour |
| `Previous Step` | Callable | ArchViz Tour |
| `Jump To Step` | Callable | ArchViz Tour |
| `Restart Tour` | Callable | ArchViz Tour |
| `Set Time Scale` | Callable | ArchViz Tour |
| `Get Time Scale` | Pure | ArchViz Tour |
| `Scrub To Alpha` | Callable | ArchViz Tour |
| `Set Playback Direction` | Callable | ArchViz Tour |
| `Get Playback Direction` | Pure | ArchViz Tour |
| `Get State` | Pure | ArchViz Tour |
| `Get Current Step Index` | Pure | ArchViz Tour |
| `Get Step Count` | Pure | ArchViz Tour |
| `Get Current Step Label` | Pure | ArchViz Tour |
| `Get Tour Progress` | Pure | ArchViz Tour |
| `Get Step Progress` | Pure | ArchViz Tour |
| `Get Step Labels` | Pure | ArchViz Tour |
| `Get Tour Times` | Pure | ArchViz Tour |
| `Is Play Button Enabled` | Pure | UI |
| `Is Pause Button Enabled` | Pure | UI |
| `Is Stop Button Enabled` | Pure | UI |
| `Is Next Button Enabled` | Pure | UI |
| `Is Previous Button Enabled` | Pure | UI |
| `Register Runtime Path` | Callable | Authoring |
| `Clear Runtime Paths` | Callable | Authoring |
| `Get Or Spawn Camera Rig` | Callable | ArchViz Tour |

**Delegates** (all dynamic multicast — bind, do not poll):
`OnTourStateChanged(ETourState Old, ETourState New)` ·
`OnStepChanged(int32 Index, const FText& Label)` ·
`OnTourProgress(float TourAlpha, float StepAlpha)` ·
`OnTourFinished(bool bWasInterrupted)` ·
`OnCustomEventTag(FGameplayTag Tag)` ·
`OnPresetLoaded(UTourSequencePreset* Preset)`

### `ATourPath` — a camera path in the level

| Function | Kind | Category |
| --- | --- | --- |
| `Build Path Data` | Callable | Path |
| `Apply Path Data` | Callable | Path |
| `Sync Spline From Points` | Callable | Path |
| `Sync Points From Spline` | Callable · Call In Editor | Path |
| `Evaluate At Distance` | Callable | Path |
| `Evaluate At Input Key` | Callable | Path |
| `Get Path Length` | Pure | Path |
| `Get Distance At Input Key` | Pure | Path |
| `Get Speed At Distance` | Pure | Path |
| `Is Traversable` | Pure | Path |
| `Generate Arc Rail` | Callable · Call In Editor | Actions |
| `Mirror Path` | Callable · Call In Editor | Actions |
| `Reverse Path` | Callable · Call In Editor | Actions |
| `Snap Points To Floor` | Callable · Call In Editor | Actions |
| `Flatten To Height` | Callable · Call In Editor | Actions |
| `Rebuild Rail Meshes` | Callable · Call In Editor | Actions |
| `Save To Preset` | Callable · Call In Editor | Actions |
| `Load From Preset` | Callable · Call In Editor | Actions |
| `Save To Preset Asset` | Callable | Path |
| `Load From Preset Asset` | Callable | Path |

### `ATourCameraRig` — the camera driven during spline steps

| Function | Kind | Category |
| --- | --- | --- |
| `Apply State` | Callable | Camera Rig |
| `Reset Smoothing` | Callable | Camera Rig |
| `Start Camera Shake` | Callable | Camera Rig |
| `Stop Camera Shake` | Callable | Camera Rig |
| `Get Last Applied State` | Pure | Camera Rig |

### `UTourGeometryLibrary` — pure static path mathematics

| Function | Kind | Category |
| --- | --- | --- |
| `Evaluate Position At Key` | Pure | Geometry |
| `Evaluate Tangent At Key` | Pure | Geometry |
| `Evaluate Position At Distance` | Pure | Geometry |
| `Compute Arc Length` | Pure | Geometry |
| `Generate Arc` | Callable | Geometry |
| `Generate Helix` | Callable | Geometry |
| `Generate Orbit Around` | Callable | Geometry |
| `Generate Dolly Line` | Callable | Geometry |
| `Generate From Params` | Callable | Geometry |
| `Resample Uniform` | Callable | Geometry |
| `Smooth Tangents` | Callable | Geometry |
| `Mirror Path` | Callable | Geometry |
| `Reverse Path` | Callable | Geometry |
| `Flatten To Height` | Callable | Geometry |
| `Evaluate Ease` | Pure | Geometry |

### `UTourPlaybackWidgetBase` — reparent your UMG widget to this

| Function | Kind | Category |
| --- | --- | --- |
| `On State Changed` | Implementable Event | Events |
| `On Step Changed` | Implementable Event | Events |
| `On Progress` | Implementable Event | Events |
| `On Tour Finished` | Implementable Event | Events |
| `On Custom Event` | Implementable Event | Events |
| `On Tour Loaded` | Implementable Event | Events |
| `Play` | Callable | Transport |
| `Pause` | Callable | Transport |
| `Toggle Pause` | Callable | Transport |
| `Stop` | Callable | Transport |
| `Next` | Callable | Transport |
| `Previous` | Callable | Transport |
| `Restart` | Callable | Transport |
| `Jump To Step` | Callable | Transport |
| `Scrub To Alpha` | Callable | Transport |
| `Set Time Scale` | Callable | Transport |
| `Set Reversed` | Callable | Transport |
| `Load Tour` | Callable | Transport |
| `Is Play Button Enabled` | Pure | UI |
| `Is Pause Button Enabled` | Pure | UI |
| `Is Stop Button Enabled` | Pure | UI |
| `Is Next Button Enabled` | Pure | UI |
| `Is Previous Button Enabled` | Pure | UI |
| `Get Tour State` | Pure | UI |
| `Get Current Step Index` | Pure | UI |
| `Get Step Count` | Pure | UI |
| `Get Current Step Label` | Pure | UI |
| `Get Step Labels` | Pure | UI |
| `Get Tour Progress` | Pure | UI |
| `Get Step Progress` | Pure | UI |
| `Get Formatted Time` | Pure | UI |
| `Get Tour Subsystem` | Pure | UI |

### `UTourAuthoringWidgetBase` — optional in-game authoring

| Function | Kind | Category |
| --- | --- | --- |
| `Add Point From Current View` | Callable | Authoring |
| `Insert Point` | Callable | Authoring |
| `Delete Point` | Callable | Authoring |
| `Reorder Point` | Callable | Authoring |
| `Duplicate Point` | Callable | Authoring |
| `Update Point From Current View` | Callable | Authoring |
| `Get Point` | Callable | Authoring |
| `Set Point` | Callable | Authoring |
| `Get Point Count` | Pure | Authoring |
| `Duplicate Preset` | Callable | Authoring |
| `Create Preset From Target Path` | Callable | Authoring |
| `Save Current Tour To Slot` | Callable | Authoring |
| `Load Tour From Slot` | Callable | Authoring |
| `Enumerate Slots` | Callable | Authoring |
| `Delete Slot` | Callable | Authoring |
| `On Path Edited` | Implementable Event | Authoring |
| `Get Tour Subsystem` | Pure | Authoring |

### `UTourPersistenceLibrary` — save-slot persistence for packaged builds

| Function | Kind | Category |
| --- | --- | --- |
| `Save Tour To Slot` | Callable | Persistence |
| `Save Tour Data To Slot` | Callable | Persistence |
| `Load Tour From Slot` | Callable | Persistence |
| `Load Tour Preset From Slot` | Callable | Persistence |
| `Enumerate Tour Slots` | Callable | Persistence |
| `Delete Tour Slot` | Callable | Persistence |
| `Does Tour Slot Exist` | Pure | Persistence |

### `UTourRenderSubsystem` — game instance subsystem

| Function | Kind | Category |
| --- | --- | --- |
| `Get Tour Render Subsystem` | Pure | Render |
| `Start Render` | Callable | Render |
| `Cancel Render` | Callable | Render |
| `Is Rendering` | Pure | Render |
| `Get Active Backend Name` | Pure | Render |
| `Is Backend Available` | Callable | Render |

**Delegates:** `OnRenderProgress(float Alpha, int32 Frame, int32 TotalFrames)` ·
`OnRenderCompleted(bool bSuccess, const FString& OutputPath, const FString& Error)`

### `UTourRenderSettings`

| Function | Kind | Category |
| --- | --- | --- |
| `Get Resolved Output Directory` | Pure | Render |
| `Get Image Extension` | Pure | Render |
| `Build Relative File Name` | Pure | Render |
| `Compute Frame Count` | Pure | Render |

### `UTourCaptureComponent`

| Function | Kind | Category |
| --- | --- | --- |
| `Configure` | Callable | Capture |
| `Set View` | Callable | Capture |
| `Capture Frame` | Callable | Capture |
| `Is Configured` | Pure | Capture |
| `Release Resources` | Callable | Capture |

### Assets

| Class | Function | Kind |
| --- | --- | --- |
| `UTourPathPreset` | `Export To Json` | Callable |
| `UTourPathPreset` | `Import From Json` | Callable |
| `UTourSequencePreset` | `Get Total Duration` | Pure |
| `UTourSequencePreset` | `Is Playable` | Pure |
| `UTourSequencePreset` | `Export To Json` | Callable |
| `UTourSequencePreset` | `Import From Json` | Callable |

### Editor-only (`ArchVizTourEditor`, never in a packaged client)

| Class | Function | Kind |
| --- | --- | --- |
| `UTourSequenceBuilder` | `Build Level Sequence` | Callable |
| `UTourSequenceBuilder` | `Import From Level Sequence` | Callable |
| `UTourPresetManagerWidgetBase` | `Get All Path Presets` | Callable |
| `UTourPresetManagerWidgetBase` | `Get All Tour Presets` | Callable |
| `UTourPresetManagerWidgetBase` | `Get Tour Paths In Level` | Callable |
| `UTourPresetManagerWidgetBase` | `Apply Preset To Path` | Callable |
| `UTourPresetManagerWidgetBase` | `Save Path To Preset` | Callable |
| `UTourPresetManagerWidgetBase` | `Bake Tour To Level Sequence` | Callable |
| `UTourPresetManagerWidgetBase` | `Export Path Preset To Json` | Callable |
| `UTourPresetManagerWidgetBase` | `Import Path Preset From Json` | Callable |

---

## Project settings

### Project Settings → Plugins → **ArchViz Tour (Runtime)**

Read by a packaged client. `Enable Default Input`, `Input Config`, `Default Path Speed`,
`Default Blend Time`, `Use Unpaused Delta Time`.

Enhanced Input is **off by default**: Space, Escape and the arrow keys almost always already mean
something in the host project, and silently claiming them is worse than making the user opt in.

### Project Settings → Plugins → **ArchViz Tour**

Editor-only authoring defaults: path defaults, viewport visualization colours and toggles,
render defaults, the ffmpeg path, and the content directory new Level Sequence bakes are
written into.

---

## Automated tests

Run from the editor's **Session Frontend → Automation**, or headless:

```
UnrealEditor-Cmd.exe <YourProject>.uproject ^
  -ExecCmds="Automation RunTests ArchVizTour; Quit" -unattended -nopause -nullrhi
```

| Test | What it proves |
| --- | --- |
| `ArchVizTour.Geometry.ArcTangentsMatchAnalyticCircle` | Generated arcs stay within **0.1 % of the radius** of a true circle across six cases including the worst the generator allows (one 90° segment, which measures ≈0.027 %). Also pins the Bezier handle constant at 0.5522847·R for a quarter turn |
| `ArchVizTour.Geometry.ArcLengthParameterisationIsConstantSpeed` | Uniform distance steps produce uniform spatial steps on a line with 0/100/1200/1400 cm point spacing, and on a 270° arc whose measured length matches `R·θ` to 0.1 % |
| `ArchVizTour.Geometry.DegenerateInputIsHandled` | Zero radius, zero-length dolly, null orbit target, single-point and empty paths all degrade rather than crash or divide by zero |
| `ArchVizTour.Geometry.EasingIsMonotonicAndBounded` | Unweighted easing is the identity; every weighting is monotonic, bounded to 0..1, and clamps out-of-range input |
| `ArchVizTour.Serialization.TourPathJsonRoundTrip` | Every `FTourPoint` field, the generator parameters, the transform, the GUID and the schema version survive a JSON round trip |
| `ArchVizTour.Serialization.TourSequenceJsonRoundTrip` | Every `FTourStep` field including blend parameters survives a round trip |
| `ArchVizTour.Serialization.SchemaMigration` | Schema 1→3 rescales cm/frame speeds to cm/s and derives `bUseExplicitRotation`; schema 2→3 does *not* rescale again; step schema 1→2 preserves the historical hard cut; current-schema data is left untouched |
| `ArchVizTour.Subsystem.StepIndexBounds` | Queries on an empty subsystem are safe; empty presets are refused; out-of-range jumps clamp; scrubbing clamps at both ends; zero and negative time scales are rejected |
| `ArchVizTour.Subsystem.NextPreviousAtTourBoundaries` | Linear, looping and single-step tours each behave correctly at both ends: Previous restarts the first step, Next finishes a linear tour, a looping tour wraps both ways |
| `ArchVizTour.Subsystem.PauseAndResume` | Pausing an idle tour is a no-op; toggle round-trips; Play on a paused tour resumes in place rather than restarting; Restart returns to step 0 |

Profiling: `stat ArchVizTour` shows cycle counters for the subsystem tick, path evaluation, rig
state application and rail rebuild.

---

## Design notes

Non-obvious decisions are explained in comments at the code that implements them. The ones worth
knowing before reading:

* **Hermite tangents are 3× the Bezier handle.** `handle = R·(4/3)·tan(θ/4)` is the Bezier
  control-point offset; `USplineComponent` stores Hermite tangents, and for a unit-parameter
  segment `T = 3·handle`. Storing the handle directly flattens the arc by a third — the classic
  version of this bug. See `UTourGeometryLibrary::GenerateArc`.
* **The arc-length table samples adaptively.** A fixed sample count per segment makes accuracy
  depend on how long each segment happens to be. Sample counts are sized from a cheap chord-length
  estimate so spacing — and therefore interpolation error — stays roughly constant.
* **The subsystem ticks as a `UTickableWorldSubsystem`.** A tour is world state, not a placeable
  thing; an actor spawned purely to obtain a Tick shows up in the outliner, in saves, and in every
  "what is this?" conversation afterwards.
* **Rail meshes are pooled and adopted.** Surplus components are parked, not destroyed — component
  churn on every property edit costs a render-state rebuild per segment. The pool is rebuilt from
  the components that actually exist, so a Blueprint reconstruction that destroys them behind our
  back is handled rather than crashed on.
* **Step-transition overshoot is carried forward.** `BeginStep` resets elapsed time, so the
  remainder has to be re-added *after* entering the next step; subtracting first makes playback
  drift slower than real time on every transition.
* **`FScopedTransaction` is never referenced from a runtime module.** It lives in `UnrealEd`.
  `CallInEditor` buttons are invoked by `FObjectDetails::ExecuteEditorFunction`, which already
  opens a transaction, so `Modify()` alone makes them undoable.
* **Render backends derive from `FGCObject`.** They own `UObject`s across frames; a bare C++ object
  holding a raw `UObject` pointer is a collected pointer waiting to happen.
* **No binary assets ship.** The Editor Utility Widget and the Enhanced Input context are C++ base
  classes and a data-asset type instead — see `Content/README.txt` for why and for the suggested
  key bindings.
