# Plugin icon

`Icon128.png` is the image shown for this plugin in the Plugins browser. It is **not**
checked in, because a binary placeholder is worse than an obvious gap: the engine falls
back to a generic plugin icon when the file is absent, and everything works.

## To add one

1. Author a **128 × 128 px PNG**, 32-bit with alpha.
2. Save it as `Plugins/ArchSkySystem/Resources/Icon128.png` — that exact name and location;
   the engine looks for nothing else.
3. Restart the editor, or use *Plugins → Refresh*.

## Suggested subject

A sun path arc over a simple building silhouette, with a small north arrow — the three
things this plugin is about. Keep it legible at 32 px, which is the size the Plugins
browser list actually renders.
