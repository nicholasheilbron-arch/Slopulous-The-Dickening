#include "World/ShamanGuardFlag.h"
#include "Game/ShamanGameData.h"
#include "Core/ShamanVisuals.h"
#include "Terrain/ShamanSpace.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AShamanGuardFlag::AShamanGuardFlag()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	auto SetupPart = [this](UStaticMeshComponent* C, bool bClickable)
	{
		C->SetupAttachment(Root);
		C->SetCanEverAffectNavigation(false);
		C->SetGenerateOverlapEvents(false);
		C->CastShadow = true;
		if (bClickable)
		{
			// Clickable (Visibility traces) but never in the way of units, spells or the camera.
			C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			C->SetCollisionObjectType(ECC_WorldDynamic);
			C->SetCollisionResponseToAllChannels(ECR_Ignore);
			C->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}
		else
		{
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	};
	Pole = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pole"));
	SetupPart(Pole, true);
	Banner = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Banner"));
	SetupPart(Banner, true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = Cylinder.Object;
	CubeMesh = Cube.Object;
	SetCanBeDamaged(false);
}

FLinearColor AShamanGuardFlag::GetFlagColor(const UObject* WorldContext, int32 InTribeId)
{
	const UShamanGameData* Data = WorldContext ? UShamanGameData::Get(WorldContext) : nullptr;
	if (const FTribeDefinition* T = Data ? Data->GetTribe(InTribeId) : nullptr) return T->Color;
	return FLinearColor(0.1f, 0.3f, 1.f, 1.f); // default: blue
}

void AShamanGuardFlag::BeginPlay()
{
	Super::BeginPlay();
	Color = GetFlagColor(this, TribeId);
	// Stand upright on the local ground (planets: along the local up).
	SetActorRotation(FShamanSpace::UprightRotation(this, GetActorLocation(), GetActorForwardVector()));

	// Engine basic shapes are 100 uu: the cylinder 100 tall / 100 wide, the cube 100 on each side.
	const float H = FMath::Max(100.f, Height);
	ShamanVisuals::ApplyMesh(Pole, TSoftObjectPtr<UStaticMesh>(), CylinderMesh, FVector(0.07f, 0.07f, H / 100.f), FLinearColor(0.28f, 0.18f, 0.09f, 1.f));
	// Banner: thin cloth hanging from the top on one side of the pole.
	const float BannerW = 120.f, BannerH = 75.f;
	ShamanVisuals::ApplyMesh(Banner, TSoftObjectPtr<UStaticMesh>(), CubeMesh, FVector(0.03f, BannerW / 100.f, BannerH / 100.f), Color);
	Banner->SetRelativeLocation(FVector(0.f, BannerW * 0.5f + 4.f, H - BannerH * 0.5f - 12.f));
}
