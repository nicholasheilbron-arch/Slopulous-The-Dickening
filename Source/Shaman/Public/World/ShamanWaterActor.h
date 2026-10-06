#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShamanWaterActor.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UMaterialInterface;

/**
 * Sea-level water for the whole map (Z = 0). Visual plane + a nav modifier that removes navmesh below
 * -FordableDepth, so AI paths across shallow rivers but never into deep water. Drowning is handled by
 * AShamanUnitBase using the same depth (shared terrain-hazard rule, spec section 21).
 */
UCLASS()
class SHAMAN_API AShamanWaterActor : public AActor
{
	GENERATED_BODY()
public:
	AShamanWaterActor();

	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Surface;
	UPROPERTY(VisibleAnywhere) UBoxComponent* DeepWaterNavBlocker;

	/** Call between SpawnActorDeferred and FinishSpawning so the nav system registers the final extents. */
	void Setup(float MapHalfSize, float SeabedZ, float FordableDepth, UMaterialInterface* Material, const FLinearColor& Color);
};
