# Dragon Alpha Asset Layout

This folder stores editable and archival asset material for the Dragon Alpha port.

## Designer-first folders

- `Images/`
  - `Source/DragonArcadia/art`: canonical source art masters (`cdr`, `psd`, etc).
  - `Legacy/Resources`: legacy image set previously used for runtime export.
  - `Legacy/Art/Additional` (canonical): legacy monster/item source JPGs.
  - `RuntimeOverrides`: curated PNG runtime overrides consumed by importer scripts.
  - `Catalog/Runtime`: scene-grouped links to runtime PNGs (`Areas`, `WorldMap`, `Battle`, `Splash`, etc).
  - `Catalog/Source`: scene-grouped links to best-known source/original art for each runtime asset.

- `Sounds/`
  - `Source/DragonArcadia/sounds`: canonical source WAV masters.
  - `Legacy/Resources`: legacy runtime WAV set.
  - `Legacy/PowerPlug`: legacy plugin AIFF source sounds.
  - `RuntimeOverrides`: curated WAV runtime overrides consumed by importer scripts.
  - `Catalog/Runtime`: usage-grouped links to runtime WAVs.
  - `Catalog/Source`: usage-grouped links to best-known source/original WAVs.

- `Fonts/`
  - `Legacy/Resources`: legacy font assets.

- `Music/`
  - `Legacy/PowerPlug`: legacy MIDI source set.
  - `Original/MIDI`: deduplicated staged MIDI links generated from legacy and recovered sources.

## Archive/support folders

- `Docs/`: canonical current generated manifests (source-of-truth, provenance map, and MIDI manifest).
- `Archive/`: preserved public provenance artifacts (including the original `DragonAlpha.dmg`) and minimal archive manifests.
- `Legacy/`: historical code-era structures and recovery datasets kept for provenance.
- `Images/RecoveredOverrides` and `Sounds/RecoveredOverrides`: non-identical recovered candidates only (byte-identical duplicates are pruned).

Large intermediate recovery snapshots are intentionally omitted from the public repository.

Public historical one-off recovery notes live under:
- `Archive/Deprecated/RecoveryNotes/`

## Runtime packaging path

Runtime package content still ships from `Resources/Images` and `Resources/Sounds`.
Use your local asset-import workflow (or manual copy) to move approved assets from `Assets/*` into runtime-ready PNG/WAV outputs under `Resources/*`.

## Contribution rule

1. Put new/editable art in `Assets/Images/Source/...` and audio in `Assets/Sounds/Source/...`.
2. Keep originals and forensic recoveries in `Assets/Legacy/...` unless intentionally curated.
3. Stage curated runtime overrides only in `Assets/Images/RuntimeOverrides` and `Assets/Sounds/RuntimeOverrides`.
4. Re-run import/package/smoke checks after any media updates.
5. Regenerate catalogs/manifests and refresh `Assets/Docs/*` outputs when asset mappings change.
