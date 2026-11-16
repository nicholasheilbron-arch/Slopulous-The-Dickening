# SHAMAN Slice 3, milestone 3.1: gathering and the tribe stockpile

**Patch:** `Claude outputs/slice3_m3_1_gathering_stockpile.patch` (24 files, 3 of them new). UE4.27.2 only. Nothing is committed or pushed.

**Plan for all of Slice 3:** Claude Doc "SHAMAN Slice 3 Plan — Civilization Building (UE4.27.2)". It covers 8 milestones, costs, the happiness formula and the open questions.

| Item | Status |
|---|---|
| Baseline | `feature/rts-stage-a` at `2061fec` plus your uncommitted guard/patrol flags patch and the mana rounding fix. I rebuilt that tree from your disk and checked the patch against it with `git apply --check`: **PASS**. |
| Build | **NOT VERIFIED.** UBT and MSVC can't run here. The pure economy rules were compiled and run off-engine with g++ against stub headers. That checks those rules only. |
| Tests | **NOT VERIFIED.** The patch adds 9 tests. Your last expected total was 126, so expect **135**. `Shaman.Economy.ProductionData` fails until the two CSVs are reimported (step 3 below). |
| PIE | **NOT VERIFIED.** See the in-editor check below. |

## What it does

- **Every tribe has a stockpile** of wood, stone, ore and food. It lives on `UTribeComponent`, the same owner as the roster and the task book.
  - Spending is all or nothing (`CanAfford` / `SpendResources`). Construction and training use it from 3.2.
  - Starting amounts come from `DA_ShamanGameData > Economy > StartingStockpile`. The default is 0 everywhere.
- **Braves gather.** In RTS mode, select Braves and right-click a tree, stone outcrop, ore deposit or berry bush. Each Brave gets a `Task.Gather.*` task and loops:
  1. Walk to the node.
  2. Work it for `HarvestSeconds`.
  3. Carry 1 bundle to the nearest Campfire.
  4. Deposit it, then repeat.
- **Full or empty nodes.**
  - A node holds `MaxGatherers` Braves. Extra Braves take the nearest other node of the same kind within `NodeSearchRadius` (3,000 uu) of the order.
  - When nothing is left in range, the order completes and the Brave stays by the last node.
- **Only Braves gather** (as in Populous). Warriors and the Shaman are skipped.
- **Order priorities.**
  - A new order replaces the gather task, but a bundle in hand is kept. The next gather order delivers it first.
  - A Brave that dies loses its bundle and frees its slot.
- **Regrowth and depletion.**
  - Trees regrow 1 bundle every 90 s, up to 4. Berry bushes regrow 1 every 60 s, up to 6.
  - Stone (12) and ore (8) do not regrow. They disappear once used up.
  - Nodes shrink as they empty.
- **Mana.** A Brave on a gather order counts as *working* the whole trip: 34.6 mana/min, the existing Populous rate. Before, a walking Brave counted as *moving* at 11.3 mana/min.
- **HUD.** A new line `Stockpile: Wood N  Stone N  Ore N  Food N` sits under the follower count. The RTS help line mentions gathering. The F3 labels show `exec Gather wood #id: harvesting, 2 trip(s)`.
- **Console.**
  - `ShamanEconomyReport` shows each tribe's stockpile, its gatherers, and the nodes and bundles left in the world.
  - `ShamanGiveResources <wood> <stone> <ore> <food>` adds resources for testing.

TAKA, spells, charges, the guard and patrol orders, and the move command are unchanged. Spell balance is unchanged.

## Data changes (reimport needed)

| File | Change |
|---|---|
| `Content/Data/Resources.csv` | New columns: `BundlesPerTrip`, `HarvestSeconds`, `MaxGatherers`, `RegrowSeconds`, `RegrowAmount`, `bDestroyWhenDepleted`. Amounts become bundles: Tree 4, Berry Bush 6, Stone 12, Ore 8. |
| `Content/Data/Buildings.csv` | New column `bIsResourceDropOff` (`true` for the Campfire). |
| `Config/DefaultGameplayTags.ini` | Comments only. The `Task.Gather.*` tags already existed. |

Until you reimport:

- The game still works. Trees keep 100 bundles and never regrow.
- With no drop-off column, Braves deliver to the settlement anchor, which is the Campfire anyway.
- Only `Shaman.Economy.ProductionData` fails.

## How to apply and check

1. **Apply the patch.** Close Unreal, choosing Don't Save if it asks about ShamanPrototype. In the project folder run:
   - `git apply --check "Claude outputs\slice3_m3_1_gathering_stockpile.patch"`
   - The same command without `--check`.
   - `git status --short`. Expect 21 modified files and 3 new ones (`TribeEconomy.h`, `TribeEconomy.cpp`, `ShamanEconomyTests.cpp`).
2. **Build.** Open `Shaman.uproject` and rebuild. If UBT does not pick up the new files, regenerate the project files first.
3. **Reimport the CSVs.** In the Content Browser, right-click `Content/Data/Resources` and choose Reimport, then do the same for `Content/Data/Buildings`. Save both. These are the same steps as the earlier Spells reimport.
4. **Run the `Shaman` automation tests.** Expect 135 of 135 with no regressions. The new tests are under `Shaman.Economy.*`.
5. **In-editor check.** Use `ShamanPrototype` with GameMode Override set to None. Press Play, then Enter for RTS mode.
   - **Wood.** Select the 3 Braves and right-click a tree near the settlement.
     - Two Braves work that tree and the third goes to a neighbouring tree.
     - Each walks to the Campfire with a bundle, and the Stockpile line counts up.
     - Gate: 10 wood banked by 3 Braves.
   - **Stone.** Right-click a stone outcrop and check that stone goes up. With `ShamanEconomyReport`, check that the outcrop's bundles go down.
   - **Order change.** Give a carrying Brave a plain move order, then a new gather order. It delivers its old bundle first.
   - **Death.** Kill a gathering Brave with Blast. Its slot frees and nothing is delivered for it.
   - **Regrowth.** Empty a tree, then wait 90 s. It grows back 1 bundle, and its mesh grows a step.
   - **Planet.** On `PlanetTest`, the Brave there can gather if a node is near. Planet maps have few nodes, so this is optional.
6. **Commit** only after all of this passes, for example `feat: Slice 3.1 gathering and tribe stockpile`. Do not push unless you mean to.

## Files

| File | Change |
|---|---|
| `Public/Tribes/TribeEconomy.h`, `Private/Tribes/TribeEconomy.cpp` | **New.** `FResourceCost`, `FTribeStockpile`, `FResourceStock`, `FGatherRules`, `FTribeEconomyConfig` (pure rules) |
| `Public/TribeComponent.h`, `Private/TribeComponent.cpp` | Stockpile, `AddResource`, `CanAfford`, `SpendResources`, `OnStockpileChanged`, starting stockpile |
| `Public/TribeMemberComponent.h` | Carried bundle (type and amount) |
| `Public/Tribes/TribeTaskExecution.h`, `.cpp` | `Gather` kind, `EGatherPhase`, gather loop, next-node search, drop-off choice, slot release on any task end |
| `Public/World/ResourceNode.h`, `.cpp`, `Public/World/ResourceRow.h` | Gatherer slots, harvest, regrowth timer, removal when used up, shrinking mesh, new row columns |
| `Public/Buildings/BuildingRow.h`, `BuildingActor.h` | `bIsResourceDropOff` |
| `Public/Game/ShamanGameData.h` | `Economy` block |
| `Public/Player/ShamanPlayerController.h`, `.cpp` | RMB on a resource node gives a gather order (`CommandGather`, `PickResourceNode`) |
| `Private/UI/ShamanHUD.cpp` | Stockpile line, help text |
| `Public/Game/ShamanGameMode.h`, `.cpp` | `ShamanEconomyReport`, `ShamanGiveResources` |
| `Private/Tests/ShamanEconomyTests.cpp` | **New.** 9 tests: Stockpile, ResourceStock, GatherRules, GatherLoop, NextNode, CarrierDeath, CarryHomeFirst, NodeDepletion, ProductionData |
| `Private/Tests/ShamanTaskExecutionTests.cpp` | One assertion updated: `Task.Gather.Wood` is now executable (`Task.Build` is still reserved) |

## Known limits

- **Movement isn't tested in automation.** The automation tests teleport units, so real navmesh and planet movement to nodes is only checked in PIE.
- **Gatherers don't fight back.** A gathering Brave ignores enemies until the order ends. This is the same as any running task today.
- **Tribes only gather on order.** The enemy tribe and idle Braves never gather by themselves. Enemy economy is Phase 4, and automatic jobs come with construction in 3.2.
- **Next-node search scans the world's resource nodes.** It only runs when a node runs out or fills up, never per frame.
- **Food isn't consumed yet.** It is only stored. Eating and happiness are milestone 3.4.

## Next milestone

**3.2 Construction:** blueprint placement inside the build zone, Braves carrying materials from the stockpile, work units, and damage and repair. Before it starts, please answer the open questions in the plan doc. The first one is whether construction draws from the stockpile or Braves deliver straight to the blueprint.
