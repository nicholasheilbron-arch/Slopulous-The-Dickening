#pragma once
#include "CoreMinimal.h"
#include "Terrain/TerrainTypes.h"
#include "Terrain/PlanetFrame.h"
#include <atomic>

/** One applied terrain edit, stored in planet-relative form (direction + angular size). */
struct SHAMAN_API FPlanetEdit
{
	ETerrainOp Op = ETerrainOp::Raise;
	FVector Dir = FVector(0.f, 0.f, 1.f);   // unit direction of the edit centre
	float CosOuter = 1.f;                    // cos(angular radius): quick reject
	float OuterAngle = 0.f;                  // radians
	float InnerAngle = 0.f;                  // radians (full strength inside)
	float Strength = 0.f;
	float TargetHeight = 0.f;
	ETerrainMaterial Material = ETerrainMaterial::Grass;
	FTerrainModification Request;            // original request (save/replay)
};

/**
 * Deterministic spherical height field: base noise from FPlanetSettings + an append-only edit log.
 * This is SHAMAN's terrain "source of truth" for heightfield-style backends (analytic, voxel, future cube-sphere).
 *
 * Thread safety: edits are appended on the game thread into a pre-sized array and published with one atomic
 * [Begin, End) slot range, so worker threads (e.g. voxel meshing) can evaluate heights concurrently without locks.
 * Published slots are never rewritten while readers may exist: ResetEdits(false) just moves Begin to End.
 * ResetEdits(true) also reclaims the storage and must only be used when no other thread is reading.
 * Engine-independent (only CoreMinimal math) so it can be verified outside Unreal.
 */
class SHAMAN_API FPlanetHeightField
{
public:
	static constexpr int32 MaxEdits = 4096;

	explicit FPlanetHeightField(const FPlanetSettings& InSettings);
	FPlanetHeightField(const FPlanetHeightField&) = delete;
	FPlanetHeightField& operator=(const FPlanetHeightField&) = delete;

	const FPlanetSettings& GetSettings() const { return Settings; }
	const FPlanetFrame& GetFrame() const { return Frame; }

	/** Procedural height only (no edits), relative to the base radius. Dir must be unit length. */
	float GetBaseHeight(const FVector& Dir) const;
	/** Height including all published edits. */
	float GetHeight(const FVector& Dir) const;
	/** Absolute distance of the ground from the planet centre. */
	float GetSurfaceRadius(const FVector& Dir) const { return Settings.Radius + GetHeight(Dir); }
	/** World-space ground point under/over Dir. */
	FVector GetSurfacePoint(const FVector& Dir) const { return Frame.GetPointAt(Dir, GetHeight(Dir)); }
	/** Ground normal by finite differences (unit). */
	FVector GetSurfaceNormal(const FVector& Dir) const;
	/** Material at Dir (painted override, else by height/slope). */
	ETerrainMaterial GetMaterial(const FVector& Dir) const;
	ETerrainMaterial GetMaterialFast(const FVector& Dir, float Height) const; // no slope (for voxel meshing)
	/** True if any edit touches Dir. */
	bool IsEdited(const FVector& Dir) const;

	/** Approximate signed distance (uu) of a point relative to the CENTRE (negative = inside the ground). */
	float GetSignedDistance(const FVector& LocalPos) const;

	/** Conservative height bounds over the whole planet including edits. */
	float GetMinHeightBound() const;
	float GetMaxHeightBound() const;

	/** Appends an edit. Returns its index in the current log, or -1 if invalid / out of storage. Game thread only. */
	int32 AddEdit(const FTerrainModification& Mod);
	/** Number of edits in the current log. */
	int32 GetNumEdits() const { const uint64 R = EditRange.load(std::memory_order_acquire); return RangeEnd(R) - RangeBegin(R); }
	/** Edit Index (0..GetNumEdits()-1) of the current log. Game thread only. */
	const FPlanetEdit& GetEdit(int32 Index) const { return Edits[RangeBegin(EditRange.load(std::memory_order_acquire)) + Index]; }
	/** Storage slots still free (every AddEdit uses one; only ResetEdits(true) gives them back). */
	int32 GetRemainingCapacity() const { return MaxEdits - RangeEnd(EditRange.load(std::memory_order_acquire)); }
	/**
	 * Removes all edits. bReclaimStorage=false is safe with concurrent readers (slots are not reused);
	 * bReclaimStorage=true reuses the storage and requires that no other thread is reading.
	 */
	void ResetEdits(bool bReclaimStorage);

	/** Noise primitives (exposed for tests). */
	static float GradientNoise3(const FVector& P, uint32 Seed);
	static float Fbm3(const FVector& P, uint32 Seed, int32 Octaves);

private:
	float ApplyEdits(const FVector& Dir, float Height, int32 Begin, int32 End) const;
	static int32 RangeBegin(uint64 R) { return (int32)(R >> 32); }
	static int32 RangeEnd(uint64 R) { return (int32)(R & 0xffffffffull); }
	static uint64 MakeRange(int32 Begin, int32 End) { return ((uint64)(uint32)Begin << 32) | (uint64)(uint32)End; }
	static float EditWeight(const FPlanetEdit& E, float CosAngle);

	FPlanetSettings Settings;
	FPlanetFrame Frame;
	uint32 SeedContinent = 0, SeedDetail = 0, SeedMountain = 0;
	FVector OffsetContinent, OffsetDetail, OffsetMountain;

	TArray<FPlanetEdit> Edits;               // pre-sized to MaxEdits, never reallocated
	std::atomic<uint64> EditRange{0};       // packed [Begin, End) of the current log, published with release
	std::atomic<float> EditRaiseBound{0.f};  // sum of positive height changes (conservative)
	std::atomic<float> EditLowerBound{0.f};  // sum of negative height changes (conservative)
};
