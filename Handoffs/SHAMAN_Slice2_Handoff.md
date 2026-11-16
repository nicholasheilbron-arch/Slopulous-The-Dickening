# SHAMAN Slice 2 Handoff — Phase 2 progress (UE4.27.2)

Oct 8, 2026 · @Nicholas Heilbron

## Status

Phase 2.1 (tribe simulation foundation) is **written but not yet built or validated** in UE4.27.2, so its status is pending, not PASS. It exists as `phase2_1_tribe_simulation_foundation.patch` in the project's `Claude outputs` folder: 17 files, applies cleanly on top of the accepted Slice 1 commit `c1d7f28`. Nothing is committed or pushed.

Phase 2.2 onward has not been started. The next step is the PM's build, the automation run (expected 37 of 37) and the in-editor acceptance check below; Phase 2.1 is accepted only after those pass.

## Phase 2 roadmap

Phase 2's goal is a functioning settlement; it is split into 11 milestones and only 2.1 has been worked on.

&#91;embedded content: Phase 2 roadmap · 11 milestones\]

Each milestone stops at its own acceptance gate; 2.1 deliberately adds no gathering, building, food, growth, water or commands.

## Audit: what was reused and what was extended

Phase 2.1 extends the Phase 1 tribe framework; it adds no parallel tribe system and no new manager class.

| Existing system | Reused as-is | Extended with | Reason |
| --- | --- | --- | --- |
| `UTribeRegistrySubsystem` (world list of tribes and members) | Yes | Nothing | Already handles any spawn order |
| `UTribeComponent` (tribe's follower roster, on its Shaman) | Roster, capacity, mana link | Task book, worker pool, change events | The tribe already owns membership, so tasks belong there |
| `UTribeMemberComponent` (each unit's tribe, kind, conversion) | Identity, conversion | Availability flag, one task slot, derived unit state | Per-unit tribe identity already lives here |
| `UTribeSubsystem` (Shaman, buildings, circle, rebirth, victory) | Yes | Settlement anchor, read-only queries | Tribe-level state already lives here |
| Units and AI (`AShamanUnitBase`, AI controller) | Yes | AI records its current activity (debug) | Units keep movement, health, physics |
| Gameplay Tags (`Faction.*`, `Unit.*`, `State.*`) | Yes | 14 `Task.*` tags | Task types stay data-driven |

## Task lifecycle

&#91;embedded content: Task lifecycle · 6 states\]

The allowed moves live in `FTribeTaskRules::CanTransition` and every other move is refused. A task can't be Completed until it has been started, and the three end states are final. A unit that dies, changes tribe or is destroyed while holding a task fails that task.

## What Phase 2.1 adds

A tribe can now record work for its units and say which units are free; nothing executes that work yet.

| Part | Where | What it does |
| --- | --- | --- |
| Tribe model | `UTribeComponent` + `UTribeSubsystem` | Tribe id, faction tag, members, living member count (followers + Shaman if alive), available workers, buildings, settlement anchor, eliminated flag |
| Task record | `FTribeTask` (`Tribes/TribeTaskTypes.h`) | Id, type (`Task.*` tag), priority (Low, Normal, High, Emergency), state, tribe, target actor and/or world position, assignee, source, end reason, created/assigned/ended times |
| Task book | `UTribeComponent` | `CreateTask`, `AssignTask`, `StartTask`, `CompleteTask`, `FailTask`, `CancelTask`, `GetTask`, `GetOpenTaskCount`; keeps the last 64 ended tasks; fires `OnTaskChanged` |
| Unit state | `UTribeMemberComponent::GetSimState` | Dead, then Unavailable, then Working (holds a task), then the AI's activity (Idle, Following, Guarding, Combat); other states reserved |
| One task per unit | `UTribeMemberComponent` | Holds one task id; a busy unit refuses another task |
| Worker pool | `GetAvailableWorkers`, `CanAssignTo` | A living follower of the tribe, not marked unavailable, with no task. The Shaman is never included. Fires `OnWorkersChanged` |
| Lifecycle safety | `UnregisterFollower`, tribe `EndPlay` | A unit leaving the tribe (death, tribe change, destroyed) fails its task; a removed tribe cancels its open tasks |
| Settlement anchor | `UTribeSubsystem::GetSettlementAnchor` | The campfire (gathering point), else the circle; storage, food, water and housing attach here later |
| Debug | F3 labels, console | Per follower: tribe, state, task, priority, target, available/busy. Commands below |

Task targets are world positions and actors, so future executors must reach them through `FShamanSpace` and the terrain interfaces, never flat-world maths.

**Console commands (both game modes):**

| Command | Does |
| --- | --- |
| `ShamanTribeReport` | Per tribe: followers, living members, available workers, open tasks, buildings, settlement |
| `ShamanTaskTest Create` / `Assign` / `Start` / `Complete` / `Fail` / `Cancel` | Runs a `Task.Debug` task on the player's tribe; Assign picks the nearest available Brave |
| `ShamanTaskTest Unavailable` / `Available` | Takes that Brave out of / back into the worker pool |
| `ShamanTribeTransfer <tribe>` | Moves the nearest player Brave to another tribe and prints both counts |

## Files changed and new tests

| File | Change |
| --- | --- |
| `Public/Tribes/TribeTaskTypes.h`, `Private/Tribes/TribeTaskTypes.cpp` | **New:** task state, priority, unit state enums; `FTribeTask`; `FTribeTaskRules` |
| `Public/TribeComponent.h`, `Private/TribeComponent.cpp` | Task book, worker pool, events; task released when a unit leaves |
| `Public/TribeMemberComponent.h`, `Private/TribeMemberComponent.cpp` | Availability, task slot, `IsAlive`, `GetSimState`, activity |
| `Public/Tribes/TribeSubsystem.h`, `Private/Tribes/TribeSubsystem.cpp` | Settlement anchor and read-only queries |
| `Public/Buildings/BuildingActor.h` | `IsGatheringPoint()` getter |
| `Public/Game/ShamanGameMode.h`, `Private/Game/ShamanGameMode.cpp` | Debug labels and the three console commands |
| `Private/Game/ShamanPlanetGameMode.cpp` | Draws the same labels in debug mode |
| `Private/Characters/ShamanUnitAIController.cpp` | Records the AI's activity (Combat, Following, Guarding, Idle) |
| `Private/Tests/ShamanTribeTestWorld.h` | **New:** shared tribe test setup (moved out of `ShamanTribeTests.cpp`) |
| `Private/Tests/ShamanTribeTests.cpp` | Uses the shared setup; tests unchanged |
| `Private/Tests/ShamanTribeSimTests.cpp` | **New:** the 5 tests below |
| `Config/DefaultGameplayTags.ini` | 14 `Task.*` tags (only `Task.Debug` used) |

| New test | Checks |
| --- | --- |
| `Shaman.Tribes.MemberLifecycle` | Add, remove, double removal, re-registration, death, dead unit cannot rejoin, tribe change, conversion, destroyed unit, empty input |
| `Shaman.Tribes.TaskLifecycle` | All 36 state-to-state changes; Completed path; Failed and Cancelled paths; finished tasks cannot restart |
| `Shaman.Tribes.TaskAssignment` | Idle accepts; busy, unavailable, dead, Shaman and other-tribe units refused; ending a task frees the unit; death or tribe change fails the task |
| `Shaman.Tribes.WorkerPool` | Assigned, unavailable, dead and transferred workers leave the pool; completed and converted workers join it; Shaman never in it |
| `Shaman.Tribes.SettlementAnchor` | Circle anchors the settlement, campfire takes over, falls back when the campfire is removed |

## How to validate

1. Close Unreal (Don't Save if it asks about ShamanPrototype). In Command Prompt in the project folder run `git apply --check "Claude outputs\phase2_1_tribe_simulation_foundation.patch"`, then the same without `--check`, then `git status --short`. Expect 13 modified and 4 new files.
2. Open `Shaman.uproject` and rebuild. Run the `Shaman` automation tests: expect **37 of 37**, no regressions in the 32 Slice 1 tests.
3. On `ShamanPrototype` (GameMode Override = None), press Play, then F3:
   - `ShamanTribeReport`: tribe 0 shows 3 followers and 3 available workers.
   - `ShamanTaskTest Create`, `Assign`, `Start`, `Complete`: states go Unassigned, Assigned, Active, Completed; available workers 3, 2, 2, 3.
   - Repeat with `Create`, `Assign`, `Cancel`, and with `Create`, `Assign`, `Fail`: the Brave returns to the pool each time.
   - `ShamanTaskTest Unavailable` then `Available`: pool drops by 1, then recovers.
   - Kill a Brave (2 Blasts), then `ShamanTribeReport`: followers 3 to 2.
   - `ShamanTribeTransfer 1`: tribe 0 loses one follower, tribe 1 gains one.
4. Confirm nothing new happens in play: no gathering, building, food, growth, water needs or new spells.
5. `PlanetTest` still runs with `ShamanPlanetGameMode` and its terrain checks unchanged.
6. Only after all of this passes: commit, for example `feat: Phase 2.1 tribe simulation foundation`. Do not push unless asked.

## Known limitations and next step

- Tasks are bookkeeping only: no unit moves, gathers or builds because of a task.
- Priority is stored but never used to choose or interrupt work.
- A unit holding any task shows "Working"; per-job states (Gathering, Building) are reserved.
- Being marked unavailable does not end a held task.
- Task ids are unique per tribe, not globally.
- The worker pool is computed on request over the tribe's roster; there is no cached list.
- The settlement records only an anchor point.
- Performance is not measured yet; there are no new tick functions and no world scans.

**Next milestone: Phase 2.2, Brave AI and task execution.** Make Braves pick up assigned tasks, move to the target through the terrain/navigation abstractions (spherical-safe), mark tasks Active, Completed or Failed, and return to the pool. Gathering, storage, construction, food, shelter, water, population growth and player commands stay in their own later milestones.
