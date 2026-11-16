# SHAMAN: spell panel, spell charges and Populous mana handoff

Written 2026-10-30. This is the state of the spell panel slice and the mana-economy pass that followed it. It covers what exists, how it works, the verified status, the known issues and what to do next.

## Where things stand

| | |
|---|---|
| **Branch** | `feature/rts-stage-a` on GitHub (`nicholasheilbron-arch/Slopulous-The-Dickening`). `main` (`54cac5e`) is untouched. |
| **Commits** | `ddbebcf` RTS Stage A/B, selection fix, team colours → `e51d047` spell panel slice → `3ef9f01` compile fix (C4458) → `7b2a938` Spells.uasset reimport (charge columns) → `2061fec` Populous mana economy |
| **Engine** | UE 4.27.2, launcher install at `C:\Program Files\Epic Games\UE_4.27` |
| **Detailed reports** | `Handoffs\SHAMAN_SpellPanel_Charges.md` (spell panel slice), `Handoffs\SHAMAN_Mana_Economy.md` (mana pass) |
| **Separate later work** | `Handoffs\SHAMAN_RTS_Guard_Patrol_Flags.md` (guard/patrol patch based on `2061fec`, from another session). Its 2 tests appear in the last local run, so it seems to be applied locally but **is not committed**. It isn't covered here. |

## Verified status (from `Saved\Logs\Shaman.log`, run on 2026-10-20 18:52 local)

- **Build:** passes. The first build of the slice failed with `ShamanCharacter.cpp(382): error C4458: declaration of 'Row' hides class member` (a local in the unlock cheat shadowed `AShamanUnitBase::Row`). `3ef9f01` fixed it.
- **Automation:** **66 / 67 passed** on the run after the Spells reimport. That run includes the 2 guard/patrol tests.
  - The run just before it, before the reimport was saved, failed 5. That was expected: the production-table tests stop at "reimport Spells.csv".
  - The one remaining failure is a test tolerance bug, not a game bug. See known issue 1.
- **PIE:** partly exercised on 10/19. The log shows panel right-clicks toggling Convert charging on and a locked tile refusing. The full 12-step PIE plan in `SHAMAN_SpellPanel_Charges.md` §8 has **not** been reported complete.
- **The reimported `Content/Data/Spells.uasset` is NOT on GitHub.** The copy on disk (saved 10/20 18:52) has the `ChargeManaCost` column. The committed copy is still the `7b2a938` one, without it. Commit and push it, or anyone cloning the branch will be on mana-at-cast.

## How the pieces fit

**One source of truth.** `USpellComponent` (`Source/Shaman/Public/SpellComponent.h`, `Private/SpellComponent.cpp`) owns:
- the spellbook (`KnownSpellIds`, per-spell `FSpellChargeState` and temporary charges);
- the mana pool (`Mana`, `MaxMana`);
- cooldowns;
- map discovery (`MapSpellIds`).

Everything else reads it or calls into it: the HUD, the panel model, the controller, monuments and the cheats.

**Who uses the new rules.** `AShamanCharacter::BeginPlay` turns on `bUseChargeEconomy` and `bTribeManaGeneration` for player-controlled tribes only (`UShamanGameData::bPlayerUsesSpellCharges`). This happens only if the spell table has the charge columns (`USpellComponent::TableHasChargeData`: some row with `MaxCharges > 0` and `ChargeManaCost > 0`). Without them the player falls back to Phase 1 mana-at-cast, the HUD says "Spell charges off: reimport Content/Data/Spells.csv", and the log warns. **AI Shamans always stay on mana-at-cast** (`AShamanUnitAIController` checks `Mana >= ManaCost`).

### Mana (Populous rules): `Public/Tribes/TribeMana.h`, `Private/Tribes/TribeMana.cpp`

- **Generation comes only from living tribe members**, per minute, by activity (`FTribeManaRules::Classify` / `RateFor`):

  | Member | Mana / min |
  |---|---|
  | Shaman alive | 66.6 |
  | Brave Working, Building, Gathering or Sleeping (resting) | 34.6 |
  | Any member Moving, Following, Guarding or in Combat | 11.3 |
  | Idle | 9.1 |
  | Dead or Unavailable | 0 |

  Activity is `UTribeMemberComponent::GetSimState()`, set by the AI controller. There is no passive regeneration.
- **Pool:** level start 0 (`StartingMana`), cap 1000 (`PoolCap`). All values live in `UShamanGameData::Mana` (`FTribeManaConfig`); there is no DA asset, so the defaults are used.
- **Each economy step** (`ChargeStepSeconds` = 0.1 s, fixed-step accumulator, so results don't depend on frame rate) does this:
  1. New mana = rate × step.
  2. Split between the spell and training pipelines (`SplitPipelines`): 50/50 when both are active, otherwise 100% to the only active one.
  3. Split the spell share fairly between the charging spells (`FSpellChargeRules::SplitBudget`, "water filling": equal shares, and a spell near full takes only what it needs).
  4. Bank whatever is left in the pool up to the cap, and waste anything past the cap. The HUD flashes red.
- **Banked mana is never drawn back into charging.** Only new mana charges spells; the user chose this.
- **Training hook:** `ActiveTrainingConsumers` (always 0; no training huts exist yet).

### Charges

| Row field | Meaning |
|---|---|
| `MaxCharges` | 1–4 (clamped); 0 = can't be held permanently. |
| `ChargeManaCost` | Mana for one charge, on the Populous scale. |
| `bChargingByDefault` | Blast only. |
| `Icon` | Empty = monogram placeholder. |
| `ManaCost` | Unchanged; still the per-cast price for AI and the fallback. |

Charge costs:

| Spell | Cost per charge | Source |
|---|---|---|
| Blast | 100 | Populous |
| Convert | 100 | Populous |
| Swarm | 400 | Populous |
| Invisibility | 500 | Populous |
| Earthquake | 1750 | Populous |
| Magical Shield | 500 | estimate |
| Land Bridge | 600 | estimate |
| Lightning | 600 | estimate |
| Hypnotize | 700 | estimate |
| Tornado | 800 | estimate |
| Swamp | 900 | estimate |
| Flatten | 1000 | estimate |
| Erode | 1750 | estimate |
| Firestorm | 2500 | estimate |
| Angel of Death | 4000 | estimate |
| Volcano | 5000 | estimate |
| Super spells | 0 | they never charge |

**Casting** checks cooldown, then a charge, then range. One charge is used per successful cast, temporary charges first. No mana is spent at cast time, and failed casts use nothing. When a temporary-only spell's last charge is used, it leaves the spellbook.

**Learning a spell** permanently gives it 0 charges, with charging set to the row default. Existing spells are never touched.

**Map discovery:** a `?` tile appears only for spells this map offers. That means an `AMonumentActor` with an `UnlockSpell` reward (registered in both directions at BeginPlay) or `SetSpellAvailableOnMap`.

**Monuments:** the Vault of Knowledge calls `LearnSpell`, and the Stone Head calls `AddCharges` (temporary charges). **Nothing in gameplay calls `AMonumentActor::Worship` yet.** The reward code is tested directly, but there is no worship action.

### Panel and HUD: `Public/UI/ShamanSpellPanel.h/.cpp`, `Private/UI/ShamanHUD.cpp`

- **Canvas HUD.** No UMG and no per-tile ticks. The model is rebuilt every frame from `USpellComponent`. There is one HUD per controller, so switching modes can't duplicate the panel.
- **Tiles**, 3 columns:

  | State | Shown as |
  |---|---|
  | Charging | Blue charge dots, amber progress bar, green lamp |
  | Idle | Dimmed, grey bar, "off" |
  | Full | Bar empty, lamp off |
  | Temporary | Red dots; red border if temporary-only |
  | Undiscovered | `?` only, no name or icon |
  | Locked | Blank padlock (implemented, chargeable spells you can't get here) |
  | Not implemented | Grey "no effect yet" |
  | Selected | Gold border |

  Future spells with no effect aren't listed.
- **Mana block** (bottom left):
  - bar: `Mana pool X / 1000 +N / min`;
  - flow line: where the mana goes right now, or a red `POOL FULL … wasted`;
  - "Made by" breakdown: Shaman, working, moving and idle counts.
- **Footer and tooltips:** per-minute rates and the time to the next charge.
- **Controls (RTS mode):** LMB on a tile selects it and never casts. RMB toggles charging. Both are swallowed, so they never reach world selection or move/guard commands. Shaman mode is unchanged: wheel or Q selects, LMB casts TAKA. Casting from RTS mode doesn't exist yet (Stage C), and the HUD says so.

### Console commands (on `AShamanCharacter`)

| Command | Effect |
|---|---|
| `ShamanUnlockAllSpells` | The cheat: learn every permanent-capable spell and fill all charges. |
| `ShamanFillSpellCharges` | Fill every permanent spell's charges. |
| `ShamanGrantTempCharges <Spell> <N>` | Stone Head path. |
| `ShamanSpellOnMap <Spell>` | Make a spell show as `?`. |
| `ShamanToggleCharging <Spell>` | Toggle charging (Shaman mode has no cursor). |
| `ShamanSpellDump` | Full spellbook and mana flow to the log. |
| `ShamanLearnSpell <Spell>` | Existing command; now a permanent learn. |

## Tests

`Private/Tests/ShamanSpellChargeTests.cpp` has 13 tests:

- **`Shaman.Spells.Charges.*`:** ToggleCharging, ProgressAndPersistence, SharedManaPool, MaxCharges, CastConsumesCharge, DiscoveryAndLearning, TemporaryCharges, InitialLevelState, MonumentRewards, ModeSwitchKeepsSpellbook.
- **`Shaman.Spells.Mana.*`:** Rules, TribeGeneration, TrainingSplit.

How they're built:
- **Bare tests** use a plain actor, a runtime table, 4 mana/s and 0.25 s steps, so the numbers are exact.
- **Production tests** (InitialLevelState, MonumentRewards, ModeSwitchKeepsSpellbook, TribeGeneration) need the reimported `Spells.uasset` and fail with a clear "reimport" message otherwise.

`Shaman.UI.SpellPanel` (in `ShamanControlTests.cpp`) was changed once, because the brief changed the meaning of `?`. Before testing `?` it now marks Convert as offered by the map, and it also checks that a spell not offered by the map never shows `?`. No other existing test was edited.

## Known issues

1. **`Shaman.Spells.Mana.TribeGeneration` fails one assertion** (`ShamanSpellChargeTests.cpp` line 727): "expected 56.250004, but it was 56.062504 within tolerance 0.1".
   - **Cause:** 30 s / 0.1 s in float runs 299 steps, not 300, and one step at 112.5/min is 0.1875 mana. The game behaves correctly; the tolerance is tighter than one step.
   - **Fix:** use a one-step tolerance on that line and on the two "banked" checks below it, i.e. change `0.1f` to `0.2f` on lines 727, 729 and 736. **Not applied yet.**
2. **The reimported `Spells.uasset` isn't committed** (see above).
3. **No worship action.** Monuments can't be used in play; use the console fixtures.
4. **No training huts,** so spells always get 100% of mana.
5. **Braves earn the "working" rate only while holding a task.** Huts, gathering and resting aren't built, so Braves following the Shaman earn 11.3 each. A Shaman with 3 Braves following makes about 100 mana/min, so one Blast charge (100) takes about 60 s; with working Braves it's about 35 s.
6. **Estimated charge costs** (marked above) aren't confirmed Populous numbers.
7. **Charge state isn't saved across maps.** `UProgressionComponent` saves only permanent spell ids.
8. **No icon art.** Tiles use monogram placeholders.

## Next steps (suggested order)

1. Apply the known issue 1 fix. Rebuild and re-run the tests: expect 67/67 with the guard/patrol patch, or 65/65 without it.
2. Commit and push `Content/Data/Spells.uasset`, plus the test fix.
3. Run the PIE plan in `SHAMAN_SpellPanel_Charges.md` §8 and check the mana flow lines in `SHAMAN_Mana_Economy.md`. Confirm the feel; tune `ChargeManaCost` in `Spells.csv` or `FTribeManaConfig` as needed.
4. Decide whether to commit the guard/patrol patch (see its own handoff).
5. Later milestones that plug into this:
   - **Worship:** the Shaman or Braves calling `AMonumentActor::Worship`.
   - **Training huts:** increment `ActiveTrainingConsumers` and consume the training share.
   - **Huts, work and rest** for Braves (feeds the 34.6 rate).
   - **Casting from RTS mode** (Stage C).
   - **Real icons.**
   - **Moving AI Shamans to charges.**

## Working notes for whoever picks this up

- **Never ask the owner to use Visual Studio.** To rebuild:
  - **Option A:** close the editor, delete `Binaries\Win64\UE4Editor-Shaman.dll`, double-click `Shaman.uproject` and click **Yes** to rebuild.
  - **Option B:** run this from a command prompt:
    ```
    "C:\Program Files\Epic Games\UE_4.27\Engine\Build\BatchFiles\Build.bat" ShamanEditor Win64 Development -Project="C:\Nick\other forms of comp\fun\Shaman\1st Go\Merged 1st steps\Shaman 4.27\Shaman.uproject" -WaitMutex
    ```
  Opening the editor does **not** recompile changed C++ on its own.
- **Build errors** are in `%LOCALAPPDATA%\UnrealBuildTool\Log.txt` (errors are lines containing `error C`). The project builds with `BuildSettingsVersion.V2`, so shadowing (C4458/C4456) is an error.
- **After any change to `FSpellRow` columns,** reimport the **Spells** DataTable: right-click it in `Content/Data`, choose Reimport, then Save All. Then commit the `.uasset`.
- **Tests:** Session Frontend > Automation, filter `Shaman`. Results are in `Saved\Logs\Shaman.log` (`Test Completed. Result=`).
- **Git:** the owner pushes from their PC. A Claude cloud session can't push to this repo (the proxy returns 403) unless the repo is added to the session's sources. Line endings are LF for `.cpp`, `.h`, `.cs`, `.ini`, `.csv`, `.md` and `.uproject` (`.gitattributes`).
