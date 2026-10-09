#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "TribeRegistrySubsystem.h"
#include "SpellComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Core/ShamanLog.h"

UTribeComponent::UTribeComponent() { PrimaryComponentTick.bCanEverTick = false; }

void UTribeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (TribeId < 0) return; // -1 is reserved for unaligned units (Wildmen); such a component stays inactive
	if (UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(this)) R->RegisterTribe(this);
	NotifyChanged();
}

void UTribeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	// The task book goes with the tribe: release every unit still holding one of its tasks.
	TArray<int32> Open;
	for (const TPair<int32, FTribeTask>& P : Tasks) if (P.Value.IsOpen()) Open.Add(P.Key);
	for (int32 Id : Open) EndTask(Id, ETribeTaskState::Cancelled, TEXT("TribeRemoved"));
	if (UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(this)) R->UnregisterTribe(this);
	Super::EndPlay(Reason);
}

void UTribeComponent::Prune()
{
	Followers.RemoveAll([](const TWeakObjectPtr<UTribeMemberComponent>& M) { return !M.IsValid(); });
}

int32 UTribeComponent::GetFollowerCount() const
{
	int32 N = 0;
	for (const TWeakObjectPtr<UTribeMemberComponent>& M : Followers) if (M.IsValid()) ++N;
	return N;
}

int32 UTribeComponent::GetPopulationUsed() const
{
	int32 Used = 0;
	for (const TWeakObjectPtr<UTribeMemberComponent>& M : Followers) if (M.IsValid()) Used += M->PopulationCost;
	return Used;
}

void UTribeComponent::SetPopulationCapacity(int32 NewCapacity)
{
	PopulationCapacity = FMath::Max(0, NewCapacity);
}

bool UTribeComponent::CanAddFollowers(int32 PopulationCostToAdd) const
{
	return GetPopulationUsed() + FMath::Max(0, PopulationCostToAdd) <= PopulationCapacity;
}

bool UTribeComponent::IsFollower(const UTribeMemberComponent* Member) const
{
	for (const TWeakObjectPtr<UTribeMemberComponent>& M : Followers) if (M.Get() == Member) return Member != nullptr;
	return false;
}

TArray<UTribeMemberComponent*> UTribeComponent::GetFollowers() const
{
	TArray<UTribeMemberComponent*> Out;
	for (const TWeakObjectPtr<UTribeMemberComponent>& M : Followers) if (UTribeMemberComponent* P = M.Get()) Out.Add(P);
	return Out;
}

bool UTribeComponent::RegisterFollower(UTribeMemberComponent* Member, bool bEnforceCapacity)
{
	if (!Member || !Member->IsFollower() || Member->TribeId != TribeId) return false;
	if (IsFollower(Member)) return true; // idempotent: never double count
	if (bEnforceCapacity && !CanAddFollowers(Member->PopulationCost)) return false;
	Prune();
	Followers.Add(Member);
	NotifyChanged();
	return true;
}

bool UTribeComponent::UnregisterFollower(UTribeMemberComponent* Member)
{
	const int32 Removed = Followers.RemoveAll([Member](const TWeakObjectPtr<UTribeMemberComponent>& M) { return M.Get() == Member; });
	Prune();
	// A unit leaving the roster (death, conversion, tribe change, destroyed) cannot keep working for this tribe.
	// (Task ids are per tribe: only end the task if this tribe really assigned it to this unit.)
	if (Member && Member->HasTask())
	{
		const FTribeTask* T = Tasks.Find(Member->GetCurrentTaskId());
		if (T && T->IsHeld() && T->Assignee.Get() == Member) EndTask(T->TaskId, ETribeTaskState::Failed, TEXT("MemberLeftTribe"));
	}
	if (Removed > 0) NotifyChanged();
	return Removed > 0;
}

void UTribeComponent::NotifyChanged()
{
	NotifyWorkerAvailabilityChanged();
	const int32 Count = GetFollowerCount();
	if (bDriveOwnerManaRegen)
		if (USpellComponent* Spells = GetOwner() ? GetOwner()->FindComponentByClass<USpellComponent>() : nullptr)
			Spells->FollowerCount = Count;
	OnFollowersChanged.Broadcast(TribeId, Count);
}

UTribeComponent* UTribeComponent::FindTribeFor(const AActor* Actor)
{
	if (!Actor) return nullptr;
	if (UTribeComponent* Own = Actor->FindComponentByClass<UTribeComponent>()) return Own;
	if (const UTribeMemberComponent* M = UTribeMemberComponent::FindOn(Actor))
		if (UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(Actor)) return R->FindTribe(M->TribeId);
	return nullptr;
}

// ---- Worker pool -------------------------------------------------------------------------------------------------

bool UTribeComponent::CanAssignTo(const UTribeMemberComponent* Member) const
{
	return Member && Member->TribeId == TribeId && Member->IsAvailableWorker() && IsFollower(Member);
}

TArray<UTribeMemberComponent*> UTribeComponent::GetAvailableWorkers() const
{
	TArray<UTribeMemberComponent*> Out;
	for (const TWeakObjectPtr<UTribeMemberComponent>& W : Followers)
		if (UTribeMemberComponent* M = W.Get())
			if (M->TribeId == TribeId && M->IsAvailableWorker()) Out.Add(M);
	return Out;
}

int32 UTribeComponent::GetAvailableWorkerCount() const
{
	int32 N = 0;
	for (const TWeakObjectPtr<UTribeMemberComponent>& W : Followers)
		if (const UTribeMemberComponent* M = W.Get())
			if (M->TribeId == TribeId && M->IsAvailableWorker()) ++N;
	return N;
}

void UTribeComponent::NotifyWorkerAvailabilityChanged()
{
	OnWorkersChanged.Broadcast(TribeId);
}

// ---- Task book ---------------------------------------------------------------------------------------------------

float UTribeComponent::Now() const
{
	const UWorld* W = GetWorld();
	return W ? W->GetTimeSeconds() : 0.f;
}

int32 UTribeComponent::CreateTask(FGameplayTag Type, ETribeTaskPriority Priority, AActor* TargetActor, FVector TargetLocation,
	bool bHasTargetLocation, FName Source)
{
	FTribeTask T;
	T.TaskId = NextTaskId++;
	T.Type = Type;
	T.Priority = Priority;
	T.TribeId = TribeId;
	T.TargetActor = TargetActor;
	T.TargetLocation = TargetLocation;
	T.bHasTargetLocation = bHasTargetLocation;
	T.Source = Source;
	T.CreatedTime = Now();
	Tasks.Add(T.TaskId, T);
	OnTaskChanged.Broadcast(TribeId, T.TaskId, T.State);
	return T.TaskId;
}

bool UTribeComponent::AssignTask(int32 TaskId, UTribeMemberComponent* Member)
{
	FTribeTask* T = Tasks.Find(TaskId);
	if (!T || !FTribeTaskRules::CanTransition(T->State, ETribeTaskState::Assigned)) return false;
	if (!CanAssignTo(Member)) return false; // dead, unavailable, busy, Shaman, other tribe...
	T->State = ETribeTaskState::Assigned;
	T->Assignee = Member;
	T->AssignedTime = Now();
	Member->SetCurrentTaskId(TaskId);
	OnTaskChanged.Broadcast(TribeId, TaskId, ETribeTaskState::Assigned);
	NotifyWorkerAvailabilityChanged();
	Member->NotifyTaskState(TaskId, ETribeTaskState::Assigned); // last: listeners may act on the task book
	return true;
}

bool UTribeComponent::StartTask(int32 TaskId)
{
	FTribeTask* T = Tasks.Find(TaskId);
	if (!T || !FTribeTaskRules::CanTransition(T->State, ETribeTaskState::Active)) return false;
	// Only the unit this tribe assigned may execute it, and only while it is still a living follower holding it.
	UTribeMemberComponent* Assignee = T->Assignee.Get();
	if (!Assignee || Assignee->GetCurrentTaskId() != TaskId || Assignee->TribeId != TribeId || !IsFollower(Assignee) || !Assignee->IsAlive())
		return false;
	T->State = ETribeTaskState::Active;
	OnTaskChanged.Broadcast(TribeId, TaskId, ETribeTaskState::Active);
	// Last, and T is not used afterwards: the unit's executor starts here and may complete / fail the task at once.
	Assignee->NotifyTaskState(TaskId, ETribeTaskState::Active);
	return true;
}

bool UTribeComponent::SetTaskAcceptanceRadius(int32 TaskId, float Radius)
{
	FTribeTask* T = Tasks.Find(TaskId);
	if (!T || T->State != ETribeTaskState::Unassigned) return false;
	T->AcceptanceRadius = FMath::Max(0.f, Radius);
	return true;
}

bool UTribeComponent::CompleteTask(int32 TaskId) { return EndTask(TaskId, ETribeTaskState::Completed, NAME_None); }
bool UTribeComponent::FailTask(int32 TaskId, FName Reason) { return EndTask(TaskId, ETribeTaskState::Failed, Reason); }
bool UTribeComponent::CancelTask(int32 TaskId, FName Reason) { return EndTask(TaskId, ETribeTaskState::Cancelled, Reason); }

bool UTribeComponent::EndTask(int32 TaskId, ETribeTaskState Final, FName Reason)
{
	FTribeTask* T = Tasks.Find(TaskId);
	if (!T || !FTribeTaskRules::CanTransition(T->State, Final)) return false;
	T->State = Final;
	T->EndReason = Reason;
	T->EndedTime = Now();
	UTribeMemberComponent* Freed = T->Assignee.Get();
	const bool bWasHeld = Freed && Freed->GetCurrentTaskId() == TaskId;
	if (bWasHeld)
	{
		Freed->SetCurrentTaskId(INDEX_NONE); // bookkeeping only
		Freed->SetTaskActivity(EUnitSimState::Working);
	}
	EndedTaskOrder.Add(TaskId);
	OnTaskChanged.Broadcast(TribeId, TaskId, Final);
	if (Freed) NotifyWorkerAvailabilityChanged();
	ForgetOldEndedTasks();
	// Last (T may be gone): the unit's executor stops its task-driven behaviour.
	if (bWasHeld) Freed->NotifyTaskState(TaskId, Final);
	return true;
}

void UTribeComponent::ForgetOldEndedTasks()
{
	while (EndedTaskOrder.Num() > FMath::Max(0, MaxEndedTasksKept))
	{
		Tasks.Remove(EndedTaskOrder[0]);
		EndedTaskOrder.RemoveAt(0);
	}
}

bool UTribeComponent::GetTask(int32 TaskId, FTribeTask& OutTask) const
{
	if (const FTribeTask* T = Tasks.Find(TaskId)) { OutTask = *T; return true; }
	return false;
}

int32 UTribeComponent::GetOpenTaskCount() const
{
	int32 N = 0;
	for (const TPair<int32, FTribeTask>& P : Tasks) if (P.Value.IsOpen()) ++N;
	return N;
}
