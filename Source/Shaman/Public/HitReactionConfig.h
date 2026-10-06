#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShamanTypes.h"
#include "HitReactionConfig.generated.h"

/** Designer-tunable: damage -> reaction tier -> impulse. Impulse = BaseImpulse * DamageMultiplier * Direction. */
UCLASS(BlueprintType)
class SHAMAN_API UHitReactionConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float StaggerMinDamage = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float KnockdownMinDamage = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RagdollMinDamage = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float LaunchMinDamage = 70.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BaseImpulse = 20000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float DamageMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float UpwardBias = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RagdollRecoverTime = 3.f;

	EHitTier GetTier(float Damage) const
	{
		if (Damage >= LaunchMinDamage) return EHitTier::Launch;
		if (Damage >= RagdollMinDamage) return EHitTier::Ragdoll;
		if (Damage >= KnockdownMinDamage) return EHitTier::Knockdown;
		if (Damage >= StaggerMinDamage) return EHitTier::Stagger;
		return EHitTier::None;
	}
};
