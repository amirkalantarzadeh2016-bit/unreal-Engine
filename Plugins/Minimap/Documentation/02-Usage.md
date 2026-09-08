# 02 — Usage Guide

Follow these in order. Steps 1–4 give a working minimap; 5–7 are optional.

---

## Step 1 — Place the bounds volume

The bounds volume is the **single source of truth for calibration**. Markers and the
captured image both derive from it, which is what guarantees they agree.

1. In the level, *Place Actors* → search **Minimap Bounds Volume** → drag it in.
2. Select it. In Details you now see a **Minimap Preview** category with three action
   groups: **Capture**, **Bounds**, **Diagnostics**.
3. Size the bounds. Either scale `BoundsBox` by hand, or press:
   - **Fit To Geometry** — *recommended.* Fits tightly to actual architectural meshes,
     weighting each by volume and trimming outliers, so empty space and stray distant
     objects do not inflate the map.
   - **Fit To All Actors** — the looser fit; unions every eligible actor's bounds.
4. Press **Validate Setup** and read the Output Log (filter: `LogMinimap`).

> Both fit buttons **change your calibration** — markers and image both move. They do
> nothing until you press them, so an existing working setup is never disturbed.

**Rotation:** only **Yaw** is supported. A pitched or rolled volume is reported as an
error by validation, because the projection is planar and a tilted volume would produce a
silently misaligned map.

---

## Step 2 — Add the view component

1. Open your **PlayerController** Blueprint.
2. *Add Component* → **Minimap View**.

Put it on the **PlayerController**, not the Pawn: the controller survives pawn death, so
respawn and repossession need no extra glue. The view re-resolves which actor to follow
every update (explicit override → controlled pawn → view target → owning pawn), so a
destroyed pawn is skipped rather than followed.

*If you skip this step, `UMinimapWidgetBase` creates one on the controller automatically.*

---

## Step 3 — Build the minimap widget

1. Create or open `WBP_Minimap`.
2. *File → Reparent Blueprint* → **MinimapWidgetBase**.
3. Add these widgets. **Names matter** — the C++ binds by name, so no graph wiring is
   needed. All are optional; add what you need.

| Widget name | Type | Purpose |
|---|---|---|
| `Background` | Image | The map image |
| `MarkerCanvas` | Canvas Panel | Parent for pooled marker widgets |
| `North_Container` | any Widget | North indicator |
| `South_Container` | any Widget | South indicator |
| `East_Container` | any Widget | East indicator |
| `West_Container` | any Widget | West indicator |

4. Tick **Is Variable** on each.
5. Add the widget to the viewport however your project normally does.

### Keeping an existing pop animation

If your widget already has `PlayPopEffect` / `StopPopEffect` custom events, they keep
working. C++ deliberately does **not** declare those names — that would collide on
reparent. Instead implement the event **Handle Pop Effect (bool bPlayForward)** and branch
to your existing two events. One Branch node; the animation asset is untouched.

---

## Step 4 — Add markers

1. Create `WBP_MinimapMarker`, reparent to **MinimapMarkerWidget**.
2. Add an **Image** named `IconImage`. The C++ drives its brush, tint and rotation.
3. On `WBP_Minimap`, set **Default Marker Widget Class** to it.
4. On any Actor that should appear: *Add Component* → **Minimap Tracked**. Set an Icon
   under *Style*.

That is the entire per-actor setup. The component self-registers on BeginPlay.

---

## Step 5 — Automatic top-down capture (optional)

Off by default; the static-texture workflow is unaffected until you opt in.

1. On the bounds volume: `Capture Settings Override` → **Background Source = Automatic
   Scene Capture**.
2. Press **Capture / Refresh**. The *Scene Capture Render Target* preview in Details fills
   in — **in the editor, no PIE required**.
3. Choose how it reaches the widget with **Background Apply Mode**:

| Mode | Use when |
|---|---|
| `Automatic` | Default. Tries the material's `MapTexture` parameter, falls back to the image brush. |
| `MaterialParameter` | Your material has a `MapTexture` Texture Sample Parameter and does its own pan/rotate. |
| `ImageBrush` | No material; static image. |
| `CompositedView` | **Cannot tile.** The plugin does pan/zoom/rotation itself into its own render target, black outside the map. Bypasses the material, so a circular mask baked into the material is lost — use widget clipping instead. |

### Hiding things from the map only

Tag actors (e.g. `MinimapHidden`) and add the tag to **Exclusion Tags**, or add them to
**Capture Excluded Actors**. This uses Scene Capture visibility, so the objects stay fully
visible to the player's camera.

> **Limitation:** if a ceiling and the floor you want to see are the *same* mesh or
> component, actor-level filtering cannot separate them. Split the mesh.

---

## Step 6 — Bake the capture to a static texture (optional)

Trades runtime capture cost for a baked image.

1. **Capture / Refresh** at least once.
2. Press **Bake To Static Texture**.

It saves a real `UTexture2D` to `StaticTextureSavePath` (default `/Game/Minimap/Generated`)
named `T_Minimap_<LevelName>`, assigns it to `StaticMapTexture`, and switches this instance
to Static Texture mode. It refuses to bake a render target that has never been captured
into. The per-level default name means two levels cannot silently overwrite each other.

---

## Step 7 — Compass and zoom

**Compass indicators** are driven automatically once bound (Step 3). They lag behind the
view for a floating feel — see [03 — Configuration](03-Configuration.md).

**Zoom** — bind buttons or a slider to these on the widget:

| Node | Purpose |
|---|---|
| `Zoom In` | One step in |
| `Zoom Out` | One step out |
| `Set Zoom Alpha (0..1)` | Drive from a slider |
| `Get Zoom Alpha` | Read back for a slider |

The transition is eased in C++. A Blueprint Timeline on the *widget's scale* composes on
top if you also want the panel itself to grow.

---

## Refreshing after level changes

| Situation | Call |
|---|---|
| Furniture moved / added / removed | `Request Background Refresh` |
| Streamed or procedural content ready | `Notify Minimap Content Ready` |
| Bounds themselves must change | `Refit Bounds And Refresh` |

`Request Background Refresh` **never** moves the calibration — that is the whole point of
keeping it separate from `Refit Bounds And Refresh`. Multiple requests inside
`RefreshCoalesceSeconds` (default 0.15 s) collapse into **one** capture, so a batch of
furniture edits costs one render, not one per item.

Blueprint: `Get Minimap Subsystem` → `Request Background Refresh`.
