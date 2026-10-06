#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TribeTypes.h"
#include "TribeComponent.generated.h"

class UTribeMemberComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTribeFollowersChanged, int32, TribeId, int32, FollowerCount);

/**
 * Smallest tribe: identity, follower roster and population capacity. Put one on each tribe's Shaman (BP_Shaman).
 * The roster is the single source of truth: counts are derived from it, and registration is idempotent,
 * so the population cannot drift or double count. Food, housing, growth etc. arrive with the tribe-simulation milestone.
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

	/** The tribe an actor acts for: its own UTribeComponent, else the tribe of its UTribeMemberComponent. */
	UFUNCTION(BlueprintCallable, Category="Tribe") static UTribeComponent* FindTribeFor(const AActor* Actor);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void Prune();
	void NotifyChanged();
	TArray<TWeakObjectPtr<UTribeMemberComponent>> Followers;
};
