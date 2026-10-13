# SHAMAN RTS controls, Stage A: control foundation

Status: **first build failed at link time (fixed below); the fix is not yet built.** Nothing is committed.

Patch: `Claude outputs/rts_stageA_control_foundation.patch` (14 files, 8 of them new). It applies cleanly both to your current working tree and to a clean `54cac5e`, and it does not touch the team-colour files.

Stages B (commands, G-key guard) and C (spell panel, targeting, altitude range) have not started. They wait for your validation of Stage A.

## Baseline gate

| Check | Result |
|---|---|
| Branch / HEAD | `main` at `54cac5e`, same as `origin/main` (checked live on GitHub) |
| Phase 2.1 / 2.2 | Committed (`8d7019e`, `54cac5e`) |
| Working tree | Committed files plus the uncommitted team colours (4 files) and the `Shaman.uproject` engine-association change. Nothing else. |
| Maps | `ShamanPrototype` uses `ShamanGameMode` (GameMode Override None); `PlanetTest` uses `ShamanPlanetGameMode`. Both pick up the new player controller through the game mode, and no map was edited. |

## Architecture assessment (whole milestone)

| Area | Existing | Milestone uses it as |
|---|---|---|
| Player controller | None: the default `APlayerController`, with all input bound on `AShamanCharacter` | New `AShamanPlayerController` (Stage A) |
| Shaman controls | WASD, mouse look (planet-aware), LMB cast, RMB/F melee, E interact, Q/wheel spell cycling, R rally, F3 debug | Unchanged in Shaman mode. Blocked in RTS mode by an input layer, never edited. |
| Unit registry | Tribe rosters (`UTribeComponent`, `UTribeSubsystem::GetFollowers / GetShaman`) | Selection candidates; no world scans |
| Tasks | Phase 2.2 runs Task.MoveTo / MoveToActor / Guard (a fixed post) | Stage B: Move = Task.MoveTo. Guard needs an orbiting-target extension of the existing Guard executor. |
| Combat | AI engage and melee (`TryMeleeAttack`), targeting rules (`FShamanTargetRules`) | Stage B: an Attack task that reuses them |
| Harvest / Build / Enter | Resource nodes and buildings exist, but no gathering, construction or interiors | Stage B: validated, then answered "not available yet" |
| Worship | `AMonumentActor::Worship / CanWorship` | Stage B: check whether Braves are allowed (worship rules: ShamanOnly or ShamanAndBraves) |
| Spells | `FSpellRow` (data table) already has a targeting type (Projectile/Ground/Self/Area), `bHoming`, a range and an unlock condition. `USpellComponent` handles mana, cooldown, charges and range. | Stage C panel and targeting: data-driven, no hard-coded spell list |
| TAKA (Blast) | Projectile, **homing**, 5 mana, range 10 (500 uu), damage 40, filter AllUnits, friendly fire on. Locked by the `Shaman.Data.TakaPreserved` test. | Stage C: the tracking category, behaviour unchanged |
| Altitude | `FPlanetFrame::GetAltitude` (height above the base radius), `FTerrainSample.Height`; flat maps use world Z against a ground trace | Stage C: Shaman height above the ground at the target zone, through the terrain query |

## What Stage A adds

| Piece | Where | What it does |
|---|---|---|
| Control mode | `AShamanPlayerController` | Starts in **RTS**. Enter toggles RTS and Shaman (also the `ShamanControlMode` console command). |
| Input routing | same | **Always possesses the Shaman.** RTS mode pushes an input layer with `bBlockInput`, so the Shaman's bindings never fire; Shaman mode pops it. Enter is bound on the RTS layer and on the controller's own layer, but only one is active at a time, so one press toggles once. Keys held during a switch are flushed. |
| Strategic camera | `AShamanRTSCamera` + `FShamanRTSCameraRig` | An actor, not a pawn, so moving it can never move the Shaman. Pan (WASD/arrows, optional screen-edge pan), rotate (Q/E, middle-mouse drag), zoom (wheel, clamped), F focus on the selection (or the Shaman), Home focus on the Shaman. Starts over the settlement looking toward the Shaman. The focus is kept on the ground and the camera lifted clear of hills. Planets: radial up, with the heading parallel-transported so the view never flips. |
| Selection | `UShamanSelectionComponent` + `FShamanSelectionRules` | Click selects; clicking a selected unit deselects it; clicking another unit replaces the selection; Shift adds; an empty click clears. Drag beyond 6 px selects with a box. Only living followers and the Shaman of the player's tribe can be selected. Selected units that die, are destroyed or change tribe are dropped 10 times a second; only the selection itself is checked, no world scan. Selecting never touches tasks, tribe membership or workers. |
| Picking | same | Screen projection of the player tribe's roster plus the Shaman. On planets, units over the horizon are skipped. |
| HUD | `AShamanHUD` | `RTS MODE` / `SHAMAN MODE` banner with key help; a gold ground-aligned selection ring plus a chevron per selected unit (drawn on the canvas, so no materials change); a translucent drag box with a glowing edge; the selected count. Crosshair and interact prompt appear only in Shaman mode. |
| Ground helper | `FShamanSpace::ProjectToGround` | Planets: the terrain query. Flat maps: the first hit on the terrain actor (not roofs or trees). |

All tuning (camera distances, pitch, speeds, zoom step, edge pan, blend time, drag threshold, pick radius, ring and box colours) is in `CameraSettings` / `SelectionSettings` on the controller and can be edited in a Blueprint subclass. Every mode switch and selection change is logged (`[Control]`, `[Select]`).

## Files

| File | Change |
|---|---|
| `Public/Player/ShamanControlTypes.h`, `Private/Player/ShamanControlTypes.cpp` | New: mode enum, settings, camera rig math, selection rules |
| `Public/Player/ShamanPlayerController.h/.cpp` | New: modes, input layer, camera control, selection gesture |
| `Public/Player/ShamanRTSCamera.h/.cpp` | New: strategic camera actor |
| `Public/Player/ShamanSelectionComponent.h/.cpp` | New: selection state |
| `Private/Tests/ShamanControlTests.cpp` | New: 4 tests |
| `Private/Game/ShamanGameMode.cpp` | `PlayerControllerClass = AShamanPlayerController` (the planet game mode inherits it) |
| `Public/UI/ShamanHUD.h`, `Private/UI/ShamanHUD.cpp` | Mode banner, rings, drag box; crosshair and prompt only in Shaman mode |
| `Public/Terrain/ShamanSpace.h`, `Private/Terrain/ShamanSpace.cpp` | `ProjectToGround` |

Not changed: spells, TAKA, Convert, tasks, AI, terrain backend, tribe code, maps, config, `.uproject`.

## Automated tests (expected 48: the existing 44 plus 4 new)

| Test | What it checks |
|---|---|
| `Shaman.Controls.CameraRig` | Flat view geometry; pan forward and right; yaw turns right; orbit keeps distance; pitch and zoom clamps. Planet: 2000 pan steps (more than one and a half laps of the globe): focus stays on the sphere, heading stays tangent and never flips, camera always above. |
| `Shaman.Controls.SelectionRules` | Eligibility (player Brave and Shaman yes; enemy, Wildman, dead, null no); click versus drag threshold; click picking (hit, miss, off-screen); box with reversed corners; inside only; off-screen excluded |
| `Shaman.Controls.Selection` | Select, deselect, replace, Shift-add; clicking an enemy clears; box keeps only player units; empty click clears; no task, worker or tribe change; death, tribe change and destruction are pruned |
| `Shaman.Controls.ModeSwitch` | A real controller possesses the Shaman. RTS is the default; the view is the RTS camera; pan, orbit and zoom never move or turn the Shaman. Enter goes to Shaman mode (view = Shaman, same actor, health, mana and tribe kept) and back to RTS. Still exactly one camera and one Shaman per tribe; no tasks; worker pool unchanged. |

**Not covered by the tests:** real mouse and keyboard input, screen projection, HUD drawing and camera rendering. The test world has no viewport, so those are left to the in-editor steps.

## How to validate

**1. Apply (Unreal closed, project folder):**
```
git apply --check "Claude outputs\rts_stageA_control_foundation.patch"
git apply "Claude outputs\rts_stageA_control_foundation.patch"
git status --short
```
Expect 6 more modified files (`ShamanGameMode.cpp`, `ShamanHUD.h/.cpp`, `ShamanSpace.h/.cpp`, plus the team-colour files and `.uproject` as before) and new untracked `Source/Shaman/*/Player/` folders and `ShamanControlTests.cpp`.

**2. Build and test:** open `Shaman.uproject`, let it rebuild, then run the `Shaman` automation tests. Expect 48/48.

**3. `ShamanPrototype` (GameMode Override None), Play:**

| # | Steps | Expected |
|---|---|---|
| 1 | Start | `RTS MODE` banner, mouse cursor visible, camera above the settlement, Shaman standing still |
| 2 | WASD / arrows, Q/E, middle-mouse drag, wheel | Camera pans, rotates and zooms within limits. The Shaman never moves. No flips, and the camera stays above hills. |
| 3 | Click a Brave; click it again; click another Brave; Shift-click a third; click empty ground | Gold ring + chevron appears; disappears; moves to the new Brave; both ringed; all cleared. Log shows `[Select]` lines. |
| 4 | Click an enemy Brave or a Wildman | Nothing selected (an empty click) |
| 5 | Drag a box over several Braves and the Shaman | Translucent box with a glowing edge while dragging; on release, only your units inside get rings |
| 6 | F, then Home | Camera centres on the selection, then on the Shaman |
| 7 | Enter | `SHAMAN MODE`: third-person camera, WASD moves the Shaman, LMB casts Blast, E interacts, Q cycles spells, F3 debug. The cast hint is shown again. |
| 8 | Enter again | `RTS MODE`, camera over the Shaman, nothing cast or moved by the switch, selection kept |
| 9 | Select a Brave, kill it with 2 Blasts in Shaman mode, Enter | Its ring is gone. Run `ShamanTribeReport`: counts as Phase 2 (death updates followers). |
| 10 | Regression | `ShamanTaskTest Move` + `Go`, TAKA, Convert, player death and rebirth all behave as before in Shaman mode. The console commands work in either mode. |

**4. `PlanetTest` (`ShamanPlanetGameMode`), Play:**
- Starts in RTS over the circle.
- Pan a long way: no flips, the view stays upright to the local surface.
- Selection rings sit flat on the slope.
- Enter: the planet third-person camera works as before.

## Known limitations

- **Esc in Play-in-Editor:** the editor uses Esc to stop play before the game sees it. Use Backspace to clear the selection, or click empty ground.
- **Cursor focus:** with the cursor shown, the first click into the editor viewport may only focus it.
- **Planet camera:** the camera works anywhere on the globe in principle (the tests pan more than one and a half laps), but it has only been designed and tested for the starting region. It doesn't avoid terrain between the camera and the focus, it only lifts the camera above the ground directly under it.
- **Picking:** screen-space picking doesn't account for occlusion. A unit hidden behind a hill but in front of the camera can still be picked. Units on the far side of a planet are excluded.
- **No commands yet:** selection persists into Shaman mode (rings stay visible) but does nothing there. Right-click and G arrive with Stage B.

## Notes for Stage C (from your message)

- The spell-tab reference you attached is saved as `claude/reference/spell_panel_reference.png`. The Stage C panel will follow its layout.
- Blast (TAKA, fireball) will show as an available spell.
- Convert will show as a **"?"** slot (discoverable on this level) until it's unlocked; it's only shown as "?", not disabled in the rules.
- Spell slots will be driven by the spell data table and the Shaman's known spells, not a hard-coded list.

## Build fix 1 (2026-10-10)

**Error:** the only errors, all at link time (`Saved/Logs/Shaman.log`):
`ShamanPlayerController.cpp.obj : error LNK2019: unresolved external symbol FInputChord::FInputChord(FKey,bool,bool,bool,bool)`, plus the matching copy constructor and destructor, then `LNK1120: 4 unresolved externals`.

**Root cause:** `UInputComponent::BindKey`, which the RTS input layer uses for raw keys, builds an `FInputChord`. In UE4.27 that type is exported by the **Slate** module, and the Shaman module didn't depend on Slate. Every Shaman source file compiled; only the link failed.

**Fix:** `Shaman.Build.cs` gets `PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });`. No code changes.

**Applying:** if Stage A is already applied (your tree), run `git apply "Claude outputs\rts_stageA_fix1_slate_link.patch"`. The regenerated `rts_stageA_control_foundation.patch` already includes the fix, for applying from a clean `54cac5e`. Changing `Build.cs` makes Unreal regenerate its build files, so expect a fuller rebuild.
