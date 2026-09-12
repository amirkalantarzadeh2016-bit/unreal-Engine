# Testing checklist

**The plugin compiles and loads against UE 5.8**, and the extraction tool has been run on real
architectural meshes. Beyond that, **nothing in this document has been executed**: the automated
tests compile but have not been run, and no manual test below has been performed.

---

## Automated tests

Run from the editor: **Tools > Session Frontend > Automation**, filter `ArchitecturalOpenings`.
From the command line:

```
UnrealEditor-Cmd.exe "<project>.uproject" -ExecCmds="Automation RunTests ArchitecturalOpenings; Quit" -unattended -nopause -testexit="Automation Test Queue Empty"
```

They are guarded by `WITH_DEV_AUTOMATION_TESTS`, so they are not compiled into shipping builds.

### `ArchitecturalOpenings.Solver.*`

| Test | Verifies |
|---|---|
| `HingeHandingAndSwing` | Viewer's left resolves to `Up x Outside`; left-hinged outward moves the free edge toward the outside; left-hinged inward moves it the other way; right-hinged outward still moves it outward; the hinge point is a fixed point of the rotation; top hinge pushes the bottom rail out and up; bottom hinge mirrors it; `Invert Direction` flips the resolved swing. |
| `RotatedFrameAndDrift` | A closed pose reproduces the captured world transform exactly at an arbitrary world rotation and translation; a full open rotates by exactly the configured angle about the frame's up axis; **200 open/close cycles leave no measurable drift**. |
| `SlidingTravel` | Travel equals the configured distance exactly; the panel does not rotate; half-open is exactly half the travel; inversion reverses direction and preserves magnitude. |
| `HandleInheritsLeafMotion` | An unactuated handle follows the leaf exactly; the handle pivot is a fixed point of the handle rotation; the composed pose applies the leaf delta **exactly once**. |
| `ScaleHandling` | Uniform, non-uniform, and mirrored scale are each detected; uniform positive scale is carried into the calibration frame; non-uniform and mirrored scale are dropped to 1 and reported; a mirrored **part** survives capture and recompose untouched. |

### `ArchitecturalOpenings.Easing.*`

| Test | Verifies |
|---|---|
| `ContractAndInversion` | Every built-in easing maps 0→0 and 1→1, stays inside [0,1], is non-decreasing, and clamps out-of-range input rather than extrapolating; inversion round-trips; `Custom` with no valid curve falls back to Smooth Step; a null curve is rejected with a reason. |

### `ArchitecturalOpenings.StateMachine.*`

| Test | Verifies |
|---|---|
| `OpenCloseCycle` | Open enters `Opening` and fires `Opening Started` once; **repeated identical commands fire nothing extra**; `Fully Opened` does not fire early and does not re-fire while ticking past the endpoint; openness lands exactly on 1 and exactly on 0; 20 further cycles each fire exactly one endpoint event and land exactly on the endpoints. |
| `MidMotionReversal` | A reversal does not move the pose in the moment it is issued (**no snap**); it moves the other way on the next tick; a short remaining distance finishes in proportional time rather than a full-length transition; rapid repeated reversals never leave [0,1] and settle coherently. |
| `TimingModesAndDelays` | Speed mode converts 90 deg at 90 deg/s into ~1 s and half speed into ~2 s; zero and negative durations snap safely instead of dividing by zero; zero speed snaps rather than stalling; a delay holds in `Opening Delay` and a reversal during the delay leaves **no stale pending action**; auto-close fires once after its delay and completes. |
| `SetOpennessAndStop` | `SetOpennessImmediate` applies at once, fires no transition events, and clamps out-of-range input; `SetOpennessAnimated` to a partial pose reports a motion stop but **not** a full-open event; `Stop` holds the pose, reports exactly one motion stop, does not fake an arrival, and a redundant `Stop` is inert. |
| `HandleSequence` | A handle group with no valid meshes does not delay the leaf; a zero-angle group does not either. |

### `ArchitecturalOpenings.Extraction.*`

| Test | Verifies |
|---|---|
| `Groups` | Default stationary + movable pair; every piece starts stationary; **group names stay unique** when a group is removed and another added, and when one is renamed onto a taken name; removing a group returns its pieces to stationary and shifts later groups' assignments without re-pointing them at the wrong group; the stationary group cannot be removed. |
| `Selection` | **Select Similar** finds identical and rotated copies of a moulding and rejects a same-sized piece with a different triangle count; assigning the ticked set moves exactly those pieces; invert / all / none / toggle behave as named; out-of-range indices are ignored rather than corrupting the set; pieces are findable by their stable key. |

---

## Manual editor tests

Every item below is **pending**.

### Motion

- [ ] A hinged door opens and closes correctly.
- [ ] A top-hinged window uses the configured axis (awning behaviour, not a side swing).
- [ ] A sliding panel travels the configured distance (measure it).
- [ ] An opening rotated arbitrarily in the level behaves identically to an axis-aligned one.
- [ ] Multiple meshes assigned as one leaf move as a rigid assembly with no relative shift.
- [ ] Fixed frame and fixed glass do not move at all during any transition.
- [ ] 50 open/close cycles in PIE leave no visible drift (covered analytically by the solver test;
      confirm visually with real geometry).
- [ ] Mid-motion reversal does not snap, with easing enabled.
- [ ] Spamming the interact key produces one open, not repeated start sounds.
- [ ] `Initial Openness` of 0.35 starts the leaf part-open at BeginPlay.
- [ ] `Open` on a door that `Stop` left half way finishes opening it (it used to do nothing).
- [ ] `Open` while an animated partial move is running retargets to fully open.
- [ ] Zero durations and zero speeds behave safely in PIE.

### Handles

- [ ] Handles visibly follow the leaf while it swings.
- [ ] Handle actuation does not double-apply the leaf motion (the handle stays on the leaf face).
- [ ] Interior and exterior handles in separate groups rotate in opposite directions.
- [ ] `Return after actuation` springs the lever back once the leaf starts moving.
- [ ] `Remain actuated while open` keeps a window handle turned until the leaf latches shut.
- [ ] Interrupting a handle actuation mid-way (reverse the command) recovers without a snap.

### Interaction

- [ ] Centre-screen interaction works with the interactor component in `Centre of screen` mode.
- [ ] Cursor interaction works with `bShowMouseCursor` on and the mode set to `Under mouse cursor`
      or `Auto`.
- [ ] `Max Interaction Distance` is enforced (standing back does nothing).
- [ ] Clicking a handle resolves to its opening, not to some other actor.
- [ ] Walking into the proximity trigger opens the opening.
- [ ] The last player leaving closes it after the configured delay.
- [ ] Re-entering during the delay cancels the close.
- [ ] Closing by hand while inside the trigger does **not** immediately reopen; leaving and
      re-entering does open it again.
- [ ] A pawn whose capsule and mesh both overlap the trigger produces one entry and one exit.
- [ ] Starting PIE with the player already inside the trigger opens the opening.
- [ ] Destroying the player pawn inside the trigger does not leave a phantom occupant.

### Safety and editor behaviour

- [ ] `Restore Pre-Preview Pose` returns every mesh to exactly its pre-preview transform.
- [ ] Preview never changes the captured closed pose; `Set Current Pose As Closed` refuses during
      preview with a clear message.
- [ ] Assigning parts, clearing roles, snapping the hinge, adding/removing handle groups and
      capturing the closed pose all Undo and Redo correctly.
- [ ] Scrubbing the preview slider for a minute does not fill the undo buffer and does not dirty
      the map on its own.
- [ ] Starting PIE while previewing restores the pose first.
- [ ] Saving a map while previewing restores the pose first, so no preview pose is saved.
- [ ] Deleting an assigned mesh actor does not crash; validation reports the missing part.
- [ ] Deleting the opening actor while previewing does not crash.
- [ ] Duplicating an opening **with** its meshes gives a fully configured, independent copy.
- [ ] Duplicating an opening **alone** clears the stale references and logs the warning.
- [ ] Save, close and reopen the level: configuration, calibration and part assignments survive.
- [ ] Package the project: it builds and runs with no reference to any editor module.
- [ ] EndPlay stops any movement loop; no audio survives leaving PIE.

### Extraction

- [ ] Analyse produces the expected number of pieces on a real door, and the boxes line up with the
      geometry in the viewport.
- [ ] Pieces are drawn live and update the instant a group colour or assignment changes - no button
      press, no wait.
- [ ] Clicking a piece in the viewport puts it in the active group and recolours it immediately.
- [ ] Ctrl-click ticks a piece without assigning it; Shift-click assigns the whole ticked set.
- [ ] Hovering a piece in the viewport highlights its row and scrolls it into view.
- [ ] Hovering the row's assign button highlights the piece in the viewport.
- [ ] Deselecting the temporary editing actor stops the drawing; re-selecting it resumes.
- [ ] **Select Similar** ticks all twelve identical mouldings from one reference piece, and does not
      also tick unrelated pieces with a coincidentally equal triangle count.
- [ ] All / None / Invert behave as named; Focus In Viewport frames the ticked pieces.
- [ ] Add Group creates a third group with a distinct colour; a double door extracts to three assets.
- [ ] Removing a group returns its pieces to the stationary bucket and does not shift other groups'
      assignments.
- [ ] Extraction is refused, with a clear message, when fewer than two groups have pieces.
- [ ] The source asset is byte-identical afterwards (check it is not marked dirty).
- [ ] Materials are preserved on every output, in the slots the subsets actually use.
- [ ] Every UV channel survives; lightmap UV index and resolution are carried over.
- [ ] The outputs together equal the source with **no duplicated geometry** (place them all and
      confirm no z-fighting).
- [ ] A welded frame-and-leaf mesh produces the clear unsupported-case message and no output.
- [ ] The LOD0-only and collision notes appear after a successful extraction.
- [ ] **Re-editability:** close the tool, re-open it on the same mesh, Analyse - the previous
      assignments come back and the status line reports how many matched.
- [ ] Change one piece's group and extract again: the **same assets are rewritten**, actors already
      placed in the level pick up the change, and no second set of assets appears.
- [ ] Extracting into a multi-LOD target falls back to a new asset and says so in the notes.
- [ ] Re-importing the source mesh is reported as unmatched pieces rather than silently
      mis-assigning them.
- [ ] Spawn Split Actors places one actor per group exactly on the source transform, sets movable
      groups to Movable mobility, and hides the source actor.
- [ ] Undo after extraction does not delete the asset files (documented, not a bug).
- [ ] Closing the tab destroys the temporary editing actor; it never appears in a saved level.
- [ ] Turning off viewport hover tracking stops the hit-proxy polling.
- [ ] Hovering anywhere on a list row (not just its button) highlights the piece in the viewport.
- [ ] Renaming a group changes the generated asset's name on the next extraction.
- [ ] Emptying a group that already generated an asset produces the stale-asset note.
- [ ] **Build Openings** on a double door creates two openings, each with the fixed parts and its
      own leaf assigned, both calibrated, with opposite handing.
- [ ] Each built opening previews and animates independently without fighting over the shared frame.

### Obstruction

- [ ] Closing onto a standing pawn stops (or reopens) rather than passing through.
- [ ] `Stop and wait` resumes once the pawn moves away and `Retry Delay` has elapsed.
- [ ] `Reopen by amount` backs off by the configured openness and then retries.
- [ ] The opening never detects its own frame, leaf, handles or trigger volume as an obstruction.
- [ ] `On Obstruction Detected` fires once per detection, not every frame.
- [ ] No sound or event loop develops while an obstruction persists.
- [ ] No animated mesh has physics simulation silently enabled (check `Simulate Physics` after a
      full setup pass).
