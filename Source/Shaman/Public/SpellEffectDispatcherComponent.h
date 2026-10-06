#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpellEffect.h"
#include "SpellEffectDispatcherComponent.generated.h"

class USpellComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSpellEffectResolved, const FSpellEffectContext&, Context, const FSpellEffectResult&, Result);

/**
 * Effect registry for non-projectile spells. Listens to the owner's USpellComponent::OnSpellCast (unchanged)
 * and routes FSpellRow.EffectId to the matching USpellEffect instance. Add next to USpellComponent on any caster
 * (player Shaman, enemy Shamans, Stone Head scripts). Ships with ConvertWildmen; later effects are added to Effects.
 * Binds at BeginPlay to the USpellComponent already on the actor; use one dispatcher per caster.
 */
UCLASS(ClassGroup=(Shaman), meta=(BlueprintSpawnableComponent))
class SHAMAN_API USpellEffectDispatcherComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USpellEffectDispatcherComponent();

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category="Spell Effects") TArray<USpellEffect*> Effects;

	/** Play the spell row's ImpactVFX / ImpactSound at the target for handled effects (optional assets). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spell Effects") bool bPlayRowImpactFeedback = true;

	UFUNCTION(BlueprintCallable, Category="Spell Effects") USpellEffect* FindEffect(FName EffectId) const;
	/** Run an effect directly (scripted encounters, tests). OnSpellCast calls this too. */
	FSpellEffectResult Dispatch(const FSpellEffectContext& Context);

	UPROPERTY(BlueprintAssignable, Category="Spell Effects") FOnSpellEffectResolved OnSpellEffectResolved;

	/** Result of the most recent dispatch (debug / tests). */
	UPROPERTY(BlueprintReadOnly, Category="Spell Effects") FSpellEffectResult LastResult;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UFUNCTION() void HandleSpellCast(FName SpellId, FName EffectId, FVector Target, float Radius, float Duration);

	UPROPERTY(Transient) USpellComponent* BoundSpells = nullptr;
};
