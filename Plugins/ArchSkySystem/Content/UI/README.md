# ArchSky UMG assets

This folder is where the plugin's Blueprint widgets live. It ships **empty**: a `.uasset`
cannot be authored as text, so the C++ side is complete and the widget assets are built
once, by hand, following the recipe below. Everything the widgets need is already
implemented — `UArchSkyWidgetBase` and `UArchSkyViewModel` do the work; the assets supply
only the visuals.

Full step-by-step instructions, including every bind-widget name, are in the plugin's
[README.md](../../README.md) under **Building the UI assets**. This file is the short
version.

## What to create

| Asset | Parent class | Purpose |
|---|---|---|
| `WBP_ArchSkyPanel` | `ArchSkyWidgetBase` | The main control panel |
| `WBP_ArchWeatherTile` | `UserWidget` | One weather preset tile, used in a list |
| `WBP_ArchPresetRow` | `UserWidget` | One saved-preset row |
| `WBP_ArchCompassDial` | `UserWidget` | The north-offset dial with its compass rose |

## The one rule

Bind to `ViewModel` fields **inside the `On Sky View Updated` event**, never through UMG
property bindings. A property binding is evaluated every frame, for every bound widget;
the event fires only when something actually changed. This is the entire reason the MVVM
layer exists — using property bindings would throw the benefit away.

## Optional bindings

Every `BindWidget` on `UArchSkyWidgetBase` is `BindWidgetOptional`, so a cut-down panel
with only a time slider and a clock label is valid and will compile. Name a widget exactly
as the C++ property is named and it will be found automatically:

`TimeOfDaySlider`, `DayOfYearSlider`, `NorthOffsetDial`, `TimeLabel`, `DateLabel`,
`SunTimesLabel`, `AnalysisLabel`, `PlayPauseButton`, `CalendarToggle`, `LocationCombo`,
`LocationSearchBox`.
