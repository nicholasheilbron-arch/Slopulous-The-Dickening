#include "Tribes/TribeTaskExecution.h"
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "TribeRegistrySubsystem.h"
#include "Game/ShamanGameData.h"
#include "Terrain/ShamanSpace.h"
#include "Core/ShamanLog.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

// ---- Rules -----------------------------------------------------------------------------------------------------------

ETaskExecKind FTribeTaskExecutionRules::KindOf(const FGameplayTag& Type)
{
	static const FGameplayTag MoveTo = FGameplayTag::RequestGameplayTag(TEXT("Task.MoveTo"), false);
	static const FGameplayTag MoveToActor = FGameplayTag::RequestGameplayTag(TEXT("Task.MoveToActor"), false);
	static const FGameplayTag Guard = FGameplayTag::RequestGameplayTag(TEXT("Task.Guard"), false);
	if (!Type.IsValid()) return ETaskExecKind::None;
	if (MoveTo.IsValid() && Type == MoveTo) return ETaskExecKind::MoveToLocation;
	if (MoveToActor.IsValid() && Type == MoveToActor) return ETaskExecKind::MoveToActor;
	if (Guard.IsValid() && Type == Guard) return ETaskExecKind::Guard;
	return ETaskExecKind::None;
}

bool FTribeTaskExecutionRules::HasValidTarget(const FTribeTask& Task, ETaskExecKind Kind)
{
	switch (Kind)
	{
	case ETaskExecKind::MoveToLocation:
	case ETaskExecKind::Guard:
		return Task.bHasTargetLocation && !Task.TargetLocation.ContainsNaN() && Task.TargetLocation.GetAbsMax() < 1.0e7f;
	case ETaskExecKind::MoveToActor:
	{
		const AActor* A = Task.TargetActor.Get();
		return A && !A->IsPendingKillPending();
	}
	default:
		return false;
	}
}

float FTribeTaskExecutionRules::AcceptanceFor(const FTribeTask& Task, ETaskExecKind Kind, const FTribeTaskExecutionConfig& Config)
{
	if (Task.AcceptanceRadius > 0.f) return Task.AcceptanceRadius;
	switch (Kind)
	{
	case ETaskExecKind::MoveToActor: return Config.ActorArrivalRadius;
	case ETaskExecKind::Guard:       return Config.GuardRadius;
	default:                         return Config.ArrivalRadius;
	}
}

FTaskStepResult FTribeTaskExecutionRules::Step(ETaskExecKind Kind, FTaskProgress& P, float Distance, float Acceptance, bool bTargetValid,
	double Now, const FTribeTaskExecutionConfig& C)
{
	FTaskStepResult R;
	if (Kind == ETaskExecKind::None)
	{
		R.Action = ETaskStepAction::Fail; R.Result = ETaskMoveResult::InvalidTarget; R.Reason = TEXT("NotExecutable");
		return R;
	}
	if (!bTargetValid)
	{
		R.Action = ETaskStepAction::Fail;
		R.Result = Kind == ETaskExecKind::MoveToActor ? ETaskMoveResult::TargetLost : ETaskMoveResult::InvalidTarget;
		R.Reason = Kind == ETaskExecKind::MoveToActor ? FName(TEXT("TargetLost")) : FName(TEXT("InvalidTarget"));
		return R;
	}

	const bool bArrived = Distance <= Acceptance;
	if (Kind == ETaskExecKind::Guard)
	{
		if (bArrived || (P.bOnStation && Distance <= FMath::Max(C.GuardLeashRadius, Acceptance)))
		{
			P.bOnStation = true;
			P.BestDistance = Distance;
			P.LastProgressTime = Now; // holding still is not being stuck
			R.Action = ETaskStepAction::Hold; R.Result = ETaskMoveResult::OnStation;
			return R;
		}
		if (P.bOnStation)
		{
			// Pushed off the post: walk back, with a fresh progress window.
			P.bOnStation = false;
			P.BestDistance = Distance;
			P.LastProgressTime = Now;
		}
	}
	else if (bArrived)
	{
		R.Action = ETaskStepAction::Complete; R.Result = ETaskMoveResult::Arrived;
		return R;
	}

	// Moving: watchdog.
	if (Distance < P.BestDistance - C.MinProgress)
	{
		P.BestDistance = Distance;
		P.LastProgressTime = Now;
	}
	if (C.StuckTimeout > 0.f && Now - P.LastProgressTime > C.StuckTimeout)
	{
		R.Action = ETaskStepAction::Fail; R.Result = ETaskMoveResult::Stuck; R.Reason = TEXT("Stuck");
		return R;
	}
	if (Kind != ETaskExecKind::Guard && C.MaxMoveDuration > 0.f && Now - P.StartTime > C.MaxMoveDuration)
	{
		R.Action = ETaskStepAction::Fail; R.Result = ETaskMoveResult::TimedOut; R.Reason = TEXT("TimedOut");
		return R;
	}
	R.Action = ETaskStepAction::Move; R.Result = ETaskMoveResult::Requested;
	return R;
}

const TCHAR* FTribeTaskExecutionRules::KindName(ETaskExecKind Kind)
{
	switch (Kind)
	{
	case ETaskExecKind::MoveToLocation: return TEXT("MoveTo");
	case ETaskExecKind::MoveToActor:    return TEXT("MoveToActor");
	case ETaskExecKind::Guard:          return TEXT("Guard");
	default:                            return TEXT("None");
	}
}

const TCHAR* FTribeTaskExecutionRules::MoveResultName(ETaskMoveResult Result)
{
	switch (Result)
	{
	case ETaskMoveResult::None:          return TEXT("None");
	case ETaskMoveResult::Requested:     return TEXT("Moving");
	case ETaskMoveResult::Arrived:       return TEXT("Arrived");
	case ETaskMoveResult::OnStation:     return TEXT("OnStation");
	case ETaskMoveResult::RequestFailed: return TEXT("RequestFailed");
	case ETaskMoveResult::Unreachable:   return TEXT("Unreachable");
	case ETaskMoveResult::Stuck:         return TEXT("Stuck");
	case ETaskMoveResult::TimedOut:      return TEXT("TimedOut");
	case ETaskMoveResult::TargetLost:    return TEXT("TargetLost");
	case ETaskMoveResult::InvalidTarget: return TEXT("InvalidTarget");
	case ETaskMoveResult::Interrupted:   return TEXT("Interrupted");
	default:                             return TEXT("?");
	}
}

// ---- Executor --------------------------------------------------------------------------------------------------------

FUnitTaskExecutor::~FUnitTaskExecutor()
{
	// Owner teardown: only drop the delegate binding (no task-book calls while objects are being destroyed).
	if (UTribeMemberComponent* M = Member.Get()) M->OnTaskStateChanged.Remove(TaskEventHandle);
}

const FTribeTaskExecutionConfig& FUnitTaskExecutor::Config() const
{
	return UShamanGameData::Get(Member.Get())->TaskExecution;
}

double FUnitTaskExecutor::WorldNow() const
{
	const UWorld* W = Member.IsValid() ? Member->GetWorld() : nullptr;
	return W ? W->GetTimeSeconds() : 0.0;
}

void FUnitTaskExecutor::Bind(UTribeMemberComponent* InMember, IUnitTaskMover* InMover)
{
	if (InMember == Member.Get() && InMover == Mover && TaskEventHandle.IsValid()) return;
	Unbind(TEXT("ExecutorRebound"));
	if (!InMember || !InMover) return;
	Member = InMember;
	Mover = InMover;
	TaskEventHandle = InMember->OnTaskStateChanged.AddRaw(this, &FUnitTaskExecutor::HandleTaskState);

	// Pick up a task the unit already holds (e.g. assigned before its controller possessed it).
	if (InMember->HasTask())
		if (const UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(InMember))
			if (const UTribeComponent* Tribe = R->FindTribe(InMember->TribeId))
				if (const FTribeTask* T = Tribe->FindTask(InMember->GetCurrentTaskId()))
				{
					if (T->State == ETribeTaskState::Assigned) PendingTaskId = T->TaskId;
					else if (T->State == ETribeTaskState::Active) Begin(T->TaskId);
				}
}

void FUnitTaskExecutor::Unbind(FName Reason)
{
	if (HasControl())
	{
		if (!Reason.IsNone()) Finish(ETribeTaskState::Failed, Reason, ETaskMoveResult::Interrupted);
		else Release(true);
	}
	PendingTaskId = INDEX_NONE;
	if (UTribeMemberComponent* M = Member.Get()) M->OnTaskStateChanged.Remove(TaskEventHandle);
	TaskEventHandle.Reset();
	Member = nullptr;
	Mover = nullptr;
}

const FTribeTask* FUnitTaskExecutor::FindRunningTask() const
{
	const UTribeComponent* Tribe = TaskTribe.Get();
	const UTribeMemberComponent* M = Member.Get();
	const FTribeTask* T = Tribe ? Tribe->FindTask(TaskId) : nullptr;
	if (!T || !M || T->State != ETribeTaskState::Active || T->Assignee.Get() != M || M->GetCurrentTaskId() != TaskId) return nullptr;
	return T;
}

bool FUnitTaskExecutor::ResolveGoal(const FTribeTask& Task, FVector& OutGoal) const
{
	if (Kind == ETaskExecKind::MoveToActor)
	{
		const AActor* A = Task.TargetActor.Get();
		if (!A || A->IsPendingKillPending()) return false;
		OutGoal = A->GetActorLocation();
		return true;
	}
	if (!FTribeTaskExecutionRules::HasValidTarget(Task, Kind)) return false;
	OutGoal = Task.TargetLocation;
	return true;
}

void FUnitTaskExecutor::HandleTaskState(UTribeMemberComponent* InMember, int32 InTaskId, ETribeTaskState NewState)
{
	if (InMember != Member.Get()) return;
	const FString Unit = GetNameSafe(InMember ? InMember->GetOwner() : nullptr);
	switch (NewState)
	{
	case ETribeTaskState::Assigned:
		PendingTaskId = InTaskId;
		UE_LOG(LogShaman, Log, TEXT("[Task] %s assigned task #%d (waiting for start)."), *Unit, InTaskId);
		return;
	case ETribeTaskState::Active:
		if (PendingTaskId == InTaskId) PendingTaskId = INDEX_NONE;
		Begin(InTaskId);
		return;
	case ETribeTaskState::Completed:
	case ETribeTaskState::Failed:
	case ETribeTaskState::Cancelled:
	{
		if (PendingTaskId == InTaskId) PendingTaskId = INDEX_NONE;
		if (InTaskId != TaskId) return; // not running here (bookkeeping-only task, or this executor already finished it)
		const FTribeTask* T = TaskTribe.IsValid() ? TaskTribe->FindTask(InTaskId) : nullptr;
		LastResult = ETaskMoveResult::Interrupted; // ended by someone else (console, scheduler, roster change)
		LastReason = T ? T->EndReason : NAME_None;
		LastTaskId = InTaskId;
		UE_LOG(LogShaman, Log, TEXT("[Task] %s task #%d %s from outside (%s): execution stopped."), *Unit, InTaskId,
			FTribeTaskRules::StateName(NewState), LastReason.IsNone() ? TEXT("no reason") : *LastReason.ToString());
		Release(true);
		return;
	}
	default:
		return;
	}
}

void FUnitTaskExecutor::Begin(int32 InTaskId)
{
	UTribeMemberComponent* M = Member.Get();
	if (!M || !Mover) return;
	const UTribeRegistrySubsystem* Registry = UTribeRegistrySubsystem::Get(M);
	UTribeComponent* Tribe = Registry ? Registry->FindTribe(M->TribeId) : nullptr;
	const FTribeTask* T = Tribe ? Tribe->FindTask(InTaskId) : nullptr;
	const FString Unit = GetNameSafe(M->GetOwner());
	if (!T || T->State != ETribeTaskState::Active || T->Assignee.Get() != M || M->GetCurrentTaskId() != InTaskId || T->TribeId != M->TribeId)
	{
		UE_LOG(LogShaman, Log, TEXT("[Task] %s ignored start of task #%d: not this unit's active task in tribe %d."), *Unit, InTaskId, M->TribeId);
		return;
	}
	const ETaskExecKind NewKind = FTribeTaskExecutionRules::KindOf(T->Type);
	if (NewKind == ETaskExecKind::None)
	{
		UE_LOG(LogShaman, Verbose, TEXT("[Task] %s task #%d (%s) is bookkeeping-only: not executed."), *Unit, InTaskId, *T->Type.ToString());
		return;
	}
	if (HasControl()) Release(true); // cannot happen with the one-task rule; never run two

	TaskId = InTaskId;
	TaskTribe = Tribe;
	Kind = NewKind;
	LastTaskId = InTaskId;
	LastResult = ETaskMoveResult::None;
	LastReason = NAME_None;
	bMoveIssued = false;
	LastRequestedGoal = FVector(FLT_MAX);
	LastRequestTime = -1000.0;
	Progress.Reset(WorldNow());
	PausedSince = -1.0;
	Acceptance = FTribeTaskExecutionRules::AcceptanceFor(*T, Kind, Config());
	if (Kind == ETaskExecKind::MoveToActor && T->AcceptanceRadius <= 0.f)
		if (const AActor* Target = T->TargetActor.Get())
			Acceptance += FMath::Clamp(Target->GetSimpleCollisionRadius(), 0.f, 2000.f); // reach the edge of big targets, not their centre

	if (!ResolveGoal(*T, Goal))
	{
		Finish(ETribeTaskState::Failed, TEXT("InvalidTarget"), ETaskMoveResult::InvalidTarget);
		return;
	}
	UE_LOG(LogShaman, Log, TEXT("[Task] %s started task #%d %s (%s) -> %s, arrival radius %.0f."), *Unit, InTaskId,
		FTribeTaskExecutionRules::KindName(Kind), FTribeTaskRules::PriorityName(T->Priority),
		T->TargetActor.IsValid() ? *T->TargetActor->GetName() : *Goal.ToCompactString(), Acceptance);
	Mover->StopTaskMove(); // drop whatever the unit was doing before (follow / wander path); the task owns movement now
	Update(Progress.StartTime); // starting the task issues the first move right away
}

void FUnitTaskExecutor::Update(double Now)
{
	if (!HasControl()) return;
	UTribeMemberComponent* M = Member.Get();
	const FTribeTask* T = FindRunningTask();
	if (!M || !T || !Mover)
	{
		// The task book no longer has this unit's active task (should have arrived as an event): drop it quietly.
		LastResult = ETaskMoveResult::Interrupted;
		LastReason = TEXT("TaskLost");
		Release(true);
		return;
	}
	if (!M->IsAlive()) { Finish(ETribeTaskState::Failed, TEXT("UnitDead"), ETaskMoveResult::Interrupted); return; }
	if (M->IsUnavailable()) { Finish(ETribeTaskState::Failed, TEXT("UnitUnavailable"), ETaskMoveResult::Interrupted); return; }
	if (PausedSince >= 0.0)
	{
		// Time spent knocked down counts neither as "stuck" nor toward MaxMoveDuration.
		const double Paused = FMath::Max(0.0, Now - PausedSince);
		Progress.StartTime += Paused;
		Progress.LastProgressTime += Paused;
		PausedSince = -1.0;
		LastRequestTime = -1000.0; // the movement layer may have dropped the move meanwhile: allow an immediate re-issue
	}

	FVector NewGoal;
	const bool bTargetValid = ResolveGoal(*T, NewGoal);
	if (bTargetValid) Goal = NewGoal;
	const AActor* Owner = M->GetOwner();
	const float Dist = FShamanSpace::HorizontalDistance(M, Owner->GetActorLocation(), Goal);
	const FTaskStepResult S = FTribeTaskExecutionRules::Step(Kind, Progress, Dist, Acceptance, bTargetValid, Now, Config());
	switch (S.Action)
	{
	case ETaskStepAction::Complete:
		Finish(ETribeTaskState::Completed, NAME_None, S.Result);
		return;
	case ETaskStepAction::Fail:
		Finish(ETribeTaskState::Failed, S.Reason, S.Result);
		return;
	case ETaskStepAction::Hold:
		if (bMoveIssued)
		{
			bMoveIssued = false;
			LastRequestedGoal = FVector(FLT_MAX);
			Mover->StopTaskMove();
		}
		if (LastResult != ETaskMoveResult::OnStation)
			UE_LOG(LogShaman, Log, TEXT("[Task] %s task #%d on station (%.0f uu from post)."), *GetNameSafe(Owner), TaskId, Dist);
		LastResult = ETaskMoveResult::OnStation;
		M->SetTaskActivity(EUnitSimState::Guarding);
		return;
	case ETaskStepAction::Move:
	default:
		M->SetTaskActivity(EUnitSimState::Moving);
		IssueMove(Now);
		return;
	}
}

bool FUnitTaskExecutor::IssueMove(double Now)
{
	const FTribeTaskExecutionConfig& C = Config();
	const bool bGoalMoved = FVector::DistSquared(Goal, LastRequestedGoal) > FMath::Square(C.RepathDistance);
	const bool bDropped = bMoveIssued && !Mover->IsTaskMoveInProgress() && (Now - LastRequestTime) >= C.RetryInterval;
	if (bMoveIssued && !bGoalMoved && !bDropped) return true;

	LastRequestTime = Now;
	// Ask the movement layer to stop a little inside the arrival radius so arrival is detected reliably.
	const FTribeTask* T = FindRunningTask();
	AActor* GoalActor = (Kind == ETaskExecKind::MoveToActor && T) ? T->TargetActor.Get() : nullptr;
	const ETaskMoveRequest Answer = Mover->RequestTaskMove(Goal, GoalActor, Acceptance * 0.75f);
	if (Answer == ETaskMoveRequest::Failed) { Finish(ETribeTaskState::Failed, TEXT("NoPath"), ETaskMoveResult::RequestFailed); return false; }
	if (Answer == ETaskMoveRequest::Unreachable) { Finish(ETribeTaskState::Failed, TEXT("Unreachable"), ETaskMoveResult::Unreachable); return false; }
	bMoveIssued = true;
	LastRequestedGoal = Goal;
	LastResult = ETaskMoveResult::Requested;
	return true;
}

void FUnitTaskExecutor::PauseWatchdog(double Now)
{
	if (HasControl() && PausedSince < 0.0) PausedSince = Now;
}

void FUnitTaskExecutor::Finish(ETribeTaskState Final, FName Reason, ETaskMoveResult Result)
{
	const int32 Id = TaskId;
	UTribeComponent* Tribe = TaskTribe.Get();
	const FString Unit = GetNameSafe(Member.IsValid() ? Member->GetOwner() : nullptr);
	const ETaskExecKind WasKind = Kind;
	LastResult = Result;
	LastReason = Reason;
	LastTaskId = Id;
	Release(true); // local state first: the task-book call below re-enters HandleTaskState
	// UE4's UE_LOG expands to a { } block, so "if (...) UE_LOG(...); else" does not compile (C2181): braces required.
	if (Final == ETribeTaskState::Completed)
	{
		UE_LOG(LogShaman, Log, TEXT("[Task] %s completed task #%d %s (%s)."), *Unit, Id, FTribeTaskExecutionRules::KindName(WasKind),
			FTribeTaskExecutionRules::MoveResultName(Result));
	}
	else // expected gameplay outcome, not an engine problem: Log, not Warning (automation treats warnings as errors)
	{
		UE_LOG(LogShaman, Log, TEXT("[Task] %s %s task #%d %s: %s (%s)."), *Unit, FTribeTaskRules::StateName(Final), Id,
			FTribeTaskExecutionRules::KindName(WasKind), Reason.IsNone() ? TEXT("no reason") : *Reason.ToString(),
			FTribeTaskExecutionRules::MoveResultName(Result));
	}
	if (!Tribe || Id == INDEX_NONE) return;
	if (Final == ETribeTaskState::Completed) Tribe->CompleteTask(Id);
	else if (Final == ETribeTaskState::Cancelled) Tribe->CancelTask(Id, Reason);
	else Tribe->FailTask(Id, Reason);
}

void FUnitTaskExecutor::Release(bool bStopMovement)
{
	const bool bWasRunning = TaskId != INDEX_NONE;
	TaskId = INDEX_NONE;
	TaskTribe = nullptr;
	Kind = ETaskExecKind::None;
	bMoveIssued = false;
	LastRequestedGoal = FVector(FLT_MAX);
	Progress.bOnStation = false;
	if (UTribeMemberComponent* M = Member.Get()) M->SetTaskActivity(EUnitSimState::Working);
	if (bWasRunning && bStopMovement && Mover) Mover->StopTaskMove();
}

FString FUnitTaskExecutor::Describe() const
{
	if (HasControl())
		return FString::Printf(TEXT("exec %s #%d: %s"), FTribeTaskExecutionRules::KindName(Kind), TaskId, FTribeTaskExecutionRules::MoveResultName(LastResult));
	if (PendingTaskId != INDEX_NONE) return FString::Printf(TEXT("exec: #%d assigned, waiting for start"), PendingTaskId);
	if (LastTaskId != INDEX_NONE)
		return FString::Printf(TEXT("exec last #%d: %s%s%s"), LastTaskId, FTribeTaskExecutionRules::MoveResultName(LastResult),
			LastReason.IsNone() ? TEXT("") : TEXT(" "), LastReason.IsNone() ? TEXT("") : *LastReason.ToString());
	return TEXT("exec: idle");
}
