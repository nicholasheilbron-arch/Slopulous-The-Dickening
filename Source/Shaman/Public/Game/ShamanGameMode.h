#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "World/WorldGenTypes.h"
#include "ShamanGameMode.generated.h"

class UShamanGameData;
class AShamanTerrainActor;
class AShamanWaterActor;
class AShamanUnitBase;
class AShamanCharacter;
class ABuildingActor;
class AResourceNode;

/**
 * Builds the Phase 1 world: runs FShamanWorldGenerator from the seeds, spawns terrain, water, buildings,
 * resources and units from the layout, sets up tribes, and spawns the player Shaman at its settlement.
 * Seed: ?Seed=123 on the URL > GameData Seeds.WorldSeed (0 = random each run).
 */
UCLASS()
class SHAMAN_API AShamanGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AShamanGameMode();

	/** Assign DA_ShamanGameData here (or leave empty to load it from DefaultGameDataPath). */
	UPROPERTY(EditDefaultsOnly, Category="Shaman") UShamanGameData* GameData = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Shaman") TSoftObjectPtr<UShamanGameData> DefaultGameDataPath;
	/** Class for every non-player unit; enemy Shamans use ShamanClass. Blueprint children add meshes/animation. */
	UPROPERTY(EditDefaultsOnly, Category="Shaman") TSubclassOf<AShamanUnitBase> UnitClass;
	UPROPERTY(EditDefaultsOnly, Category="Shaman") TSubclassOf<AShamanCharacter> ShamanClass;

	const UShamanGameData* GetGameData() const { return ActiveData; }
	const FWorldLayout& GetLayout() const { return Layout; }

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Rebuild everything from a seed without restarting the editor session (console: ShamanRegenerate 42). */
	UFUNCTION(Exec) void ShamanRegenerate(int32 Seed);

	/** Tribe simulation (Phase 2.1) report: members, living/available counts, open tasks, settlement anchor. */
	UFUNCTION(Exec) void ShamanTribeReport();
	/**
	 * Developer test of tasks on the player's tribe.
	 *   Create                          Task.Debug at the player (bookkeeping only, no behaviour; Phase 2.1)
	 *   Move | Guard                    Task.MoveTo / Task.Guard at the aim point (Phase 2.2, executed)
	 *   Marker | MoveActor              spawn a debug marker at the aim point / Task.MoveToActor to it (spawns one if none)
	 *   DestroyTarget                   destroy the marker (a running MoveToActor then fails with TargetLost)
	 *   MoveInvalid | MoveUnreachable   MoveTo without a location / far outside the world (both fail on start)
	 *   Assign | Start | Go             assign to the available Brave nearest the player / start / assign + start
	 *   Complete | Fail | Cancel | Info end the task from the console / print task, unit and executor state
	 *   Unavailable | Available         take that Brave out of / back into the worker pool
	 * The last created task is the one Assign/Start/Go/Complete/Fail/Cancel/Info act on.
	 */
	UFUNCTION(Exec) void ShamanTaskTest(const FString& Action);
	/** Developer test: moves the player's Brave nearest to the player into tribe NewTribeId (membership transfer). */
	UFUNCTION(Exec) void ShamanTribeTransfer(int32 NewTribeId);

	/** Per-unit tribe simulation labels (tribe, state, task, priority, target, availability). Debug mode only. */
	void DrawTribeSimDebug() const;

protected:
	/** Builds the world once (flat Phase 1 world here; AShamanPlanetGameMode builds a planet). */
	virtual void EnsureWorldGenerated();
	/** Body of the ShamanRegenerate console command. */
	virtual void RegenerateWorld(int32 Seed);
	void ResolveGameData();
	void SpawnWorld();
	void DestroyWorld();
	FVector GroundPoint(const FVector2D& P) const;
	FTransform UnitTransform(const FWorldMarker& M, float HalfHeight) const;
	AShamanUnitBase* SpawnUnit(TSubclassOf<AShamanUnitBase> Class, FName UnitId, int32 Tribe, const FTransform& T);
	FName UnitIdFor(EWorldMarkerType Type) const;
	FName BuildingIdFor(EWorldMarkerType Type) const;
	FName ResourceIdFor(EWorldMarkerType Type) const;
	void DrawDebug() const;

	UPROPERTY(Transient) UShamanGameData* ActiveData = nullptr;
	UPROPERTY(Transient) AShamanTerrainActor* Terrain = nullptr;
	UPROPERTY(Transient) TArray<AActor*> SpawnedActors;

	FWorldLayout Layout;
	TArray<FVector> MarkerGround;   // ground position per Layout.Markers entry (debug)
	bool bWorldGenerated = false;
	int32 DebugTaskId = INDEX_NONE;                       // ShamanTaskTest's current task
	TWeakObjectPtr<class UTribeMemberComponent> DebugMember; // ShamanTaskTest Unavailable/Available target
	TWeakObjectPtr<AActor> DebugTargetActor;                 // ShamanTaskTest Marker / MoveActor / DestroyTarget
	int32 SeedOverride = 0;
	bool bHasSeedOverride = false;
};
