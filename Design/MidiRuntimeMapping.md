# MIDI Runtime Mapping

Date: 2026-03-26

This document tracks runtime MIDI usage and file mapping for the standalone macOS build.

## Runtime Contract

Runtime MIDI files live under `Resources/Music/` and are consumed directly by scene playback:

- `theme.mid` for front-door/menu scenes (`Splash`, `NewGame`, `NewAvatar`)
- area map tracks for `WorldMap`
- area battle tracks for `Battle`

## Scene Mapping

- `Map/Core`: `Core__map.mid`
- `Map/Meadows`: `Meadows__map.mid`
- `Map/Forests`: `Forest__map.mid`
- `Map/Caves`: `Caves__map.mid`
- `Map/Mountains+Peak`: `Mountain__map.mid`
- `Battle/Core`: `Core__battle.mid`
- `Battle/Meadows`: `Core__battle.mid` (until meadow-specific battle track is recovered)
- `Battle/Forests`: `Forest__battle.mid`
- `Battle/Caves`: `Caves__battle.mid`
- `Battle/Mountains+Peak`: `Mountain__battle.mid`

## Notes

- Runtime playback now honors saved music preference (`isMusicOn`) in real time.
- Source MIDI provenance remains under `Assets/Legacy/Data/Areas/*/Music/` and `Assets/Music/Legacy/PowerPlug/theme.mid`.
