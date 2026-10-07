#pragma once
#include "CoreMinimal.h"
#include "TerrainTypes.generated.h"

// SHAMAN terrain vocabulary. Backend-independent: nothing here knows about Voxel Plugin.
// Units are Unreal units (cm). "Height" is always measured radially from the planet's base radius.

/** Surface material classes. Backends map these to their own material representation. */
UENUM(BlueprintType)
enum class ETerrainMaterial : uint8
{
	Seabed, Sand, Grass, Rock, Snow,
	Scorched,   // future: Firestorm / Volcano
	Swamp,      // future: Swamp hazard visual
	Custom
};

/** Bit flags carried by FTerrainSample::Flags. */
namespace ETerrainFlags
{
	enum Type : uint8
	{
		None       = 0,
		Underwater = 1 << 0,
		Protected  = 1 << 1,
		Edited     = 1 << 2,
		Steep      = 1 << 3,
	};
}

/** Terrain operations. Not every backend supports every op (see ITerrainModifier::SupportsOperation). */
UENUM(BlueprintType)
enum class ETerrainOp : uint8
{
	Raise,      // add Strength (uu) with radial falloff
	Lower,      // subtract Strength (uu) with radial falloff
	Flatten,    // blend toward TargetHeight; Strength 0..1 is blend amount
	Smooth,     // future
	Paint,      // set material inside the radius
	RaisePath   // future: Land Bridge style raised path from Center to PathEnd
};

UENUM(BlueprintType)
enum class ETerrainModifyStatus : uint8
{
	Applied,
	RejectedProtected,
	RejectedUnsupported,
	RejectedInvalid,
	RejectedNoBackend,
	RejectedCapacity
};

/** Deterministic planet description. Same settings => same planet. */
USTRUCT(BlueprintType)
struct SHAMAN_API FPlanetSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") int32 Seed = 1337;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") FVector Center = FVector(0.f, 0.f, 0.f);
	/** Base radius (uu). Sea level and all heights are measured from here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float Radius = 20000.f;
	/** Sea level relative to Radius (uu). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float SeaLevelOffset = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float SeabedDepth = 1400.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float LandHeight = 900.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float MountainHeight = 2200.f;
	/** Noise cycles around the planet for continents / hills / mountains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float ContinentFrequency = 1.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float DetailFrequency = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float MountainFrequency = 2.4f;
	/** Shifts the land/sea balance (+ = more land). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float LandBias = 0.04f;
	/** Water shallower than this is wadeable (no drowning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float FordableDepth = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float MaxWalkableSlopeDeg = 50.f;
	/** Backend resolution hint (voxel size for voxel backends). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float VoxelSize = 100.f;
};

/** Everything gameplay usually needs to know about the terrain at one place. */
USTRUCT(BlueprintType)
struct SHAMAN_API FTerrainSample
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Terrain") bool bValid = false;
	/** Surface point (on the ground, not at sea level). */
	UPROPERTY(BlueprintReadOnly, Category="Terrain") FVector Location = FVector(0.f, 0.f, 0.f);
	UPROPERTY(BlueprintReadOnly, Category="Terrain") FVector Normal = FVector(0.f, 0.f, 1.f);
	/** Radial up (away from planet centre) at Location. */
	UPROPERTY(BlueprintReadOnly, Category="Terrain") FVector Up = FVector(0.f, 0.f, 1.f);
	/** Ground height relative to the planet base radius. */
	UPROPERTY(BlueprintReadOnly, Category="Terrain") float Height = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Terrain") ETerrainMaterial Material = ETerrainMaterial::Grass;
	UPROPERTY(BlueprintReadOnly, Category="Terrain") bool bWalkable = false;
	UPROPERTY(BlueprintReadOnly, Category="Terrain") bool bUnderwater = false;
	/** Sea depth above the ground (0 on dry land). */
	UPROPERTY(BlueprintReadOnly, Category="Terrain") float WaterDepth = 0.f;
	/** ETerrainFlags bit mask. */
	UPROPERTY(BlueprintReadOnly, Category="Terrain") uint8 Flags = 0;
};

/** A terrain change request. Gameplay builds one of these; it never touches backend APIs. */
USTRUCT(BlueprintType)
struct SHAMAN_API FTerrainModification
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") ETerrainOp Op = ETerrainOp::Raise;
	/** World location; only its direction from the planet centre matters for heightfield-style ops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") FVector Center = FVector(0.f, 0.f, 0.f);
	/** Surface radius of the affected area (uu). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float Radius = 500.f;
	/** Raise/Lower: uu. Flatten: blend 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float Strength = 200.f;
	/** Fraction of Radius that receives full strength before the falloff starts (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float InnerFraction = 0.4f;
	/** Flatten target, relative to the planet base radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float TargetHeight = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") ETerrainMaterial Material = ETerrainMaterial::Grass;
	/** RaisePath end point (future). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") FVector PathEnd = FVector(0.f, 0.f, 0.f);
	/** Who asked (spell id, debug command...). For logs, saves and events. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") FName Source;
};

USTRUCT(BlueprintType)
struct SHAMAN_API FTerrainModificationResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Terrain") ETerrainModifyStatus Status = ETerrainModifyStatus::RejectedNoBackend;
	UPROPERTY(BlueprintReadOnly, Category="Terrain") int32 EditIndex = -1;
	/** Id of the protected region that blocked the request, or -1. */
	UPROPERTY(BlueprintReadOnly, Category="Terrain") int32 BlockingRegionId = -1;
	/** Synchronous time spent applying the change (ms). Collision/mesh updates may continue asynchronously. */
	UPROPERTY(BlueprintReadOnly, Category="Terrain") float ApplyMs = 0.f;
	/** Conservative world-space bounds of the change (sphere). */
	UPROPERTY(BlueprintReadOnly, Category="Terrain") FVector BoundsCenter = FVector(0.f, 0.f, 0.f);
	UPROPERTY(BlueprintReadOnly, Category="Terrain") float BoundsRadius = 0.f;

	bool WasApplied() const { return Status == ETerrainModifyStatus::Applied; }
};

/** Area that terrain modification must never touch (Reincarnation Circles, scripted sites). */
USTRUCT(BlueprintType)
struct SHAMAN_API FTerrainProtectedRegion
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Terrain") int32 Id = -1;
	UPROPERTY(BlueprintReadOnly, Category="Terrain") FVector Center = FVector(0.f, 0.f, 0.f);
	UPROPERTY(BlueprintReadOnly, Category="Terrain") float Radius = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Terrain") FName Tag;
};

/** Broadcast after terrain data changed (and again when the backend finished rebuilding mesh/collision). */
USTRUCT(BlueprintType)
struct SHAMAN_API FTerrainChangeEvent
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Terrain") FVector Center = FVector(0.f, 0.f, 0.f);
	UPROPERTY(BlueprintReadOnly, Category="Terrain") float Radius = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Terrain") int32 EditIndex = -1;
	UPROPERTY(BlueprintReadOnly, Category="Terrain") ETerrainOp Op = ETerrainOp::Raise;
	UPROPERTY(BlueprintReadOnly, Category="Terrain") FName Source;
	/** ms from the request until the backend reported mesh/collision up to date (completion event only). */
	UPROPERTY(BlueprintReadOnly, Category="Terrain") float UpdateMs = 0.f;
};

USTRUCT(BlueprintType)
struct SHAMAN_API FTerrainRaycastHit
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Terrain") bool bHit = false;
	UPROPERTY(BlueprintReadOnly, Category="Terrain") FVector Location = FVector(0.f, 0.f, 0.f);
	UPROPERTY(BlueprintReadOnly, Category="Terrain") FVector Normal = FVector(0.f, 0.f, 1.f);
	UPROPERTY(BlueprintReadOnly, Category="Terrain") float Distance = 0.f;
};
