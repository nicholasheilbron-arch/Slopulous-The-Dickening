#pragma once
#include "CoreMinimal.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "Terrain/TerrainTypes.h"
#include "ShamanPlanetVoxelGenerator.generated.h"

class FPlanetHeightField;

/**
 * Voxel Plugin generator that turns SHAMAN's FPlanetHeightField into a signed-distance volume.
 * VOXEL-SPECIFIC (ShamanVoxel module). The voxel world must be placed at the planet centre with identity rotation
 * and scale, so voxel coordinates * VoxelSize == position relative to the planet centre.
 * Transient: created at runtime by UVoxelPluginTerrainBackend, never saved as an asset.
 * PRIVATE to ShamanVoxel: it derives from UVoxelGenerator, so it must not appear in any public header
 * (keeps the "Voxel" module a private dependency).
 */
UCLASS(Transient)
class UShamanPlanetVoxelGenerator : public UVoxelGenerator
{
	GENERATED_BODY()
public:
	/** Height field shared with the backend (read concurrently by voxel worker threads). */
	TSharedPtr<FPlanetHeightField, ESPMode::ThreadSafe> HeightField;

	/** Vertex colours indexed by ETerrainMaterial (RGB material config). Placeholder look; art is out of scope. */
	UPROPERTY(VisibleAnywhere, Category="Planet") TArray<FLinearColor> MaterialColors;

	UShamanPlanetVoxelGenerator();
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
};

class FShamanPlanetVoxelGeneratorInstance
	: public TVoxelGeneratorInstanceHelper<FShamanPlanetVoxelGeneratorInstance, UShamanPlanetVoxelGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<FShamanPlanetVoxelGeneratorInstance, UShamanPlanetVoxelGenerator>;

	explicit FShamanPlanetVoxelGeneratorInstance(const UShamanPlanetVoxelGenerator& Object);

	virtual void Init(const FVoxelGeneratorInit& InitStruct) override;

	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;
	/** Radial up: the planet centre is the voxel origin. */
	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;

private:
	/** Voxel values are clamped to [-1, 1]; spreading the distance over 2 voxels keeps gradients/normals smooth. */
	static constexpr float ValueScaleVoxels = 2.f;

	TSharedPtr<FPlanetHeightField, ESPMode::ThreadSafe> HeightField;
	float VoxelSize = 100.f;
	FVoxelMaterial Materials[8];
};
