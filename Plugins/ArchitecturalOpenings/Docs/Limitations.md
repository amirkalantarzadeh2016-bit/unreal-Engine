# Limitations and status

## Verification status

**Compilation is unverified.** No Unreal Engine 5.8 installation (or any UE installation) was
available in the environment where this plugin was written. Nothing has been compiled, no editor has
been opened, and no test has been run. Every statement about behaviour describes what the code is
written to do, not an observed result.

The engine APIs used were chosen to be long-stable UE5 surfaces. The most version-sensitive parts,
in order of risk, are:

1. **Editor mesh-description APIs** used by extraction: `UStaticMesh::GetMeshDescription`,
   `CreateMeshDescription`, `CommitMeshDescription`, `AddSourceModel`, `GetStaticMaterials`, and the
   `FStaticMeshAttributes` / `FStaticMeshConstAttributes` accessors. These have moved and changed
   signature across UE5 releases more than anything else here.
2. **`FCollisionShape::MakeBox`** overload resolution (`FVector` vs `FVector3f`) after LWC.
3. **`Engine/OverlapResult.h`** and `CollisionShape.h` include paths.
4. **Slate declarative attribute/event overloads** in the two panels.

If any of these fail to compile, the fix is local to the file involved; none of them affect the
motion, state machine, interaction or audio design.

---

## Status table

| Feature | Implemented | Compiled | Tested | Limitations |
|---|---|---|---|---|
| Runtime data model, calibration frame, rest capture | Yes | No | Automated tests written, not run | Opening component scale is honoured only when uniform and positive; otherwise dropped to 1 with a warning |
| Drift-free pose recomputation | Yes | No | Automated (`RotatedFrameAndDrift`), not run | — |
| Hinged motion, handing and swing presets | Yes | No | Automated (`HingeHandingAndSwing`), not run | Single axis only; no tilt-and-turn dual mode |
| Sliding motion | Yes | No | Automated (`SlidingTravel`), not run | Straight-line travel only; no curved or multi-segment tracks |
| State machine, timing modes, delays, auto-close | Yes | No | Automated (`TimingModesAndDelays`), not run | Speed mode defines nominal full-travel time, not instantaneous velocity |
| Mid-motion reversal | Yes | No | Automated (`MidMotionReversal`), not run | Position continuous; **velocity is not continuous** across a reversal by design |
| Easing, curve validation and clamping | Yes | No | Automated (`ContractAndInversion`), not run | Custom curves must be non-decreasing with f(0)=0 and f(1)=1; failures fall back to Smooth Step |
| Handle groups, actuation, both return behaviours | Yes | No | Partly automated; visual behaviour pending | Rotation only; no sliding or multi-stage hardware |
| Blueprint commands and events | Yes | No | Event counts automated, not run | — |
| Click interaction + world registry | Yes | No | Not tested | Requires a clickable mesh (or proxy) that blocks the trace channel |
| Optional interactor component (centre screen / cursor) | Yes | No | Not tested | Does not create input mappings; you bind `Try Interact` yourself |
| Proximity trigger, occupancy, close-on-exit | Yes | No | Not tested | Trigger is created at BeginPlay; it does not exist in the editor world (the viewport draws it instead) |
| Combined-mode manual-close suppression | Yes | No | Not tested | Suppression is per occupancy cycle, cleared when the trigger empties |
| Obstruction queries and policies | Yes | No | Not tested | Conservative discrete multi-box overlap, **not** continuous collision detection — see below |
| Audio (all six slots, loop, fades, EndPlay cleanup) | Yes | No | Not tested | No audio assets ship with the plugin; every slot is optional |
| Presets (Data Asset) | Yes | No | Not tested | Behaviour only; carries no level references, no hinge or slide placement |
| Validation and diagnostics | Yes | No | Not tested | Runs on demand and at BeginPlay; log output is de-duplicated per issue key |
| Editor setup panel (12-step workflow) | Yes | No | Not tested | Selection-driven; no drag-and-drop asset picking |
| Details-panel commands | Yes | No | Not tested | — |
| Viewport visualizer (hinge, axis, outside, arc, slide, bounds, trigger) | Yes | No | Not tested | Drawn for the selected opening; no interactive gizmo handles |
| Editor preview + safety (PIE / save / map change / shutdown) | Yes | No | Not tested | Preview is transient and untransacted by design |
| Undo/Redo on persistent setup operations | Yes | No | Not tested | Does not cover newly created **assets** from extraction |
| Assisted leaf extraction (analysis + asset writing) | Yes | No | Not tested | LOD0 only; see the detailed list below |
| Extraction preview in the viewport | Partial | No | Not tested | Draws per-piece bounding boxes in two colours, **not** per-triangle geometry |
| Automated tests | Yes | No | Not run | Solver, easing and state machine only; nothing that needs a world |
| Multiplayer replication | **Not implemented** | — | — | Out of scope for this version |
| Double-leaf coordination, folding, roller shutters | **Not implemented** | — | — | Out of scope; the architecture leaves room for them (see below) |
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

## Extraction: what it carries and what it does not

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

**Carried over:** vertex positions; per-instance normals, tangents, binormal signs and vertex
colours; **every** UV channel; polygon-group material slot names and the matching material
assignments; the source's build settings, lightmap UV index, lightmap resolution and Nanite
settings. Normal and tangent recomputation is explicitly disabled on the new assets so the copied
data is what ships.

**Not carried over, and reported after every extraction:**

* **LODs.** Only LOD0 is read and only one LOD is written. Any LODs on the source are gone. This is
  stated in the tool before you extract and again in the result notes.
* **Simple collision primitives.** A convex hull or box authored for the whole source shape would be
  wrong for a subset of it, so none are copied. The collision option controls what the new assets get
  instead: complex-as-simple (the default, which makes them clickable), none, or the source's trace
  flag with no primitives.
* **Generated lightmap UVs** are rebuilt per new asset by the build and will not match the source's
  packing.
* **Sockets** and any custom asset metadata on the source.

**Safety:**

* The source asset is never modified. There is no in-place mode.
* Outputs are created under the folder you specify with unique names (`<Source>_Leaf`,
  `<Source>_Fixed`), and packages are only marked dirty — **nothing is saved to disk by the tool**.
* Selecting every piece is refused (it would leave nothing behind as the fixed part).
* A source with no source models, no LOD0 mesh description, or no triangles is rejected with a
  specific message rather than producing an empty asset.
* **Undo does not delete created assets.** Undo can revert level changes; new asset files must be
  deleted from the Content Browser by hand. No asset-file rollback is implemented and none is
  promised.

**Unsupported case:** if the frame and the leaf are one connected piece, no selection of whole pieces
can separate them. The tool says so plainly and produces nothing. It does not cut arbitrary geometry
and it never reports a separation it did not achieve.

**Preview limitation:** the viewport preview draws each piece's **bounding box** — selected in green,
retained in grey — not the triangles themselves. It is enough to identify pieces at a glance; it is
not a shaded preview of the split.

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
