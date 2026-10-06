#include "Core/ShamanVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

void ShamanVisuals::ApplyMesh(UStaticMeshComponent* Comp, const TSoftObjectPtr<UStaticMesh>& Mesh, UStaticMesh* Fallback,
	const FVector& Scale, const FLinearColor& Tint)
{
	if (!Comp) return;
	UStaticMesh* SM = Mesh.IsNull() ? nullptr : Mesh.LoadSynchronous();
	if (!SM) SM = Fallback;
	if (!SM) return;
	Comp->SetStaticMesh(SM);
	Comp->SetRelativeScale3D(Scale);
	const FBoxSphereBounds B = SM->GetBounds();
	const float BottomZ = (B.Origin.Z - B.BoxExtent.Z) * Scale.Z;
	Comp->SetRelativeLocation(FVector(0.f, 0.f, -BottomZ));
	SetTint(Comp, Tint);
}

void ShamanVisuals::SetTint(UStaticMeshComponent* Comp, const FLinearColor& Tint)
{
	if (!Comp || Comp->GetNumMaterials() == 0) return;
	if (UMaterialInstanceDynamic* MID = Comp->CreateAndSetMaterialInstanceDynamic(0))
		MID->SetVectorParameterValue(TEXT("Color"), Tint);
}
