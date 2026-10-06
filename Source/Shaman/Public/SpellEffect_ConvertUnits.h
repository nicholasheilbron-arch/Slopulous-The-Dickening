#pragma once
#include "CoreMinimal.h"
#include "SpellEffect.h"
#include "TribeTypes.h"
#include "SpellEffect_ConvertUnits.generated.h"

class UParticleSystem;
class USoundBase;

/**
 * Generic "change the side of units in an area" effect. Convert = this effect with Kind = Recruit (Wildmen only).
 * Hypnotize / permanent conversion will reuse it with another Kind once UTribeMemberComponent supports them.
 * Deals no damage. Targets come from UTribeRegistrySubsystem (unit identity), never from collision channels.
 */
UCLASS(meta=(DisplayName="Convert Units"))
class SHAMAN_API USpellEffect_ConvertUnits : public USpellEffect
{
	GENERATED_BODY()
public:
	USpellEffect_ConvertUnits();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect") EConversionKind Kind = EConversionKind::Recruit;
	/** Optional per-unit feedback (empty = none). Area feedback uses the spell row's ImpactVFX / ImpactSound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect") TSoftObjectPtr<UParticleSystem> PerTargetVFX;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect") TSoftObjectPtr<USoundBase> PerTargetSound;

	virtual FSpellEffectResult Execute(const FSpellEffectContext& Context) override;
};
