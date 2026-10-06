#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SpellEffect.generated.h"

class AActor;
class USpellComponent;

/** Everything an effect handler gets from a non-projectile cast (mirrors USpellComponent::OnSpellCast). */
USTRUCT(BlueprintType)
struct SHAMAN_API FSpellEffectContext
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) AActor* Caster = nullptr;
	UPROPERTY(BlueprintReadOnly) USpellComponent* SpellComponent = nullptr;
	UPROPERTY(BlueprintReadOnly) FName SpellId;
	UPROPERTY(BlueprintReadOnly) FName EffectId;
	UPROPERTY(BlueprintReadOnly) FVector Target = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float Radius = 0.f;    // FSpellRow::Radius
	UPROPERTY(BlueprintReadOnly) float Duration = 0.f;  // FSpellRow::Duration
};

USTRUCT(BlueprintType)
struct SHAMAN_API FSpellEffectResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bHandled = false;          // an effect exists for this EffectId
	UPROPERTY(BlueprintReadOnly) int32 Candidates = 0;           // valid targets found
	UPROPERTY(BlueprintReadOnly) int32 Affected = 0;             // targets actually changed
	UPROPERTY(BlueprintReadOnly) int32 SkippedForCapacity = 0;   // valid targets left alone because the tribe was full
};

/**
 * One reusable effect behind a spell EffectId (data: FSpellRow.EffectId -> handler). Instances live on
 * USpellEffectDispatcherComponent and are editable per Blueprint. Subclass for new effect types
 * (Invisibility, MagicalShield, ...) in later milestones; spells with the same behaviour share one class.
 */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced, CollapseCategories)
class SHAMAN_API USpellEffect : public UObject
{
	GENERATED_BODY()
public:
	/** Must match FSpellRow::EffectId (e.g. ConvertWildmen). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect") FName EffectId;

	virtual FSpellEffectResult Execute(const FSpellEffectContext& Context) { return FSpellEffectResult(); }
};
