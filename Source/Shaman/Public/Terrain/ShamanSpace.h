#pragma once
#include "CoreMinimal.h"
#include "Terrain/PlanetFrame.h"

/**
 * Gameplay-facing "which way is up / how far apart" helpers. Every gameplay call site that used world Z
 * (Size2D, GetSafeNormal2D, yaw-only rotations, XY offsets) goes through here, so the same code works on the
 * flat prototype map and on a planet:
 *   - no planet active  -> exactly the previous flat-world math (world +Z up);
 *   - planet active     -> radial up from UShamanTerrainSubsystem's planet frame.
 */
struct SHAMAN_API FShamanSpace
{
	/** Planet frame of Ctx's world; false on flat worlds. */
	static bool GetPlanet(const UObject* Ctx, FPlanetFrame& OutFrame);
	static FVector GetUp(const UObject* Ctx, const FVector& Location);

	/** Ground distance: Dist2D on flat worlds, great-circle distance on planets. */
	static float HorizontalDistance(const UObject* Ctx, const FVector& A, const FVector& B);
	/** Unit direction along the ground from From toward To (GetSafeNormal2D on flat worlds). */
	static FVector HorizontalDirection(const UObject* Ctx, const FVector& From, const FVector& To);
	/** V with its "vertical" part removed at Location, normalised (GetSafeNormal2D on flat worlds). */
	static FVector HorizontalNormal(const UObject* Ctx, const FVector& Location, const FVector& V);
	/** Upright actor rotation at Location facing Forward (yaw-only FRotator on flat worlds). */
	static FRotator UprightRotation(const UObject* Ctx, const FVector& Location, const FVector& Forward);
	/** Origin moved by a ground-plane offset (X/Y on flat worlds; tangent plane + re-projected to the same radius on planets). */
	static FVector OffsetAlongGround(const UObject* Ctx, const FVector& Origin, const FVector2D& Offset);
};
