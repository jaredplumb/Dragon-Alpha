# Dragon Alpha

Dragon Alpha is a standalone macOS modernization of the original Dragon Alpha RPG.

## Project Direction

- Preserve the original game flow and content where practical.
- Run on modern Apple hardware with a local runtime (`Code/`) and loose local resources (`Resources/`).
- Ship from this repository without runtime/build dependencies on sibling projects.

The durable product direction lives in:
- `Design/01.Product-Guide.md`
- `Design/Backlog.md`
- `Design/Testing.md`

## Repository Scope

This repository now contains active game code, runtime resources, source assets, and design docs for the standalone macOS port.

If you are specifically looking for the historical disk image, it is still available at:
- `Assets/Archive/DragonAlpha.dmg`

## Build (macOS)

From repository root:

```bash
xcodebuild -project "Dragon Alpha.xcodeproj" \
  -scheme "Dragon Alpha" \
  -configuration Debug \
  -destination "platform=macOS" \
  build
```

## Key Folders

- `Code/` - runtime and game code owned by Dragon Alpha.
- `Code/Legacy/` - Dragon Alpha's own Carbon-era reference snapshots kept for parity/audit context (not external runtime source).
- `Resources/` - runtime-ready assets used by the app.
- `Assets/` - source/original/archive asset material and catalogs.
- `Design/` - product direction, backlog, testing coverage, and release checklists.

## Validation Shortcuts

- See `Design/Testing.md` for the current deterministic validation inventory and coverage notes.
- Use the `xcodebuild` command above for a direct public-checkout smoke build.

## License

This project is licensed under the MIT License. See `LICENSE`.
