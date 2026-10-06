#pragma once
#include "CoreMinimal.h"
#include "WorldGenTypes.generated.h"

/** All five seeds are stored so any world (or bug) can be reproduced exactly.
 *  If only WorldSeed is set, the other four are derived from it (see FShamanWorldGenerator::DeriveSeeds). */
USTRUCT(BlueprintType)
struct SHAMAN_API FWorldSeeds
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 WorldSeed = 1337;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 BiomeSeed = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ResourceSeed = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TribeSeed = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TerrainSeed = 0;

	bool operator==(const FWorldSeeds& O) const
	{
		return WorldSeed == O.WorldSeed && BiomeSeed == O.BiomeSeed && ResourceSeed == O.ResourceSeed
			&& TribeSeed == O.TribeSeed && TerrainSeed == O.TerrainSeed;
	}
};

/** Designer-tunable generation rules. Distances are Unreal units (1 uu = 1 cm). Sea level is always Z = 0. */
USTRUCT(BlueprintType)
struct SHAMAN_API FWorldGenConfig
{
	GENERATED_BODY()

	// --- Terrain grid ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") int32 GridVertices = 129;    // per side; map is (GridVertices-1)*CellSize wide
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float CellSize = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float SeabedDepth = -600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float BaseLandHeight = 260.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float HillHeight = 560.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float MountainHeight = 2200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float NoiseFrequency = 3.f;      // noise cycles across the map
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float IslandFalloffStart = 0.62f; // normalized radius where coast begins
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float IslandFalloffEnd = 0.98f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float MountainMaskStart = 0.05f;  // mountain-region noise thresholds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float MountainMaskEnd = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float LakeThreshold = 0.22f;      // higher = fewer lakes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float LakeDepth = -350.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float RiverWidth = 0.03f;        // in noise units; 0 disables rivers
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float RiverDepth = -35.f;        // shallower than FordableDepth = wadeable
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float MaxWalkableStep = 140.f;   // max height change between neighbouring vertices
	/** Water shallower than this is wadeable: no drowning, navigable. Shared by the generator, water nav modifier and drowning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain") float FordableDepth = 60.f;

	// --- Start area constraints ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float StartFlatRadius = 700.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float StartMaxHeightDelta = 220.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float StartMinHeight = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float StartMaxHeight = 900.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float StartAreaRadius = 4000.f;    // wood/food/stone/ore must be inside
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float WaterSearchRadius = 3500.f;  // water must be inside
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") bool bCarvePondIfNoWater = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float WildmenDistMin = 2200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float WildmenDistMax = 4500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float EnemyDistMin = 7000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float EnemyDistMax = 11500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float SettlementClearRadius = 900.f; // no resources this close to a settlement centre
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Start") float MinObjectSpacing = 220.f;

	// --- Populations ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Population") int32 PlayerBraves = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Population") int32 EnemyBraves = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Population") int32 EnemyWarriors = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Population") int32 Wildmen = 5;

	// --- Resources ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resources") int32 StartTrees = 24;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resources") int32 StartFood = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resources") int32 StartStone = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resources") int32 StartOre = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resources") int32 EnemyTrees = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resources") int32 EnemyFood = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resources") int32 WorldTrees = 220;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resources") int32 WorldFood = 14;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resources") int32 WorldStone = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resources") int32 WorldOre = 6;

	// --- Retry budget ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation") int32 MaxTerrainAttempts = 6;
};

UENUM(BlueprintType)
enum class EWorldMarkerType : uint8
{
	Tree, Food, Stone, Ore,
	Wildman,
	Shaman, Brave, Warrior,
	House, Campfire, ReincarnationCircle,
	MAX UMETA(Hidden)
};

/** One thing the generator wants spawned. TribeIndex: 0 = player, 1 = enemy, -1 = none/wild. */
USTRUCT(BlueprintType)
struct SHAMAN_API FWorldMarker
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EWorldMarkerType Type = EWorldMarkerType::Tree;
	UPROPERTY(BlueprintReadOnly) FVector2D Location = FVector2D(0.f, 0.f);
	UPROPERTY(BlueprintReadOnly) float Yaw = 0.f;
	UPROPERTY(BlueprintReadOnly) int32 TribeIndex = -1;

	bool operator==(const FWorldMarker& O) const
	{
		return Type == O.Type && Location == O.Location && Yaw == O.Yaw && TribeIndex == O.TribeIndex;
	}
};

/** Bit flags for start-area validation failures (0 = valid). */
namespace EStartAreaFail
{
	enum Type : int32
	{
		None = 0,
		NoStartSite = 1 << 0, NoWater = 1 << 1, NoWood = 1 << 2, NoFood = 1 << 3, NoStone = 1 << 4, NoOre = 1 << 5,
		NoWildmen = 1 << 6, NoEnemy = 1 << 7, NoEnemyShaman = 1 << 8, EnemyUnreachable = 1 << 9, PlayerSettlementIncomplete = 1 << 10,
		WildmenUnreachable = 1 << 11
	};
}

/** Pure-data result of generation. Heights are indexed [X * GridVertices + Y]; vertex (0,0) is at (-HalfSize, -HalfSize). */
USTRUCT(BlueprintType)
struct SHAMAN_API FWorldLayout
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FWorldSeeds Seeds;
	UPROPERTY(BlueprintReadOnly) int32 GridVertices = 0;
	UPROPERTY(BlueprintReadOnly) float CellSize = 0.f;
	UPROPERTY(BlueprintReadOnly) float HalfSize = 0.f;
	UPROPERTY(BlueprintReadOnly) float MinHeight = 0.f;
	UPROPERTY(BlueprintReadOnly) float MaxHeight = 0.f;
	UPROPERTY() TArray<float> Heights;
	UPROPERTY(BlueprintReadOnly) TArray<FWorldMarker> Markers;
	UPROPERTY(BlueprintReadOnly) FVector2D PlayerStart = FVector2D(0.f, 0.f);
	UPROPERTY(BlueprintReadOnly) FVector2D EnemyStart = FVector2D(0.f, 0.f);
	UPROPERTY(BlueprintReadOnly) FVector2D WildmenCamp = FVector2D(0.f, 0.f);
	UPROPERTY(BlueprintReadOnly) bool bPondCarved = false;
	UPROPERTY(BlueprintReadOnly) bool bValid = false;
	UPROPERTY(BlueprintReadOnly) int32 FailMask = 0;
	UPROPERTY(BlueprintReadOnly) int32 TerrainAttempts = 0;
};
