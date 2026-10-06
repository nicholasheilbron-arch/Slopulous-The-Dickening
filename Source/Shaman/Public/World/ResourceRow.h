#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShamanTypes.h"
#include "ResourceRow.generated.h"

class UStaticMesh;

/** One row per resource node in DT_Resources (Content/Data/Resources.csv). Gathering arrives in Phase 2. */
USTRUCT(BlueprintType)
struct SHAMAN_API FResourceRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EResourceType Type = EResourceType::Wood;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Amount = 100;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bBlocksMovement = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector MeshScale = FVector(1.f, 1.f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Tint = FLinearColor::White;
};
