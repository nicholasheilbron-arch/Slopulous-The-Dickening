#pragma once
#include "CoreMinimal.h"
#include "Terrain/ShamanTerrainBackend.h"
#include "VoxelPluginTerrainBackend.generated.h"

class AActor;
class APawn;
class UActorComponent;

/**
 * VOXEL-SPECIFIC backend: renders + collides SHAMAN's height-field planet with Voxel Plugin Free.
 *
 * Source of truth stays FPlanetHeightField (inherited from UAnalyticPlanetTerrainBackend): queries, raycasts,
 * save data and determinism do not depend on voxel state. The voxel world only turns the height field into
 * LOD meshes + collision, and is remeshed in the edited box after every modification.
 * Swap this class for another UShamanTerrainBackend (e.g. a cube-sphere heightmap) without touching gameplay.
 *
 * Dependency boundary: this public header names no Voxel Plugin type and includes no Voxel header, so the
 * module's dependency on "Voxel" stays private (ShamanVoxel.Build.cs). Voxel objects are held through engine
 * base types and cast in the .cpp.
 */
UCLASS(Config=Game)
class SHAMANVOXEL_API UVoxelPluginTerrainBackend : public UAnalyticPlanetTerrainBackend
{
	GENERATED_BODY()
public:
	/** Vertex-colour material for the RGB material config (falls back to the engine default material). */
	UPROPERTY(Config, EditAnywhere, Category="Voxel") FSoftObjectPath VoxelMaterialPath = FSoftObjectPath(TEXT("/Voxel/Examples/Materials/RGB/M_VoxelMaterial_Colors.M_VoxelMaterial_Colors"));
	/** Extra room (uu) above the highest possible ground for future Raise edits; the voxel world cannot grow later. */
	UPROPERTY(Config, EditAnywhere, Category="Voxel") float EditHeadroom = 4000.f;
	/** Full-resolution LOD around the player pawn (uu). */
	UPROPERTY(Config, EditAnywhere, Category="Voxel") float InvokerLODRange = 3000.f;
	/** Full-resolution collision around the player pawn (uu). Elsewhere visible-chunk collision is used. */
	UPROPERTY(Config, EditAnywhere, Category="Voxel") float InvokerCollisionRange = 5000.f;
	/** Collision on visible chunks up to this LOD (covers units away from the player). */
	UPROPERTY(Config, EditAnywhere, Category="Voxel") int32 VisibleChunksCollisionsMaxLOD = 3;

	virtual bool InitializeBackend(UWorld* World, const FPlanetSettings& InSettings) override;
	virtual void ShutdownBackend() override;
	virtual FName GetBackendName() const override { return TEXT("VoxelPluginFree"); }
	virtual void TickBackend(float DeltaSeconds) override;
	virtual bool IsUpdatePending() const override;
	virtual FString GetStatsString() const override;

	/** The spawned AVoxelWorld, as a plain actor (debug/inspection only). */
	AActor* GetVoxelWorldActor() const { return VoxelWorldActor; }

protected:
	virtual void OnHeightFieldEdited(const FTerrainModificationResult& Result) override;
	virtual void OnHeightFieldReset() override;
	/** Voxel worker threads read the height field at any time: never reuse edit storage. */
	virtual bool CanReclaimEditStorage() const override { return false; }

private:
	void RemeshBox(const FBox& WorldBox);
	void UpdatePlayerInvoker();

	// Voxel objects behind engine base types (see the boundary note above): AVoxelWorld,
	// UShamanPlanetVoxelGenerator (private class) and UVoxelSimpleInvokerComponent.
	UPROPERTY() AActor* VoxelWorldActor = nullptr;
	UPROPERTY() UObject* GeneratorObject = nullptr;
	UPROPERTY() UActorComponent* PlayerInvokerComponent = nullptr;
	TWeakObjectPtr<APawn> InvokerPawn;

	int32 RenderOctreeDepth = 0;
	double CreateStartTime = 0.0;
	float InitialMeshMs = -1.f; // CreateWorld -> first time no mesh work pending
	int32 RemeshCount = 0;
};
