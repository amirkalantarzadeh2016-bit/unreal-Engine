# Minimap Plugin — Documentation

Runtime minimap system for Unreal Engine 5.8: calibrated world-to-map projection, a marker
registry, and an optional automatically captured top-down background.

| Document | Read it when |
|---|---|
| [01 — Setup](01-Setup.md) | Installing the plugin, dependencies, first build |
| [02 — Usage Guide](02-Usage.md) | Step-by-step: level → widget → markers → capture |
| [03 — Configuration](03-Configuration.md) | What every setting does and what to set it to |
| [04 — Blueprint Reference](04-BlueprintReference.md) | Every class, function, event and delegate |
| [05 — Troubleshooting](05-Troubleshooting.md) | Black map, tiling, misaligned markers, crashes |

## What the plugin gives you, and what you build

This distinction matters and is the source of most confusion:

**The plugin ships (C++):**
- `AMinimapBoundsVolume` — calibration actor you place in the level
- `UMinimapSubsystem` — registry and the single batched update loop
- `UMinimapTrackedComponent` — add to any Actor to put it on the map
- `UMinimapViewComponent` — describes one minimap viewport
- `UMinimapWidgetBase` / `UMinimapMarkerWidget` — C++ base classes for your widgets
- `UMinimapCaptureComponent` — the top-down scene capture
- `UMinimapPresetAsset` — portable settings
- `UMinimapFunctionLibrary` — pure maths, Blueprint-callable

**You build (Blueprint assets — the plugin cannot create these for you):**
- `WBP_Minimap` — reparented to `UMinimapWidgetBase`
- `WBP_MinimapMarker` — reparented to `UMinimapMarkerWidget`
- `M_Minimap` — your map material, if you use material mode
- Any icons, compass art, buttons and layout

The C++ drives the Blueprint widgets by **name binding**, so you add a widget with the
right name and it works with no graph wiring. See [02 — Usage Guide](02-Usage.md).

## Architecture in one picture

```
AMinimapBoundsVolume ──BeginPlay──> UMinimapSubsystem (calibration + registry)
                                            │
UMinimapTrackedComponent ──BeginPlay──> registry
                                            │  ONE batched pass @ TickInterval (1/30 s)
                                            ▼
                              per active UMinimapViewComponent
                                  1. resolve anchor + view yaw, cache sin/cos
                                  2. project every marker -> snapshot array
                                  3. broadcast OnMinimapViewUpdated
                                            │
                                            ▼
                              UMinimapWidgetBase -> background + pooled markers
```

The subsystem owns the only recurring tick. Markers and widgets are passive.
