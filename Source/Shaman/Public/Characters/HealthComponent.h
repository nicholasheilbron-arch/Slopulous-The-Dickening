#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

class UDamageType;
class AController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHealthChanged, float, Health, float, MaxHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDied, AActor*, Killer, AController*, KillerController);

/** Health for any actor. Listens to the owner's OnTakeAnyDamage, so every existing damage source
 *  (UGameplayStatics::ApplyDamage from Blast, melee, drowning) works without knowing about this component. */
UCLASS(ClassGroup=(Shaman), meta=(BlueprintSpawnableComponent))
class SHAMAN_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UHealthComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxHealth = 100.f;
	UPROPERTY(BlueprintReadOnly) float Health = 100.f;

	UFUNCTION(BlueprintCallable) void InitHealth(float InMax) { MaxHealth = FMath::Max(1.f, InMax); Health = MaxHealth; bDead = false; }
	UFUNCTION(BlueprintCallable) void ResetHealth() { Health = MaxHealth; bDead = false; }
	UFUNCTION(BlueprintCallable) bool IsDead() const { return bDead; }
	UFUNCTION(BlueprintCallable) float GetHealthFraction() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }
	/** Instant death (drowning, swamp, out of world). */
	UFUNCTION(BlueprintCallable) void Kill(AActor* Killer);

	AActor* GetLastDamageCauser() const { return LastCauser.Get(); }
	float GetLastDamage() const { return LastDamage; }

	UPROPERTY(BlueprintAssignable) FOnHealthChanged OnHealthChanged;
	UPROPERTY(BlueprintAssignable) FOnDied OnDied;

protected:
	virtual void BeginPlay() override;
	UFUNCTION() void HandleAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

private:
	void Die(AActor* Killer, AController* KillerController);
	bool bDead = false;
	float LastDamage = 0.f;
	TWeakObjectPtr<AActor> LastCauser;
};
