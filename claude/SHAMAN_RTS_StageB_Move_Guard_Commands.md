# SHAMAN RTS: right-click Move and Guard-Shaman commands

Patch: `Claude outputs/rts_stageB_move_guard_commands.patch`. It goes on top of `rts_acceptance_fix_selection_spellpanel.patch`, which is already applied in your tree. It applies cleanly to your current files (checked with `git apply --check`).

UE4.27.2 only. Nothing is committed or pushed. Team colours and `Shaman.uproject` are untouched. TAKA is unchanged.

| Item | Status |
|---|---|
| Build | **NOT VERIFIED** |
| Tests | **NOT VERIFIED.** Expect **52**: the 50 from your last run plus 2 new ones. |
| PIE | **NOT VERIFIED** |

## What your last run showed

Your log of 10/10 01:00 shows:

- **Selection works in PIE.** Box selection and clicks hit your 3 Braves and the Shaman. The spell panel clicks logged correctly.
- **One test failed:** `Shaman.Controls.PlayerTribeLate`, at "Selection still works after switching modes". The bug was in the test, not the game: a plain click on a unit that is already selected deselects it (the Stage A rule). The test now clears the selection first.
- **Right-click did nothing in RTS mode.** That was expected: commands are Stage B, which had not been written. The RTS input layer swallowed the right mouse button, so it reached nothing.
- **"Braves would not guard the Shaman."** I read this as asking for a command. The Braves' Phase 1 default order is FollowShaman, but there was no way to give that order from RTS mode. The R key (rally) and E (interact) belong to the Shaman-mode controls, and the RTS layer blocks them.

  If you meant that Braves don't follow the Shaman in Shaman mode either, that is a separate issue. Run `ShamanSelectionDump` and send the log. It now prints each unit's order, task, state and distance to the Shaman.

## New behaviour (RTS mode only)

| Input | Effect |
|---|---|
| **RMB on ground** with units selected | Each selected Brave gets a `Task.MoveTo` from the tribe's task book (source `PlayerCommand`), executed by the existing Phase 2.2 executor. The Braves spread in a ring formation, closest Braves taking the inner slots. When a Brave arrives, it **holds** the spot (`HoldPosition`) and defends itself there instead of running back to the Shaman. A new command cancels the previous task (`Cancelled / PlayerCommand`); tasks never stack. If a spot can't be reached, the task fails as in Phase 2.2 and the Brave still walks as close as it can and holds. |
| **RMB on ground** with the Shaman selected | The Shaman walks there: a navmesh path on flat maps, the surface path on planets. If no path is found, he walks in a straight line. The walk stops when he arrives, when he is stuck for 4 s, when he dies, or when you press Enter (WASD takes over). |
| **RMB on your Shaman** with Braves selected | The Braves **guard** him: their tasks are cancelled and they get the existing `FollowShaman` order. That means following within 350 uu and fighting enemies inside their leash around him. |
| RMB with nothing selected, over the spell panel, or with no ground under the cursor | Nothing happens. A log line says why. |

The HUD shows a shrinking ring where each order went: green for a move, gold around the Shaman for a guard. The help line now lists RMB.

Log lines:

- `[Command] move: N Brave(s) + Shaman to (...)`
- `[Command] guard Shaman: N Brave(s)`
- `[Command] Shaman walking to ...` and `[Command] Shaman move ended: ...`
- The existing Phase 2.2 `[Task] ...` lines for each Brave.

**Not included yet:**

- Attack commands. Right-clicking an enemy is treated as a move to that spot; the Braves then fight it through their normal defence.
- Queued (Shift) orders.
- Path or destination preview.
- Casting from RTS mode (Stage C).

## Changed files

- `Source/Shaman/Private/Player/ShamanPlayerController.cpp`: RMB binding, the commands, the Shaman walk, and the per-unit dump.
- `Source/Shaman/Public/Player/ShamanPlayerController.h`
- `Source/Shaman/Private/Player/ShamanControlTypes.cpp` and `Public/Player/ShamanControlTypes.h`: `FShamanCommandRules::FormationOffsets`, plus the formation spacing and marker colour settings.
- `Source/Shaman/Private/UI/ShamanHUD.cpp` and `Public/UI/ShamanHUD.h`: the command marker and the help text.
- `Source/Shaman/Private/Tests/ShamanControlTests.cpp`: the test fix and 2 new tests.

## Tests (expected total: 52)

- `Shaman.Controls.PlayerTribeLate`: fixed.
- `Shaman.Controls.FormationRules` (new) checks that:
  - each unit gets one slot, and a single unit goes to the clicked point;
  - slots are at least the spacing apart;
  - the result is the same every time.
- `Shaman.Controls.Commands` (new) checks that:
  - a move gives 3 Braves a held `PlayerCommand` `Task.MoveTo` each, with separate spots and a `HoldPosition` order afterwards;
  - the enemy Brave and the Wildman are untouched, and the selected Shaman walks;
  - a second move cancels the first tasks, leaving exactly 3 open;
  - a guard sets every Brave to FollowShaman, ends their tasks and frees all workers;
  - pressing Enter cancels the Shaman's walk.

## Apply, build and test

```
git apply --check "Claude outputs/rts_stageB_move_guard_commands.patch"
git apply "Claude outputs/rts_stageB_move_guard_commands.patch"
```

Then:

1. Rebuild `ShamanEditor`.
2. Run the Automation tests under `Shaman`. You should see 52, all expected to pass.

## PIE steps (`ShamanPrototype`)

1. Play, then click once in the viewport to focus it.
2. Drag a box around your 3 Braves (not the Shaman).
3. Right-click open ground about 15 m away. A green ring appears, and the Braves walk there in formation and stop. In the log: `[Command] move: 3 Brave(s)`, then `[Task] ... started task #N MoveTo`, then `completed`.
4. Wait about 10 s. The Braves stay at the spot; they don't run back.
5. Right-click a new spot. The Braves go to the new spot, and the log shows the old tasks `Cancelled from outside (PlayerCommand)`.
6. With the Braves still selected, right-click your Shaman. A gold ring appears around him, they run back to him, and the log shows `[Command] guard Shaman: 3 Brave(s)`.
7. Click the Shaman alone and right-click ground. He walks there, the guarding Braves follow, and the log ends with `Shaman move ended: arrived`.
8. Right-click ground for the Shaman again, then press Enter mid-walk. The walk stops (`player took direct control`) and WASD works. Press Enter again to return to RTS mode, where right-click still works.
9. Send `Saved/Logs/Shaman.log` whatever happens.
