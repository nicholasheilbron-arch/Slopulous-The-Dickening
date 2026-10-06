#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/WorldGenTypes.h"
#include "ShamanTerrainActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Phase 1 terrain: one procedural mesh built from FWorldLayout heights, with complex collision so characters,
 * traces and the (dynamic) navmesh all use it. Runtime deformation (Land Bridge, Flatten, ...) is Phase 3/5;
 * it will go through SetHeights() + Rebuild() on this same actor so spell code never touches the mesh directly.
 */
UCLASS()
class SHAMAN_API AShamanTerrainActor : public AActor
{
	GENERATED_BODY()
public:
	AShamanTerrainActor();

	UPROPERTY(VisibleAnywhere) UProceduralMeshComponent* Mesh;
	/** Flip if the terrain renders inside-out / characters fall through (engine winding convention check). */
	UPROPERTY(EditAnywhere) bool bFlipWinding = true; // review: unflipped order likely faces down in UE (CCW front faces)

	/** Call between SpawnActorDeferred and FinishSpawning so collision and navigation register the built mesh. */
	void Build(const FWorldLayout& Layout, UMaterialInterface* Material);

private:
	static FLinearColor ColorFor(float Height, float Slope, float MaxHeight);
};
