# MATERIALS.md — the `MPC_ArchSky` contract

This document is the **contract between `AArchSkyDirector` and every material in your
project**. The Director writes the parameters listed here, by name, once per frame, in one
batched call. A material artist reads them. Neither side needs to know anything else about
the other.

If a name here and a name in `MPC_ArchSky` disagree, the write silently does nothing —
Unreal's parameter-collection API has no "unknown parameter" error. **Copy the names
exactly.**

---

## Creating the collection

1. *Content Browser → Add → Materials → **Material Parameter Collection***.
2. Name it **`MPC_ArchSky`**. Put it wherever you like; the Director finds it by the
   reference in Project Settings, not by path.
3. Add every parameter in the two tables below, with the given default.
4. Assign it in *Project Settings → Plugins → ArchSky → **Sky Parameter Collection***.

> A Material Parameter Collection is limited to **1024 scalars and 1024 vectors**, so
> there is no need to economise — but note that changing the collection's *layout*
> (adding or removing parameters) recompiles every material that references it.

---

## Scalar parameters

| Name | Range | Default | Meaning |
|---|---|---|---|
| `SunAltitude01` | −1 … 1 | 0 | Sun altitude normalised by 90°. `0` = horizon, `1` = zenith, negative = below the horizon. |
| `SunAltitudeDegrees` | −90 … 90 | 0 | The same value in degrees, for when you need the real angle. |
| `MoonIllumination` | 0 … 1 | 0 | Lit fraction of the lunar disc. `0` at new moon, `1` at full. |
| `MoonAltitude01` | −1 … 1 | 0 | Moon altitude normalised by 90°. |
| `Wetness` | 0 … 1 | 0 | How wet horizontal surfaces should look. |
| `SnowCoverage` | 0 … 1 | 0 | How much lying snow covers up-facing surfaces. |
| `RainIntensity` | 0 … 1 | 0 | Rain (or hail) rate. Zero unless the weather's precipitation type is rain or hail. |
| `DustIntensity` | 0 … 1 | 0 | Airborne dust density. Zero unless the precipitation type is dust. |
| `SeasonBlend` | 0 … 1 | 0.5 | Continuous seasonal position. `0` = midwinter, `1` = midsummer. Hemisphere-corrected. |
| `TimeOfDay01` | 0 … 1 | 0.5 | Local clock time over 24 h. `0.5` = 12:00. **Not** a lighting term — use `SunAltitude01` for that. |
| `FogDensity` | 0 … 0.2 | 0.02 | The height-fog density currently in force. |
| `CloudCoverage` | 0 … 1 | 0.1 | Fraction of sky covered. The primary input to both cloud paths. |
| `WindStrength` | 0 … 60 | 2 | Ground-level wind speed, m/s. |
| `WindTurbulence` | 0 … 1 | 0.2 | Gustiness. `0` = steady flow. |
| `Turbidity` | 1 … 20 | 3 | Linke turbidity. `2` = pristine, `5` = urban, `10+` = dust storm. |

## Vector parameters

| Name | XYZ | W | Default |
|---|---|---|---|
| `SunDirection` | Unit vector **from the scene towards the sun**, world space, plan-north corrected. | `SunAltitude01` | `(0,0,1,0)` |
| `MoonDirection` | Unit vector from the scene towards the moon. | `MoonIllumination` | `(0,0,1,0)` |
| `SunLightColor` | Linear RGB of the sun light, from its colour-temperature curve. | unused | `(1,1,1,1)` |
| `WindVector` | Wind as a world-space vector: direction × speed in m/s, Z always 0. | `CloudErosion` (0…1) | `(0,0,0,0.4)` |
| `FogInscatteringColor` | Linear RGB the fog scatters into the view. | unused | `(0.45,0.64,1,1)` |

> **The W channels are not padding.** Packing the most-used scalar alongside its vector
> halves the number of collection lookups in the shaders that need both, which is most of
> them.

---

## Sign conventions you must respect

`SunDirection` points **from the scene towards the sun** — it is the `L` in a `dot(N, L)`
term, already the right way round. The *light component's* forward vector is its negation,
because a light shines in the direction it faces. Getting this backwards produces a scene
lit from underneath, which reads as "the normals are flipped" and sends people looking in
entirely the wrong place.

The direction is **already corrected for plan north**. Do not apply the north offset again
in a material.

---

## Usage examples

### Day/night blend

```
Lerp( NightColor, DayColor, saturate(SunAltitude01 * 4) )
```

`SunAltitude01 * 4` reaches 1 at about 22° of altitude, so the transition happens across
sunrise rather than being spread over the whole morning.

### Wet surfaces

```
Roughness   = Lerp( BaseRoughness, 0.06, Wetness )
BaseColor   = Lerp( BaseColor, BaseColor * 0.72, Wetness )
Specular    = Lerp( BaseSpecular, 0.9, Wetness )
```

Wet materials get darker *and* smoother. Doing only one of the two is the most common
reason rain-soaked archviz looks like plastic.

### Snow accumulation on up-facing surfaces

```
SnowMask = saturate( dot(WorldNormal, float3(0,0,1)) * 2 - 0.6 ) * SnowCoverage
BaseColor = Lerp( BaseColor, SnowAlbedo, SnowMask )
Normal    = Lerp( Normal, float3(0,0,1), SnowMask * 0.8 )
```

Mask by the world normal so snow lands on sills and roofs and not on walls.

### Foliage wind

```
Offset = WindVector.xyz * 0.01
       * (1 + WindTurbulence * sin(Time * 3 + WorldPosition.x * 0.01))
       * VertexColor.R
```

`VertexColor.R` as a stiffness mask keeps trunks still while leaves move.

### Moonlit night term

```
NightAmbient = MoonAmbientColor * MoonIllumination * saturate(MoonAltitude01 * 3)
```

Both terms matter: a full moon below the horizon contributes nothing, and a crescent
overhead contributes very little.

### Distance haze keyed to real turbidity

```
HazeAmount = saturate( (Turbidity - 2) / 8 ) * saturate(PixelDepth / 20000)
```

Ties the material's own aerial perspective to the same turbidity the atmosphere is using,
so a dust-storm preset does not leave your mid-ground looking clean.

### Sun disc on a sky sphere (Path B)

```
SunDisc = smoothstep( 0.9995, 0.9998, dot(normalize(CameraVector), SunDirection.xyz) )
Emissive += SunDisc * SunLightColor * 50000
```

`0.9995` corresponds to roughly a 1.8° disc — larger than the true 0.545°, which reads
better once bloom is applied.

---

## The two cloud paths

`EArchCloudMode` in Project Settings selects one. The Director fully supports both and
**skips the unused one entirely** — the volumetric component is hidden and its tick
disabled in Path B, and the sky sphere is hidden in Path A. Neither path costs anything
when it is not selected.

### Path A — `UVolumetricCloudComponent` (default)

Highest fidelity: real ray-marched clouds that self-shadow, cast shadows onto the scene,
and are correctly lit by the sun through the atmosphere.

**What the Director sets on the component:** `LayerBottomAltitude` (km),
`LayerHeight` (km), and `TracingMaxDistance` — the last scaled with layer thickness,
because a 9 km thunderstorm anvil needs a longer march than a 1.5 km fair-weather deck and
a fixed value wastes either quality or milliseconds.

**What your cloud material must do:** everything else. Coverage, density and erosion are
*not* component properties; they live in the material and reach it through the MPC. Read
`CloudCoverage`, `WindVector.xyz` (for advection) and `WindVector.a` (erosion).

Budget roughly **2–5 ms** on a desktop GPU at 1080p, more at higher resolutions. It scales
with screen coverage, so a scene looking mostly at the sky costs the most.

### Path B — sky-sphere material

A panoramic material on a large inverted sphere. No ray marching, no self-shadowing, and
clouds do not cast shadows on the scene. In exchange it costs **well under 0.5 ms** and is
stable in stereo, which is what makes it the right answer for VR.

**Setup:** assign a sphere mesh to the Director's `SkySphereComponent`, assign your
material, and set *Cloud Mode* to `SkySphere`. The Director makes the sphere visible and
stops driving the volumetric component. If the mode is `SkySphere` but no mesh is
assigned, the Director logs a warning naming this document rather than rendering nothing
silently.

**What your material should read:** `CloudCoverage` to blend between a clear and an
overcast cloud texture, `WindVector.xyz` to pan the UVs, `SunDirection` for the sun disc
and for a forward-scattering glow, and `Turbidity` for horizon haze.

### `Disabled`

No cloud rendering at all. The atmosphere still responds to weather, and the sky-light,
fog and lighting response are unaffected. Useful for interior-only work where the sky is
never in frame.

---

## Performance notes

The Director writes the collection **once per frame, batched**, and only when time is
flowing, a weather transition is running, or something was explicitly marked dirty. It
never touches a dynamic material instance, so the cost is O(1) in the number of materials
in your project rather than O(n).

Set `bWriteMaterialParameterCollection` to `false` in Project Settings if no material in
your project reads the collection. The lighting, atmosphere and fog are all driven through
component properties and are entirely unaffected by that switch.
