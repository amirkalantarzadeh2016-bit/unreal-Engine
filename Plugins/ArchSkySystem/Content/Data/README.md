# ArchSky data assets

This folder holds weather presets and the optional curated city library. It ships **empty**
and the plugin is fully functional without it: twelve weather presets and sixteen cities
are compiled into `ArchWeatherPreset.cpp` and `ArchLocationPreset.cpp` respectively.

Create assets here only when you want to **override** a built-in or **add** to the library.

## Weather presets

1. *Content Browser → Add → Miscellaneous → Data Asset → **ArchSky Weather Preset***.
2. Name it `DA_Weather_<Something>`.
3. Set **PresetId**. This is the key everything addresses the preset by — Blueprint, the
   console (`ArchSky.SetWeather <PresetId>`), and saved presets.
   **An asset whose `PresetId` matches a built-in replaces that built-in.** Set it to
   `Overcast` to retune the shipped overcast preset; set it to something new to add one.
4. Fill in **DisplayName** so the weather tile has a translatable label. Built-in presets
   have no `FText` name and fall back to showing their id.
5. Tune the parameters. Every field is documented in its tooltip, with units.

The twelve built-in ids are: `Clear`, `ClearHot`, `PartlyCloudy`, `Overcast`, `LightRain`,
`HeavyRain`, `Thunderstorm`, `Fog`, `Haze`, `DustStorm`, `LightSnow`, `Blizzard`.

### Getting the numbers right

`AerosolTurbidity` is the parameter that does most of the work for a Middle-Eastern sky.
Real Linke turbidity runs about **2** for pristine mountain air, **4–6** for a typical
urban day, and **10+** during a dust event. `DiffuseToDirectRatio` should approach **1.0**
for overcast — that is what produces the soft, nearly shadowless light an overcast
daylight study needs.

## Location library

1. *Content Browser → Add → Miscellaneous → Data Asset → **ArchSky Location Preset Library***.
2. Name it `DA_ArchLocations`.
3. Add entries. An entry whose `CityId` matches a built-in replaces it **in place**, so
   the dropdown order stays stable.
4. Assign the asset in *Project Settings → Plugins → ArchSky → Location Library*.

Both asset types implement `IsDataValid`, so *Asset Validation* will flag a missing
`PresetId`, a duplicate `CityId`, an impossible latitude, or a timezone more than two hours
from what the longitude implies.

## Cooking

If you keep presets here and package the project, make sure the folder is cooked. The
Asset Manager scan path defaults to `/ArchSkySystem/Data`; add that path under
*Project Settings → Packaging → Additional Asset Directories to Cook* if your presets are
not referenced by any level.
