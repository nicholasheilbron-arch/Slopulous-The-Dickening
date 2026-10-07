#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetWaterActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Debug/placeholder spherical sea: an icosphere at the planet's sea-level radius. Visual only (no collision);
 * the water rule itself is UShamanTerrainSubsystem::GetWaterDepthAt. Final water is out of scope.
 */
UCLASS()
class SHAMAN_API APlanetWaterActor : public AActor
{
	GENERATED_BODY()
public:
	APlanetWaterActor();
	UPROPERTY(VisibleAnywhere) UProceduralMeshComponent* Mesh;
	/** 0..6; 5 = 20480 triangles (~3 uu chord error at a 20 km radius). */
	UPROPERTY(EditAnywhere) int32 Subdivisions = 5;
	/** Flip if the sea renders inside-out on your setup. */
	UPROPERTY(EditAnywhere) bool bFlipWinding = false;

	void Build(float SeaRadius, UMaterialInterface* Material, const FLinearColor& Color);
};
