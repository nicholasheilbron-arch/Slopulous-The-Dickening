# SHAMAN RTS: Guard Shaman (G), guard flags, patrols and guarding buildings

**Patch:** `Claude outputs/rts_guard_patrol_flags.patch`

- It is based on `feature/rts-stage-a` at `2061fec` ("feat: Populous tribe mana economy"). This is your current HEAD, and every file it touches matches HEAD on your computer.
- It applies cleanly (checked with `git apply --check`).
- UE4.27.2 only. Nothing is committed or pushed.
- TAKA, spells, mana, the task book and the Phase 2.2 executor are unchanged.

| Item | Status |
|---|---|
| Build | **NOT VERIFIED.** A separate code review found no compile problems; its two header fixes are already in the patch. |
| Tests | **NOT VERIFIED.** Expect **126**: your last run had 124 (121 passed plus the 3 charge-column tests fixed by the reimport), and this patch adds 2. |
| PIE | **NOT VERIFIED** |

## Controls (RTS mode)

| Input | Effect |
|---|---|
| Select followers, then **G** (or RMB on your Shaman) | **Guard Shaman.** Every selected unit except the Shaman runs to her, spreads around a ring and walks circles around her. The ring follows her wherever she goes. Guards attack enemies that come within about 10 m of her, then return to the ring. If she dies, they wait at the Reincarnation Circle and resume when she is reborn. |
| **P** | Plant a **guard flag** where the mouse points. It is a thin, tall pole about 4.2 m high, with a banner in your tribe's colour (blue by default). Flags are free; you can have up to 32. |
| **Delete** while pointing at one of your flags | Remove it. Units that were guarding it keep guarding the spot where it stood. |
| RMB on a **flag** | The selected Braves go to it, walk circles around it and defend it. |
| RMB on one of **your buildings** (house, Reincarnation Circle, ...) | The same, with the ring just outside the building's footprint. |
| Hold **Ctrl** and RMB flags one after another | **Patrol loop.** Each Brave starts at the point nearest to it and walks the loop endlessly. The route is finished when you release Ctrl. |
| Hold **Ctrl+Alt** and RMB up to 8 ground points | **Patrol without flags.** You can mix in flags too. |
| Hold **Ctrl+Alt** and double right-click a spot | **Spot guard:** circle that patch of ground. |
| Plain RMB on ground | Move and hold, as before. Any new order replaces a guard or patrol. |

The HUD changes in RTS mode:

- A second help line lists the new keys.
- While you build a route, "Patrol route: N / 8 points" shows.
- For the selected units, their guard ring (gold) or patrol loop (cyan lines and point markers) is drawn on the ground.

Log lines:

- `[Command] guard Shaman: N Brave(s) circling her`
- `[Command] guard <actor>: N`
- `[Command] guard spot ...`
- `[Command] patrol: N Brave(s), P point(s)`
- `[Command] patrol route finished`
- `[Flag] placed ...` and `[Flag] removed ...`

`ShamanSelectionDump` now prints the new order names.

## How it works (existing systems)

- **Orders, not tasks.** The new orders are added to `EFollowerOrder`: `GuardShaman`, `GuardTarget`, `GuardSpot` and `Patrol`. They are appended, so existing values don't change. They are standing orders like FollowShaman and HoldPosition, run by `AShamanUnitAIController::FollowOrders`. They are not tasks, so they create no task-book entries.
- **Mana.** Guarding units count as Guarding and patrolling units as Moving, which gives the "moving" mana rate.
- **Ring walk** (`FShamanGuardRules`, a new pure helper):
  - The ring is about 2.8 m around the Shaman, 2.2 m around a flag, 1.8 m around a spot, and the building's footprint plus 1.5 m for buildings.
  - It widens for big groups so guards stay about 1.1 m apart.
  - A guard off the ring heads for its own evenly spaced slot. Slots are assigned by where each unit already stands, so nobody crosses the ring.
  - On the ring, a guard aims about 1.6 m further round (counter-clockwise), so it keeps walking instead of stopping.
  - This works on flat maps and planets (ground offsets from `FShamanSpace`).
- **Defending.** Guards and patrols use the normal Phase 1 combat, limited to 10 m around what they protect (the unit's own leash still applies if it is smaller). The centre is checked live, so a guard chasing an enemy measures from where the Shaman is now.
- **The flag** (`AShamanGuardFlag`, new) is built from engine basic shapes, with no new assets. It is clickable but never blocks units, spells, the camera or navigation. Enemies ignore it: it is not a building and can't be damaged. There is no fog of war yet, so "invisible to enemies" has no visual meaning so far.

**Limits in this pass:**

- No "N" break-patrol command and no buildings panel. Flags are placed with P only.
- A patrol that includes a flag keeps its point even after the flag is removed.
- The enemy AI does not use guards or patrols.

## Changed and new files

- `Public/ShamanTypes.h`: the new `EFollowerOrder` values.
- `Public/Characters/ShamanUnitBase.h` and `Private/Characters/ShamanUnitBase.cpp`: guard and patrol order data, `SetGuardOrder`, `SetPatrolOrder`, `AdvancePatrol` and `ResolveGuardCentre`. `SetOrder` now clears guard and patrol data.
- `Public/Characters/ShamanUnitAIController.h` and `Private/Characters/ShamanUnitAIController.cpp`: the ring walk, patrols, the guard leash, a live leash anchor, and a repath distance for `MoveToward`.
- `Public/Characters/ShamanGuardRules.h` and `Private/Characters/ShamanGuardRules.cpp` (new): the ring, slot, patrol and double-click rules.
- `Public/World/ShamanGuardFlag.h` and `Private/World/ShamanGuardFlag.cpp` (new): the flag actor.
- `Public/Player/ShamanPlayerController.h` and `Private/Player/ShamanPlayerController.cpp`:
  - the G, P and Delete keys and the Ctrl / Ctrl+Alt RMB routing;
  - guard Shaman, guard target, guard spot and patrol commands;
  - the route builder, double-click handling, and flag placement, removal and picking;
  - picking your own buildings.
- `Public/Player/ShamanControlTypes.h`: guard radii, spacing, flag pick tolerance and the patrol colour.
- `Public/UI/ShamanHUD.h` and `Private/UI/ShamanHUD.cpp`: the help line, the route status and the guard ring / patrol loop overlay.
- `Private/Tests/ShamanControlTests.cpp`:
  - `Shaman.Controls.Commands`: guard now checks GuardShaman slots instead of FollowShaman.
  - `Shaman.Controls.GuardRules` (new).
  - `Shaman.Controls.GuardFlagsPatrol` (new).

## Apply, build and test

```
git apply --check "Claude outputs/rts_guard_patrol_flags.patch"
git apply "Claude outputs/rts_guard_patrol_flags.patch"
```

Then:

1. Rebuild `ShamanEditor`. There are new .cpp files, so let UBT regenerate; if the editor complains, run "Refresh Visual Studio Project".
2. Run the Automation tests under `Shaman`. You should see 126.

## PIE steps (`ShamanPrototype`)

1. **Guard the Shaman.** Box-select your 3 Braves and press G. They run to the Shaman, spread around her and keep walking circles. Press Enter, walk her around with WASD, and the ring follows. Press Enter again to return to RTS mode.
2. **Plant flags.** Point at open ground and press P: a tall blue flag appears. Plant two more somewhere else.
3. **Guard a flag.** Select the Braves and right-click a flag. They circle it, and a gold ring shows while they're selected.
4. **Remove a flag.** Point at the guarded flag and press Delete. The flag goes, and the Braves keep circling that spot.
5. **Flag patrol.** Select the Braves, hold Ctrl and right-click flag 1, then flag 2. Release Ctrl. They walk the loop, and cyan lines show the route.
6. **Ground patrol.** Hold Ctrl+Alt and right-click 3 or 4 ground points, then release. They walk that loop.
7. **Spot guard.** Hold Ctrl+Alt and double right-click one spot. They circle it.
8. **Guard a building.** Right-click one of your huts or the Reincarnation Circle. They circle outside it.
9. **Defend.** If an enemy Brave walks up to a guarded Shaman or flag, the guards attack it and then return to the ring.
10. **Plain move.** Right-click plain ground. The move replaces the guard or patrol as before.
11. Send `Saved/Logs/Shaman.log`.
