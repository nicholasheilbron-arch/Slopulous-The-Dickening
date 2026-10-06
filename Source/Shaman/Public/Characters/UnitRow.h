#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShamanTypes.h"
#include "UnitRow.generated.h"

class USkeletalMesh;
class UAnimInstance;

/** One row per unit archetype in DT_Units (Content/Data/Units.csv). Shaman, Brave, Warrior, Wildman are rows,
 *  not classes: the same AShamanUnitBase runs all of them. */
USTRUCT(BlueprintType)
struct SHAMAN_API FUnitRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EUnitRole Role = EUnitRole::Brave;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxHealth = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float WalkSpeed = 380.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bCanFight = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MeleeDamage = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MeleeRange = 160.f;      // uu from capsule edge
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MeleeCooldown = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MeleeKnockback = 1.f;    // multiplies damage fed to the hit-reaction tiers
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AggroRadius = 1200.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float LeashRadius = 2200.f;    // max chase distance from home / Shaman
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bCountsAsFollower = true; // counts for mana regen / rebirth time
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 PopulationCost = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bDrowns = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float DrownTime = 3.f;         // seconds in deep water before death
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CorpseLifetime = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CapsuleRadius = 34.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CapsuleHalfHeight = 88.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> SkeletalMesh;   // empty = placeholder cylinder
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftClassPtr<UAnimInstance> AnimClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector PlaceholderScale = FVector(0.6f, 0.6f, 1.76f);
};
