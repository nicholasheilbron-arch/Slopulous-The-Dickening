#include "ShamanPlanetVoxelGenerator.h"
#include "Terrain/PlanetHeightField.h"
#include "VoxelItemStack.h"

// VOXEL-SPECIFIC: adapts FPlanetHeightField (SHAMAN) to Voxel Plugin's generator interface.

UShamanPlanetVoxelGenerator::UShamanPlanetVoxelGenerator()
{
	// Indexed by ETerrainMaterial: Seabed, Sand, Grass, Rock, Snow, Scorched, Swamp, Custom
	MaterialColors = {
		FLinearColor(0.10f, 0.09f, 0.07f), FLinearColor(0.55f, 0.47f, 0.27f), FLinearColor(0.12f, 0.27f, 0.06f),
		FLinearColor(0.20f, 0.19f, 0.17f), FLinearColor(0.90f, 0.90f, 0.93f), FLinearColor(0.03f, 0.02f, 0.02f),
		FLinearColor(0.08f, 0.11f, 0.05f), FLinearColor(0.80f, 0.00f, 0.80f) };
}

TVoxelSharedRef<FVoxelGeneratorInstance> UShamanPlanetVoxelGenerator::GetInstance()
{
	return MakeVoxelShared<FShamanPlanetVoxelGeneratorInstance>(*this);
}

FShamanPlanetVoxelGeneratorInstance::FShamanPlanetVoxelGeneratorInstance(const UShamanPlanetVoxelGenerator& Object)
	: Super(&Object)
	, HeightField(Object.HeightField)
{
	for (int32 i = 0; i < 8; ++i)
		Materials[i] = FVoxelMaterial::CreateFromColor(Object.MaterialColors.IsValidIndex(i) ? Object.MaterialColors[i] : FLinearColor::Gray);
}

void FShamanPlanetVoxelGeneratorInstance::Init(const FVoxelGeneratorInit& InitStruct)
{
	VoxelSize = FMath::Max(InitStruct.VoxelSize, 1.f);
}

v_flt FShamanPlanetVoxelGeneratorInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	if (!HeightField.IsValid()) return 1; // empty
	const FVector P((float)X * VoxelSize, (float)Y * VoxelSize, (float)Z * VoxelSize);
	// Positive = empty, negative = solid. Small offset avoids exact zeros (the plugin handles them poorly).
	return (v_flt)(HeightField->GetSignedDistance(P) / (VoxelSize * ValueScaleVoxels) + 0.001f);
}

FVoxelMaterial FShamanPlanetVoxelGeneratorInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	if (!HeightField.IsValid()) return Materials[(int32)ETerrainMaterial::Custom];
	const FVector Dir = FVector((float)X, (float)Y, (float)Z).GetSafeNormal();
	if (Dir.IsZero()) return Materials[(int32)ETerrainMaterial::Rock];
	const ETerrainMaterial M = HeightField->GetMaterialFast(Dir, HeightField->GetHeight(Dir));
	return Materials[FMath::Clamp((int32)M, 0, 7)];
}

TVoxelRange<v_flt> FShamanPlanetVoxelGeneratorInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	if (!HeightField.IsValid()) return TVoxelRange<v_flt>::Infinite();
	// Distance range of the box from the planet centre (voxel origin)...
	const FVector Min = FVector(Bounds.Min) * VoxelSize, Max = FVector(Bounds.Max) * VoxelSize;
	const FVector Closest(FMath::Clamp(0.f, Min.X, Max.X), FMath::Clamp(0.f, Min.Y, Max.Y), FMath::Clamp(0.f, Min.Z, Max.Z));
	const FVector Farthest(FMath::Max(FMath::Abs(Min.X), FMath::Abs(Max.X)), FMath::Max(FMath::Abs(Min.Y), FMath::Abs(Max.Y)), FMath::Max(FMath::Abs(Min.Z), FMath::Abs(Max.Z)));
	const float RMin = Closest.Size(), RMax = Farthest.Size();
	// ...minus the possible ground radius (bounds include every published edit).
	const float Radius = HeightField->GetSettings().Radius;
	const float SurfMin = Radius + HeightField->GetMinHeightBound(), SurfMax = Radius + HeightField->GetMaxHeightBound();
	const float Scale = VoxelSize * ValueScaleVoxels;
	return TVoxelRange<v_flt>((v_flt)((RMin - SurfMax) / Scale + 0.001f), (v_flt)((RMax - SurfMin) / Scale + 0.001f));
}

FVector FShamanPlanetVoxelGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	const FVector P((float)X, (float)Y, (float)Z);
	return P.SizeSquared() > KINDA_SMALL_NUMBER ? P.GetSafeNormal() : FVector::UpVector;
}
