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

## The playback transport bar

These are also `BindWidgetOptional`, so build as much of the bar as you need:

`TimelineSlider`, `PlayButton`, `PauseButton`, `StopButton`, `StepForwardButton`,
`StepBackwardButton`, `SpeedPresetCombo`, `LoopToggle`, `PlaybackTimeLabel`,
`PlaybackDateLabel`, `SpeedPresetLabel`.

Two things are done for you in C++ and must **not** be duplicated in the Blueprint:

1. **`TimelineSlider`'s range is set to 0 - 1440 automatically.** Leave the Min/Max Value
   fields in the designer alone; they are overwritten on construct.
2. **The scrubber is already wired bidirectionally.** Dragging seeks, and the simulation
   writes the handle back. The mouse-capture events are hooked so a drag suspends playback
   and resumes it from wherever you let go — do not add your own `OnValueChanged` handler.

A suggested layout, left to right:

```
[◀◀ Step]  [▶ Play] [❚❚ Pause] [■ Stop]  [Step ▶▶]
──────────────────────────────────────────────────────────
 06:42                                        18 Sep 2026
 ├──────────●───────────────────────────────────────────┤
 0:00                                               24:00
 Speed: Hour per second (x3600)        [Loop] 06:00-18:00
```

`PlaybackTimeLabel` shows `HH:MM`, `PlaybackDateLabel` shows the simulated date (it
advances when playback runs past midnight), and `SpeedPresetLabel` sits beside the slider.

For the tick marks the brief's sibling ticket asks for, `GetTimeSliderTicks` on the
ViewModel returns normalised sunrise / solar-noon / sunset positions plus a validity flag,
and `LoopStart01` / `LoopEnd01` give you the loop window as 0-1 fractions for shading the
track.
