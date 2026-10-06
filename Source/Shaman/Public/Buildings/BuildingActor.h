#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/ShamanInterfaces.h"
#include "Buildings/BuildingRow.h"
#include "BuildingActor.generated.h"

class UStaticMeshComponent;

/**
 * Data-driven building (House, Campfire, Reincarnation Circle, ...). Behaviour comes from FBuildingRow flags,
 * not subclasses. Spawn deferred, set BuildingId + TribeId, then finish spawning.
 */
UCLASS()
class SHAMAN_API ABuildingActor : public AActor, public ITribeOwned, public IShamanInteractable, public IShamanDamageReceiver
{
	GENERATED_BODY()
public:
	ABuildingActor();

	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ExposeOnSpawn=true)) FName BuildingId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ExposeOnSpawn=true)) int32 TribeId = -2;

	const FBuildingRow& GetRow() const { return Row; }
	UFUNCTION(BlueprintCallable) bool IsReincarnationSite() const { return Row.bIsReincarnationSite; }
	UFUNCTION(BlueprintCallable) int32 GetPopulationCapacity() const { return Row.PopulationCapacity; }
	/** Where a reborn Shaman appears (just beside the circle so it does not stand inside the mesh). */
	UFUNCTION(BlueprintCallable) FVector GetRebirthLocation() const;

	// ITribeOwned
	virtual int32 GetTribeId() const override { return TribeId; }
	// IShamanInteractable
	virtual FText GetInteractionText(const AActor* Interactor) const override;
	virtual void Interact(AActor* Interactor) override;
	// IShamanDamageReceiver (building damage arrives in Phase 4/5)
	virtual bool IsAlive() const override { return true; }
	virtual bool IsInvulnerable() const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY() UStaticMesh* FallbackMesh = nullptr;
	FBuildingRow Row;
};
