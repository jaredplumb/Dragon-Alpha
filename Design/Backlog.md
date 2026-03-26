# Dragon Alpha Backlog

Last updated: 2026-03-26.

## Purpose

- Track the remaining work to move Dragon Alpha from a standalone playable port into a fully working, publicly shippable macOS game.
- Separate first-public-push release blockers from broader gameplay-parity and modernization work.
- Reflect current repo truth: local engine ownership and the implemented deterministic validation ladder are already in place.

## Current Baseline

- Standalone macOS ownership/build/export validation is green.
- The local engine/runtime surface now uses repo-owned `Engine*` files and `E*` types instead of the old `G*` naming, and that rename has been revalidated through smoke builds, open-source export validation, and focused `DRAGON_TEST` proof runs.
- The implemented `DRAGON_TEST` ladder is green end to end in `Agents/Reports/VisualValidationSuite.md`.
- The world-map interaction regression cluster now passes repeated soak execution (`./Agents/SoakWorldMapInteractionRegressionGroup.sh --iterations 10`), reducing risk of route/interact/facing timing flakes in longer sessions.
- The biggest remaining gaps are no longer bootstrap/engine gaps; they are gameplay-parity gaps, native-only validation gaps, and a small number of release-handoff steps.

## P0: Release Signoff And Public Push

- [ ] Run the native-only remainder from `Design/Release-Readiness.md`.
- [ ] Capture one screenshot per manual checkpoint with pass/fail notes.
- [ ] Complete one 30+ minute mixed session across traversal, battle, shop, and status without soft-locks.
- [ ] Run one capture-capable proof pass so at least one visual-validation lane is pixel-backed instead of telemetry-only.
- [x] Validate stripped-export compatibility, then retire the temporary checkout. Verified the sanitized export path with `Agents/PrepareOpenSourceReleaseTree.sh` in a temporary checkout and then removed the temporary export workspace so this repo remains the single active surface.
- [x] Rerun smoke build and strict release-readiness in the primary workspace before first push. `./Agents/BuildSmoke.sh` and `./Agents/AuditOpenSourceReleaseReadiness.sh --strict` pass in the current repo.
- [x] Update GitHub-facing README files (root `README.md` plus public-facing guide README surfaces) to match the standalone macOS direction before first push. Added a new root `README.md` with current migration direction + legacy DMG pointer (`Assets/Archive/DragonAlpha.dmg`), restored root `LICENSE` (MIT), and updated `Design/README.md` for GitHub onboarding context.

## P1: Front Door And Save-System Correctness

- [x] Remove the legacy registration flow from the modern build, including the splash/menu button, any runtime code that still routes into registration, and any preference/storage handling that only exists to support serial entry.
- [x] Add deterministic coverage for filled-slot `NewGame` layout, not only empty-slot boot.
- [x] Add deterministic coverage for delete-confirmation flow in `NewGame`.
- [x] Add deterministic coverage for corrupted-save recovery messaging/behavior.
- [x] Verify slot metadata parity for level/progress/readability across all three visible slots.
- [x] Confirm save/load preserves area unlocks and progression flags beyond the starter-path round trip.
- [x] Add deterministic fixtures for later-game save states instead of validating only a newly created starter slot.

## P1: World Map Gameplay Correctness

- [x] Verify distant-tile auto-pathing behaves correctly in normal play, not just first-entry layout.
- [x] Verify tapping the current tile during auto-path cancels the route and triggers the expected interaction attempt.
- [x] Verify gate/challenge/warp markers magnetize route selection toward the correct objective tile. Current recovered live marker coverage is gate-based; `Validation_WorldMap_GateRouteMagnet` plus `Agents/Reports/WorldMapObjectiveReachability.md` now cover the shared magnetization path.
- [x] Verify `ACT`/`MAGIC` side-button interaction near event tiles triggers the intended interaction without helper-only fallback text.
- [x] Verify `TECH` opens status/save-related flow correctly from live world-map play.
- [x] Port and validate talk event flow end to end.
- [x] Port and validate treasure event flow end to end.
- [x] Port and validate heal event flow end to end. Current recovered legacy script tables still expose no active `type=8` heal objects, so `Validation_WorldMap_HealEvent` now exercises the legacy heal action path through a deterministic synthetic DRAGON_TEST event and verifies full HP restoration plus heal message telemetry.
- [x] Port and validate train event flow end to end.
- [x] Port and validate shop entry from organic map play, not only startup fixtures.
- [x] Port and validate gate event flow end to end.
- [x] Port and validate challenge event flow end to end.
- [x] Port and validate warp event flow end to end. The active recovered warp path is now covered through live cell-warp transition in `Validation_WorldMap_WarpTransition`.
- [x] Validate decline/cancel branches for map interactions so decline text is context-aware rather than generic.
- [x] Add deterministic world-map interaction scenarios where the current manual checklist still depends on live play only.

## P1: Shop And Economy Correctness

- [x] Implement `Validation_Shop_DuplicateOwned`.
- [x] Implement `Validation_Shop_InventoryFull`.
- [x] Verify row-state, ownership-state, and page-state remain correct after blocked purchases.
- [x] Verify shop entry/exit preserves expected world-map state.
- [x] Verify merchant pricing and gold deltas match legacy economy expectations beyond the currently tested cases. `Validation_Shop_MultiPurchase` now proves sequential live purchases at `50` and `70` gold with cumulative gold dropping from `999` to `879`.
- [x] Verify training/shop interaction sequencing from live world-map progression rather than fixture-only entry.

## P1: Battle Correctness

- [x] Verify normal battle entry from live world-map play, not only strict startup fixtures. `Validation_WorldMap_RandomBattleEntry` now enters a live roaming encounter from `WorldMap` movement in `Eleusis Caves` instead of relying on direct `Battle` autorun.
- [x] Verify gate/challenge battle entry and progression. `Validation_WorldMap_GateBattleEntry`, `Validation_WorldMap_GateBattleProgression`, and `Validation_WorldMap_ChallengeVictoryProgression` now cover live gate/challenge battle entry plus deterministic post-victory world-map progression.
- [x] Verify battle popups for skills/items anchor correctly in preferred view. `Validation_Battle_Magic_SeededOutcome` and `Validation_Battle_Tech_SeededOutcome` now assert that the strict-legacy `MAGIC`/`TECH` popup menus stay centered in the preferred view, keep header/action/cancel rows vertically ordered, and remain above the battle command lane.
- [x] Verify battle result/feedback panels stay centered and unclipped in all important combat states. `Validation_WorldMap_RandomBattleRetreat`, `Validation_WorldMap_ChallengeDefeatPanel`, and `Validation_Battle_VictoryPanel` now hold retreat, defeat, and victory result panels and assert their headline/body telemetry without clipping.
- [x] Verify multi-enemy battles continue correctly after the first enemy defeat. `Validation_Battle_MultiEnemyContinue` now weakens the first enemy in the strict-legacy four-foe fixture, lands one kill, and proves the battle stays live at `Enemy count: 3` with the next target stepped forward.
- [x] Verify enemy special attacks can trigger and display readable, correct combat text. `Validation_Battle_EnemySpecialResponse` now forces one deterministic enemy reply after `DEFEND` and verifies the readable strict-legacy combat line `Goblin Peasant used Dark Jab for 84.`.
- [x] Verify enemy heal behavior can trigger and display correct combat text/state updates. `Validation_Battle_EnemyHealResponse` now forces a deterministic enemy heal response after `DEFEND` and verifies the readable combat line `Goblin Peasant used Goblin Courage and recovered 15 HP.` alongside the healed `HP 45/60` state.
- [x] Verify no-damage combat outcomes show explicit no-damage messaging. `Validation_Battle_NoDamageResponse` now forces one deterministic enemy reply after `DEFEND` and verifies the explicit no-damage line `Goblin Peasant used Dark Jab, but it dealt no damage.` while player HP stays unchanged.
- [x] Verify element-heavy enemies produce visibly and numerically distinct elemental outcomes versus neutral enemies. `Validation_Battle_ElementalNeutralOutcome` and `Validation_Battle_ElementalResistOutcome` now prove the same `Pin Shot` tech produces a normal damage result (`Dead Fighter`, `HP 350/380`, `Pin Shot deals 30.`) versus an absorb/heal result on an air-resistant enemy (`Sky Dragon`, `HP 972/1900`, `Sky Dragon absorbed Pin Shot.`).
- [x] Add deterministic seeded coverage for at least one magic-command outcome. `Validation_Battle_Magic_SeededOutcome` now opens the strict-legacy `MAGIC` menu, selects `Cure`, and freezes the first post-command state at `Cure restores 75 HP.` with `HP 125/125`.
- [x] Add deterministic seeded coverage for at least one tech-command outcome. `Validation_Battle_Tech_SeededOutcome` now opens the strict-legacy `TECH` menu, selects `Rally`, and freezes the first post-command state at `Rally restores 45 HP.` with `HP 125/125`.
- [x] Confirm whether item-in-battle coverage is required for first push. The current player-facing battle lane only exposes `ATTACK`, `DEFEND`, `MAGIC`, `TECH`, and `RUN`; the old consumable submenu code remains unreachable from live battle UI, so no first-push deterministic item-command case is required unless battle UI scope changes.
- [x] Add deterministic seeded coverage for at least one enemy-response outcome after player action. `Validation_Battle_EnemySpecialResponse`, `Validation_Battle_EnemyHealResponse`, and `Validation_Battle_NoDamageResponse` now force deterministic enemy replies immediately after `DEFEND` and verify exact post-response telemetry.
- [x] Verify run/no-run behavior and encounter-exit state where applicable. `Validation_WorldMap_RandomBattleRetreat` now proves a live roaming encounter accepts `RUN`, resolves a `RETREATED` result panel, and returns to `WorldMap` state near the encounter tile with combat UI cleared.

## P1: Status, Inventory, And Character Correctness

- [x] Add deterministic coverage for armor equip flow.
- [x] Add deterministic coverage for armor unequip flow.
- [x] Add deterministic coverage for relic/accessory equip flow if present in runtime content.
- [x] Add deterministic coverage for relic/accessory unequip flow if present in runtime content.
- [x] Verify selling equipped gear is handled safely and consistently.
- [x] Verify inventory-full edge handling in status/inventory flows.
- [x] Verify later-game inventory pages and action rows remain readable with larger inventories.
- [x] Verify training/progression/stat growth behavior against legacy expectations.
- [x] Verify class-specific progression differences remain correct after avatar creation.

## P1: Data And Progression Parity

- [x] Audit loaded map/object data against legacy expectations area by area.
- [x] Verify all active map object action types needed for the game are actually ported and reachable.
- [x] Verify warp semantics/load-control points across all supported areas.
- [x] Verify battle data loading covers the intended encounter set without placeholder-only fallbacks.
- [x] Verify progression flags, unlock gates, and challenge completion states persist correctly across save/load.
- [x] Identify any remaining recovered-content gaps where legacy content exists but is not yet reachable in live play.

## P2: Native macOS Runtime Quality

- Runtime lifecycle handling now pauses gameplay/audio on focus loss/minimize and resumes on refocus; remaining work in this section is native manual confirmation on real hardware.
- [ ] Verify pointer/click feel for avatar cards, world-map controls, shop rows, battle targeting, and status actions on real hardware.
- [ ] Verify native audio startup/output behavior in normal non-automation runs.
- [ ] Verify app lifecycle/interruption behavior for audio and window state.
- [ ] Verify final text readability and scene feel in a real Xcode-launched build, not only telemetry-backed automation.
- [ ] Verify fullscreen/sound/music toggle behavior from the splash/front door on native hardware.

## P2: Asset And Content Follow-Through

- [x] Audit remaining active runtime art for placeholder, stretched, clipped, or incorrect legacy conversions. `./Agents/ValidateActiveRuntimeArtAudit.sh` now rolls world-map layer integrity, battle backdrop integrity, final visual-source parity, asset-source completeness, chroma cleanup, and fallback/temp placeholder gates into one repeatable proof, with the latest summary recorded in `Agents/Reports/ActiveRuntimeArtAudit.md`.
- [x] Restore real alpha on player portrait and battle sprite PNGs instead of keeping white-matted RGB conversions. `./Agents/ValidatePlayerSpriteAlpha.sh` now verifies that the runtime `Avatar*` / `BattlePlayer*` PNGs plus their active source mirrors under `Assets/Images/Legacy/Resources/` and `Assets/Images/RecoveredOverrides/` all carry real alpha and stay byte-matched where expected.
- [x] Audit remaining active runtime sounds for consistency and appropriateness on modern output devices. `./Agents/ValidateActiveRuntimeSoundAudit.sh` now rolls level consistency, trigger coverage, scene semantic isolation, legacy sound parity, traversal integrity, world-map/menu routing, and audio-device fallback into one repeatable proof, with the latest summary recorded in `Agents/Reports/ActiveRuntimeSoundAudit.md`.
- [x] Decide the long-term runtime music format/path for `Resources/Music/`. Dragon Alpha now treats repo-owned Standard MIDI files (`.mid`) as the long-term runtime format, staged from `Assets/Music/Original/MIDI/` and destined for stable local copies under `Resources/Music/` per `Design/MidiRuntimeMapping.md`.
- [x] Implement runtime music playback integration with repo-owned MIDI routing. Scene-based runtime MIDI playback now runs through `Resources/Music/` (`theme.mid` for front-door/menu, area map tracks in `WorldMap`, and area battle tracks in `Battle`) while honoring saved music enable/disable preference in real time.
- [x] Verify all runtime assets needed for reachable areas/scenes are present in the loose-resource contract. `./Agents/ValidateLooseResourceContract.sh` now rolls layout, runtime ID coverage, fallback audit, area alignment, battle backdrop integrity, and visual source parity into one repeatable pass, with the latest summary recorded in `Agents/Reports/LooseResourceContract.md`.

## P2: Automation And Validation Expansion

- [x] Add capture-capable automation coverage to complement telemetry-only proof on constrained hosts. `./Agents/visual-validate.sh` and `./Agents/prove-visual-pipeline.sh` now accept `--require-png-capture`, so capture-capable Macs can promote the same deterministic cases into a strict pixel-backed gate while constrained hosts keep the truthful telemetry-backed fallback path.
- [x] Reduce dependence on startup-fixture exceptions by reaching more cases through normal front-door flow where practical. `Validation_WorldMap_FrontDoorTalkRoute` now boots from isolated empty storage, enters the first live `Eleusis` map through the normal front door, routes into `Talk 22,17` through live map taps, and returns cleanly to `WorldMap`, so the talk lane no longer depends only on the startup-fixture case.
- [x] Add broader deterministic world-map interaction scenarios once fixture/state setup is stable. `Validation_WorldMap_FrontDoorRouteCancel` now complements the clean-start talk route by booting from isolated empty storage, routing toward the first visible `Talk 22,17` tile through the normal front door, and proving that a live current-tile re-tap cancels the route cleanly without leaving stale route telemetry behind.
- [x] Add broader deterministic later-game save fixtures for regression coverage beyond the starter slot. `Validation_SaveLoad_LaterGameLoadoutState` now complements `Validation_SaveLoad_ProgressionState` with a second later-game `Eleusis Caves` save that preserves a level-18 warrior, six-area discovery, later-game progression flags, readable slot metadata, and a 24-item later-game inventory/loadout across save -> menu -> reload -> status flow.
- [x] Keep `Design/Testing.md` aligned as new cases move from proposed to implemented. `./Agents/ValidateTestingInventory.sh` now compares the implemented case inventory across `Code/Main.cpp`, `Agents/visual-validate.sh`, `Design/Testing.md`, `Agents/README.md`, and `Agents/Handoffs/policy-continuity.md`, with the latest summary recorded in `Agents/Reports/TestingInventoryAlignment.md`.

## P3: Cleanup After Public Push

- [x] Promote the normal Dragon Alpha workspace to the primary git workflow connected to `origin` (`https://github.com/jaredplumb/Dragon-Alpha.git`) and validate push-readiness directly from this repo.
- [ ] Remove local-only `AGENTS.md` / `Agents/` from the final public-facing working flow when the migration phase is truly complete.
- [ ] Decide whether `AGENTS.md` / `Agents/` should become public for blog/process context; if yes, ship a scrubbed public-ready version and adjust `.gitignore` policy intentionally.

## Likely Order Of Work

1. Finish native-only release signoff and public push handoff.
2. Close front-door/save edge cases and remove obsolete front-door flows such as registration.
3. Close world-map interaction parity.
4. Close battle parity gaps.
5. Close remaining shop/status/economy edge cases.
6. Finish music/runtime polish and longer-tail cleanup.
