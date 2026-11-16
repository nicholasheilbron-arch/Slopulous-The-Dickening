# SHAMAN RTS: acceptance fixes (unit selection and spell panel)

Patch: `Claude outputs/rts_acceptance_fix_selection_spellpanel.patch`. It applies on top of Stage A and the Slate link fix (checked with `git apply --check` against copies of your current files). UE4.27.2 only. Nothing is committed or pushed. Team colours and `Shaman.uproject` are untouched.

## Verification status

| Item | Status |
|---|---|
| Build with this patch | **NOT VERIFIED.** I can't run UBT. |
| Automation tests | **NOT VERIFIED.** Expect **50**: your last run was 48/48 passed, and this patch adds 2. |
| PIE behaviour | **NOT VERIFIED.** Use the steps below. |
| Diagnosis | Based on your `Saved/Logs/Shaman.log` (10/09 21:14 run) and a code trace. |

## Failure 1: Braves cannot be selected

**Evidence from your log.** The game reached the right controller (`[Control] RTS MODE (start)`), LMB fired, and the gesture ran. Every drag still logged `[Select] box: 0 unit(s)`. Plain clicks logged nothing. With this bug, a click on a unit takes the "empty click" path, and that path is silent when nothing is selected.

**Root cause.** The selection latched the wrong tribe at possession.

- `AShamanPlayerController::OnPossess` called `Selection->SetPlayerTribe(GetPlayerTribe())`.
- In PIE, the player Shaman is possessed during login, before world BeginPlay. At that point `AShamanUnitBase::ApplyIdentity` (run from `BeginPlay`) has not copied `TribeId = 0` into `TribeMember->TribeId`. So `FShamanTargetRules::GetTribeOf` returned -1, and `UShamanSelectionComponent::PlayerTribe` became -1 and stayed there.
- `GatherScreenCandidates` re-reads the tribe live (0) and found the Braves.
- Then `UShamanSelectionComponent::ClickUnit` / `SelectUnits` checked `FShamanSelectionRules::IsSelectable(U, -1)`. That is always false, so every unit was rejected.
- The rings never drew because the selection stayed empty.
- The Stage A tests possess after BeginPlay, so they never hit this ordering.

**Fix** (`ShamanPlayerController.cpp/.h`):

- `GetPlayerTribe()` treats a negative pawn tribe as "not known yet".
- New `RefreshPlayerTribe()` pushes the tribe into the selection only once it is valid (>= 0). It runs at OnPossess, every `PlayerTick` and before each selection gesture resolves.
- Mode switching is unaffected.

**Shaman (direct) mode.** By design, there is no click selection in Shaman mode: LMB casts TAKA, the Phase 1 binding, which is preserved. The selection made in RTS mode is kept, and its rings stay drawn in Shaman mode. If you want selection in Shaman mode too (for example Ctrl+LMB), say so; it's a small addition.

**Diagnostics added** (off by default):

- `shaman.DebugSelection 1` logs `[SelectDiag]` lines for each step: LMB press with the mouse position, release, click or drag, both tribe values, candidate count, how many are on screen, the pick result and the nearest unit in pixels.
- Console `ShamanSelectionDump` logs the pawn, both tribes, the roster count and every candidate's screen position.
- A one-line `[Select] player tribe -1 -> 0` appears whenever the late tribe is picked up. With this fix you should see it once at PIE start, if the stale value occurs at all.

## Failure 2: no spells in the spell panel

**Root cause.** There was no spell panel. Stage A only shipped the Phase 1 selected-spell text (bottom centre). The panel belonged to Stage C, which had not been started.

The spell data itself was fine. Your log shows the missing `DA_ShamanGameData` fallback working: `No DA_ShamanGameData asset; using built-in defaults` and then `Using /Game/Data/Spells.Spells for SpellRow rows`.

**Fix.** A minimal real, data-driven panel, with no new assets.

- `UI/ShamanSpellPanel.h/.cpp` (new): `FShamanSpellPanelModel::Build` reads `USpellComponent::SpellTable` rows in table order, plus `KnownSpellIds`. A spell counts as **implemented** only when casting it does something:
  - for a projectile spell, a projectile class is registered for its `EffectId`;
  - otherwise, the effect dispatcher has a matching effect.

  Each spell then falls into one case:

  | Spell | Shown as |
  |---|---|
  | Known and implemented | **Available** (Blast / TAKA) |
  | Implemented but not known on this level | **"?"**, not selectable (Convert) |
  | Known but not implemented (for example after `ShamanLearnSpell Swarm`) | Grey tile, "not implemented" |
  | Unknown and not implemented | Not shown; counted in a "+17 spells not implemented yet" footer |
  | No spell table at all | "No spell data", with no fake tiles |

- `ShamanHUD.cpp/.h` draws the panel on the right edge in both modes. It uses 3 columns of parchment tiles after your reference image. Each tile shows the name ("Blast" / "(Taka)"), the real mana cost (red when you can't afford it), a cooldown shade and a gold border on the selected spell.
- **Icons are text placeholders.** There are no icon assets in the project, and I did not invent any.
- `AShamanCharacter::SelectSpell(Id)` (new, additive) sets the selected spell index. It never casts.
- In RTS mode, clicking a tile selects that spell. Clicks on the panel never start a unit selection.
  - An RTS-mode hint says the spell is "Selected for casting in Shaman mode (Enter, then LMB). RTS-mode casting is not implemented yet."
  - That is accurate: casting from RTS mode (targeting) is Stage C.
- TAKA's data and behaviour are unchanged, and a test asserts it.

## Changed files

- `Source/Shaman/Private/Player/ShamanPlayerController.cpp`
- `Source/Shaman/Public/Player/ShamanPlayerController.h`
- `Source/Shaman/Private/UI/ShamanHUD.cpp`
- `Source/Shaman/Public/UI/ShamanHUD.h`
- `Source/Shaman/Private/UI/ShamanSpellPanel.cpp` (new)
- `Source/Shaman/Public/UI/ShamanSpellPanel.h` (new)
- `Source/Shaman/Private/Characters/ShamanCharacter.cpp`: adds `SelectSpell`
- `Source/Shaman/Public/Characters/ShamanCharacter.h`: adds `SelectSpell`
- `Source/Shaman/Private/Tests/ShamanControlTests.cpp`: 2 new tests

## New tests (expected total: 50)

- `Shaman.Controls.PlayerTribeLate` reproduces possess-before-BeginPlay and the stale -1 tribe. It then checks:
  - the tribe is picked up;
  - click selects, clicking another Brave changes the selection and an empty click deselects;
  - a drag keeps only friendly Braves;
  - two mode switches keep both the tribe and the selection.
- `Shaman.UI.SpellPanel` checks:
  - Blast is available and selected, with its real name and cost;
  - Convert shows as "?" and can't be selected;
  - unknown unimplemented spells are hidden and counted;
  - Convert becomes available once learned, and selecting it moves the highlight;
  - selecting never spends mana or starts a cooldown;
  - a known but unimplemented spell is shown as unavailable;
  - rebuilding the panel never duplicates tiles;
  - the TAKA row is unchanged;
  - no table gives an empty panel with no data;
  - the layout and hit test agree.

## Apply, build and test

```
git apply --check "Claude outputs/rts_acceptance_fix_selection_spellpanel.patch"
git apply "Claude outputs/rts_acceptance_fix_selection_spellpanel.patch"
```

Then:

1. Rebuild (close the editor, build `ShamanEditor` Development Win64).
2. Run Session Frontend > Automation > `Shaman`. You should see 50 tests, all expected to pass.

## PIE steps (`ShamanPrototype`)

1. Open `Maps/ShamanPrototype`, then Play (Selected Viewport). Click once in the viewport to focus it. Press `` ` `` and type `shaman.DebugSelection 1`.
2. The banner reads **RTS MODE**. The **SPELLS** panel is on the right: **Blast (Taka)** with a gold border showing "5 mana", then a **"?"** tile (Convert), then "+17 spells not implemented yet".
3. Left-click one of your Braves. Expect a gold ring and chevron, "Selected: 1", and `[Select] select: 1 unit(s) - ...` in the log.
4. Left-click a second Brave. The ring moves, and the log shows `select` with the other name.
5. Left-click empty ground. The ring disappears, and the log shows `[Select] clear: 0 unit(s)`.
6. Drag a box around several Braves. All of your Braves inside get rings, and the log shows `box: N`. Enemy Braves and Wildmen are never selected.
7. Click the "?" tile. You should see `[SpellPanel] Convert is not discovered on this level`, and no unit-selection change.
8. Click the Blast tile. You should see `[SpellPanel] selected Blast (not cast ...)`. Mana stays the same.
9. Press **Enter**. The banner reads SHAMAN MODE, the rings stay and the panel is unchanged (no duplicate tiles).
10. LMB fires TAKA as before. Wheel or Q changes the spell, and the panel highlight follows.
11. Press **Enter** again. Back in RTS mode, repeat steps 3–6: they should still work.
12. Optional: type `ShamanLearnSpell Convert`. Convert becomes a normal tile and can be selected.
13. Stop PIE. If anything fails, send `Saved/Logs/Shaman.log`; the `[SelectDiag]` lines show which stage broke.
