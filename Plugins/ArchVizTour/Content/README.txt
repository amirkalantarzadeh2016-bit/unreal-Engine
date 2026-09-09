ArchViz Tour — plugin content
=============================

This directory is intentionally almost empty, and the plugin ships no binary .uasset files.

Two assets are usually expected here and are deliberately not shipped:

1. The Editor Utility Widget for preset management.
   The plugin ships the C++ base class `UTourPresetManagerWidgetBase` instead. Create an
   Editor Utility Widget in your own project, reparent it to that class, and lay it out
   however suits your team. A shipped blueprint would have to be re-customised by hand after
   every plugin update, and its layout is a project decision rather than a plugin one.

2. The Enhanced Input mapping context and actions.
   The plugin ships `UTourInputConfig` (a data asset) and reads it only when
   Project Settings > Plugins > ArchViz Tour (Runtime) > Enable Default Input is switched on.
   Create the Input Mapping Context and Input Actions in your project, assign them to a Tour
   Input Config asset, and point the setting at it. Suggested defaults:
       Space        Toggle Pause
       Right Arrow  Next Step
       Left Arrow   Previous Step
       Escape       Stop Tour
       R            Restart Tour

The runtime tour never depends on anything in this directory.
