#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpellRow.h"
#include "SpellProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;

/** Generic spell projectile (Blast/Taka and any future projectile). Make a Blueprint child per EffectId. */
UCLASS()
class SHAMAN_API ASpellProjectile : public AActor
{
	GENERATED_BODY()
public:
	ASpellProjectile();
	void Init(const FSpellRow& InRow, float RangeUnits);

	UPROPERTY(VisibleAnywhere) USphereComponent* Collision;
	UPROPERTY(VisibleAnywhere) UProjectileMovementComponent* Movement;
	UPROPERTY(EditAnywhere) float HomingAcquireRadius = 2500.f;
	UPROPERTY(EditAnywhere) float HomingConeDegrees = 45.f;

protected:
	UFUNCTION() void OnHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
	FSpellRow Row;
};
