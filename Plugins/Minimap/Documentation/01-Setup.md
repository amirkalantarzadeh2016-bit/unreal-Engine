# 01 — Technical Setup

## Requirements

| | |
|---|---|
| Engine | Unreal Engine 5.8 (Win64) |
| Project type | **C++ project.** A Blueprint-only project must be converted first — add any C++ class from the editor, which creates `Source/` and a `.Build.cs`. |
| Modules pulled in | `Core`, `CoreUObject`, `Engine`, `UMG` (public); `Slate`, `SlateCore` (private) |
| Third-party | **None.** |

## Module layout

```
Plugins/Minimap/
├── Minimap.uplugin
├── Content/                     mount point "/Minimap/"
├── Documentation/               this folder
└── Source/
    ├── Minimap/                 Runtime — ships in a packaged game
    │   ├── Minimap.Build.cs
    │   ├── Public/              9 headers
    │   └── Private/             implementations + Tests/
    └── MinimapEditor/           Editor — never packaged
        ├── MinimapEditor.Build.cs
        └── Private/             Details panel customisation
```

The editor module depends on `UnrealEd` and `PropertyEditor`; the runtime module depends on
neither, so a packaged build never pulls editor code in.

## Installing

1. Copy the `Plugins/Minimap` folder into your project's `Plugins/` directory.
   The result must be `<Project>/Plugins/Minimap/Minimap.uplugin` — **not** under `Source/`.
2. Right-click the `.uproject` → **Generate Visual Studio project files**.
3. Build the editor target:
   ```
   "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ^
     <YourProject>Editor Win64 Development ^
     -Project="<FullPath>\<YourProject>.uproject" -WaitMutex
   ```
4. Open the project. The plugin is `"EnabledByDefault": true`, so it needs no manual
   enabling — but you can confirm under *Edit → Plugins → UI → Minimap*.

### If Visual Studio's build fails but Build.bat works

A `MSB4018 / System.ArgumentException: Environment variable name or value is too long`
during MSBuild's `SetEnv` task is a Windows environment-size limit, not a plugin problem.
UE's `.vcxproj` is only a shim around `Build.bat`, so **run `Build.bat` directly** as above.
The underlying cause is usually an oversized `PATH`; check with:

```powershell
$m=[Environment]::GetEnvironmentVariable('Path','Machine')
$u=[Environment]::GetEnvironmentVariable('Path','User')
"Total: $($m.Length + $u.Length)"
$m.Split(';') | Group-Object | Where-Object Count -gt 1 | Select Count, Name
```

## Initialisation order

Nothing needs to be initialised by hand. The order is:

1. **`UMinimapSubsystem`** is created with the world. It starts with **no calibration** on
   purpose — until real bounds arrive it refuses to project rather than guess.
2. **`AMinimapBoundsVolume::BeginPlay`** registers itself and, with *Apply On Begin Play*
   enabled, pushes its calibration to the subsystem.
3. **`UMinimapTrackedComponent::BeginPlay`** registers each marker. No `GetAllActorsOfClass`
   is ever used at runtime.
4. **`UMinimapViewComponent::BeginPlay`** registers the viewport.
5. **`UMinimapWidgetBase::NativeConstruct`** binds to the view, or creates one on the
   PlayerController if none exists.

The system is resilient to these happening in any order. For **streamed or procedurally
generated levels**, call `NotifyMinimapContentReady()` once the geometry that should appear
on the map exists — do not rely on the startup delay.

## Dedicated servers

No capture component, render target or widget resources are allocated when
`GetNetMode() == NM_DedicatedServer`. Markers are never replicated: each client projects
from actor transforms it already receives.
