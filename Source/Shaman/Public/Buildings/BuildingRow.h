#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "BuildingRow.generated.h"

class UStaticMesh;

/** One row per building in DT_Buildings (Content/Data/Buildings.csv). House, Campfire and Reincarnation Circle
 *  are rows run by the same ABuildingActor. */
USTRUCT(BlueprintType)
struct SHAMAN_API FBuildingRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 PopulationCapacity = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bIsReincarnationSite = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bIsGatheringPoint = false;   // Campfire: interacting calls followers here
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bTerrainLocked = false;      // spells may never modify terrain under it
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bInvulnerable = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxHealth = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bBlocksMovement = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector MeshScale = FVector(1.f, 1.f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Tint = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float TribeColorBlend = 0.f;      // 0 = Tint, 1 = tribe colour
};
