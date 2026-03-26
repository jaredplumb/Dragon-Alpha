# DragonAlpha.dmg Recovery Report

Date: 2026-02-28
Input image: local `DragonAlpha.dmg` source image

## Output Location

- `Assets/Legacy/Recovered/DragonAlphaDMG/`

## Recovery Mode Used

- `Carved` fallback only.
- Mounted extraction did not complete in this environment.

## Recovered Counts

- Images (`.png`, `.jpg`): 1064
- Audio (`.aif`): 34
- MIDI (`.mid`): 22
- Total files: 1121 (includes summary text file)

## Important Limitation

Because mounted extraction was unavailable, this pass does **not** guarantee preservation of classic Mac resource-fork metadata. The carve pass recovers data-fork payloads by file signatures.

## Next Requirement For True Resource-Fork Preservation

Run mounted extraction in a normal macOS Terminal session where `hdiutil attach` works and then copy out files with:

```bash
ditto --noqtn --rsrc --extattr "/Volumes/<Mounted Legacy Volume>" "<target-folder>"
```

Once that succeeds, place the extracted folder under:

- `Assets/Legacy/Recovered/DragonAlphaDMG/Mounted/`
