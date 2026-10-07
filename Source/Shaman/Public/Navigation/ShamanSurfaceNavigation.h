#pragma once
#include "CoreMinimal.h"

class UShamanTerrainSubsystem;

/** Result of a surface path query. Points are world-space ground positions, last one is the reachable end. */
struct SHAMAN_API FShamanSurfacePath
{
	bool bValid = false;
	/** True when the goal itself is not reachable and Points end before it. */
	bool bPartial = false;
	TArray<FVector> Points;
};

/**
 * Navigation extension point for planets (UE4's Recast navmesh is Z-up and cannot cover a sphere).
 * AI asks this for a path; a real implementation (e.g. a graph over the cube-sphere / voxel surface) replaces
 * the prototype without touching AI code.
 */
class SHAMAN_API IShamanSurfacePathfinder
{
public:
	virtual ~IShamanSurfacePathfinder() {}
	virtual FName GetPathfinderName() const = 0;
	virtual FShamanSurfacePath FindPath(const UShamanTerrainSubsystem& Terrain, const FVector& Start, const FVector& Goal) const = 0;
};

/**
 * PROTOTYPE ONLY - not real pathfinding. Follows the great circle from Start to Goal, sampling walkability every
 * SampleSpacing uu, and stops before the first unwalkable sample (deep water, cliff): a partial path. It never
 * routes around obstacles. Enough for a Brave following the Shaman across open land.
 */
class SHAMAN_API FGreatCirclePathfinder : public IShamanSurfacePathfinder
{
public:
	float SampleSpacing = 200.f;
	float MaxPathLength = 30000.f;
	virtual FName GetPathfinderName() const override { return TEXT("GreatCirclePrototype"); }
	virtual FShamanSurfacePath FindPath(const UShamanTerrainSubsystem& Terrain, const FVector& Start, const FVector& Goal) const override;
};

/** Active pathfinder (defaults to FGreatCirclePathfinder). */
struct SHAMAN_API FShamanSurfaceNavigation
{
	static const IShamanSurfacePathfinder& Get();
	static void Set(TSharedPtr<IShamanSurfacePathfinder> Pathfinder);
};
