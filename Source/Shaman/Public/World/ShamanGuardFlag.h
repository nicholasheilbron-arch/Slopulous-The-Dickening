#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/ShamanInterfaces.h"
#include "ShamanGuardFlag.generated.h"

class UStaticMeshComponent;
class UStaticMesh;

/**
 * Guard post (RTS): a thin, tall flag in its tribe's colour (blue for the player tribe by default). Replaces the
 * original game's campfire guard post. Free to place (P in RTS mode), removable (Delete). Followers ordered to it
 * (RMB) walk circles around it and defend it; Ctrl+RMB on several flags makes a patrol loop.
 * Placeholder art from engine basic shapes (pole + banner); no collision for movement (units walk through the pole's
 * ring), a Visibility-only query collision so it can be clicked. Enemies have no reason to look at it: it is not a
 * damage receiver and not a building.
 */
UCLASS()
class SHAMAN_API AShamanGuardFlag : public AActor, public ITribeOwned
{
	GENERATED_BODY()
public:
	AShamanGuardFlag();

	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Pole;
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Banner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ExposeOnSpawn=true)) int32 TribeId = 0;
	/** Flag height (uu) and the radius guards circle at (before crowd widening). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Height = 420.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float GuardRadius = 220.f;

	/** The tribe's colour from game data; blue when the tribe has none. */
	static FLinearColor GetFlagColor(const UObject* WorldContext, int32 InTribeId);
	FLinearColor GetColor() const { return Color; }

	// ITribeOwned
	virtual int32 GetTribeId() const override { return TribeId; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY() UStaticMesh* CylinderMesh = nullptr;
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	FLinearColor Color = FLinearColor(0.1f, 0.3f, 1.f, 1.f);
};
