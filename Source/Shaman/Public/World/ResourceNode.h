#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/ShamanInterfaces.h"
#include "World/ResourceRow.h"
#include "ResourceNode.generated.h"

class UStaticMeshComponent;

/** Data-driven resource node (tree, berry bush, stone, ore). Phase 1: placed and inspectable; Phase 2 adds gathering. */
UCLASS()
class SHAMAN_API AResourceNode : public AActor, public IShamanInteractable
{
	GENERATED_BODY()
public:
	AResourceNode();

	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ExposeOnSpawn=true)) FName ResourceId;
	UPROPERTY(BlueprintReadOnly) int32 Remaining = 0;

	const FResourceRow& GetRow() const { return Row; }

	virtual FText GetInteractionText(const AActor* Interactor) const override;
	virtual void Interact(AActor* Interactor) override {}

protected:
	virtual void BeginPlay() override;
	UPROPERTY() UStaticMesh* FallbackMesh = nullptr;
	FResourceRow Row;
};
