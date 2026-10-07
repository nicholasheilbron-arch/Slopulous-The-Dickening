#pragma once
#include "CoreMinimal.h"
#include "Terrain/TerrainTypes.h"
#include "Terrain/PlanetFrame.h"

class FPlanetHeightField;

/**
 * Backend-independent query helpers over an FPlanetHeightField: terrain samples, raycasts, protected-region
 * overlap. Shared by every heightfield-style backend so results are identical whichever one is active.
 * Engine-independent (verified outside Unreal by Tools/TerrainHarness).
 */
struct SHAMAN_API FPlanetTerrainQueries
{
	/** Sample the ground under/over a world location. */
	static FTerrainSample Sample(const FPlanetHeightField& HF, const FVector& WorldLocation);
	/** Sample the ground along a unit direction from the planet centre. */
	static FTerrainSample SampleDirection(const FPlanetHeightField& HF, const FVector& Dir);
	/** Segment vs ground (sphere tracing + bisection). */
	static bool Raycast(const FPlanetHeightField& HF, const FVector& Start, const FVector& End, FTerrainRaycastHit& OutHit);
	/** True if the modification's footprint touches the region (surface distance). */
	static bool Overlaps(const FPlanetFrame& Frame, const FTerrainProtectedRegion& Region, const FTerrainModification& Mod);
	/** Unit directions covering a modification's footprint: centre + 3 rings of 12 (edge ring at exactly Radius). */
	static void GetFootprintDirections(const FPlanetFrame& Frame, const FTerrainModification& Mod, TArray<FVector, TInlineAllocator<40>>& OutDirs);
	/** Conservative world-space radius of what a modification can change (horizontal + Raise/Lower vertical). */
	static float GetAffectedRadius(const FTerrainModification& Mod);
};
