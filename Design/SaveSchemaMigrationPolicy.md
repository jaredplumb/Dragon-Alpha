# Save Schema Migration Policy

Date: 2026-03-02

## Current Runtime Versions

- `DragonSaveInfo.version`: `3`
- `DragonSaveSlot.version`: `2`
- `DragonWorldState.version`: `1`

Source of truth: `Code/Global.cpp`.

## Compatibility Rules

1. Always support read-forward from at least one prior released save version.
2. Never silently reinterpret unknown versions as current schema.
3. Default-initialize new fields and preserve gameplay-safe values when migrating old data.
4. Keep migration logic deterministic and side-effect free before first successful write.

## Migration Process

1. Increment target version constant in `Code/Global.cpp`.
2. Add explicit migration branch in load path:
   - `DragonEnsureSaveInfo()` for `DragonSaveInfo`.
   - `DragonReadSlotPreview()` / `DragonLoadSlot()` for `DragonSaveSlot`.
   - `DragonLoadWorldState()` for `DragonWorldState`.
3. Normalize migrated state:
   - clamp enum/range values.
   - compact inventory.
   - ensure progression invariants (`WorldMap::NormalizeProgressionState()`).
4. Write migrated payload on next save (`DragonSaveCurrentSlot()`), not during read-failure path.
5. Add/refresh regression script coverage and update changelog.

## Safety Invariants

- Area 0 (`Eleusis`) must always remain discovered.
- A discovered area beyond area 0 must have an unlocked previous-route chain.
- Progression flags must remain monotonic:
  - `Challenge II` implies `Challenge I`.
  - `Warp opened` implies `Gate cleared`.
- Map coordinates must clamp into map bounds and resolve to open cells.

## Rollback/Post-Release

- If migration bugs are discovered, add a one-time repair branch keyed by the old version.
- Never reuse version numbers.
- Keep this policy updated whenever version constants change.
