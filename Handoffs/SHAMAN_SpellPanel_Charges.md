# SHAMAN: spell panel, spell charging and temporary spells

Patch: `Claude outputs/shaman_spellpanel_charges.patch`. It goes on top of your current uncommitted working tree on `feature/rts-stage-a`. Nothing is committed or pushed. UE 4.27.2 only.

## Verification status

| Item | Status |
|---|---|
| `git apply --check` against your working tree | **PASS**. I checked it against an exact copy of your tree (base `54cac5e` plus your uncommitted files). |
| Build | **NOT VERIFIED.** I can't run UBT or MSVC. A second, independent desk review of the patch for UE 4.27, UHT and C++14 issues found no compile problems. |
| Automation tests | **NOT VERIFIED.** The patch adds 10 tests. Your last expected total was 52 (Stage B report, also unverified), so expect **62**. |
| PIE | **NOT VERIFIED.** Use the PIE plan below. |

## 1. Pre-flight (what I found before editing)

I could not run `git` on your PC from here. I rebuilt the state instead: I cloned `origin/main` (= `54cac5e`, the same commit as your `HEAD`, `main` and `feature/rts-stage-a`), then laid your on-disk `Source/`, `Config/`, `.uproject` and `.gitignore` over it. The equivalent of `git status -sb`:

```
## feature/rts-stage-a
 M .gitignore
 M Shaman.uproject
 M Source/Shaman/Private/Characters/ShamanCharacter.cpp
 M Source/Shaman/Private/Characters/ShamanUnitBase.cpp
 M Source/Shaman/Private/Core/ShamanVisuals.cpp
 M Source/Shaman/Private/Game/ShamanGameMode.cpp
 M Source/Shaman/Private/Terrain/ShamanSpace.cpp
 M Source/Shaman/Private/UI/ShamanHUD.cpp
 M (matching headers) ShamanCharacter.h, ShamanUnitBase.h, ShamanVisuals.h, ShamanSpace.h, ShamanHUD.h, Shaman.Build.cs
?? Source/Shaman/Private/Player/            (Stage A/B controller, camera, selection)
?? Source/Shaman/Private/Tests/ShamanControlTests.cpp
?? Source/Shaman/Private/UI/ShamanSpellPanel.cpp
?? Source/Shaman/Public/Player/
?? Source/Shaman/Public/UI/ShamanSpellPanel.h
```

**One difference from the brief:** your tree also has `rts_stageB_move_guard_commands.patch` applied, not just the acceptance-fix patch. It reverse-applies cleanly, and so does `testing_team_colors.patch`. I built on top of all of it.

**Overlap with uncommitted RTS work.** These files already carry uncommitted changes and this patch edits them too:

- `ShamanHUD.cpp/.h`
- `ShamanPlayerController.cpp/.h`
- `ShamanCharacter.cpp/.h`
- `ShamanSpellPanel.cpp/.h`
- `ShamanControlTests.cpp`

Every hunk is additive or local to the spell panel, and nothing else in those files changes. The patch applies cleanly on top, and `git apply -R` removes it again. I did not change `Shaman.uproject`, the team colours, the RTS selection or the commands.

## 2. What was built

There is one source of truth: `USpellComponent`, which already held `KnownSpellIds`, `Mana` and the Stone Head charges. It is extended rather than replaced. The HUD, the controller, the monuments and the cheats only read it or call it.

| Area | Change |
|---|---|
| Spell data (`SpellRow.h`, `Spells.csv`) | New columns `MaxCharges` (0–4; 0 means it can't be learned permanently), `bChargingByDefault` (Blast only) and `Icon` (empty = placeholder). `ManaCost` is now the mana per charge. TAKA's existing values are untouched. |
| `USpellComponent` | Per-spell charge state: permanent flag, stored charges, partial progress and the charging switch. Charging runs in fixed steps from the shared economy. Casting spends a charge. Temporary (monument) charges are tracked separately. Spells can be marked as available on the current map. A native `OnSpellbookChanged` event fires on every change. |
| Player Shaman | Spell charges are on for player-controlled tribes (`UShamanGameData::bPlayerUsesSpellCharges`). AI Shamans keep the Phase 1 mana-at-cast rule. If the spell table has no charge columns yet, the player also stays on mana-at-cast, so TAKA keeps working, and the HUD shows *"Spell charges off: reimport Content/Data/Spells.csv"*. The selection now follows the spell id, so temporary spells coming and going don't move it. |
| Map discovery | A `?` tile appears only for spells this map offers: an `AMonumentActor` whose reward is `UnlockSpell`, or `SetSpellAvailableOnMap`. This works in both directions: Shamans scan existing monuments at BeginPlay, and monuments register with existing Shamans. |
| Panel (`ShamanSpellPanel`, `ShamanHUD`) | 3 columns of parchment tiles. Each tile has blue charge dots along the top (an empty socket means not charged yet), red dots for temporary charges, an icon or a monogram placeholder, the name, and a progress bar along the bottom: amber while charging, grey while paused. Idle tiles are dimmed and marked "off". A green lamp shows charging. The selected tile gets a gold border. Other states: `?` (undiscovered), blank padlock (locked), grey "no effect yet" (not implemented), and a red border for temporary-only spells. The footer shows mana per second, how many spells are charging and how much of the generation goes to charging. Hovering a tile in RTS mode shows a tooltip with charges, cost per charge, mana per second and the time to the next charge. The "+17 not implemented" footer is gone: future spells without an effect are simply not listed. |
| Controls (RTS) | LMB on a tile selects the spell; it never casts. RMB toggles charging for eligible permanent spells. Both swallow the click, so it never reaches world selection or move/guard commands. Shaman mode is unchanged: wheel or Q selects, LMB casts TAKA. The panel is the same canvas HUD in both modes, so it can't duplicate. |

## 3. Charging and the shared mana pool

All of this runs inside `USpellComponent::AdvanceChargeEconomy`, called from the component tick in fixed `ChargeStepSeconds` steps (0.1 s by default) with a time accumulator. Results depend only on elapsed game time, never on frame rate. The UI never deducts mana.

Each step:

1. **Generate mana.** `G = GetManaRegen() × step`, the existing follower-driven formula, unchanged. `Mana = min(MaxMana, Mana + G)`.
2. **Collect the charging spells.** A spell takes part when it is permanent, charging is on, it can charge (`MaxCharges > 0`, `ManaCost > 0`, not a super spell), its effect is implemented, and it is below its maximum. They are gathered in spellbook order. Each spell's need is `(Max − Stored) × ManaCost − Progress`.
3. **Set the budget.** `Budget = min(G, Mana)`. Charging can use only this step's generation, so there is no progress without generation and no mana from nothing.
4. **Split the budget fairly.** Each spell gets an equal share. A spell that needs less than its share takes only what it needs, and the rest is shared among the others. The order is deterministic: smallest need first, ties in spellbook order. So N active spells each charge at about 1/N of the rate, and a charge takes `ManaCost / share` seconds. A 10-mana spell therefore takes twice as long as a 5-mana spell.
5. **Bank the charges.** Progress turns into charges. A full spell stops drawing and its progress is cleared. The total given is subtracted from `Mana`. Whatever no spell takes stays in the pool.

**Casting** (charge economy):

- The checks are cooldown, then a charge (temporary or permanent), then range.
- A successful cast uses exactly one charge: temporary charges first, then permanent ones. It spends no mana.
- A failed cast uses nothing.
- When a temporary-only spell's last charge is used, the spell leaves the spellbook.
- The TAKA projectile path, damage, homing, cooldown and target rules are unchanged.

## 4. Cheats and test fixtures (console, works in PIE)

| Command | Effect |
|---|---|
| **`ShamanUnlockAllSpells`** | **The cheat you asked for.** Learns every spell that can be held permanently (every row with `MaxCharges > 0`) and fills all of their charges. It doesn't change charging settings or the selection. Spells with no effect yet show as "no effect yet" and still can't be cast. Today only Blast and Convert have effects. |
| `ShamanFillSpellCharges` | Fills every permanent spell to its maximum. |
| `ShamanGrantTempCharges <Spell> <N>` | Temporary charges, through the same path a Stone Head uses. |
| `ShamanSpellOnMap <Spell>` | Marks a spell as discoverable here, so it shows as `?`. |
| `ShamanToggleCharging <Spell>` | Toggles charging. Useful in Shaman mode, which has no cursor. |
| `ShamanSpellDump` | Logs the whole spellbook: charges, progress, charging, mana and map spells. |
| `ShamanLearnSpell <Spell>` | Existing command. It now learns permanently, with 0 charges and charging off. |

## 5. Tests

**New file** `Private/Tests/ShamanSpellChargeTests.cpp`, 10 tests. The numbers in the bare tests (4 mana/s, 0.25 s steps) were checked against a separate Python model of the algorithm.

| Test | Covers (brief §10) |
|---|---|
| `Shaman.Spells.Charges.InitialLevelState` (production Shaman) | 1, plus the unlock cheat, plus AI Shamans staying on mana-at-cast, plus TAKA row values. |
| `…ToggleCharging` | 2, 3 |
| `…ProgressAndPersistence` | 4, 8, 11, plus zero generation giving zero progress. |
| `…SharedManaPool` | 5, 6, plus a full spell handing its share to the others, conservation of mana, and identical results for frame slices of 1/16, 1/4, 1/2 and 5 s. |
| `…MaxCharges` | 7 (limits of 1/2/3/4, and a row value of 9 clamped to 4). |
| `…CastConsumesCharge` | 9, 10 (cooldown, no charge, out of range, unknown spell), plus recharge after spending. |
| `…DiscoveryAndLearning` | 12, 13 |
| `…TemporaryCharges` | 14, 15, 16 |
| `…MonumentRewards` (real `AMonumentActor::Worship`) | 12 and 14 through the real Vault of Knowledge and Stone Head code. |
| `…ModeSwitchKeepsSpellbook` (real controller) | 17, plus selection following the spell when a temporary spell runs out. |

**Changed existing test:** `Shaman.UI.SpellPanel`. Its assertion that Convert is `?` encoded the old rule ("any implemented spell"), which the brief replaces. The test now marks Convert as offered by the map first, so the original assertion stays word for word. It also gains a check that a spell not offered by the map is never `?`. No assertion was removed or loosened.

**18 (existing TAKA, tribe and task tests):** none edited. The legacy mana-at-cast path is unchanged for components with the charge economy off, which is what every Convert and Rule test builds.

**Three tests need the reimport.** InitialLevelState, MonumentRewards and ModeSwitch use the production table. Until you reimport the CSV they fail with the message *"reimport Content/Data/Spells.csv"*. That failure is intended: it is the check that guards the data.

## 6. Known limitations and missing integrations

- **You must reimport `Spells.csv`.** The patch edits the CSV only. I can't produce a `.uasset`, so `Content/Data/Spells.uasset` has to be reimported in the editor. Until then the game stays on mana-at-cast, with a HUD and log warning.
- **Monument worship isn't wired to gameplay yet.** Nothing in the current game calls `AMonumentActor::Worship`: there is no Shaman or Brave worship action and monuments aren't interactable. The reward side is real: the Vault teaches permanently, the Stone Head grants temporary charges, and map discovery works. That is tested by calling `Worship` directly. In PIE, use `ShamanGrantTempCharges` and `ShamanSpellOnMap`. **Monument integration is not complete.**
- **No casting from RTS mode** (Stage C). The panel says so: select in RTS mode, then press Enter and LMB to cast.
- **No right-click on the panel in Shaman mode**, because there is no cursor there. Use `ShamanToggleCharging`.
- **AI Shamans keep mana-at-cast.** Moving them onto charges would also need AI changes.
- **No art.** Tiles use monogram placeholders; set `Icon` in the table to use textures.
- **Stored mana doesn't speed up charging.** Only each step's generation feeds charging, as the brief requires ("no generation, no progress"). Mana that builds up while nothing charges stays in the pool, where nothing currently spends it.
- **Charge state isn't saved across maps.** `UProgressionComponent` saves only permanent spell ids, as before.
- **The "no duplicate panel" check is weak in automation.** It counts `AHUD` actors in a test world without a viewport. The real check is PIE step 10 below.

## 7. Apply, build, test

From the project folder:

```
git apply --check "Claude outputs/shaman_spellpanel_charges.patch"
git apply "Claude outputs/shaman_spellpanel_charges.patch"
```

1. Open the editor (it rebuilds the module), or close it and build `ShamanEditor` Development Win64.
2. **Reimport the spell data.** In the Content Browser, go to `Content/Data`, right-click **Spells**, choose **Reimport** and save. If it asks for a file, use `Content/Data/Spells.csv`. Check that the Blast row shows `MaxCharges 4` and `bChargingByDefault` ticked.
3. Session Frontend > Automation, filter `Shaman`. Run everything and expect 62 tests. Send me `Saved/Logs/Shaman.log` and the results.

To undo: `git apply -R "Claude outputs/shaman_spellpanel_charges.patch"`, then reimport the old CSV.

## 8. PIE test plan (`ShamanPrototype`)

1. Play and click into the viewport.
   - **Expect:** the SPELLS panel on the right shows Blast with 4 empty blue sockets, a green lamp and an amber bar filling. Convert shows as a padlock tile.
   - **Expect:** the footer reads *Mana +x/s · 1 charging*.
2. **Charging Blast alone.** Run `ShamanSpellDump` and time the first Blast charge. It should be about 5 / regen seconds, so roughly 2.9 s with 3 followers.
3. **Toggle Blast.** RMB on Blast: the tile dims, "off" appears, the bar turns grey and stops, and the mana bar starts rising. RMB again: it resumes from the same point. You should see `[SpellPanel] Blast charging ON/OFF`, and no units should move or get selected.
4. **Second spell.** Run `ShamanLearnSpell Convert`. Convert gets a real tile with 0 charges, charging off, and Blast's charges unchanged. RMB on Convert: the footer shows 2 charging and both bars fill at about half speed. Both have the same cost (5), so they fill together.
5. **Different costs.** Run `ShamanUnlockAllSpells`; everything fills up. Then spend some charges, or compare with the tooltips: hovering shows *mana/s* and *next charge in …s*.
6. **Spend and recharge.** Press Enter, then LMB fires TAKA. One blue dot empties and the amber bar refills it. Firing with no charge ready shows *"No charge ready…"* and uses nothing.
7. **Fill to max.** Leave Blast charging. At 4/4 the bar disappears, the lamp goes off and the mana bar resumes rising.
8. **Undiscovered.** Run `ShamanSpellOnMap Swarm`. A `?` tile appears with no name; the tooltip says *Undiscovered spell*, and LMB/RMB do nothing except log.
9. **Temporary charge.** Run `ShamanGrantTempCharges Blast 2`: 2 red dots appear next to the blue ones. Cast: the red dots go first and the blue dots stay. Then run `ShamanGrantTempCharges Convert 1` on a fresh level (before learning Convert): a red-bordered "temporary" tile appears, and after one cast it returns to a padlock.
10. **Mode switching.** Press Enter several times. There is exactly one panel, and the selection, charges, progress and charging are unchanged. The cursor shows only in RTS mode, and clicks on the panel never select units.
11. **TAKA.** In Shaman mode, LMB still fires the homing fire blast with the same damage and knockback.
12. Send `Saved/Logs/Shaman.log` (search for `[Spells]` and `[SpellPanel]`).

## Changed files

- `Content/Data/Spells.csv` (3 new columns)
- `Source/Shaman/Public/SpellRow.h`
- `Source/Shaman/Public/SpellComponent.h`, `Private/SpellComponent.cpp`
- `Source/Shaman/Public/Game/ShamanGameData.h`
- `Source/Shaman/Public/MonumentActor.h`, `Private/MonumentActor.cpp`
- `Source/Shaman/Public/Characters/ShamanCharacter.h`, `Private/Characters/ShamanCharacter.cpp`
- `Source/Shaman/Public/Player/ShamanPlayerController.h`, `Private/Player/ShamanPlayerController.cpp`
- `Source/Shaman/Public/UI/ShamanSpellPanel.h`, `Private/UI/ShamanSpellPanel.cpp`
- `Source/Shaman/Public/UI/ShamanHUD.h`, `Private/UI/ShamanHUD.cpp`
- `Source/Shaman/Private/Tests/ShamanControlTests.cpp` (`Shaman.UI.SpellPanel`, see section 5)
- `Source/Shaman/Private/Tests/ShamanSpellChargeTests.cpp` (new)
