# Dragon Alpha Changelog

Concise active changelog for current release work.
Historical long-form logs remain in `Agents/Archive/DesignHistory/History/`.

## 2026-03-26
- Hardened Apple lifecycle handling so app focus/minimize transitions now pause runtime draw/audio state and resume cleanly on refocus (`EngineSystem_Apple.mm` + `EngineSound_Apple.mm` lifecycle pause wiring).
- Completed a 10-iteration world-map interaction soak with all passes (`Agents/SoakWorldMapInteractionRegressionGroup.sh --iterations 10`), confirming stable clean-start shop/talk/facing/route-cancel behavior under repeated execution.
- Confirmed this host still cannot satisfy strict PNG capture (`--require-png-capture`) and continues to validate in truthful `text-telemetry` mode until a capture-capable Mac runs the same lane.
- Implemented runtime MIDI playback integration on Apple runtime with scene-based routing (`Splash`/`NewGame`/`NewAvatar` menu theme, area map tracks in `WorldMap`, area battle tracks in `Battle`) and real-time `isMusicOn` preference control.
- Staged repo-owned runtime MIDI assets into `Resources/Music/` (`Core__map.mid`, `Meadows__map.mid`, `Forest__map.mid`, `Caves__map.mid`, `Mountain__map.mid`, `Core__battle.mid`, `Forest__battle.mid`, `Caves__battle.mid`, `Mountain__battle.mid`, `theme.mid`).
- Wired fullscreen preference toggles to apply immediately at runtime on macOS (`DragonSetFullscreenEnabled` now drives host fullscreen state via `ESystem`), and applied saved fullscreen preference on splash startup.
- Added `RunVisualBatch.sh --group worldmap-interaction` for one-build focused execution of front-door interaction regressions.
- Refactored `ValidateWorldMapInteractionRegressionGroup.sh` to run through that one-build grouped path instead of rebuilding per case.
- Added `SoakWorldMapInteractionRegressionGroup.sh` with repeat-count rollups (`--iterations`, default `50`) and report output to `Agents/Reports/WorldMapInteractionSoak.md`.
- Expanded deterministic interaction coverage with four new `WorldMap` cases: `Validation_WorldMap_ActMissThrottleRepeat`, `Validation_WorldMap_FrontDoorFacingPriorityRetarget`, `Validation_WorldMap_RouteCancelDirectionalRetarget`, and `Validation_WorldMap_RouteTargetRetapThrottle`.
- Tightened route-clear validation semantics to require both `Route target:` and `Route requested:` to be absent after cancel/no-route flows.
- Expanded deterministic inventory references from 73 to 77 implemented `DRAGON_TEST` cases across active runner/docs/handoff surfaces.

## 2026-03-24
- Reproduced and fixed the clean-start stronghold shop-NPC `INTERACT` talk miss by adding adjacent interact-only fallback resolution in `WorldMap::TryInteractFacing`.
- Added deterministic clean-start coverage for the exact manual lane in `Validation_WorldMap_FrontDoorShopNpcTalk` (startup route -> stronghold shop NPC via `INTERACT` -> talk -> merchant).
- Hardened route-cancel behavior to keep current-tile active-route taps cancel-only, and updated `Validation_WorldMap_FrontDoorRouteCancel` to assert route-clear/no-talk semantics deterministically.
- Expanded deterministic inventory references from 65 to 66 implemented `DRAGON_TEST` cases across active runner/docs/handoff surfaces.

## 2026-03-23
- Reworked `Design/` to be human-facing only.
- Moved all generated validation output to `Agents/Reports/`.
- Consolidated release gate docs into `Design/Release-Readiness.md`.
- Moved the legacy disk-image recovery runbook to `Agents/Runbooks/`.
- Removed low-signal design clutter (`Design/Archive/`, old migration/checklist duplicates, generated feature-port doc from `Design/`).
- Relocated runtime tuning config to `Resources/Config/RuntimeDesign.cfg` and updated active validators/generators.
- Slimmed `Design/Testing.md` to a concise maintained inventory format while preserving deterministic case coverage alignment.
- Added focused authority docs `Design/02.Gameplay-Guide.md` and `Design/03.Visual-Guide.md`.

## 2026-03-22
- Added root public-facing `README.md` and confirmed root `LICENSE`.
- Kept legacy release image at `Assets/Archive/DragonAlpha.dmg` and removed root-level `DragonAlpha.dmg`.
- Promoted this workspace to primary GitHub-connected workflow (`origin`).
- Added strict pre-push validation flow (`BuildSmoke` and strict open-source readiness audit).
