Plugin content root, mounted as "/Minimap/".

Textures placed here are loadable at runtime with:

    UMinimapFunctionLibrary::LoadPluginTexture("Textures/T_MyMap")

which resolves to "/Minimap/Textures/T_MyMap". The mount point exists because the
plugin descriptor sets "CanContainContent": true.

Keep LEVEL-SPECIFIC maps in the project's own content (the default save path for
Save Capture As Static Texture is /Game/Minimap/Generated). Only assets that are
genuinely shared across projects belong here, or they travel with the plugin into
every project that installs it.
