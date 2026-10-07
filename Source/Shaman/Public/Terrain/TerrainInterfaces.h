#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Terrain/TerrainTypes.h"
#include "Terrain/PlanetFrame.h"
#include "TerrainInterfaces.generated.h"

/**
 * SHAMAN's gameplay-facing terrain contracts. Gameplay code (movement, AI, spells, buildings, water) talks to these,
 * never to a terrain implementation (Voxel Plugin, heightmap, flat test world...). UShamanTerrainSubsystem
 * implements all three; backends sit behind it (see UShamanTerrainBackend).
 * Native C++ only (not implementable in Blueprint); Blueprint uses the subsystem's UFUNCTIONs.
 */

DECLARE_MULTICAST_DELEGATE_OneParam(FOnShamanTerrainChangedNative, const FTerrainChangeEvent& /*Event*/);

UINTERFACE(meta=(CannotImplementInterfaceInBlueprint))
class SHAMAN_API UTerrainWorld : public UInterface { GENERATED_BODY() };

/** Planet-level facts. "Up" is always away from the planet centre. */
class SHAMAN_API ITerrainWorld
{
	GENERATED_BODY()
public:
	virtual FVector GetPlanetCenter() const = 0;
	/** Base radius (heights are measured from here). */
	virtual float GetPlanetRadius() const = 0;
	/** Absolute radius of the sea surface. */
	virtual float GetWaterLevel() const = 0;
	/** All radial geometry helpers (up, tangent basis, surface distance...). */
	virtual FPlanetFrame GetPlanetFrame() const = 0;
};

UINTERFACE(meta=(CannotImplementInterfaceInBlueprint))
class SHAMAN_API UTerrainQuery : public UInterface { GENERATED_BODY() };

/** Read-only terrain queries. All locations are world space. */
class SHAMAN_API ITerrainQuery
{
	GENERATED_BODY()
public:
	/** Ground under/over WorldLocation (radially). */
	virtual FTerrainSample QueryTerrain(const FVector& WorldLocation) const = 0;
	/** Ground along a unit direction from the planet centre. */
	virtual FTerrainSample QueryTerrainDirection(const FVector& Direction) const = 0;
	virtual FVector GetSurfaceLocation(const FVector& Direction) const = 0;
	virtual FVector GetSurfaceNormal(const FVector& WorldLocation) const = 0;
	/** Ground height above the planet base radius under WorldLocation. */
	virtual float GetTerrainHeight(const FVector& WorldLocation) const = 0;
	virtual ETerrainMaterial GetTerrainMaterial(const FVector& WorldLocation) const = 0;
	/** Ground under WorldLocation is walkable (slope ok, not deeper than fordable water). */
	virtual bool IsWalkable(const FVector& WorldLocation) const = 0;
	/** WorldLocation itself (e.g. a unit's feet) is below the sea surface. */
	virtual bool IsUnderwater(const FVector& WorldLocation) const = 0;
	/** Depth of WorldLocation below the sea surface (<= 0 when above water). */
	virtual float GetWaterDepthAt(const FVector& WorldLocation) const = 0;
	virtual bool Raycast(const FVector& Start, const FVector& End, FTerrainRaycastHit& OutHit) const = 0;
};

UINTERFACE(meta=(CannotImplementInterfaceInBlueprint))
class SHAMAN_API UTerrainModification : public UInterface { GENERATED_BODY() };

/** Terrain changes go through here: validation, protected regions, change notification. */
class SHAMAN_API ITerrainModification
{
	GENERATED_BODY()
public:
	virtual bool SupportsOperation(ETerrainOp Op) const = 0;
	virtual FTerrainModificationResult ModifyTerrain(const FTerrainModification& Request) = 0;

	/** Returns the new region id. */
	virtual int32 RegisterProtectedRegion(const FVector& Center, float Radius, FName Tag) = 0;
	virtual bool UnregisterProtectedRegion(int32 RegionId) = 0;
	/** First protected region a request would touch, or -1. */
	virtual int32 FindBlockingRegion(const FTerrainModification& Request) const = 0;

	/** Fired right after terrain data changed (queries already reflect the edit). */
	virtual FOnShamanTerrainChangedNative& OnTerrainChangedNative() = 0;
	/** Fired when the backend finished rebuilding mesh/collision for an edit (UpdateMs filled in). */
	virtual FOnShamanTerrainChangedNative& OnTerrainUpdateCompletedNative() = 0;

	/** Ordered list of applied edits (save data together with FPlanetSettings). */
	virtual void GetEditLog(TArray<FTerrainModification>& OutEdits) const = 0;
	/** Clears all edits and re-applies Edits in order (load). Protection is not re-checked. */
	virtual bool ReplayEditLog(const TArray<FTerrainModification>& Edits) = 0;
};
