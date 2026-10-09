#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TribeTypes.h"
#include "Tribes/TribeTaskTypes.h"
#include "TribeComponent.generated.h"

class UTribeMemberComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTribeFollowersChanged, int32, TribeId, int32, FollowerCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTribeTaskChanged, int32, TribeId, int32, TaskId, ETribeTaskState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTribeWorkersChanged, int32, TribeId);

/**
 * Smallest tribe: identity, follower roster and population capacity. Put one on each tribe's Shaman (BP_Shaman).
 * The roster is the single source of truth: counts are derived from it, and registration is idempotent,
 * so the population cannot drift or double count.
 *
 * Phase 2.1 (tribe simulation foundation): the tribe also owns its task book (create / assign / start / complete /
 * fail / cancel) and answers worker-pool queries. Leaving the roster (death, conversion, tribe change, destruction)
 * fails the member's task, so no task can be held by a unit that is no longer a living follower of this tribe.
 * Food, housing, growth etc. arrive with later Phase 2 milestones.
 */
UCLASS(ClassGroup=(Shaman), meta=(BlueprintSpawnableComponent))
class SHAMAN_API UTribeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UTribeComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribe") int32 TribeId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribe") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribe", meta=(ClampMin="0")) int32 PopulationCapacity = 10;
	/** Actor spawned when a unit is recruited (assign BP_Brave). Must carry a UTribeMemberComponent.
	 *  Empty = convert the unit in place (stub until the Brave Blueprint exists). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribe") TSubclassOf<AActor> ConvertedFollowerClass;
	/** Push follower count into the owner's USpellComponent (mana regen is follower-driven). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribe") bool bDriveOwnerManaRegen = true;

	UFUNCTION(BlueprintCallable, Category="Tribe") int32 GetTribeId() const { return TribeId; }
	UFUNCTION(BlueprintCallable, Category="Tribe") int32 GetFollowerCount() const;
	/** Sum of PopulationCost of current followers (compared against PopulationCapacity). */
	UFUNCTION(BlueprintCallable, Category="Tribe") int32 GetPopulationUsed() const;
	UFUNCTION(BlueprintCallable, Category="Tribe") int32 GetPopulationCapacity() const { return PopulationCapacity; }
	UFUNCTION(BlueprintCallable, Category="Tribe") void SetPopulationCapacity(int32 NewCapacity);
	UFUNCTION(BlueprintCallable, Category="Tribe") bool CanAddFollowers(int32 PopulationCostToAdd = 1) const;
	UFUNCTION(BlueprintCallable, Category="Tribe") bool IsFollower(const UTribeMemberComponent* Member) const;
	/** Current followers (valid members only). */
	UFUNCTION(BlueprintCallable, Category="Tribe") TArray<UTribeMemberComponent*> GetFollowers() const;

	/** Adds a follower. Idempotent. With bEnforceCapacity, refuses when full (returns false). */
	UFUNCTION(BlueprintCallable, Category="Tribe") bool RegisterFollower(UTribeMemberComponent* Member, bool bEnforceCapacity = false);
	UFUNCTION(BlueprintCallable, Category="Tribe") bool UnregisterFollower(UTribeMemberComponent* Member);

	UPROPERTY(BlueprintAssignable, Category="Tribe") FOnTribeFollowersChanged OnFollowersChanged;

	// ---- Worker pool -----------------------------------------------------------------------------------------------
	/** Followers that can take a task now (UTribeMemberComponent::IsAvailableWorker). Walks this tribe's roster only;
	 *  call on demand or on OnWorkersChanged, never per frame for every unit. */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") TArray<UTribeMemberComponent*> GetAvailableWorkers() const;
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") int32 GetAvailableWorkerCount() const;
	/** True when Member is an available worker of this tribe. */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool CanAssignTo(const UTribeMemberComponent* Member) const;
	/** Fired when worker availability may have changed (roster, task held/released, unavailable flag). */
	UPROPERTY(BlueprintAssignable, Category="Tribe|Simulation") FOnTribeWorkersChanged OnWorkersChanged;
	void NotifyWorkerAvailabilityChanged();

	// ---- Task book -------------------------------------------------------------------------------------------------
	/** New Unassigned task owned by this tribe. Returns its id. */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") int32 CreateTask(FGameplayTag Type, ETribeTaskPriority Priority,
		AActor* TargetActor, FVector TargetLocation, bool bHasTargetLocation, FName Source);
	/** Unassigned -> Assigned to Member. Refused if Member is not an available worker of this tribe (one primary task per unit). */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool AssignTask(int32 TaskId, UTribeMemberComponent* Member);
	/** Assigned -> Active (the executor started). */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool StartTask(int32 TaskId);
	/** Active -> Completed. Frees the unit. */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool CompleteTask(int32 TaskId);
	/** Assigned/Active -> Failed. Frees the unit. */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool FailTask(int32 TaskId, FName Reason);
	/** Unassigned/Assigned/Active -> Cancelled. Frees the unit. */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool CancelTask(int32 TaskId, FName Reason);
	/** Copy of a task (open or recently ended). False if unknown or already forgotten. */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool GetTask(int32 TaskId, FTribeTask& OutTask) const;
	const FTribeTask* FindTask(int32 TaskId) const { return Tasks.Find(TaskId); }
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") int32 GetOpenTaskCount() const;
	UPROPERTY(BlueprintAssignable, Category="Tribe|Simulation") FOnTribeTaskChanged OnTaskChanged;
	/** Ended tasks kept for inspection (debug, tests); older ones are forgotten. */
	UPROPERTY(EditAnywhere, Category="Tribe|Simulation", meta=(ClampMin="0")) int32 MaxEndedTasksKept = 64;

	/** The tribe an actor acts for: its own UTribeComponent, else the tribe of its UTribeMemberComponent. */
	UFUNCTION(BlueprintCallable, Category="Tribe") static UTribeComponent* FindTribeFor(const AActor* Actor);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void Prune();
	void NotifyChanged();
	/** Moves a held/open task to a final state and frees its unit. */
	bool EndTask(int32 TaskId, ETribeTaskState Final, FName Reason);
	void ForgetOldEndedTasks();
	float Now() const;
	TArray<TWeakObjectPtr<UTribeMemberComponent>> Followers;
	TMap<int32, FTribeTask> Tasks;
	TArray<int32> EndedTaskOrder;
	int32 NextTaskId = 1;
};
