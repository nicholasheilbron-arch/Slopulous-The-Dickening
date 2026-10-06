#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "World/WorldGenTypes.h"
#include "Tribes/TribeDefinitions.h"
#include "ShamanGameData.generated.h"

class UDataTable;
class UHitReactionConfig;
class ASpellProjectile;
class UMaterialInterface;

/**
 * Single entry point for all Phase 1 tuning (DA_ShamanGameData). Every balance value lives here or in the
 * data tables it references. If the asset does not exist, AShamanGameMode builds a transient one and loads
 * the tables from their default paths (/Game/Data/DT_Spells, DT_Units, DT_Buildings, DT_Resources).
 */
UCLASS(BlueprintType)
class SHAMAN_API UShamanGameData : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UShamanGameData();

	/** Game data for the current world (from AShamanGameMode), or the class defaults if none. Never null. */
	static const UShamanGameData* Get(const UObject* WorldContext);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tables") UDataTable* SpellTable = nullptr;     // FSpellRow
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tables") UDataTable* UnitTable = nullptr;      // FUnitRow
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tables") UDataTable* BuildingTable = nullptr;  // FBuildingRow
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tables") UDataTable* ResourceTable = nullptr;  // FResourceRow

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") UHitReactionConfig* HitReaction = nullptr;

	/** WorldSeed 0 = pick a random seed each run (it is logged and shown on the HUD). Override with ?Seed=123. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World") FWorldSeeds Seeds;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World") FWorldGenConfig WorldGen;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World") UMaterialInterface* TerrainMaterial = nullptr; // null = vertex colours
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World") UMaterialInterface* WaterMaterial = nullptr;   // null = tinted basic shape
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World") FLinearColor WaterColor = FLinearColor(0.05f, 0.25f, 0.55f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribes") TArray<FTribeDefinition> Tribes;  // index = tribe id
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribes") FReincarnationConfig Reincarnation;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribes") FLinearColor WildmenColor = FLinearColor(0.55f, 0.42f, 0.28f, 1.f);

	/** Spells every Shaman starts with (spec: TAKA / Blast only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spells") TArray<FName> StartingSpells;
	/** Projectile class per spell EffectId. Defaults FireBlast -> ADefaultSpellProjectile (placeholder visual). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spells") TMap<FName, TSubclassOf<ASpellProjectile>> ProjectileClasses;

	// Row ids used when spawning generator markers.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName ShamanUnitId = TEXT("Shaman");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName BraveUnitId = TEXT("Brave");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName WarriorUnitId = TEXT("Warrior");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName WildmanUnitId = TEXT("Wildman");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName HouseBuildingId = TEXT("House");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName CampfireBuildingId = TEXT("Campfire");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName CircleBuildingId = TEXT("ReincarnationCircle");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName TreeResourceId = TEXT("Tree");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName FoodResourceId = TEXT("BerryBush");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName StoneResourceId = TEXT("Stone");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning") FName OreResourceId = TEXT("Ore");

	/** Interaction / command ranges for the Shaman. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shaman") float InteractRange = 350.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shaman") float RallyRadius = 3000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shaman") float FollowDistance = 350.f;

	const FTribeDefinition* GetTribe(int32 TribeId) const { return Tribes.IsValidIndex(TribeId) ? &Tribes[TribeId] : nullptr; }
};
