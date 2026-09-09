# Architectural Openings

Calibrated, drift-free animation of architectural doors, windows and single-panel sliding openings
for Unreal Engine **5.8**. Built for architectural visualization and interactive presentations, not
for combat gameplay.

An artist selects meshes that already exist in the level, says which ones are the movable leaf and
which are the handles, places a hinge or a slide path, sets timing and sound, and the opening
responds to clicks or to a player walking up to it. No per-door C++ and no per-door Blueprint.

> **Build status: compiles and loads against UE 5.8.** The extraction tool has been used on real
> architectural meshes. The automated tests have not been run and no runtime motion, interaction or
> audio behaviour has been exercised in Play In Editor. See `Docs/Limitations.md` for the full,
> honest status table.

---

## Architecture at a glance

Two modules, with a hard separation:

| Module | Type | Contains |
|---|---|---|
| `ArchitecturalOpenings` | Runtime | Opening component and actor, motion solver, easing, state machine, handle animation, click and proximity interaction, obstruction queries, audio, presets, validation, Blueprint API, automation tests |
| `ArchitecturalOpeningsEditor` | Editor | Setup panel, details-panel commands, viewport visualizer, editor preview driver, assisted leaf extraction |

The runtime module depends on `Core`, `CoreUObject`, `Engine` and `PhysicsCore` only. It has no
Slate, no `UnrealEd`, no editor module of any kind, so nothing editor-only reaches a packaged build.
Editor-facing code inside the runtime module (calibration, preview, part assignment) is guarded by
`WITH_EDITOR` and uses only `Engine` and `CoreUObject` APIs.

### The one idea everything rests on

Every driven part stores **one rest transform, expressed in the opening's own local frame**:

```
RestRelative = PartRestWorld.GetRelativeTransform(CalibrationFrame)
```

and every animated pose is **recomputed** from it:

```
PartWorld = RestRelative * MotionDelta(openness) * CalibrationFrame
```

Poses are never accumulated from the previous frame. That single decision is what gives:

* no positional or rotational drift over any number of cycles,
* an exactly identical closed pose every time,
* no Euler wrapping artefacts (quaternions and axis-angle throughout, no Euler angles anywhere),
* correct behaviour at any world rotation or translation,
* a mid-motion reversal that continues from the current pose instead of snapping.

Handles compose one extra delta *before* the leaf delta, which is what makes a handle inherit the
leaf's swing while still rotating about its own pivot:

```
HandleWorld = RestRelative * HandleDelta * LeafDelta * CalibrationFrame
```

### Ownership model

The opening component **references** its parts. It never reparents them, never merges them, never
edits their assets and never modifies a Blueprint class template or removes a construction-script
component. Each mesh keeps its own actor, its own materials and its own place in the level.

Consequences, stated plainly:

* Moving the opening component carries the whole configured assembly with it (editor, opt-in, one
  transacted operation), or is reported as a stale calibration if you turn that off.
* Moving an assigned mesh by hand while closed does **not** silently recalibrate. Use
  *Set Current Pose As Closed*.
* Stationary parts are recorded but never written to during animation, so their mobility is never
  changed and baked lighting on them is never disturbed.

---

## Directory tree

```
Plugins/ArchitecturalOpenings/
├── ArchitecturalOpenings.uplugin
├── README.md
├── Docs/
│   ├── SetupGuide.md          step-by-step editor workflow
│   ├── Examples.md            three worked configurations
│   ├── TestingChecklist.md    automated + manual acceptance tests
│   └── Limitations.md         status table and explicit limitations
└── Source/
    ├── ArchitecturalOpenings/
    │   ├── ArchitecturalOpenings.Build.cs
    │   ├── Public/
    │   │   ├── ArchitecturalOpeningsModule.h
    │   │   ├── ArchOpeningLog.h                  dedicated log category
    │   │   ├── ArchOpeningTypes.h                enums, settings structs, validation types
    │   │   ├── ArchOpeningEasing.h               easing contract, evaluation, inversion
    │   │   ├── ArchOpeningSolver.h               pure transform maths (unit tested)
    │   │   ├── ArchOpeningComponent.h            the controller
    │   │   ├── ArchOpeningActor.h                convenience actor
    │   │   ├── ArchOpeningPreset.h               reusable behaviour data asset
    │   │   ├── ArchOpeningPieceSetComponent.h    live extraction session state (editor-only)
    │   │   ├── ArchOpeningExtractionProfile.h    persisted piece -> group classification
    │   │   ├── ArchOpeningSubsystem.h            click -> opening registry
    │   │   ├── ArchOpeningInteractorComponent.h  optional player helper
    │   │   └── ArchOpeningFunctionLibrary.h      Blueprint helpers
    │   └── Private/
    │       ├── ArchitecturalOpeningsModule.cpp
    │       ├── ArchOpeningEasing.cpp
    │       ├── ArchOpeningSolver.cpp
    │       ├── ArchOpeningComponent.cpp            lifecycle, calibration, pose application
    │       ├── ArchOpeningComponent_Motion.cpp     commands, state machine, timing, handles
    │       ├── ArchOpeningComponent_Interaction.cpp click, proximity, obstruction, audio
    │       ├── ArchOpeningComponent_Editor.cpp     assignment, snapping, preview (WITH_EDITOR)
    │       ├── ArchOpeningComponent_Validation.cpp validation rules
    │       ├── ArchOpeningActor.cpp
    │       ├── ArchOpeningPieceSetComponent.cpp
    │       ├── ArchOpeningPreset.cpp
    │       ├── ArchOpeningSubsystem.cpp
    │       ├── ArchOpeningInteractorComponent.cpp
    │       ├── ArchOpeningFunctionLibrary.cpp
    │       └── Tests/
    │           ├── ArchOpeningTestSupport.h            event counter for the tests
    │           ├── ArchOpeningSolverTests.cpp          handing, rotation, drift, scale, easing
    │           └── ArchOpeningStateMachineTests.cpp    cycles, reversal, timing, events
    └── ArchitecturalOpeningsEditor/
        ├── ArchitecturalOpeningsEditor.Build.cs
        ├── Public/
        │   ├── ArchitecturalOpeningsEditorModule.h
        │   └── ArchOpeningExtractionSubsystem.h    piece analysis, multi-group extraction, profiles
        └── Private/
            ├── ArchitecturalOpeningsEditorModule.cpp
            ├── ArchOpeningPreviewManager.h/.cpp     core-ticker preview, safety on PIE/save/close
            ├── ArchOpeningComponentVisualizer.h/.cpp hinge, axis, outside arrow, arc, slide, box
            ├── ArchOpeningPieceSetVisualizer.h/.cpp   live piece boxes + click-to-assign hit proxies
            ├── ArchOpeningComponentDetails.h/.cpp    details-panel commands
            ├── SArchOpeningSetupPanel.h/.cpp         the 12-step setup panel
            ├── SArchOpeningExtractionPanel.h/.cpp    multi-group leaf extraction UI
            └── ArchOpeningExtractionSubsystem.cpp
```

---

## Installation and compilation

1. Copy the `ArchitecturalOpenings` folder into your project's `Plugins/` directory, so you have
   `YourProject/Plugins/ArchitecturalOpenings/ArchitecturalOpenings.uplugin`.
2. The project must be a **C++ project**. If yours is Blueprint-only, add any C++ class once
   (*Tools > New C++ Class*) to generate the `Source` folder and the module build files.
3. Right-click `YourProject.uproject` and choose *Generate Visual Studio project files*
   (or run `GenerateProjectFiles` / `UnrealBuildTool` on Linux/macOS).
4. Build the `Development Editor` target for your platform.
5. Launch the editor and enable the plugin in *Edit > Plugins > Architecture > Architectural
   Openings*, then restart if prompted. (`EnabledByDefault` is `false`, so it must be enabled once.)

Command-line build, for reference:

```
<Engine>/Engine/Build/BatchFiles/Build.bat YourProjectEditor Win64 Development -project="<full path>/YourProject.uproject" -waitmutex
```

Packaged builds require nothing extra: the editor module is `Type: Editor` in the `.uplugin` and is
not staged.

---

## Where things live in the editor

* **Window > Architectural Openings** - the setup panel.
* **Window > Opening Leaf Extraction** - splits a one-mesh source into a stationary frame plus one
  asset per movable leaf. Pieces are drawn live in the viewport, colour-coded by group, and clicking
  one assigns it. Assignments persist in a profile asset, so extraction is re-editable rather than
  one-way.
* Select an opening actor and the **Details** panel gains an *Opening Commands* category with
  calibration, snapping, preview and a live validation summary.
* The viewport draws the hinge, hinge axis, reference outside arrow, swing arc, slide path, leaf
  bounds and proximity trigger for the selected opening.

Read `Docs/SetupGuide.md` next.

---

## Blueprint API summary

Commands on `UArchOpeningComponent`:

| Function | Behaviour |
|---|---|
| `Open` / `Close` / `Toggle` | Idempotent. A repeat while already heading that way does nothing, so no duplicated events or sounds. |
| `Stop` | Holds the current pose, cancels pending delays, fires `On Motion Stopped`. |
| `SetOpennessImmediate` | Teleports to an openness. No animation, no delays, no sounds, no transition events. |
| `SetOpennessAnimated` | Animates there with the configured timing and easing. |
| `GetOpenness` / `GetOpeningState` / `IsMoving` / `IsClosed` | State queries. |
| `SetInteractionEnabled` / `IsInteractionEnabled` | Master interaction switch. |
| `HandleClickInteraction` | Click entry point. Applies the mode and enabled checks, fires `On Interaction Accepted`, then toggles. |
| `ApplyPreset` | Applies a behaviour preset without touching assigned parts or calibrated pivots. |
| `Validate` | Returns the full validation report. |
| `SetCurrentPoseAsClosed` / `ResetToClosedPose` | Calibration. The first refuses while a preview is showing a non-closed pose. |

Two separate openness setters exist deliberately, rather than one ambiguous `SetOpenness`.

Events, each firing once per real transition (they are broadcast only from state entry, and the
endpoint events additionally require that the leaf actually travelled there):

`On Opening Started`, `On Fully Opened`, `On Closing Started`, `On Fully Closed`,
`On Motion Stopped`, `On Obstruction Detected`, `On Interaction Accepted`.

---

## Property categories

`Opening Type`, `Assigned Parts`, `Calibration`, `Hinged Motion`, `Sliding Motion`, `Timing`,
`Handles`, `Interaction`, `Proximity`, `Obstruction`, `Audio`, `Preview`, `Diagnostics`, plus the
editor-added `Opening Commands`.

Settings are enums and structured groups rather than a wall of booleans, with units, ranges,
tooltips and conditional visibility on the properties that need them.
