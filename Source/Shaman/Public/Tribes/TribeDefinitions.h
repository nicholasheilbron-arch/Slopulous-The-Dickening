#pragma once
#include "CoreMinimal.h"
#include "ShamanTypes.h"
#include "TribeDefinitions.generated.h"

/** Static description of a tribe. Index in UShamanGameData::Tribes = tribe id (0 = player). */
USTRUCT(BlueprintType)
struct SHAMAN_API FTribeDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName FactionTag;                 // e.g. Faction.Player (GameplayTag name)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bPlayerControlled = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 BaseCapacity = 10;          // population cap before houses
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EFollowerOrder DefaultFollowerOrder = EFollowerOrder::GuardHome;
};

/** RebirthTime = Clamp(BaseTime / (1 + PerFollowerModifier * Followers), MinTime, MaxTime). */
USTRUCT(BlueprintType)
struct SHAMAN_API FReincarnationConfig
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BaseTime = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float PerFollowerModifier = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MinTime = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxTime = 30.f;
	/** Phase 1: enemy Shamans also reincarnate. Phase 4 turns this off and makes enemy circles destructible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bEnemyShamansReincarnate = true;
};
