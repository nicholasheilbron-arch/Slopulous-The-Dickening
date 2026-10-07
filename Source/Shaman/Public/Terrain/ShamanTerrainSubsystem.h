#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Terrain/TerrainInterfaces.h"
#include "ShamanTerrainSubsystem.generated.h"

class UShamanTerrainBackend;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShamanTerrainChanged, const FTerrainChangeEvent&, Event);

/**
 * The one terrain entry point for gameplay. Owns the active backend, enforces protected regions, times edits,
 * broadcasts change events, keeps the edit log for save/load.
 *
 * Until InitializeTerrain() is called the world has no planet: IsPlanetActive() is false and gameplay keeps its
 * existing flat-world behaviour (the current ShamanPrototype map is unaffected).
 */
UCLASS()
class SHAMAN_API UShamanTerrainSubsystem : public UWorldSubsystem,
	public ITerrainWorld, public ITerrainQuery, public ITerrainModifier, public FTickableGameObject
{
	GENERATED_BODY()
public:
	static UShamanTerrainSubsystem* Get(const UObject* WorldContext);
	/** Planet frame for WorldContext's world if a planet is active (the common gameplay entry point). */
	static bool TryGetPlanetFrame(const UObject* WorldContext, FPlanetFrame& OutFrame);

	/** Creates the backend and builds the planet. Replaces any previous terrain. */
	bool InitializeTerrain(TSubclassOf<UShamanTerrainBackend> BackendClass, const FPlanetSettings& Settings);
	void ShutdownTerrain();
	UFUNCTION(BlueprintPure, Category="Terrain") bool IsPlanetActive() const { return Backend != nullptr; }
	UShamanTerrainBackend* GetBackend() const { return Backend; }
	const FPlanetSettings& GetPlanetSettings() const { return Settings; }

	// ITerrainWorld
	virtual FVector GetPlanetCenter() const override { return Frame.Center; }
	virtual float GetPlanetRadius() const override { return Frame.Radius; }
	virtual float GetWaterLevel() const override { return Frame.SeaLevelRadius; }
	virtual FPlanetFrame GetPlanetFrame() const override { return Frame; }

	// ITerrainQuery
	virtual FTerrainSample QueryTerrain(const FVector& WorldLocation) const override;
	virtual FTerrainSample QueryTerrainDirection(const FVector& Direction) const override;
	virtual FVector GetSurfaceLocation(const FVector& Direction) const override;
	virtual FVector GetSurfaceNormal(const FVector& WorldLocation) const override;
	virtual float GetTerrainHeight(const FVector& WorldLocation) const override;
	virtual ETerrainMaterial GetTerrainMaterial(const FVector& WorldLocation) const override;
	virtual bool IsWalkable(const FVector& WorldLocation) const override;
	virtual bool IsUnderwater(const FVector& WorldLocation) const override;
	virtual float GetWaterDepthAt(const FVector& WorldLocation) const override;
	virtual bool Raycast(const FVector& Start, const FVector& End, FTerrainRaycastHit& OutHit) const override;

	// ITerrainModifier
	virtual bool SupportsOperation(ETerrainOp Op) const override;
	virtual FTerrainModificationResult ModifyTerrain(const FTerrainModification& Request) override;
	virtual int32 RegisterProtectedRegion(const FVector& Center, float Radius, FName Tag) override;
	virtual bool UnregisterProtectedRegion(int32 RegionId) override;
	virtual int32 FindBlockingRegion(const FTerrainModification& Request) const override;
	virtual FOnShamanTerrainChangedNative& OnTerrainChangedNative() override { return TerrainChangedNative; }
	virtual FOnShamanTerrainChangedNative& OnTerrainUpdateCompletedNative() override { return TerrainUpdateCompletedNative; }
	virtual void GetEditLog(TArray<FTerrainModification>& OutEdits) const override;
	virtual bool ReplayEditLog(const TArray<FTerrainModification>& Edits) override;

	const TArray<FTerrainProtectedRegion>& GetProtectedRegions() const { return ProtectedRegions; }
	bool IsUpdatePending() const;

	// Blueprint access
	UFUNCTION(BlueprintCallable, Category="Terrain", meta=(DisplayName="Query Terrain")) FTerrainSample K2_QueryTerrain(const FVector& WorldLocation) const { return QueryTerrain(WorldLocation); }
	UFUNCTION(BlueprintCallable, Category="Terrain", meta=(DisplayName="Modify Terrain")) FTerrainModificationResult K2_ModifyTerrain(const FTerrainModification& Request) { return ModifyTerrain(Request); }

	UPROPERTY(BlueprintAssignable, Category="Terrain") FOnShamanTerrainChanged OnTerrainChanged;
	UPROPERTY(BlueprintAssignable, Category="Terrain") FOnShamanTerrainChanged OnTerrainUpdateCompleted;

	/** Stats for reports/acceptance logs. */
	struct FStats
	{
		int32 EditsApplied = 0, EditsRejected = 0;
		float LastApplyMs = 0.f, MaxApplyMs = 0.f;
		float LastUpdateMs = 0.f, MaxUpdateMs = 0.f; // request -> backend mesh/collision up to date
		float GenerationMs = 0.f;
	};
	const FStats& GetStats() const { return Stats; }
	FString GetReport() const;

	// UWorldSubsystem
	virtual void Deinitialize() override;
	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional; }
	virtual bool IsTickable() const override { return Backend != nullptr; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

private:
	struct FPendingUpdate { FTerrainChangeEvent Event; double RequestTime = 0.0; int32 FramesWaited = 0; };

	UPROPERTY() UShamanTerrainBackend* Backend = nullptr;
	FPlanetSettings Settings;
	FPlanetFrame Frame;
	TArray<FTerrainProtectedRegion> ProtectedRegions;
	int32 NextRegionId = 1;
	TArray<FPendingUpdate> PendingUpdates;
	FOnShamanTerrainChangedNative TerrainChangedNative;
	FOnShamanTerrainChangedNative TerrainUpdateCompletedNative;
	FStats Stats;
};
