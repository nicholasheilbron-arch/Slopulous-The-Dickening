#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tribes/TribeDefinitions.h"
#include "TribeSubsystem.generated.h"

class AShamanUnitBase;
class ABuildingActor;
class UTribeComponent;
class UTribeMemberComponent;

/**
 * Where a tribe lives. Phase 2.1 only records anchors from the buildings that already exist; storage, food sources,
 * water access and housing state attach here in later Phase 2 milestones (not global variables).
 */
struct FTribeSettlement
{
	/** Settlement centre for future jobs (return home, deliver resources): the gathering point (campfire), else the circle. */
	FVector Anchor = FVector::ZeroVector;
	bool bHasAnchor = false;
	TWeakObjectPtr<ABuildingActor> GatheringPoint;
};

/** Per-tribe game state that is not the follower roster: definition, Shaman, buildings, circle, rebirth. */
struct FTribeState
{
	int32 TribeId = -1;
	FTribeDefinition Def;
	TWeakObjectPtr<AShamanUnitBase> Shaman;
	TArray<TWeakObjectPtr<ABuildingActor>> Buildings;
	TWeakObjectPtr<ABuildingActor> Circle;
	FTribeSettlement Settlement;
	double RebirthAt = -1.0;                              // world time of pending rebirth, -1 = none
	bool bShamanEliminated = false;
	FTimerHandle RebirthTimer;
	FTimerHandle DeathCheckTimer;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShamanEvent, int32, TribeId, AShamanUnitBase*, Shaman);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTribeEvent, int32, TribeId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLevelWon);

/**
 * Tribe game state for the world: definitions, Shamans, buildings, population capacity and reincarnation.
 * The follower roster itself is owned by UTribeComponent (on each Shaman) via UTribeRegistrySubsystem;
 * this subsystem reads counts from there and pushes capacity (BaseCapacity + houses) into it.
 */
UCLASS()
class SHAMAN_API UTribeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	void InitTribes(const TArray<FTribeDefinition>& Definitions, const FReincarnationConfig& InRebirth);

	void RegisterShaman(AShamanUnitBase* Shaman);
	void UnregisterShaman(AShamanUnitBase* Shaman);
	void RegisterBuilding(ABuildingActor* Building);
	void UnregisterBuilding(ABuildingActor* Building);

	/** The tribe's roster/capacity component (on its Shaman), or null. */
	UTribeComponent* GetTribeComponent(int32 TribeId) const;

	UFUNCTION(BlueprintCallable) int32 GetFollowerCount(int32 TribeId) const;
	UFUNCTION(BlueprintCallable) int32 GetPopulationCapacity(int32 TribeId) const;
	UFUNCTION(BlueprintCallable) AShamanUnitBase* GetShaman(int32 TribeId) const;
	UFUNCTION(BlueprintCallable) ABuildingActor* GetReincarnationCircle(int32 TribeId) const;
	UFUNCTION(BlueprintCallable) FLinearColor GetTribeColor(int32 TribeId) const;
	/** Seconds until the tribe's Shaman is reborn, or -1 if not dead. */
	UFUNCTION(BlueprintCallable) float GetRebirthRemaining(int32 TribeId) const;
	TArray<AShamanUnitBase*> GetFollowers(int32 TribeId) const;

	// ---- Tribe simulation view (Phase 2.1). Membership, tasks and the worker pool live on the tribe's UTribeComponent;
	//      these are convenience reads by tribe id.
	/** Living followers + the Shaman if alive. */
	UFUNCTION(BlueprintCallable) int32 GetLivingMemberCount(int32 TribeId) const;
	/** Followers that can take a task now (never the Shaman). */
	TArray<UTribeMemberComponent*> GetAvailableWorkers(int32 TribeId) const;
	UFUNCTION(BlueprintCallable) int32 GetAvailableWorkerCount(int32 TribeId) const;
	/** Settlement anchor (gathering point, else circle). False if the tribe has neither. */
	bool GetSettlementAnchor(int32 TribeId, FVector& OutAnchor) const;
	/** Buildings registered to the tribe (valid ones only). */
	TArray<ABuildingActor*> GetBuildings(int32 TribeId) const;
	FName GetFactionTag(int32 TribeId) const;
	int32 GetNumTribes() const { return Tribes.Num(); }

	/**
	 * Called by a Shaman when it dies. Resolved at the end of the frame, after every other death of the same event
	 * (e.g. one Blast killing the Shaman and its last followers):
	 *  - enemy tribe with 0 followers left: its Reincarnation Circle is destroyed and the tribe is eliminated;
	 *  - otherwise (and always for the player's tribe): rebirth timer at the tribe's circle, as before.
	 */
	void NotifyShamanDied(AShamanUnitBase* Shaman);

	/** Tribe's Shaman can no longer return (circle destroyed / gone). */
	UFUNCTION(BlueprintCallable) bool IsTribeEliminated(int32 TribeId) const;
	/** Every enemy (non-player-controlled) tribe is eliminated: the level is won. */
	UFUNCTION(BlueprintCallable) bool IsLevelWon() const { return bLevelWon; }

	UPROPERTY(BlueprintAssignable) FOnTribeEvent OnTribeEliminated;
	UPROPERTY(BlueprintAssignable) FOnLevelWon OnLevelWon;

	static float ComputeRebirthTime(const FReincarnationConfig& Config, int32 Followers);

	UPROPERTY(BlueprintAssignable) FOnShamanEvent OnShamanDied;
	UPROPERTY(BlueprintAssignable) FOnShamanEvent OnShamanReborn;

private:
	FTribeState* Find(int32 TribeId) { return Tribes.IsValidIndex(TribeId) ? &Tribes[TribeId] : nullptr; }
	const FTribeState* Find(int32 TribeId) const { return Tribes.IsValidIndex(TribeId) ? &Tribes[TribeId] : nullptr; }
	int32 ComputeCapacity(const FTribeState& T) const;
	void RefreshCapacity(int32 TribeId);
	void DoRebirth(int32 TribeId);
	void ResolveShamanDeath(int32 TribeId);
	void RefreshSettlement(FTribeState& T);
	void EliminateTribe(FTribeState& T, const TCHAR* Reason);
	void CheckLevelWon();

	TArray<FTribeState> Tribes;
	bool bLevelWon = false;
	FReincarnationConfig Rebirth;
};
