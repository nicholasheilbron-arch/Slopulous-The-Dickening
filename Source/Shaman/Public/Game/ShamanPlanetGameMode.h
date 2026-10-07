#pragma once
#include "CoreMinimal.h"
#include "Game/ShamanGameMode.h"
#include "Terrain/TerrainTypes.h"
#include "ShamanPlanetGameMode.generated.h"

class UShamanTerrainBackend;
class ABuildingActor;
class ASurfaceProbeActor;
class APlanetWaterActor;

/**
 * Spherical-terrain spike game mode (milestone: UE4 spherical terrain foundation).
 * Builds a seeded planet through UShamanTerrainSubsystem (Voxel Plugin backend by default, analytic fallback),
 * then places the player Shaman, a Reincarnation Circle (protected terrain region), a Brave that follows the
 * Shaman, a few Wildmen (Convert test) and a normal-aligned surface object on a land site.
 *
 * Use: World Settings > GameMode Override = ShamanPlanetGameMode, or open a map with
 *      ?game=/Script/Shaman.ShamanPlanetGameMode   (options: ?Seed=123  ?TerrainBackend=Analytic)
 * Console: ShamanTerrainRaise / Lower / Flatten [radius] [strength], ShamanTerrainProtectedTest, ShamanTerrainReport,
 *          ShamanTerrainSaveReload, ShamanTerrainBenchmark [count], ShamanSpawnStress [count], ShamanTerrainProbe
 */
UCLASS(Config=Game)
class SHAMAN_API AShamanPlanetGameMode : public AShamanGameMode
{
	GENERATED_BODY()
public:
	AShamanPlanetGameMode();

	UPROPERTY(EditDefaultsOnly, Config, Category="Planet") FPlanetSettings Planet;
	/** Backend class (soft, so Shaman does not depend on the ShamanVoxel module). Falls back to the analytic backend. */
	UPROPERTY(EditDefaultsOnly, Config, Category="Planet") FSoftClassPath TerrainBackendClass;
	UPROPERTY(EditDefaultsOnly, Category="Planet") float CircleProtectRadius = 700.f;
	UPROPERTY(EditDefaultsOnly, Category="Planet") int32 NumWildmen = 4;
	/** Spawn a directional + sky light when the level has no lights (empty test maps). */
	UPROPERTY(EditDefaultsOnly, Category="Planet") bool bSpawnLightsIfMissing = true;

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(Exec) void ShamanTerrainRaise(float Radius, float Strength);
	UFUNCTION(Exec) void ShamanTerrainLower(float Radius, float Strength);
	UFUNCTION(Exec) void ShamanTerrainFlatten(float Radius, float Height);
	UFUNCTION(Exec) void ShamanTerrainProtectedTest();
	UFUNCTION(Exec) void ShamanTerrainReport();
	UFUNCTION(Exec) void ShamanTerrainSaveReload();
	UFUNCTION(Exec) void ShamanTerrainBenchmark(int32 Count);
	UFUNCTION(Exec) void ShamanSpawnStress(int32 Count);
	UFUNCTION(Exec) void ShamanTerrainProbe();

	FVector GetStartDirection() const { return StartDir; }
	FTransform GetPlayerSpawnTransform() const { return PlayerSpawn; }

protected:
	virtual void EnsureWorldGenerated() override;
	virtual void RegenerateWorld(int32 Seed) override;

	bool BuildPlanet();
	FVector FindStartDirection() const;
	/** Ground point at a tangent-plane offset (uu) from the start site. */
	FVector GroundAt(const FVector2D& OffsetFromStart, FVector* OutUp = nullptr) const;
	FTransform UnitSpawnAt(const FVector2D& OffsetFromStart, float HalfHeight) const;
	void SpawnPlanetContent();
	void SpawnLightsIfMissing();
	FVector GetEditTarget() const;
	void ApplyDebugEdit(ETerrainOp Op, float Radius, float Strength, float TargetHeight);
	uint32 HashHeights(int32 Samples) const;
	void DrawPlanetDebug() const;

	FVector StartDir = FVector::UpVector;
	FTransform PlayerSpawn;
	UPROPERTY(Transient) ABuildingActor* Circle = nullptr;
	UPROPERTY(Transient) ASurfaceProbeActor* Probe = nullptr;
	UPROPERTY(Transient) APlanetWaterActor* Sea = nullptr;
	int32 CircleRegionId = -1;
	FString BackendOption;

	// Rolling performance log
	double FpsWindowStart = 0.0;
	int32 FpsFrames = 0;
	float FpsWorstFrameMs = 0.f;
	float LastAvgFps = 0.f, LastWorstFrameMs = 0.f;

	// Benchmark state (one edit at a time so update timings do not overlap)
	int32 BenchRemaining = 0, BenchIndex = 0;
	bool bBenchWaiting = false;
	TArray<float> BenchApplyMs, BenchUpdateMs;
	FDelegateHandle BenchHandle;
};
