# Example configurations

These are starting points, not universal truths. Real architectural hardware varies enormously:
a lever's throw, whether a handle springs back, how fast a closer swings a door, and how far a
sliding panel travels are all specific to the product being represented. Treat every number below
as something to adjust against the reference you are visualizing.

---

## 1. Typical interior hinged door

A single leaf in a plasterboard partition, lever handles on both faces, opening into the room.

**Assigned parts**

| Role | Meshes |
|---|---|
| Stationary | Door frame / lining, architrave |
| Leaf | Door leaf slab |
| Handle group "Levers" | Interior lever, exterior lever, both escutcheons if they rotate |

**Calibration**

* Opening actor placed at the door jamb, +X pointing into the corridor (the "outside").
* `Outside Direction` `(1, 0, 0)`, `Up Direction` `(0, 0, 1)`.

**Opening Type / Hinged Motion**

| Setting | Value |
|---|---|
| Motion Type | `Hinged` |
| Axis Preset | `Vertical side hinge` |
| Hinge Side | `Left` (as seen from the corridor) |
| Swing Direction | `Inward` |
| Open Angle | `95` deg |
| Hinge Location | Press **Snap Hinge To Leaf Edge** |

**Timing**

| Setting | Value |
|---|---|
| Timing Mode | `Duration` |
| Open Duration | `1.1` s |
| Close Duration | `1.6` s (a closer shuts more slowly than a person pushes) |
| Opening Easing | `Ease In/Out` |
| Closing Easing | `Ease Out` |
| Auto Close | on, `4.0` s |

**Handles**

| Setting | Value |
|---|---|
| Pivot Location | **Snap Handle Pivot**, then nudge onto the spindle |
| Rotation Axis | Outside direction (set by the snap) |
| Rotation Angle | `-38` deg |
| Actuation Duration | `0.22` s |
| Return Duration | `0.30` s |
| Delay Before Leaf Movement | `0.16` s |
| Return Behavior | `Return after actuation` |
| Actuate On Closing | on |

This gives the classic sequence: interaction accepted, handle presses down, leaf starts opening,
handle springs back.

If the two levers visually rotate opposite ways in your model, put them in **two groups** with the
same pivot and opposite `Rotation Angle` signs.

**Interaction**

| Setting | Value |
|---|---|
| Mode | `Click and proximity` |
| Max Interaction Distance | `200` cm |
| Trace Channel | `Visibility` |
| Leaf / Handle clickable | on |
| Box Extent | `(140, 110, 100)` |
| Box Offset | `(0, 0, 100)` |
| Close On Exit | on, `2.5` s |

**Obstruction**

| Setting | Value |
|---|---|
| Policy | `Stop and wait` |
| Only Pawns Obstruct | on |
| Obstruction Channel | `Pawn` |
| Sweep Slice Count | `4` |
| Retry Delay | `1.0` s |

---

## 2. PVC glazed hinged leaf inside a larger fixed window assembly

The representative case: a wide PVC-and-glass façade element with several fixed glazed panels and
one door-sized opening leaf. Only that leaf moves.

**Assigned parts**

| Role | Meshes |
|---|---|
| Stationary | Outer PVC frame, all fixed glazing panes, all fixed mullions and transoms, adjacent façade profiles |
| Leaf | The opening leaf's PVC profile **and** its glass pane **and** its gasket/trim |
| Handle group "Window handle" | The single PVC window handle |

The point of the plugin's design is visible here: the leaf's PVC profile and its glass are separate
meshes with separate materials, and they move as one assembly **without being merged**. The fixed
panes may share the same glass material as the moving pane and are still unaffected, because roles
are assigned per object, not per material.

**Opening Type / Hinged Motion**

| Setting | Value |
|---|---|
| Motion Type | `Hinged` |
| Axis Preset | `Vertical side hinge` |
| Hinge Side | `Right` |
| Swing Direction | `Inward` (typical European inward-opening casement) |
| Open Angle | `80` deg |

For a top-hung awning vent instead, change `Axis Preset` to `Horizontal top hinge`, set
`Swing Direction` to `Outward`, and `Open Angle` to `12`-`20` deg. Snap the hinge again afterwards.

**Timing**

| Setting | Value |
|---|---|
| Timing Mode | `Speed` |
| Open Speed | `45` deg/s |
| Close Speed | `40` deg/s |
| Opening / Closing Easing | `Ease In/Out` |
| Auto Close | off |

A window is usually left where the user put it, so auto-close is off.

**Handles**

| Setting | Value |
|---|---|
| Rotation Angle | `-90` deg (PVC window handles turn a quarter turn) |
| Actuation Duration | `0.55` s |
| Return Duration | `0.55` s |
| Delay Before Leaf Movement | `0.55` s (the leaf waits for the full turn) |
| **Return Behavior** | **`Remain actuated while open`** |
| Actuate On Closing | on |

This is the case that is *not* like a room door: the handle stays turned for as long as the window
is not closed, and returns to vertical only when the leaf latches shut.

**Interaction**

| Setting | Value |
|---|---|
| Mode | `Click only` |
| Handle clickable | on |
| Leaf clickable | on |
| Stationary clickable | **off** - clicking the fixed glazing must do nothing |

If the glass mesh has collision disabled (common for archviz glass), add a thin invisible collision
box over the leaf as an `Interaction Proxy`. Validation warns you if nothing clickable blocks the
trace channel.

---

## 3. Single sliding glass panel

One sliding pane in a fixed frame, no handle animation.

**Assigned parts**

| Role | Meshes |
|---|---|
| Stationary | Track, fixed pane, fixed frame profiles |
| Leaf | Sliding pane's frame profile, its glass, its pull rail |
| Handle groups | none |

**Opening Type / Sliding Motion**

| Setting | Value |
|---|---|
| Motion Type | `Sliding` |
| Slide Direction | Press **Fit Slide To Leaf Width**, then check the viewport line |
| Travel Distance | `165` cm (or whatever the fitted value gave you) |
| Invert Direction | Toggle if it slides the wrong way |

**Timing**

| Setting | Value |
|---|---|
| Timing Mode | `Duration` |
| Open Duration | `2.0` s |
| Close Duration | `2.0` s |
| Opening / Closing Easing | `Ease In/Out` |
| Initial Openness | `0.0` |
| Auto Close | on, `6.0` s |

**Interaction**

| Setting | Value |
|---|---|
| Mode | `Proximity only` |
| Box Extent | `(200, 160, 110)` |
| Box Offset | `(0, 0, 100)` |
| Close On Exit | on, `3.0` s |
| Require Player Controlled Pawn | on |

**Audio**

| Slot | Suggestion |
|---|---|
| Open Start | short roller start |
| Movement Loop | continuous roller rumble |
| Close Start | short roller start |
| Latch | soft thud at the jamb |
| End Stop | soft thud at full travel |
| Loop Fade Out Time | `0.2` s |

The movement loop is started when the leaf begins moving and faded out whenever motion stops or
reverses, so a mid-travel reversal does not stack a second loop.

---

## Applying a preset

Save the timing, easing, handle behaviour, interaction options, obstruction policy and audio from a
door you like into an *Architectural Opening Preset*, then apply it to every other door of that type.
The parts you assigned and the hinges you placed are untouched.
