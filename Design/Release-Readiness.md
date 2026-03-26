# Release Readiness

Last updated: 2026-03-23.

## Goal
- Define the remaining human gate for first public push from this workspace.

## Automated Gate Status
- Standalone engine ownership: passing.
- macOS smoke build: passing.
- Strict open-source readiness audit: passing.
- Deterministic `DRAGON_TEST` coverage: passing and tracked in `Design/Testing.md`.

## Native-Only Signoff Checklist
- Run one 30+ minute mixed session across traversal, battle, shop, and status.
- Confirm click/target feel on real hardware for world map, shop rows, battle targets, and status actions.
- Verify splash/front-door toggle behavior (fullscreen/sound/music) in a normal native run.
- Verify lifecycle/audio behavior in a normal native run (launch/resume/focus changes).
- Capture one screenshot per checkpoint into `Agents/Reports/Screenshots/`.

## Recommended Checkpoints
1. Splash and front door layout/readability.
2. New Game slots and delete-mode readability.
3. New Avatar card selection and class text readability.
4. World map frame, controls, objective line, and interaction messaging.
5. Event interaction flow (talk/treasure/heal/train/shop/gate/challenge/warp) with at least one cancel/decline branch.
6. Shop row/paging feel and purchase/rejection behavior.
7. Battle targeting, command popup placement, result panels, and multi-enemy continuation.
8. Status/inventory action flow (equip/use/sell/unequip) and text fit.
9. Save/load round trip preserving area/progression/inventory state.

## Push Sequence
1. Complete the native-only checklist above and record pass/fail notes.
2. Run `./Agents/BuildSmoke.sh`.
3. Run `./Agents/AuditOpenSourceReleaseReadiness.sh --strict`.
4. Stage changes in this primary workspace and push to `origin`.
