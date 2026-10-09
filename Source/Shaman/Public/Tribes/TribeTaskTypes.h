#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TribeTaskTypes.generated.h"

class UTribeMemberComponent;

/**
 * Tribe simulation foundation (Phase 2.1): bookkeeping types for unit tasks.
 *
 * A task is what a unit has been asked to accomplish (gather wood, build, guard...). The owning tribe
 * (UTribeComponent) creates, assigns and ends tasks; executing them (moving, gathering, building) is the job of later
 * systems (Phase 2.2: Tribes/TribeTaskExecution.h). Ending a task only changes bookkeeping: it never kills, converts or
 * moves a unit.
 */

/** Task lifecycle. Unassigned -> Assigned -> Active -> Completed | Failed | Cancelled (see FTribeTaskRules). */
UENUM(BlueprintType)
enum class ETribeTaskState : uint8
{
	Unassigned,
	Assigned,   // reserved for a unit; the unit is busy
	Active,     // the unit is executing it
	Completed,
	Failed,
	Cancelled
};

/** Relative importance for future schedulers (combat, starvation, construction, player commands). Not used to
 *  pre-empt anything yet. */
UENUM(BlueprintType)
enum class ETribeTaskPriority : uint8
{
	Low,
	Normal,
	High,
	Emergency
};

/**
 * What a unit is doing, as seen by the tribe simulation. Only Idle, Unavailable and Dead drive behaviour in 2.1
 * (worker availability); Working marks a unit holding a task; Following / Guarding / Combat mirror what the existing
 * Phase 1 AI is doing (debug visibility). Phase 2.2: a unit executing a task shows Moving or Guarding instead of
 * Working. The rest are reserved for later milestones and are never set yet.
 */
UENUM(BlueprintType)
enum class EUnitSimState : uint8
{
	Idle,
	Moving,      // executing a move (Phase 2.2)
	Working,     // holds an Assigned/Active task
	Building,    // reserved
	Gathering,   // reserved
	Eating,      // reserved
	Drinking,    // reserved
	Sleeping,    // reserved
	Following,
	Guarding,
	Combat,
	Dead,
	Unavailable
};

/** One unit of work owned by a tribe. Targets are world-space (actor and/or location): executors must reach them
 *  through the terrain/navigation abstractions (FShamanSpace, ITerrainQuery), never by flat-world assumptions. */
USTRUCT(BlueprintType)
struct SHAMAN_API FTribeTask
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Task") int32 TaskId = INDEX_NONE;
	/** Task.* gameplay tag (Task.Gather.Wood, Task.Build, Task.Debug...). */
	UPROPERTY(BlueprintReadOnly, Category="Task") FGameplayTag Type;
	UPROPERTY(BlueprintReadOnly, Category="Task") ETribeTaskPriority Priority = ETribeTaskPriority::Normal;
	UPROPERTY(BlueprintReadOnly, Category="Task") ETribeTaskState State = ETribeTaskState::Unassigned;
	UPROPERTY(BlueprintReadOnly, Category="Task") int32 TribeId = INDEX_NONE;
	UPROPERTY() TWeakObjectPtr<AActor> TargetActor;
	UPROPERTY(BlueprintReadOnly, Category="Task") FVector TargetLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Task") bool bHasTargetLocation = false;
	/** Arrival radius for executed move/guard tasks; 0 = UShamanGameData::TaskExecution default (Phase 2.2). */
	UPROPERTY(BlueprintReadOnly, Category="Task") float AcceptanceRadius = 0.f;
	UPROPERTY() TWeakObjectPtr<UTribeMemberComponent> Assignee;
	/** Who asked for it (system or command name), for debugging. */
	UPROPERTY(BlueprintReadOnly, Category="Task") FName Source;
	/** Why it ended (Failed/Cancelled), for debugging. */
	UPROPERTY(BlueprintReadOnly, Category="Task") FName EndReason;
	UPROPERTY(BlueprintReadOnly, Category="Task") float CreatedTime = -1.f;
	UPROPERTY(BlueprintReadOnly, Category="Task") float AssignedTime = -1.f;
	UPROPERTY(BlueprintReadOnly, Category="Task") float EndedTime = -1.f;

	bool IsOpen() const { return State == ETribeTaskState::Unassigned || State == ETribeTaskState::Assigned || State == ETribeTaskState::Active; }
	bool IsHeld() const { return State == ETribeTaskState::Assigned || State == ETribeTaskState::Active; }
};

/** The task state machine, kept pure so it is deterministic and testable on its own. */
struct SHAMAN_API FTribeTaskRules
{
	static bool CanTransition(ETribeTaskState From, ETribeTaskState To)
	{
		switch (From)
		{
		case ETribeTaskState::Unassigned: return To == ETribeTaskState::Assigned || To == ETribeTaskState::Cancelled;
		case ETribeTaskState::Assigned:   return To == ETribeTaskState::Active || To == ETribeTaskState::Failed || To == ETribeTaskState::Cancelled;
		case ETribeTaskState::Active:     return To == ETribeTaskState::Completed || To == ETribeTaskState::Failed || To == ETribeTaskState::Cancelled;
		default:                          return false; // Completed / Failed / Cancelled are final
		}
	}
	static const TCHAR* StateName(ETribeTaskState S);
	static const TCHAR* PriorityName(ETribeTaskPriority P);
	static const TCHAR* SimStateName(EUnitSimState S);
};
