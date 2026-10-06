#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShamanTypes.h"
#include "RagdollReactionComponent.generated.h"

class UHitReactionConfig;

/** Put on any ACharacter. Turns (damage, direction) into stagger / ragdoll / launch. */
UCLASS(ClassGroup=(Shaman), meta=(BlueprintSpawnableComponent))
class SHAMAN_API URagdollReactionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) UHitReactionConfig* Config = nullptr;

	UFUNCTION(BlueprintCallable) void ApplyHit(float Damage, FVector Direction);
	UFUNCTION(BlueprintCallable) bool IsRagdolling() const { return bRagdolling; }

	// --- Added in Phase 1 completion (additive; ApplyHit behaviour unchanged for skeletal meshes) ---
	/** Characters without a skeletal mesh (placeholder art) are launched instead: velocity = Impulse * this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FallbackLaunchScale = 0.05f;
	/** Death: ragdoll (or fallback launch) that never auto-recovers. Direction/Damage feed the normal impulse formula. */
	UFUNCTION(BlueprintCallable) void EnterDeathRagdoll(FVector Direction, float Damage);
	/** Rebirth / scripted recovery: stand back up immediately. */
	UFUNCTION(BlueprintCallable) void ForceRecover();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHitReaction, EHitTier, Tier, float, Damage);
	UPROPERTY(BlueprintAssignable) FOnHitReaction OnHitReaction;

private:
	void StartRagdoll(const FVector& Impulse);
	void EndRagdoll();
	FVector ComputeImpulse(FVector Direction, float Damage) const;
	bool HasSkeletalMesh() const;
	bool bRagdolling = false;
	bool bDeathRagdoll = false;
	FTimerHandle RecoverTimer;
};
