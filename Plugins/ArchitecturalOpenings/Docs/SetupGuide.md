# Setup guide

This is the whole workflow, in the order the setup panel presents it.
Open it with **Window > Architectural Openings**.

---

## Before you start

You do **not** need to change any pivot in your modelling package. The plugin calibrates against
wherever the meshes already are, and the hinge is a position you place in the editor, not a mesh
origin.

What you do need:

* The meshes present in the level, positioned as they look when the opening is **shut**.
* The leaf as separate mesh objects from the frame. If your leaf is a separate *piece of geometry*
  inside one static mesh asset, use the extraction tool (step "Optional" below) first. If it is
  welded to the frame, it cannot be separated here - see `Limitations.md`.

---

## 1. Select or assign source objects

Either select an existing actor that already has an *Architectural Opening* component, or select
the meshes of the opening and press **Create Opening Here**. That spawns an `ArchOpening` actor at
the centre of the selection, borrowing the first selected mesh's yaw so the opening's local frame
lines up with the geometry.

Place and rotate the opening actor so that:

* its origin sits at the opening (anywhere sensible - the jamb or the leaf centre both work),
* its **+X axis points to the reference "outside" face** of the opening.

That +X convention is the default `Outside Direction`; you can change the direction vector under
*Calibration* instead of rotating the actor if you prefer. The viewport draws it as a blue arrow
labelled "Outside".

Everything about handing and swing direction is measured from that arrow, in the opening's local
space, so **rotating the opening in the level never changes the intended behaviour**.

## 2. Identify stationary parts

Select the outer frame, the fixed glazing, the fixed PVC profiles, and press **Assign Selection**
under *Identify stationary parts*.

These are recorded so the tool can reason about them (they are excluded from obstruction queries,
and conflicting assignments are detected), but they are **never moved and their mobility is never
changed**. Assigning them is optional but recommended.

## 3. Identify the movable leaf

Select every mesh that moves rigidly as the leaf - its PVC profiles, its glass, its trim, any
decorative parts fixed to it - and press **Assign Selection** under *Identify the movable leaf*.

They will move as one assembly. They are not merged; each stays its own object.

## 4. Assign handle parts (optional)

Press **Add Group**, then select the handle meshes and press **Assign Selection**.

* One group = meshes that rotate **together about one pivot**. An interior and an exterior lever
  that share a spindle can be one group.
* Hardware with a different pivot or a different rotation direction goes in its **own group**. Two
  mirrored levers usually want two groups with opposite `Rotation Angle` signs.
* A mesh can only be in one role at a time. Assigning it to a handle group automatically removes it
  from the leaf list, which is what stops a handle from receiving the leaf motion twice. If you
  somehow end up with a duplicate, validation reports it as an error.

Press **Snap Handle Pivot** to put the group's pivot at the centre of its meshes and set its
rotation axis to the outside direction (correct for a lever on a door face). Adjust from there.

## 5. Capture the closed pose

With everything sitting where it belongs when the opening is shut, press
**Set Current Pose As Closed**.

This stores one rest transform per part, in the opening's local frame, and promotes the leaf and
handle meshes to *Movable* mobility (stationary parts are left alone).

> **Lighting:** a mesh that moves cannot be treated as unchanged baked static geometry. Promoting
> the leaf to Movable is required for it to animate at runtime, and it means the leaf takes dynamic
> lighting rather than its old baked contribution. Re-build lighting after setup. The plugin logs
> each promotion and never touches the mobility of stationary parts.

The command **refuses** if an editor preview is showing a non-closed pose, so a preview can never
be captured as the authored closed pose by mistake. Press *Restore Pre-Preview Pose* first.

**Reset To Closed Pose** puts everything back on the captured rest at any time.

**Re-anchor Parts To Opening** carries every assigned part rigidly with the opening after you have
moved the opening actor, and re-anchors the calibration. This runs automatically when you move the
opening component in the viewport, unless you turn off *Move Parts With Opening* under
*Calibration*. With it off, moving the opening produces a validation warning instead.

## 6. Choose hinged or sliding, and place the hinge or slide path

Set **Opening Type > Motion Type**.

### Hinged

| Setting | Meaning |
|---|---|
| `Hinge Location` | Position in the opening's local frame, in cm. Press **Snap Hinge To Leaf Edge** to place it from the calibrated leaf bounds. |
| `Axis Preset` | `Vertical side hinge` (doors, casement windows), `Horizontal top hinge` (awning), `Horizontal bottom hinge` (hopper), or `Custom local axis`. |
| `Hinge Side` | `Left` or `Right`, **as seen standing outside and looking in**. Only used by the vertical preset. |
| `Swing Direction` | `Inward` or `Outward`, measured against the outside arrow. |
| `Open Angle` | Degrees at fully open. |
| `Invert Direction` | Advanced. Flips the resolved rotation without changing the presets. Use it if the leaf swings the wrong way and the presets look right. |

The viewport draws the hinge marker, the hinge axis, and a green arc from the closed radial line to
the open one, so the direction and the angular range are both visible.

### Sliding

| Setting | Meaning |
|---|---|
| `Slide Direction` | Local direction. Defaults to the observer's left (`Up x Outside`). |
| `Travel Distance` | Centimetres between fully closed and fully open. |
| `Invert Direction` | Reverses the travel. |

Press **Fit Slide To Leaf Width** to set the direction and the travel distance from the calibrated
leaf bounds. The viewport draws the slide line, its end point, and a ghost box of the leaf at full
travel.

A sliding opening works perfectly well with no handle group at all.

## 7-10. Direction and range, timing, interaction, sounds

These are the property groups in the panel's embedded details view (identical to the Details panel).

### Timing

Two modes, one authoritative at a time:

* **Duration (default)** - `Open Duration` and `Close Duration`, in seconds.
* **Speed** - `Open Speed` and `Close Speed`, deg/s for hinged and cm/s for sliding.

> With easing enabled, a speed setting defines the **nominal full-travel time** (total travel
> divided by speed). It does not guarantee a constant instantaneous velocity at every moment; the
> easing curve still shapes the motion within that total.

Also here: `Delay Before Opening`, `Delay Before Closing`, `Initial Openness`, `Auto Close` and its
delay, separate `Opening Easing` and `Closing Easing`, and optional float curves.

Built-in easings (Linear, Smooth Step, Ease In, Ease Out, Ease In/Out) need no curve asset.

**Custom curve requirements:** a curve must evaluate to 0 at time 0, to 1 at time 1, and must be
non-decreasing across [0,1]. A curve that fails is rejected with a warning and Smooth Step is used
instead. Output is clamped to [0,1] regardless, so a curve can never drive the leaf beyond its
configured range.

### Interaction

`Mode` is `Click only`, `Proximity only`, `Click and proximity`, or `Disabled`.

For clicking: set `Max Interaction Distance`, the `Interaction Trace Channel`, and which roles are
clickable (`Leaf`, `Handle`, `Stationary`). Assigned meshes must **block** the chosen channel for a
trace to hit them - validation tells you if none do. Glass materials with collision disabled are a
common cause; assign an `Interaction Proxy` (any collision component) instead.

For proximity: set `Box Extent` and `Box Offset` (drawn in the viewport in magenta),
`Allowed Occupant Classes` (empty means "any `APawn`"), `Require Player Controlled Pawn`,
`Close On Exit` and `Close Delay`.

### Audio

Every sound slot is optional and the plugin ships no audio. Assign what you have:
`Handle Actuation`, `Open Start`, `Close Start`, `Movement Loop`, `Latch` (fully closed),
`End Stop` (fully open). Set `Attenuation`, `Volume Multiplier`, `Pitch Multiplier`.

`Attach To Leaf` (default on) plays sounds at the first leaf mesh, so a swinging door is audible
where the leaf actually is. `Play Sounds In Editor Preview` is **off** by default.

## 11. Preview

**Open**, **Close**, **Toggle**, **Stop Preview**, the **Openness** scrub slider, and
**Restore Pre-Preview Pose**.

Preview runs without Play In Editor, driven from the editor's core ticker rather than a gameplay
world tick. It only writes transient poses, so it does not create a transaction per frame and does
not dirty the map by itself.

Preview state is restored automatically when you start PIE, when you save a map, when the map
changes, and when the editor module shuts down.

## 12. Check, then save the level

The panel lists every validation issue with its severity. Fix the errors, then save the level -
that is what persists the configuration.

---

## Component and ownership restrictions

Assignment accepts:

* the root component of any selected **Static Mesh Actor** (or any actor), and
* individually selected **scene components** belonging to an existing actor (select components in
  the level editor's component tree; component selection takes priority over actor selection).

Assignment is rejected, with a message, when the component:

* is the opening component itself, or an **ancestor** of it (driving it would move the calibration
  frame the rest poses are measured against),
* belongs to a **different world**,
* belongs to a **different level** than the opening (cross-level references do not survive
  streaming - move the opening into the same level),
* has no owning actor.

Additional rules:

* A component holds exactly one role. Re-assigning moves it.
* Deleting an assigned actor is safe: the reference is nulled by the garbage collector, animation
  skips it, and validation reports it.
* A mesh driven by two openings at once is detected and reported as an error.

### Duplicating a configured opening

* Duplicate the opening **together with its meshes** and the engine remaps everything to the copies:
  the duplicate is fully configured and independent.
* Duplicate the opening **on its own** and its part references would otherwise still point at the
  original's meshes. The plugin detects exactly this case (the referenced actor still carries the
  name recorded at assignment, whereas a remapped copy always has a new unique name), clears those
  references, invalidates the calibration and logs a warning. PIE world duplication is excluded from
  this check, so play sessions are unaffected.

### Blueprint actors

Nothing here modifies a Blueprint class template and nothing removes construction-script components.
Because the plugin references rather than reparents, that class of problem does not arise. If you
want an opening baked into a Blueprint, add the `Architectural Opening` component to the Blueprint
and assign components of that same actor; assignments to *other* actors are level data and belong on
a level instance, not on a class.

---

## Runtime interaction without the helper component

The opening works entirely through Blueprint commands. The helper is optional.

**With the helper:** add `Architectural Opening Interactor` to your player pawn or player
controller, choose `Centre of screen`, `Under mouse cursor`, or `Auto` (cursor if the controller is
showing one, otherwise centre screen), and call `Try Interact` from whatever input you already use.

It does **not** replace your player controller, does not add input mappings and does not require
Enhanced Input. If you use Enhanced Input, bind your `IA_Interact` handler to `Try Interact`; if you
use legacy action bindings, bind those; if you drive it from UMG, call it from the button.

**Without the helper:** trace however you like, then call
`Resolve Opening From Hit` and `Handle Click Interaction`, or call `Open` / `Close` / `Toggle`
directly on a component reference.

### Combined click + proximity priority

The rule is deliberate and predictable:

* A qualifying occupant enters the trigger: the opening is requested to open.
* While anyone qualifying is inside, no automatic closing happens.
* The last qualifying occupant leaves: the configured close delay starts.
* Someone re-enters during that delay: the close is cancelled.
* **If a player closes the opening by hand while still inside the trigger**, proximity re-opening is
  suppressed for that occupancy cycle. It is cleared as soon as the trigger is empty, so the next
  entry opens it again.

An explicit Blueprint `Close` counts as manual and sets the same suppression when the trigger is
occupied; a `Close` issued by proximity or by auto-close does not. An explicit `Open` always opens,
regardless of suppression.

Occupancy is counted per actor, so a pawn whose capsule and mesh both overlap the trigger produces
one entry and one exit. An occupant destroyed inside the trigger is handled through its `OnDestroyed`
delegate, so it cannot leave a phantom occupant behind.

Proximity closing and auto-close share **one** deferred-close timer, whichever wants to close
sooner, so the two features can never run competing schedules.

---

## Obstruction

Set `Obstruction > Policy`:

* `Ignore` - no queries at all.
* `Stop and wait` - hold the pose until the way is clear, then continue.
* `Reopen by amount` - back off by `Reopen Amount`, wait, then retry.

`Retry Delay` is how long the way must stay clear before the interrupted transition resumes.
`On Obstruction Detected` fires once on detection, never repeatedly while obstructed, which is what
keeps it from becoming an event or sound loop.

Read `Limitations.md` for exactly what the query does and does not guarantee. In short: it is a
deliberately conservative discrete multi-box overlap test of the pose slightly ahead of the current
one, not continuous collision detection, and the plugin does not pretend otherwise.

Physics simulation is never enabled on animated meshes as a side effect. The leaf is kinematic and
its transforms are written with `TeleportPhysics`.

---

## Presets

Create one with **Content Browser > Miscellaneous > Data Asset > Architectural Opening Preset**.

A preset carries behaviour only: motion type, open angle and travel distance, timing, easing,
handle behaviour, interaction options, proximity box shape, obstruction policy, audio references.

It deliberately carries **no level-specific data**: no part references, no hinge location, no slide
direction, no calibration. Applying a preset therefore cannot destroy assigned meshes or calibrated
pivots. Handle behaviour is written into existing handle groups' settings; groups are never created,
removed or reassigned, and pivots and axes are untouched. Interaction proxies are preserved.
