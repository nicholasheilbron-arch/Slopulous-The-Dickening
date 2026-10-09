#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Tribes/TribeTaskTypes.h"
#include "TribeTaskExecution.generated.h"

class UTribeComponent;
class UTribeMemberComponent;

/**
 * Phase 2.2: task execution. The tribe's task book (UTribeComponent) stays the single owner of task state; this file
 * turns an Active task into unit behaviour. Split in three parts so each can be tested on its own:
 *   - FTribeTaskExecutionRules: pure decisions (which tasks are executable, arrived / stuck / timed out / hold).
 *   - IUnitTaskMover: the unit's movement layer (AShamanUnitAIController implements it with its existing navmesh /
 *     planet-surface movement). No second movement system.
 *   - FUnitTaskExecutor: per-unit glue. Listens to its member's task events, asks the mover to move, and reports the
 *     outcome back to the task book (Complete / Fail). It never changes task state directly.
 * Only Task.MoveTo, Task.MoveToActor and Task.Guard are executable; every other type (Task.Debug, reserved gather /
 * build tags) remains bookkeeping-only.
 */

/** Executable task kinds (from the task's Task.* tag). */
UENUM(BlueprintType)
enum class ETaskExecKind : uint8
{
	None,            // not executable here: bookkeeping only
	MoveToLocation,  // Task.MoveTo: go to TargetLocation, complete on arrival
	MoveToActor,     // Task.MoveToActor: go to TargetActor, complete within reach, fail if it disappears
	Guard            // Task.Guard: go to TargetLocation and stay there until cancelled
};

/** What the executor last saw happen to its movement (debug / inspection). */
UENUM(BlueprintType)
enum class ETaskMoveResult : uint8
{
	None,
	Requested,      // a move is under way
	Arrived,        // move task reached its goal
	OnStation,      // guard is at its post
	RequestFailed,  // the movement layer could not move at all (no navigation / no path)
	Unreachable,    // the movement layer reported the goal cannot be reached
	Stuck,          // no progress for StuckTimeout seconds
	TimedOut,       // move task exceeded MaxMoveDuration
	TargetLost,     // MoveToActor target destroyed / invalid
	InvalidTarget,  // task data cannot be executed (no location, NaN, no actor)
	Interrupted     // task ended from outside (cancelled, unit lost eligibility, executor removed)
};

/** Answer of the movement layer to a move request. */
enum class ETaskMoveRequest : uint8 { Accepted, Unreachable, Failed };

/** Tuning for task execution (UShamanGameData::TaskExecution). Distances are ground distances (FShamanSpace). */
USTRUCT(BlueprintType)
struct SHAMAN_API FTribeTaskExecutionConfig
{
	GENERATED_BODY()
	/** Task.MoveTo completes within this distance of its location (a task's AcceptanceRadius > 0 overrides). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks", meta=(ClampMin="10")) float ArrivalRadius = 150.f;
	/** Task.MoveToActor completes within this distance of the target actor's location, plus the target's collision
	 *  radius (so large targets such as buildings are reached at their edge). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks", meta=(ClampMin="10")) float ActorArrivalRadius = 220.f;
	/** Task.Guard: the unit is on station within this distance of its post... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks", meta=(ClampMin="10")) float GuardRadius = 150.f;
	/** ...and only walks back once pushed further than this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks", meta=(ClampMin="10")) float GuardLeashRadius = 400.f;
	/** MoveToActor: re-issue the move when the target has moved this far from the last requested goal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks", meta=(ClampMin="10")) float RepathDistance = 200.f;
	/** Fail with Stuck when the unit gets no closer by MinProgress within this many seconds (0 = never). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks", meta=(ClampMin="0")) float StuckTimeout = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks", meta=(ClampMin="1")) float MinProgress = 50.f;
	/** Move tasks fail with TimedOut after this many seconds (0 = no limit). Guard tasks never time out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks", meta=(ClampMin="0")) float MaxMoveDuration = 180.f;
	/** Re-issue a move the movement layer dropped (e.g. after a ragdoll) at most this often. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks", meta=(ClampMin="0.1")) float RetryInterval = 1.f;
};

/** Per-run progress the rules update (kept by the executor). */
struct SHAMAN_API FTaskProgress
{
	double StartTime = 0.0;
	double LastProgressTime = 0.0;
	float BestDistance = TNumericLimits<float>::Max();
	bool bOnStation = false;
	void Reset(double Now) { StartTime = Now; LastProgressTime = Now; BestDistance = TNumericLimits<float>::Max(); bOnStation = false; }
};

enum class ETaskStepAction : uint8 { Move, Hold, Complete, Fail };

struct SHAMAN_API FTaskStepResult
{
	ETaskStepAction Action = ETaskStepAction::Move;
	ETaskMoveResult Result = ETaskMoveResult::Requested;
	FName Reason;
};

/** Pure execution rules (no world needed): deterministic and unit-tested on their own. */
struct SHAMAN_API FTribeTaskExecutionRules
{
	static ETaskExecKind KindOf(const FGameplayTag& Type);
	/** The task carries what its kind needs (finite location / live actor). */
	static bool HasValidTarget(const FTribeTask& Task, ETaskExecKind Kind);
	static float AcceptanceFor(const FTribeTask& Task, ETaskExecKind Kind, const FTribeTaskExecutionConfig& Config);
	/** One decision step. Distance = ground distance to the goal; bTargetValid = false when a MoveToActor target is gone. */
	static FTaskStepResult Step(ETaskExecKind Kind, FTaskProgress& Progress, float Distance, float Acceptance, bool bTargetValid,
		double Now, const FTribeTaskExecutionConfig& Config);
	static const TCHAR* KindName(ETaskExecKind Kind);
	static const TCHAR* MoveResultName(ETaskMoveResult Result);
};

/** The unit's movement layer, as seen by task execution. */
class SHAMAN_API IUnitTaskMover
{
public:
	virtual ~IUnitTaskMover() {}
	/** Start (or redirect) a move to Goal. GoalActor (MoveToActor tasks) lets the movement layer path to the actor
	 *  itself (navmesh: its reachable edge) rather than to its centre. Must not block; arrival is detected by the executor. */
	virtual ETaskMoveRequest RequestTaskMove(const FVector& Goal, AActor* GoalActor, float Acceptance) = 0;
	/** Stop any task-driven movement (no-op when idle). */
	virtual void StopTaskMove() = 0;
	/** A move is still being followed (false once it finished, was dropped or never started). */
	virtual bool IsTaskMoveInProgress() const = 0;
};

/**
 * Executes the Active task of one unit. Owned by the unit's AI controller (or a test). Event-driven: it only reacts to
 * its own member's task events and to Update() calls from its owner (4x per second); it never scans the world.
 * At most one task at a time, mirroring the task book's one-primary-task rule.
 */
class SHAMAN_API FUnitTaskExecutor
{
public:
	FUnitTaskExecutor() = default;
	~FUnitTaskExecutor();
	FUnitTaskExecutor(const FUnitTaskExecutor&) = delete;
	FUnitTaskExecutor& operator=(const FUnitTaskExecutor&) = delete;

	/** Start listening to Member's task events. Picks up a task Member already holds. */
	void Bind(UTribeMemberComponent* InMember, IUnitTaskMover* InMover);
	/** Stop listening. A task still executing is failed with Reason (the unit can no longer carry it out). */
	void Unbind(FName Reason);
	bool IsBound() const { return Member.IsValid() && Mover != nullptr; }

	/** Decision step; Now = world time in seconds. No-op unless a task is executing. */
	void Update(double Now);
	/** The unit cannot move for a while (ragdoll, stagger): don't count it as stuck. */
	void PauseWatchdog(double Now);

	/** An executable task is Active and this executor drives the unit (autonomous behaviour must yield). */
	bool HasControl() const { return TaskId != INDEX_NONE; }
	int32 GetTaskId() const { return TaskId; }
	/** Assigned to this unit but not started yet (the unit knows about it; autonomous behaviour continues). */
	int32 GetPendingTaskId() const { return PendingTaskId; }
	ETaskExecKind GetKind() const { return Kind; }
	ETaskMoveResult GetLastResult() const { return LastResult; }
	FName GetLastReason() const { return LastReason; }
	int32 GetLastTaskId() const { return LastTaskId; }
	FVector GetGoal() const { return Goal; }
	bool IsOnStation() const { return HasControl() && Progress.bOnStation; }
	FString Describe() const;

private:
	void HandleTaskState(UTribeMemberComponent* InMember, int32 InTaskId, ETribeTaskState NewState);
	void Begin(int32 InTaskId);
	/** Report the outcome to the task book. Local state is released first, so re-entrant task events are harmless. */
	void Finish(ETribeTaskState Final, FName Reason, ETaskMoveResult Result);
	/** Forget the running task locally (stops task-driven movement). Never touches the task book. */
	void Release(bool bStopMovement);
	bool ResolveGoal(const FTribeTask& Task, FVector& OutGoal) const;
	bool IssueMove(double Now);
	const FTribeTask* FindRunningTask() const;
	const FTribeTaskExecutionConfig& Config() const;
	double WorldNow() const;

	TWeakObjectPtr<UTribeMemberComponent> Member;
	IUnitTaskMover* Mover = nullptr;
	FDelegateHandle TaskEventHandle;

	int32 TaskId = INDEX_NONE;
	int32 PendingTaskId = INDEX_NONE;
	TWeakObjectPtr<UTribeComponent> TaskTribe;
	ETaskExecKind Kind = ETaskExecKind::None;
	FTaskProgress Progress;
	double PausedSince = -1.0; // knocked down since (watchdog and duration clock paused)
	FVector Goal = FVector::ZeroVector;
	FVector LastRequestedGoal = FVector(FLT_MAX);
	float Acceptance = 0.f;
	double LastRequestTime = -1000.0;
	bool bMoveIssued = false;

	// Inspection (survives the end of a task)
	ETaskMoveResult LastResult = ETaskMoveResult::None;
	FName LastReason;
	int32 LastTaskId = INDEX_NONE;
};
