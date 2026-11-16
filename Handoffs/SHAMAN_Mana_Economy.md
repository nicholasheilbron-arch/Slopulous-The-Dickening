# SHAMAN: Populous mana economy

This changes the code that charges spells from mana, on top of `feature/rts-stage-a` (`7b2a938`). UE 4.27.2. Nothing is committed or pushed.

| Item | Status |
|---|---|
| Build | **NOT VERIFIED** |
| Tests | **NOT VERIFIED.** Expect **65**: your 62 plus 3 new ones. |
| PIE | **NOT VERIFIED** |

## What changed

### Mana comes only from your tribe

There is no background regeneration any more. Each living member makes mana every minute, depending on what it is doing. The rates are in `DA_ShamanGameData > Mana`, and the defaults come from Populous:

| Member | Mana / min |
|---|---|
| Shaman (alive) | 66.6 |
| Brave working, or resting in a hut | 34.6 |
| Any follower moving, following, guarding or fighting | 11.3 |
| Idle follower | 9.1 |
| Dead or unavailable | 0 |

### The level starts with an empty pool

The pool starts at 0 mana (`StartingMana`). From the first second, everything the tribe makes goes into charging Blast.

### Where new mana goes

- **Spells and training split it 50/50.** If only one of them is active, it gets 100%.
- **Training doesn't exist yet**, so in practice spells get everything. There is a hook ready for training huts (`ActiveTrainingConsumers`).
- **The spell share is split evenly** between every spell that is charging.
- **Unused mana is banked** in the pool, up to `PoolCap` (1000). This happens when nothing is charging or a spell tops out.
- **Once the pool is full, unused mana is wasted.** The mana bar flashes red and the HUD says "POOL FULL … wasted".
- **Banked mana is never used for charging.** Only newly made mana charges spells.

### Charge costs come from Populous

There is a new column, `ChargeManaCost`, giving the mana needed for one charge. The old `ManaCost` column is still used by AI Shamans, which keep paying mana when they cast.

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
| Super spells | 0 | (they never charge) |

**What that means for Blast:**

- A Shaman with 3 Braves following him makes about 100 mana/min, so one Blast charge takes about 60 s.
- With the Braves working it is about 170/min, so about 35 s.
- With 3 idle Braves it is about 94/min, so about 64 s.

### HUD

- **Mana bar:** shows `Mana pool X / 1000 +N / min`.
- **Flow line under it:** shows where the mana goes right now, e.g. `-> Spells 100.5/min (1 charging) Training 0 Pool +0`. In red: `POOL FULL: … wasted`.
- **Made-by line:** shows who makes it, e.g. `Made by: Shaman 66.6 · 0 working x34.6 · 3 moving x11.3 · 0 idle x9.1`.
- **Spell panel footer and tooltips:** shown per minute, including the time to the next charge.
- **`ShamanSpellDump`:** logs the whole flow.

## Tests (+3)

- `Shaman.Spells.Mana.Rules`: every rate, plus the 50/50 split and its overrides.
- `Shaman.Spells.Mana.TribeGeneration`: uses a real Shaman and Braves.
  - Checks idle, working, following and dead rates.
  - Checks the pool starts at 0 and everything goes into Blast.
  - Checks the first Blast charge arrives at 60 s for 112.5 mana/min.
  - Checks mana is banked when nothing charges, and wasted when the pool is full.
- `Shaman.Spells.Mana.TrainingSplit`: with a training consumer active, spells get half; with no spell charging, training gets all of it.
- `InitialLevelState` also checks the empty starting pool and Blast's cost of 100.

## Steps

1. **Rebuild without Visual Studio.** Close the editor, delete `Binaries\Win64\UE4Editor-Shaman.dll`, double-click `Shaman.uproject` and click Yes to rebuild.
2. **Reimport the Spells DataTable** (right-click > Reimport, then Save All). This adds the `ChargeManaCost` column.
   - **Until you reimport, the player stays on the old mana-at-cast rule**, with the "reimport" warning on the HUD.
   - 4 tests will fail until then: the ones that use the production spell table (`InitialLevelState`, `MonumentRewards`, `ModeSwitchKeepsSpellbook` and `TribeGeneration`).
3. **Run the Automation tests.** Expect 65.
4. **Play.** Watch the flow line. Right-click Blast off and the pool fills. Fill it and it flashes red.

## Limitations

- **No training huts yet.** The 50/50 split is coded and tested, but in the game spells always get 100%.
- **Brave activity comes from the AI.** Braves following you count as "moving" (11.3). Working (34.6) happens only while they hold a task, because huts, gathering and resting don't exist yet.
- **AI Shamans unchanged.** They still regenerate mana the Phase 1 way and pay it when they cast.
- **Estimated costs.** Costs marked "estimate" aren't confirmed Populous values; tune them in `Spells.csv`.
