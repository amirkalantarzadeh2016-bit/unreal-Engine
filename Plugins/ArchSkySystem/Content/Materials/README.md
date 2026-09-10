# ArchSky materials

This folder holds the material parameter collection and the optional cloud/precipitation
materials. It ships **empty** — `.uasset` files cannot be authored as text — and the
complete specification for everything that belongs here is in
[MATERIALS.md](../../MATERIALS.md) in the plugin root.

## What to create, in order

1. **`MPC_ArchSky`** — a Material Parameter Collection. This is the one required asset.
   MATERIALS.md lists every scalar and vector parameter with its exact name, range, and a
   usage example. Names must match exactly; the Director writes them by name.
   Assign it in *Project Settings → Plugins → ArchSky → Sky Parameter Collection*.

2. **A volumetric cloud material** (Path A only) — reads `CloudCoverage`, `WindVector`
   and the erosion term from `WindVector.A`. Assign it to the Director's
   `VolumetricCloudComponent`.

3. **A sky-sphere material** (Path B only) — the panoramic alternative for VR and low-end.
   Assign it to a sphere mesh on the Director's `SkySphereComponent` and switch
   *Cloud Mode* to `SkySphere`.

4. **Surface response** — wetness, snow and dust are exposed as `Wetness`,
   `SnowCoverage` and `DustIntensity`. Layer them into your architectural materials as
   described in MATERIALS.md. This is optional; the sky is correct without it.

Nothing in this folder is required for correct sun geometry. A shadow study works with an
empty Materials folder — the MPC only exists so *materials* can react to the sky the
Director is already driving.
