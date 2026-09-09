# Limitations and status

## Verification status

**The plugin compiles against Unreal Engine 5.8 and loads in the editor.** That is confirmed by it
building and by the extraction tool being used on real assets. Three compile errors and one
UnrealHeaderTool error found on the first real builds have been fixed.

What is confirmed and what is not:

* **Confirmed:** both modules compile and link; the editor module loads; the extraction tool runs
  against real architectural meshes.
* **Not confirmed:** the automated tests have not been run; no runtime motion, interaction,
  proximity, obstruction or audio behaviour has been exercised in Play In Editor; nothing in the
  manual checklist has been ticked off.

Statements below about behaviour describe what the code is written to do, except where this
document says a thing has actually been observed.

---

## Status table

| Feature | Implemented | Compiled | Tested | Limitations |
|---|---|---|---|---|
| Runtime data model, calibration frame, rest capture | Yes | Yes | Automated tests written, not run | Opening component scale is honoured only when uniform and positive; otherwise dropped to 1 with a warning |
| Drift-free pose recomputation | Yes | Yes | Automated (`RotatedFrameAndDrift`), not run | — |
| Hinged motion, handing and swing presets | Yes | Yes | Automated (`HingeHandingAndSwing`), not run | Single axis only; no tilt-and-turn dual mode |
| Sliding motion | Yes | Yes | Automated (`SlidingTravel`), not run | Straight-line travel only; no curved or multi-segment tracks |
| State machine, timing modes, delays, auto-close | Yes | Yes | Automated (`TimingModesAndDelays`), not run | Speed mode defines nominal full-travel time, not instantaneous velocity |
| Mid-motion reversal | Yes | Yes | Automated (`MidMotionReversal`), not run | Position continuous; **velocity is not continuous** across a reversal by design |
| Easing, curve validation and clamping | Yes | Yes | Automated (`ContractAndInversion`), not run | Custom curves must be non-decreasing with f(0)=0 and f(1)=1; failures fall back to Smooth Step |
| Handle groups, actuation, both return behaviours | Yes | Yes | Partly automated; visual behaviour pending | Rotation only; no sliding or multi-stage hardware |
| Blueprint commands and events | Yes | Yes | Event counts automated, not run | — |
| Click interaction + world registry | Yes | Yes | Not tested | Requires a clickable mesh (or proxy) that blocks the trace channel |
| Optional interactor component (centre screen / cursor) | Yes | Yes | Not tested | Does not create input mappings; you bind `Try Interact` yourself |
| Proximity trigger, occupancy, close-on-exit | Yes | Yes | Not tested | Trigger is created at BeginPlay; it does not exist in the editor world (the viewport draws it instead) |
| Combined-mode manual-close suppression | Yes | Yes | Not tested | Suppression is per occupancy cycle, cleared when the trigger empties |
| Obstruction queries and policies | Yes | Yes | Not tested | Conservative discrete multi-box overlap, **not** continuous collision detection — see below |
| Audio (all six slots, loop, fades, EndPlay cleanup) | Yes | Yes | Not tested | No audio assets ship with the plugin; every slot is optional |
| Presets (Data Asset) | Yes | Yes | Not tested | Behaviour only; carries no level references, no hinge or slide placement |
| Validation and diagnostics | Yes | Yes | Not tested | Runs on demand and at BeginPlay; log output is de-duplicated per issue key |
| Editor setup panel (12-step workflow) | Yes | Yes | Not tested | Selection-driven; no drag-and-drop asset picking |
| Details-panel commands | Yes | Yes | Not tested | — |
| Viewport visualizer (hinge, axis, outside, arc, slide, bounds, trigger) | Yes | Yes | Not tested | Drawn for the selected opening; no interactive gizmo handles |
| Editor preview + safety (PIE / save / map change / shutdown) | Yes | Yes | Not tested | Preview is transient and untransacted by design |
| Undo/Redo on persistent setup operations | Yes | Yes | Not tested | Does not cover newly created **assets** from extraction |
| Piece analysis (connected-component decomposition) | Yes | Yes | Used on real assets | LOD0 only; see the detailed list below |
| Multi-group classification (N groups, not leaf/fixed) | Yes | Not yet | Not tested | Every populated group becomes one asset; at least two groups must be populated |
| Live colour-coded viewport drawing | Yes | Not yet | Not tested | Only draws while the temporary session actor is selected |
| Click-to-assign in the viewport (hit proxies) | Yes | Not yet | Not tested | Clicks land on a piece's bounding box, not its triangles - see below |
| Viewport-to-list and list-to-viewport hover | Yes | Not yet | Not tested | Viewport hover polls the hit proxy under the cursor; can be switched off |
| Batch selection (all / none / invert / similar) | Yes | Not yet | Not tested | "Similar" matches triangle count plus orientation-independent bounding size |
| Re-editable state (extraction profile asset) | Yes | Not yet | Not tested | Keyed on triangle ids; a re-import of the source can invalidate them |
| In-place asset update on re-extraction | Yes | Not yet | Not tested | Refused for multi-LOD targets, which fall back to a new asset |
| Spawn split actors in the level | Yes | Not yet | Not tested | Hides rather than deletes the source actor |
| Automated tests | Yes | Yes | **Not run** | Solver, easing and state machine only; nothing that needs a world |
| Multiplayer replication | **Not implemented** | — | — | Out of scope for this version |
| Double-leaf *motion* coordination, folding, roller shutters | **Not implemented** | — | — | Extraction now classifies multiple leaves, but the runtime still drives one leaf per opening component; use one opening per leaf |
| Lock-and-key system | **Not implemented** | — | — | Out of scope; `SetInteractionEnabled` is the hook |
| Destructible openings, physics-driven simulation | **Not implemented** | — | — | Out of scope by design; motion is deterministic |
| Automatic semantic recognition of imported door geometry | **Not implemented** | — | — | Deliberately not attempted |
| Automatic cutting of welded frame-and-leaf geometry | **Not implemented** | — | — | Deliberately refused, with a clear message |
| Tilt-and-turn dual-mode mechanism | **Not implemented** | — | — | Top/bottom hinge presets exist; combining modes does not |
| Sequencer integration | Not specifically implemented | — | — | Openness and settings are ordinary reflected properties, so keying them may work; this has not been verified and is not claimed |

---

## Transform and scale

* Motion is authored in the opening's **unscaled local centimetres**. A uniform, positive scale on
  the opening component is folded into the calibration frame; anything else is dropped to 1.0 and
  reported as a warning.
* Non-uniform scale on the opening is not supported for motion. Composing a rotation with a
  non-uniform parent scale shears the result, and the plugin will not silently produce sheared
  geometry. Scale the source meshes instead.
* Mirrored (negative) scale on the **opening** makes the resolved swing direction ambiguous; the
  mirror is ignored for motion and a warning tells you to check the direction and use
  `Invert Direction` if needed.
* A **part's** own scale, including non-uniform and mirrored, is preserved exactly and never
  rewritten. The plugin only ever writes a part's full world transform recomposed from its captured
  rest, so its scale round-trips unchanged.
* There is no support for shear in a part's transform. If a part's parent chain produces shear, the
  captured rest cannot represent it and the result will be wrong.

---

## Obstruction: what the query actually does

This is stated precisely because it is easy to over-claim.

* The check is a **discrete overlap test**, run at `Check Interval` (default 0.05 s) while the leaf
  is moving, against the pose `Lookahead Openness` further along the direction of travel.
* The leaf's calibrated bounding volume is divided into `Sweep Slice Count` oriented boxes along its
  longest local axis, and each is tested with `UWorld::OverlapMultiByChannel`.
* Slicing approximates the arc a rotating leaf sweeps. It does **not** make the test continuous. A
  fast-moving leaf, a very large slice, or a thin obstruction can still be missed between samples.
* **A root-component sweep would not describe the motion of the other meshes in the assembly**, and
  Unreal does not provide continuous collision detection for a rotation merely because a move is
  flagged as swept. That is exactly why this explicit query exists instead of a sweep flag, and why
  no claim of complete continuous detection is made anywhere in this plugin.
* Boxes are shrunk by 0.5 cm per axis so a leaf resting flush against its own frame or the floor
  does not register a permanent obstruction.
* The opening's own actor, every assigned part's actor, every interaction proxy's actor, and the
  proximity trigger are all ignored.
* Concave geometry is approximated by its bounding volume, so a leaf with a large cut-out will
  report obstructions inside the cut-out.
* By default only pawns count as obstructions (`Only Pawns Obstruct`). Turning that off makes any
  actor blocking the channel an obstruction, which is more expensive and much easier to trigger
  accidentally.

Physics simulation is never enabled on animated meshes as a side effect. Poses are written with
`ETeleportType::TeleportPhysics` and sweeping is deliberately off.

---

## Extraction: workflow, and what it carries

**Method.** Union-find over triangle adjacency: two triangles are in the same piece when they share
a vertex. A mesh description already shares one vertex across the several vertex *instances* that a
hard edge or UV seam creates, so **split normals and UV seams do not fragment a part**. Duplicated
*vertices* at the same position (which some exporters emit) do, and the optional weld tolerance
exists for exactly that. It defaults to **0** because too large a tolerance silently welds a leaf to
the frame it rests flush against, and quietly combining separate architectural parts is worse than
reporting more pieces than expected.

**Material sections** are shown as a selection aid only. A material section is not a physical part:
one glass or PVC material routinely spans both the fixed frame and the movable leaf, so material
identity is never used to define a leaf.

**Classification is into any number of groups.** A stationary bucket plus one movable group per leaf
- two for a double door, one per panel for a folding door. Every group holding pieces becomes
exactly one asset. Extraction is refused when fewer than two groups have pieces, because there would
be nothing to separate.

**A session never modifies the artist's actors.** It spawns one transient, editor-only holder actor
carrying the piece set, and destroys it when the tool closes. Nothing about a session can be saved
into a level.

### Viewport interaction, and its limits

* Pieces are drawn live, every frame, coloured by group. There is no snapshot to refresh and no
  wait.
* Clicking a piece assigns it to the active group. Ctrl-click ticks it without assigning;
  Shift-click adds it to the ticked set and assigns the whole set.
* **Clicks land on a piece's axis-aligned bounding box, not its triangles.** Where a small piece
  sits inside a larger one's box, the two boxes overlap and the click resolves to whichever the
  renderer put in front. Tick the row in the list instead when that happens.
* **Drawing only happens while the temporary "Opening Piece Editing" actor is selected**, because
  that is how component visualizers work. Clicking elsewhere in the level deselects it and the boxes
  disappear; re-select it to carry on.
* The optional per-triangle wireframe assigns each triangle to the **tightest bounding box that
  contains its centroid**. That is an approximation and can mis-colour triangles where boxes
  overlap. It is a visual aid; the actual split always uses the exact triangle sets from the
  adjacency pass, never the boxes.
* Viewport hover polls the hit proxy under the cursor a few times a second, and only when the cursor
  has moved. It can force a hit-proxy render; switch it off in the panel if it costs too much on a
  heavy scene.

### Re-editability

* Assignments persist in an **extraction profile** asset written next to the source mesh as
  `<SourceMeshName>_OpeningProfile`. Re-opening the tool on that mesh restores them.
* Assignments are matched by a **stable piece key** - the lowest triangle id in the piece - not by
  list position, so a re-analysis that orders pieces differently still restores the right groups.
  Pieces the profile has never seen stay stationary and are counted in the status line.
* **Re-importing the source mesh can renumber its triangles**, which invalidates the keys. The tool
  will then report most pieces as unmatched rather than silently mis-assigning them.
* Extracting again **rewrites the assets each group produced last time**, so correcting one
  misassigned piece does not create a second set of meshes and does not require re-pointing the
  actors already placed in the level. In-place update is refused for a target with more than one
  LOD (the tool only ever writes single-LOD assets, so such a target is not one of ours); that group
  falls back to a new asset and says so.
* **Group assignment changes are not on the undo stack.** The piece set lives on a transient actor,
  so there is nothing for a transaction to restore. Re-assigning a piece is one click, and the
  profile is what carries the work between sessions. Spawning split actors *is* transacted.

**Carried over into the generated assets:** vertex positions; per-instance normals, tangents,
binormal signs and vertex colours; **every** UV channel; polygon-group material slot names and the
matching material assignments; the source's build settings, lightmap UV index, lightmap resolution
and Nanite settings. Normal and tangent recomputation is explicitly disabled so the copied data is
what ships.

**Not carried over, and reported after every extraction:**

* **LODs.** Only LOD0 is read and only one LOD is written. Any LODs on the source are gone.
* **Simple collision primitives.** A convex hull or box authored for the whole source shape would be
  wrong for a subset of it, so none are copied. The collision option controls what the new assets
  get instead.
* **Generated lightmap UVs** are rebuilt per asset by the build and will not match the source's
  packing.
* **Sockets** and any custom asset metadata on the source.

**Safety:**

* The source asset is never modified. There is no in-place mode for it.
* Nothing is saved to disk by the tool: packages are marked dirty and the artist saves them.
* A source with no source models, no LOD0 mesh description, or no triangles is rejected with a
  specific message rather than producing an empty asset.
* **Undo does not delete created assets.** New asset files must be deleted from the Content Browser
  by hand. No asset-file rollback is implemented and none is promised.
* "Spawn Split Actors In Level" **hides** the source actor rather than deleting it, so a bad split
  is reversible.

**Unsupported case:** if the frame and the leaf are one connected piece, no selection of whole pieces
can separate them. The tool says so plainly and produces nothing. It does not cut arbitrary geometry
and it never reports a separation it did not achieve.

---

## Other explicit limitations

* **Cross-level assignment is rejected.** An assigned component must be in the same level as the
  opening, because a cross-level reference would not survive level streaming.
* **The proximity trigger does not exist in the editor world.** It is created at BeginPlay and
  destroyed at EndPlay; the editor draws the configured box instead. That keeps the level free of an
  extra component and stops trigger resizing from dirtying the map, but it also means you cannot
  select or gizmo the trigger — edit `Box Extent` and `Box Offset` numerically.
* **Moving the opening component after calibration** either carries the parts with it (default) or
  leaves the calibration stale and warns. It never silently animates from a mismatched frame.
* **Mobility promotion changes lighting.** A leaf promoted to Movable no longer contributes as baked
  static geometry. This is logged, documented, and never applied to stationary parts.
* **Two openings driving the same mesh** is detected and reported, but not prevented at assignment
  time across different openings — the second opening's assignment succeeds and validation flags it.
* **No interactive viewport gizmo** for the hinge or slide path. They are drawn but edited
  numerically or through the snap buttons.
* **Velocity is not continuous across a mid-motion reversal.** Position is. The reversing transition
  re-enters its easing at t = 0, so an eased profile decelerates to a stop and accelerates back the
  other way. For a door that reads correctly, and it is a deliberate choice.
* **`Handle Preparation` uses the longest `Delay Before Leaf Movement`** across the groups that
  actuate for the transition. Per-group leaf delays are not independently staged.
* **Editor preview does not run PIE logic.** No interaction, no proximity, no obstruction and (by
  default) no sound during preview.

---

## Extending this later

The architecture was shaped so the out-of-scope features can be added without unpicking it:

* **Double-leaf coordination** — a coordinator component holding two `UArchOpeningComponent`s and
  forwarding commands; the openness-driven, event-per-transition design already gives it everything
  it needs to keep two leaves in step.
* **Folding / multi-panel** — `ComputeLeafDelta` is a pure function of openness. A multi-segment
  motion type is a new branch there plus a per-segment part list; nothing about calibration, timing,
  interaction, audio or obstruction has to change.
* **Roller shutters** — a sliding variant with a curved path; again a new `ComputeLeafDelta` branch.
* **Replication** — openness plus the state enum plus the last command source is a small, complete
  replication surface, and all motion is derived from it deterministically.
* **Locking** — `SetInteractionEnabled` already gates every interaction entry point; a lock system
  is a layer above it, not a change to it.
* **Tilt-and-turn** — two hinge configurations and a mode selector; the solver already resolves an
  arbitrary axis from a preset, so a second preset set plus a mode switch is the shape of it.
