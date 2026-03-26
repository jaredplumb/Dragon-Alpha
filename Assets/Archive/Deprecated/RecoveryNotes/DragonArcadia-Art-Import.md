# Dragon Arcadia Art.zip Import

Date: 2026-02-28
Source archive: local `Dragon Arcadia/Art.zip` source archive

## Imported Locations

- Raw source archive copy:
  - `Assets/Legacy/DragonArcadia/OriginalZip/Art.zip`
- Direct unzip output (includes historical metadata wrappers):
  - `Assets/Legacy/DragonArcadia/Extracted/`
- Clean working copy (metadata noise removed):
  - `Assets/Legacy/DragonArcadia/Working/Art/`

The clean working copy excludes:
- `__MACOSX/`
- `._*` AppleDouble sidecar files
- `.DS_Store`

## Inventory Summary (clean working copy)

- Total files: `275`
- Extensions:
  - `psd`: 179
  - `cpt`: 37
  - `cdr`: 32
  - `wav`: 19
  - `doc`: 5
  - `xls`: 2
  - `csv`: 1

## Generated Manifests

- `Assets/Legacy/DragonArcadia/Manifest/all-files.txt`
- `Assets/Legacy/DragonArcadia/Manifest/extensions.tsv`
- `Assets/Legacy/DragonArcadia/Manifest/top-level.tsv`
- `Assets/Legacy/DragonArcadia/Manifest/category-ext.tsv`

These files are intended to drive future conversion passes into:
- `Assets/Converted/Images`
- `Assets/Converted/Sounds`
- and finally runtime package assets under `Resources/`.

## Notes

- Most source art is editable (`.psd`) with additional legacy vector/raster formats (`.cdr`, `.cpt`) preserved.
- Audio is present as legacy WAV candidates under `Working/Art/Sounds`.
- No runtime assets were replaced in this step; this import is archival and conversion-ready.
