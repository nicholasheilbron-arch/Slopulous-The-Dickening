#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Terrain/TerrainTypes.h"
#include "Terrain/PlanetFrame.h"
#include "ShamanTerrainBackend.generated.h"

class UWorld;
class FPlanetHeightField;

/**
 * Implementation side of SHAMAN terrain. One backend is active per world, owned by UShamanTerrainSubsystem.
 * Gameplay never sees a backend; it uses the subsystem through ITerrainWorld / ITerrainQuery / ITerrainModification.
 *
 * Backends: UAnalyticPlanetTerrainBackend (pure math, no visuals; tests/headless), UVoxelPluginTerrainBackend
 * (ShamanVoxel module, Voxel Plugin Free). A future cube-sphere heightmap backend plugs in the same way.
 * Protection and change events are handled by the subsystem, not here.
 */
UCLASS(Abstract)
class SHAMAN_API UShamanTerrainBackend : public UObject
{
	GENERATED_BODY()
public:
	/** Build the terrain for this world. Return false on failure (subsystem then has no terrain). */
	virtual bool InitializeBackend(UWorld* World, const FPlanetSettings& InSettings) { Settings = InSettings; Frame = FPlanetFrame(InSettings.Center, InSettings.Radius, InSettings.SeaLevelOffset); return true; }
	virtual void ShutdownBackend() {}
	virtual FName GetBackendName() const { return NAME_None; }
	virtual void TickBackend(float DeltaSeconds) {}

	const FPlanetSettings& GetSettings() const { return Settings; }
	const FPlanetFrame& GetFrame() const { return Frame; }
	/** The height field when the backend is heightfield-driven (null otherwise). Read-only. */
	virtual const FPlanetHeightField* GetHeightField() const { return nullptr; }

	// Queries
	virtual FTerrainSample SampleAt(const FVector& WorldLocation) const { return FTerrainSample(); }
	virtual FTerrainSample SampleDirection(const FVector& Direction) const { return FTerrainSample(); }
	virtual bool Raycast(const FVector& Start, const FVector& End, FTerrainRaycastHit& OutHit) const { return false; }
	/** Ground height only (cheaper than a full sample; used every movement step). */
	virtual float GetHeightAt(const FVector& WorldLocation) const { return SampleAt(WorldLocation).Height; }

	// Modification (already validated and protection-checked by the subsystem)
	virtual bool SupportsOperation(ETerrainOp Op) const { return false; }
	virtual FTerrainModificationResult ApplyModification(const FTerrainModification& Request);
	/** True while mesh/collision is still being rebuilt after an edit (or generation). */
	virtual bool IsUpdatePending() const { return false; }
	virtual void GetEditLog(TArray<FTerrainModification>& OutEdits) const {}
	/** Remove every edit and rebuild. */
	virtual bool ResetEdits() { return false; }

	/** Time spent building the terrain in InitializeBackend (ms), and backend-specific stats for reports. */
	float GetGenerationMs() const { return GenerationMs; }
	virtual FString GetStatsString() const { return FString(); }

protected:
	FPlanetSettings Settings;
	FPlanetFrame Frame;
	float GenerationMs = 0.f;
};

/**
 * Pure-math planet: FPlanetHeightField + FPlanetTerrainQueries, no rendering, no collision.
 * Used for automation tests, headless checks and as the query layer of heightfield-driven visual backends.
 */
UCLASS()
class SHAMAN_API UAnalyticPlanetTerrainBackend : public UShamanTerrainBackend
{
	GENERATED_BODY()
public:
	virtual bool InitializeBackend(UWorld* World, const FPlanetSettings& InSettings) override;
	virtual void ShutdownBackend() override;
	virtual FName GetBackendName() const override { return TEXT("Analytic"); }
	virtual const FPlanetHeightField* GetHeightField() const override { return HeightField.Get(); }

	virtual FTerrainSample SampleAt(const FVector& WorldLocation) const override;
	virtual FTerrainSample SampleDirection(const FVector& Direction) const override;
	virtual bool Raycast(const FVector& Start, const FVector& End, FTerrainRaycastHit& OutHit) const override;
	virtual float GetHeightAt(const FVector& WorldLocation) const override;

	virtual bool SupportsOperation(ETerrainOp Op) const override;
	virtual FTerrainModificationResult ApplyModification(const FTerrainModification& Request) override;
	virtual void GetEditLog(TArray<FTerrainModification>& OutEdits) const override;
	virtual bool ResetEdits() override;
	virtual FString GetStatsString() const override;

protected:
	/** Called after an edit was appended to the height field (visual backends rebuild the affected area here). */
	virtual void OnHeightFieldEdited(const FTerrainModificationResult& Result) {}
	/** Called after all edits were removed. */
	virtual void OnHeightFieldReset() {}
	/** False when other threads may read the height field (edit storage is then not reused on reset). */
	virtual bool CanReclaimEditStorage() const { return true; }

	/** World-space box that the last applied edit could have changed (ground before and after). */
	FBox LastEditBounds = FBox(ForceInit);

	/** Shared with worker threads of visual backends (voxel meshing), hence thread-safe shared ownership. */
	TSharedPtr<FPlanetHeightField, ESPMode::ThreadSafe> HeightField;
};
