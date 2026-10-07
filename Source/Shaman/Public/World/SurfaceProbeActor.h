#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Terrain/TerrainTypes.h"
#include "SurfaceProbeActor.generated.h"

class UStaticMeshComponent;

/**
 * A placeholder surface object (spike acceptance G): stands on the terrain, aligned to the terrain normal, and
 * re-snaps itself when a terrain change touches it (listens to ITerrainModification::OnTerrainChangedNative).
 * Only uses the SHAMAN terrain abstraction.
 */
UCLASS()
class SHAMAN_API ASurfaceProbeActor : public AActor
{
	GENERATED_BODY()
public:
	ASurfaceProbeActor();
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Mesh;
	/** Re-snap to the ground at the current location, aligned to the normal. Returns false without a planet. */
	bool SnapToSurface();
	int32 GetRealignCount() const { return RealignCount; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void HandleTerrainChanged(const FTerrainChangeEvent& Event);
	FDelegateHandle ChangedHandle;
	int32 RealignCount = 0;
};
