#include "Spells/DefaultSpellProjectile.h"
#include "Core/ShamanVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ADefaultSpellProjectile::ADefaultSpellProjectile()
{
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Collision);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetRelativeScale3D(FVector(0.35f));
	Visual->CastShadow = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded()) Visual->SetStaticMesh(Sphere.Object);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Collision);
	Glow->SetIntensity(6000.f);
	Glow->SetAttenuationRadius(450.f);
	Glow->SetCastShadows(false);
}

void ADefaultSpellProjectile::BeginPlay()
{
	Super::BeginPlay();
	ShamanVisuals::SetTint(Visual, Color);
	Glow->SetLightColor(Color);
}
