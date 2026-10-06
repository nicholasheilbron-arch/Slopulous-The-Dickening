#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MonumentRow.h"
#include "MonumentActor.generated.h"

class UDataTable;

/** Any worshippable monument. Behavior comes from its FMonumentRow; the reward is chosen per instance. */
UCLASS()
class SHAMAN_API AMonumentActor : public AActor
{
	GENERATED_BODY()
public:
	AMonumentActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite) UDataTable* MonumentTable = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName MonumentRowName;               // VaultOfKnowledge / StoneHead / Obelisk / Gargoyle
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bOverrideRewardType = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EMonumentRewardType RewardTypeOverride = EMonumentRewardType::UnlockSpell;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName RewardId;                      // SpellId, BuildingId, or world EventId
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 RewardCountOverride = -1;      // -1 = use row default

	/** Call each tick (or at throttled intervals) from a worshipping actor. Beneficiary = that tribe's Shaman.
	 *  Returns true if the worshipper should play its worship animation. */
	UFUNCTION(BlueprintCallable) bool Worship(AActor* Worshipper, AActor* Beneficiary, float DeltaTime);
	UFUNCTION(BlueprintCallable) bool CanWorship(AActor* Worshipper) const;
	UFUNCTION(BlueprintCallable) float GetProgress01() const;
	UFUNCTION(BlueprintCallable) bool IsSpent() const { return bSpent; }

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProgress, float, Progress01);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCompleted, AMonumentActor*, Monument, AActor*, Beneficiary);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWorldEvent, FName, EventId, int32, Count, AActor*, Beneficiary);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDepleted);
	UPROPERTY(BlueprintAssignable) FOnProgress OnProgress;       // drive obelisk segment spin / progress bar
	UPROPERTY(BlueprintAssignable) FOnCompleted OnCompleted;
	UPROPERTY(BlueprintAssignable) FOnWorldEvent OnWorldEvent;   // handled by the world-event system (paths, landscape, teleport, ending AoDs)
	UPROPERTY(BlueprintAssignable) FOnDepleted OnDepleted;       // Stone Head sinking animation

private:
	const FMonumentRow* GetRow() const;
	void CompleteCycle(AActor* Beneficiary, const FMonumentRow& Row);
	float Progress = 0.f;
	int32 ChargePool = -1;
	bool bSpent = false;
	TMap<TWeakObjectPtr<AActor>, double> ActiveWorshippers;
};
