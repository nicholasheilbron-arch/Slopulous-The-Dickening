# SHAMAN – Phase 2.1 Tribe Simulation Foundation (UE4.27.2)

Status: implemented as `phase2_1_tribe_simulation_foundation.patch` on top of c1d7f28. **Not yet built/validated in UE4.27.2** (awaiting PM build + automation + in-editor acceptance). No commit.

## Audit (existing → decision)
| Existing system | Reusable as-is | Needs extension | Reason |
|---|---|---|---|
| UTribeRegistrySubsystem (tribe id → UTribeComponent, all members, BeginPlay-order safe) | yes | no | already the world-level member registry |
| UTribeComponent (on each tribe's Shaman: idempotent roster, capacity, mana link) | roster/capacity | task book + worker pool | tribe is the authoritative owner of membership; tasks belong with it |
| UTribeMemberComponent (per-unit tribe id, kind, conversion) | identity/conversion | availability flag, one task slot, derived sim state | per-unit tribe identity already lives here |
| UTribeSubsystem (definitions, Shaman, buildings, circle, rebirth, elimination, victory) | yes | settlement anchor + read-only views | tribe-level state already here; avoid a new manager |
| AShamanUnitBase / AI controller (health, movement, orders, combat) | yes | AI mirrors its activity into sim state | units keep movement/health/physics |
| Gameplay tags (Faction/Unit/State) | yes | Task.* tags | data-driven task types without an enum explosion |

## Architecture
- **Tribe** = UTribeComponent (roster, task book, worker pool) + UTribeSubsystem state (faction, Shaman, buildings, circle, settlement anchor, elimination). Views: GetLivingMemberCount, GetAvailableWorkers(Count), GetSettlementAnchor, GetBuildings, GetFactionTag.
- **Task** = FTribeTask {id, Task.* tag, priority (Low/Normal/High/Emergency), state, tribe, target actor/location, assignee, source, end reason, times}. Lifecycle Unassigned→Assigned→Active→Completed|Failed|Cancelled (FTribeTaskRules, pure). Tribe API: CreateTask/AssignTask/StartTask/CompleteTask/FailTask/CancelTask/GetTask. Ended tasks kept (last 64) for inspection. Bookkeeping only: no execution.
- **Unit** = UTribeMemberComponent: CurrentTaskId (one primary task), SetUnavailable, IsAvailableWorker, GetSimState (Dead > Unavailable > Working > AI activity Idle/Following/Guarding/Combat). Other EUnitSimState values reserved.
- **Worker pool**: living follower of the tribe, not unavailable, no task; Shaman never included. Roster walk on demand + OnWorkersChanged / OnTaskChanged events; no per-frame or world scans.
- **Lifecycle safety**: leaving the roster (death, tribe change, destroy) fails the held task; tribe removal cancels its open tasks.
- **Debug**: F3 labels per follower (tribe, state, task, priority, target, availability); console ShamanTribeReport, ShamanTaskTest Create|Assign|Start|Complete|Fail|Cancel|Unavailable|Available, ShamanTribeTransfer N.

## New tests (5)
Shaman.Tribes.MemberLifecycle, TaskLifecycle, TaskAssignment, WorkerPool, SettlementAnchor. Expected suite: 37.

## Not in scope (future milestones)
Phase 2.2 Brave AI / task execution; gathering; storage; construction; food; shelter; water; population growth; player commands.
