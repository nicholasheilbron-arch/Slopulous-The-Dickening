#pragma once
#include "CoreMinimal.h"
#include "SpellProjectile.h"
#include "DefaultSpellProjectile.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

/** ASpellProjectile plus placeholder visuals (glowing sphere + light) so Blast is visible before art exists.
 *  Replace via UShamanGameData::ProjectileClasses with a Blueprint child of ASpellProjectile when VFX are ready. */
UCLASS()
class SHAMAN_API ADefaultSpellProjectile : public ASpellProjectile
{
	GENERATED_BODY()
public:
	ADefaultSpellProjectile();
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Visual;
	UPROPERTY(VisibleAnywhere) UPointLightComponent* Glow;
	UPROPERTY(EditAnywhere) FLinearColor Color = FLinearColor(1.f, 0.42f, 0.05f, 1.f);
protected:
	virtual void BeginPlay() override;
};
