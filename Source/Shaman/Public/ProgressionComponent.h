#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProgressionComponent.generated.h"

USTRUCT(BlueprintType)
struct FProgressionState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite) TArray<FName> PermanentSpells;
	UPROPERTY(BlueprintReadWrite) TArray<FName> UnlockedBuildings;
};

/** Knowledge that persists across maps (Vault of Knowledge). Save/load via Get/SetState. */
UCLASS(ClassGroup=(Shaman), meta=(BlueprintSpawnableComponent))
class SHAMAN_API UProgressionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable) void UnlockSpell(FName SpellId) { PermanentSpells.Add(SpellId); }
	UFUNCTION(BlueprintCallable) void UnlockBuilding(FName BuildingId) { UnlockedBuildings.Add(BuildingId); }
	UFUNCTION(BlueprintCallable) bool IsBuildingUnlocked(FName BuildingId) const { return UnlockedBuildings.Contains(BuildingId); }
	UFUNCTION(BlueprintCallable) FProgressionState GetState() const { return {PermanentSpells.Array(), UnlockedBuildings.Array()}; }
	UFUNCTION(BlueprintCallable) void SetState(const FProgressionState& S) { PermanentSpells = TSet<FName>(S.PermanentSpells); UnlockedBuildings = TSet<FName>(S.UnlockedBuildings); }
	const TSet<FName>& GetPermanentSpells() const { return PermanentSpells; }
private:
	TSet<FName> PermanentSpells, UnlockedBuildings;
};
