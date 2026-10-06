#include "World/ShamanWaterActor.h"
#include "Core/ShamanVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "NavAreas/NavArea_Null.h"
#include "UObject/ConstructorHelpers.h"

AShamanWaterActor::AShamanWaterActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Surface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface"));
	Surface->SetupAttachment(Root);
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Surface->SetCanEverAffectNavigation(false);
	Surface->CastShadow = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (Plane.Succeeded()) Surface->SetStaticMesh(Plane.Object);

	DeepWaterNavBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("DeepWaterNavBlocker"));
	DeepWaterNavBlocker->SetupAttachment(Root);
	DeepWaterNavBlocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DeepWaterNavBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	DeepWaterNavBlocker->SetCanEverAffectNavigation(true);
	DeepWaterNavBlocker->bDynamicObstacle = true;
	DeepWaterNavBlocker->AreaClass = UNavArea_Null::StaticClass();
}

void AShamanWaterActor::Setup(float HalfSize, float SeabedZ, float FordableDepth, UMaterialInterface* Material, const FLinearColor& Color)
{
	const float Extent = HalfSize * 1.6f; // reach past the map edge so the horizon is water
	Surface->SetRelativeLocation(FVector::ZeroVector);
	Surface->SetRelativeScale3D(FVector(Extent * 2.f / 100.f, Extent * 2.f / 100.f, 1.f)); // Plane is 100x100 uu
	if (Material) Surface->SetMaterial(0, Material);
	else ShamanVisuals::SetTint(Surface, Color);

	// Box spans from below the seabed up to -FordableDepth.
	const float Bottom = SeabedZ - 200.f, Top = -FordableDepth;
	DeepWaterNavBlocker->SetBoxExtent(FVector(HalfSize + 500.f, HalfSize + 500.f, (Top - Bottom) * 0.5f));
	DeepWaterNavBlocker->SetRelativeLocation(FVector(0.f, 0.f, (Top + Bottom) * 0.5f));
}
