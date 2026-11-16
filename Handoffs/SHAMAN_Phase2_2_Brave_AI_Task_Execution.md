# SHAMAN Phase 2.2: Brave AI and task execution

Status: **first build failed (one compile error, fixed below). The fix has not been compiled yet.** It still needs your build, your automation run and the in-editor checks below. Nothing is committed.

Patch: `Claude outputs/phase2_2_brave_ai_task_execution.patch` (14 files, 4 of them new).

## Baseline (checked before editing)

| Item | Finding |
|---|---|
| Branch / HEAD | `main` at `c1d7f28` ("fix: close Phase 1 acceptance blockers") |
| Uncommitted | The Phase 2.1 patch (13 modified files, 4 new) plus the team-colours testing patch (4 files) |
| Check | Removing both patches from the working tree gives back the HEAD index exactly. No other edits were found. |
| Patch base | The 2.2 patch applies on top of that working tree. It does not touch the team-colours files, so `git apply -R` on the colours still works on its own. |

The baseline was checked by comparing every file under `Source/` and `Config/` with the blob hashes in `.git/index`, because this session can't run git on your PC.

## Audit: what each existing system already did

| Existing system | Reused as-is | Extended | Reason |
|---|---|---|---|
| `UTribeComponent` (task book, worker pool) | Lifecycle, rules, pool | Relays every state change of a held task to its assignee. `StartTask` now refuses a task whose assignee is no longer a living follower of the tribe holding it. Adds a per-task arrival radius. | It stays the only owner of task state |
| `UTribeMemberComponent` | Identity, availability, one task slot | Native `OnTaskStateChanged` event. `TaskActivity`, so the unit shows Moving or Guarding instead of Working while it executes. | Each unit learns about its own task with no world scan |
| `FTribeTask` / `TribeTaskTypes` | Unchanged lifecycle | `AcceptanceRadius` (0 = config default) | The smallest addition needed |
| `AShamanUnitAIController` | Combat, follow, guard-home, wander, navmesh movement, planet movement | Owns a `FUnitTaskExecutor`. A running task comes before autonomous behaviour. Provides the movement layer for tasks. | Movement stays in one place, with no second movement system |
| Death / transfer (`HandleDeath`, `ChangeTribe` → `UnregisterFollower`) | Yes | None | Both already fail the task (2.1). The executor now reacts to that event. |
| Gameplay Tags | `Task.Guard` (was reserved) | `Task.MoveTo`, `Task.MoveToActor` | Task types stay data-driven |
| `UShamanGameData` | Yes | `TaskExecution` tuning block | Radii and timeouts are data, not magic numbers |

## Architecture

```
UTribeComponent (task book: state, assignee, end reason)
   │ Assign / Start / Complete / Fail / Cancel
   ▼ relays the new state to the assignee only
UTribeMemberComponent::OnTaskStateChanged
   ▼
FUnitTaskExecutor (one per unit, owned by its AI controller)
   ├─ FTribeTaskExecutionRules::Step   pure: arrived / hold / stuck / timed out / target lost
   └─ IUnitTaskMover                   movement layer = AShamanUnitAIController
        flat:   navmesh MoveTo (actor targets: MoveToActor, partial path allowed);
                no usable navmesh → straight line (the Phase 1 convention)
        planet: FShamanSurfaceNavigation; a partial path that stops short → Unreachable
   ▲ reports Complete / Fail back to the task book (it never changes task state directly)
```

- **Assigned:** the unit knows about the task (it is pending and logged). Autonomous behaviour carries on until the task starts.
- **Started (Active):** the executor checks that the task belongs to this unit, this tribe and this state. It stops whatever the unit was doing and issues the first move straight away.
- **Bookkeeping-only types:** `Task.Debug` and the reserved tags are never executed, so Phase 2.1 behaviour is unchanged.
- **Ending:** the executor clears its own state before calling the task book, so the re-entrant end event is harmless. The task book notifies the unit last, after it has finished touching the task map. This prevents double completion and stale references.
- **Who ends a running task:**

| Cause | Result |
|---|---|
| Arrival | Completed |
| No path | Failed: `NoPath` |
| Goal can't be reached | Failed: `Unreachable` |
| No progress for 8 s | Failed: `Stuck` |
| Over 180 s (move tasks only) | Failed: `TimedOut` |
| Target actor destroyed or invalid | Failed: `TargetLost` |
| Task has no usable target | Failed: `InvalidTarget` |
| Unit marked unavailable | Failed: `UnitUnavailable` |
| Controller goes away | Failed: `ControllerLost` |
| Death or tribe transfer | Failed: `MemberLeftTribe` (the 2.1 roster path) |
| Console cancel, fail or complete | Execution stops |

- **Priority:** an executing task comes before autonomous combat, follow, guard-home and wander. A guard on station still melees an enemy within reach, but never leaves its post to chase one. There is no pre-emption between tasks, because the one-task-per-unit rule already holds.
- **Knocked down:** time spent ragdolled pauses the stuck and duration clocks. After recovery the move is re-issued.
- **Cost:** decisions run 4 times per second per executing unit and only look at that unit's own task. Events cover everything else. There are no world scans and no new tick functions.
- **Spherical terrain:** all distances use `FShamanSpace::HorizontalDistance`. Planet moves use the existing surface pathfinder. There are no Z = 0 or flat-XY assumptions.

## Tuning (`UShamanGameData::TaskExecution`)

| Setting | Default | Meaning |
|---|---|---|
| ArrivalRadius | 150 | Task.MoveTo completes within this distance |
| ActorArrivalRadius | 220 | Task.MoveToActor completes within this distance, plus the target's collision radius |
| GuardRadius | 150 | Guard is on station within this distance |
| GuardLeashRadius | 400 | Guard walks back only when pushed further than this |
| RepathDistance | 200 | MoveToActor re-issues the move when the target has moved this far |
| StuckTimeout | 8 s | Fail with Stuck after this long without progress |
| MinProgress | 50 | Distance gained that counts as progress |
| MaxMoveDuration | 180 s | Move tasks fail with TimedOut after this |
| RetryInterval | 1 s | Re-issue a move the movement layer dropped, at most this often |

## Console (`ShamanTaskTest <action>`, both game modes)

| Action | Does |
|---|---|
| `Move` / `Guard` | Create Task.MoveTo / Task.Guard at the aim point |
| `Marker` / `MoveActor` / `DestroyTarget` | Spawn an orange marker cone at the aim point / create Task.MoveToActor to it / destroy it |
| `MoveInvalid` / `MoveUnreachable` | MoveTo with no location / far outside the world |
| `Assign` / `Start` / `Go` | Assign to the available Brave nearest you / start / assign and start |
| `Cancel` / `Fail` / `Complete` / `Info` | End the task from the console / print task, unit and executor state |
| `Create`, `Unavailable`, `Available` | Unchanged from 2.1 |

Every line printed includes the task type, state and end reason, the unit's state, the executor's movement result and the number of available workers. With F3 on, the labels above each Brave add an `exec …` line and draw a line to the task goal.

## Automated tests (expected 44: the existing 37 plus 7 new)

| Test | What it checks |
|---|---|
| `Shaman.Tasks.ExecutionRules` | Pure rules: tag to kind, valid targets, radii, arrival, stuck window, timeout, guard hold / leash / return, target lost |
| `Shaman.Tasks.MoveToLocation` | Assign makes the unit aware but it doesn't move. Start issues exactly one move. No spamming. Another unit's task is ignored. Arrival completes exactly once (counted by event). The executor and the worker are released. |
| `Shaman.Tasks.MoveToActor` | Moves to the actor. Re-issues the move when the target moves far, not when it drifts slightly. Completes in reach. A destroyed target fails with `TargetLost` and stops movement. A missing target fails with `InvalidTarget`. |
| `Shaman.Tasks.MoveFailures` | `NoPath`, `Unreachable`, `Stuck` (the unit is not left busy), `InvalidTarget`, `UnitUnavailable` |
| `Shaman.Tasks.Guard` | Moves to the post, holds it indefinitely, tolerates a nudge inside the leash, walks back beyond it. Cancel stops it and releases the worker. |
| `Shaman.Tasks.EndingAndEligibility` | External fail, complete and cancel release the unit. Death fails the task and updates followers and the worker pool. Transfer fails the old task, updates both pools, and the new tribe's tasks then run. The Shaman is excluded. |
| `Shaman.Tasks.AutonomyYields` | A real `AShamanUnitAIController` possesses a Brave. Follow normally moves it. An active Guard task suppresses follow and stops the move already under way. After cancel, follow resumes and the state shows Following. |

What a real UE4 world exercised here, and what it didn't:

- **Covered:** task books, rosters, units, death and transfer all run on real production unit rows in a test world. The AutonomyYields test uses the real controller.
- **Not covered:** UE4 path following, navmesh queries and planet steering. Execution tests use a recording fake movement layer and teleport the unit to simulate arrival. Real movement is covered only by the in-editor steps below.

## In-editor acceptance (`ShamanPrototype`, GameMode Override = None)

**Setup.** Close Unreal and run, in the project folder:

```
git apply --check "Claude outputs\phase2_2_brave_ai_task_execution.patch"
git apply "Claude outputs\phase2_2_brave_ai_task_execution.patch"
```

Then rebuild, run the `Shaman` tests (expect 44), press Play and press F3.

| # | Steps | Expected |
|---|---|---|
| 1 | `ShamanTribeReport` | Tribe 0: 3 followers, 3 available workers |
| 2 | Aim at open ground about 15 m away. `ShamanTaskTest Move`, then `ShamanTaskTest Go` | The nearest Brave walks there and stops following you. Its label shows `Moving`. Available workers: 2. |
| 3 | Wait for it to arrive | The log shows `completed task #N MoveTo (Arrived)`. `ShamanTaskTest Info` shows Completed and available workers 3. The Brave goes back to following you. |
| 4 | Aim elsewhere. `ShamanTaskTest Marker`, `ShamanTaskTest MoveActor`, `ShamanTaskTest Go` | A Brave walks to the orange cone and completes |
| 5 | `Marker`, `MoveActor`, `Go`, then `DestroyTarget` while the Brave is still walking | The task fails with reason `TargetLost`. The Brave stops and is available again. |
| 6 | Aim at a spot. `ShamanTaskTest Guard`, `ShamanTaskTest Go`, then walk away | The Brave reaches the post, shows `Guarding` / `OnStation` and stays there even when you leave |
| 7 | `ShamanTaskTest Cancel` | Cancelled. The Brave is released and follows you again. Available workers 3. |
| 7b | `MoveInvalid` + `Go`, then `MoveUnreachable` + `Go` | Failed with `InvalidTarget`, then Failed with `Unreachable` (or `Stuck` after about 8 s if the level has no navmesh). The Brave is available again each time. |
| 8 | `Move`, `Go`, then kill that Brave with 2 Blasts. `ShamanTribeReport` | The task is Failed (`MemberLeftTribe`). Followers and available workers both drop by 1. |
| 9 | `Move`, `Go` (nearest Brave), then `ShamanTribeTransfer 1` | The old task is Failed. Tribe 0 drops by 1 follower; tribe 1 gains 1 follower and 1 available worker. |
| 10 | No active task | Follow, idle and combat behaviour are as before |
| 11 | Regression check | TAKA, Convert, player death and rebirth, and enemy Shaman reincarnation and elimination all still work. On `PlanetTest`, `Move` + `Go` also works across the surface. |

Note for step 9: `ShamanTribeTransfer` moves the Brave nearest to you. Stand next to the Brave that holds the task.

## Known limitations

- Tasks start only when something calls `StartTask`. Assigned-but-not-started units keep their autonomous behaviour. There is no scheduler or auto-start yet.
- Priority is stored and shown, but there is still no pre-emption between tasks, and autonomous combat never interrupts a task. A moving Brave ignores enemies, and a guard only fights what is within reach.
- Flat map: with no `NavMeshBoundsVolume`, or while the navmesh is still building, task moves fall back to straight lines, as Phase 1 does. Blocked straight-line moves then fail through the stuck watchdog (8 s) rather than straight away.
- Planet: the prototype great-circle pathfinder doesn't route around obstacles, so goals behind water or a cliff are reported as Unreachable.
- A unit with no AI controller (for example in the 2.1 tests) keeps an executable task Active until someone ends it. In the game, every unit has a controller.
- `UnitUnavailable` now fails an executing task. In 2.1, unavailability did not end a held task, and for bookkeeping-only tasks it still doesn't.
- The missing `/Game/Data/DA_ShamanGameData` warning is unchanged. The new tuning uses class defaults until that asset exists.

## Out of scope (not implemented)

Gathering, resource depletion, carrying and storage, construction, food, water, shelter, population, training, new spells or spell balance, new combat, enemy strategy, RTS command UI and new navigation all remain future milestones.

## Recommended next milestone

**Phase 2.3: resource nodes and gathering.** Add `Task.Gather.*` as executable kinds on the same executor. This is the first case where reaching a target leads into a work phase, which would add a work step to `ETaskStepAction`.

## Build fix 1 (2026-10-09)

**First error** (`Saved/Logs/Shaman.log`, the only compiler error in either build attempt):
`TribeTaskExecution.cpp(401): error C2181: illegal else without matching if`

**Root cause:** in UE4.27, `UE_LOG` expands to a braced `{ ... }` block, not a single statement. In `FUnitTaskExecutor::Finish`, `if (...) UE_LOG(...); else UE_LOG(...);` therefore becomes `if (...) { ... } ; else`. The `;` closes the `if`, which leaves the `else` with no matching `if`. Phase 2.2 introduced this. There are no other errors and no cascade:

- UHT generated the reflection code successfully.
- Every other Shaman file compiled without errors, including all Phase 2.2 files.

**Fix:** braces around both `UE_LOG` branches. Behaviour is unchanged. Nothing else in the project has this pattern.

**Not yet verified:** the Shaman module never got as far as linking, so linker errors and the automation run are still unknown.

**Applying:**
- Your tree already has Phase 2.2 applied: `git apply "Claude outputs\phase2_2_fix1_ue_log_else.patch"`
- From the pre-2.2 baseline: use the regenerated `phase2_2_brave_ai_task_execution.patch`. It already includes the fix.
