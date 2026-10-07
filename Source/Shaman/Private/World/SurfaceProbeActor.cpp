#include "World/SurfaceProbeActor.h"
#include "Terrain/ShamanTerrainSubsystem.h"
#include "Core/ShamanVisuals.h"
#include "Core/ShamanLog.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ASurfaceProbeActor::ASurfaceProbeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (Cone.Succeeded()) Mesh->SetStaticMesh(Cone.Object);
	Mesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 2.5f));
}

void ASurfaceProbeActor::BeginPlay()
{
	Super::BeginPlay();
	ShamanVisuals::SetTint(Mesh, FLinearColor(1.f, 0.45f, 0.f));
	if (UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this))
		ChangedHandle = T->OnTerrainChangedNative().AddUObject(this, &ASurfaceProbeActor::HandleTerrainChanged);
	SnapToSurface();
}

void ASurfaceProbeActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this)) T->OnTerrainChangedNative().Remove(ChangedHandle);
	Super::EndPlay(Reason);
}

bool ASurfaceProbeActor::SnapToSurface()
{
	const UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	if (!T || !T->IsPlanetActive()) return false;
	const FTerrainSample S = T->QueryTerrain(GetActorLocation());
	if (!S.bValid) return false;
	// Cone pivot is at its centre (100 uu tall basic shape): lift by half its scaled height along the normal.
	const float HalfHeight = 50.f * Mesh->GetRelativeScale3D().Z;
	const FVector Fwd = FVector::VectorPlaneProject(GetActorForwardVector(), S.Normal);
	const FRotator Rot = FRotationMatrix::MakeFromZX(S.Normal, Fwd.IsNearlyZero() ? FVector::ForwardVector : Fwd).Rotator();
	SetActorLocationAndRotation(S.Location + S.Normal * HalfHeight, Rot);
	return true;
}

void ASurfaceProbeActor::HandleTerrainChanged(const FTerrainChangeEvent& Event)
{
	if (FVector::Dist(Event.Center, GetActorLocation()) > Event.Radius + 500.f) return;
	if (SnapToSurface())
	{
		++RealignCount;
		UE_LOG(LogShaman, Log, TEXT("SurfaceProbe %s re-aligned after terrain edit %d"), *GetName(), Event.EditIndex);
	}
}
