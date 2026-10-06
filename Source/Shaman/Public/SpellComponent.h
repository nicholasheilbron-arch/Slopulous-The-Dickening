#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpellRow.h"
#include "SpellComponent.generated.h"

class UDataTable;
class ASpellProjectile;

/** Owns known spell ids, mana, cooldowns. ManaRegen = BaseRegen * FollowerMultiplier * TemporaryModifier.
 *  Projectile spells spawn a projectile; every other spell fires OnSpellCast for an effect handler to implement. */
UCLASS(ClassGroup=(Shaman), meta=(BlueprintSpawnableComponent))
class SHAMAN_API USpellComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USpellComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite) UDataTable* SpellTable = nullptr;       // rows of FSpellRow
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> KnownSpellIds;            // e.g. Blast, Convert
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName, TSubclassOf<ASpellProjectile>> ProjectileClasses; // by EffectId
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RangeUnitsPerPoint = 50.f;        // 100 raw range = 5000 uu
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxMana = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Mana = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BaseRegen = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RegenPerFollower = 0.25f;
	UPROPERTY(BlueprintReadWrite) int32 FollowerCount = 3;
	UPROPERTY(BlueprintReadWrite) float TemporaryModifier = 1.f;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnSpellCast, FName, SpellId, FName, EffectId, FVector, Target, float, Radius, float, Duration);
	UPROPERTY(BlueprintAssignable) FOnSpellCast OnSpellCast;

	UFUNCTION(BlueprintCallable) bool CastSpell(FName SpellId, FVector AimTarget);
	UFUNCTION(BlueprintCallable) float GetManaRegen() const;
	UFUNCTION(BlueprintCallable) void LearnSpell(FName SpellId);
	UFUNCTION(BlueprintCallable) void AddCharges(FName SpellId, int32 Charges); // Stone Heads: super spells run on charges
	UFUNCTION(BlueprintCallable) int32 GetCharges(FName SpellId) const { const int32* C = SpellCharges.Find(SpellId); return C ? *C : 0; }
	UFUNCTION(BlueprintCallable) float GetRangeUnits(FName SpellId) const;

	// --- Added in Phase 1 completion (additive; CastSpell behaviour unchanged) ---
	/** Pure rule check used by CastSpell, AI and tests. Check order matches the original CastSpell. */
	static ESpellCastResult EvaluateCast(const FSpellRow& Row, float CurrentMana, int32 Charges, double Now,
		double CooldownEndTime, float DistanceToTarget, float RangeUnitsPerPoint);
	UFUNCTION(BlueprintCallable) float GetCooldownRemaining(FName SpellId) const;
	/** Row lookup (null if unknown). */
	const FSpellRow* GetSpellRow(FName SpellId) const;
	/** Why the last CastSpell call failed (Success if it worked). For HUD feedback. */
	UPROPERTY(BlueprintReadOnly) ESpellCastResult LastCastResult = ESpellCastResult::Success;
	const TMap<FName, int32>& GetAllCharges() const { return SpellCharges; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	TMap<FName, double> CooldownEnd;
	TMap<FName, int32> SpellCharges;
};
