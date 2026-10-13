#include "Core/ShamanVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarShamanTeamColors(
	TEXT("shaman.TeamColors"), 1,
	TEXT("1 = testing colours: player tribe blue, other tribes red, Wildmen grey. 0 = tribe colours from game data. ")
	TEXT("Use the ShamanTeamColors command to change it in play (recolours existing units)."),
	ECVF_Default);

bool ShamanVisuals::TeamColorsEnabled() { return CVarShamanTeamColors.GetValueOnGameThread() != 0; }

void ShamanVisuals::SetTeamColorsEnabled(bool bEnabled)
{
	CVarShamanTeamColors.AsVariable()->Set(bEnabled ? 1 : 0, ECVF_SetByConsole);
}

FLinearColor ShamanVisuals::GetTeamColor(int32 TribeId, int32 PlayerTribeId)
{
	if (TribeId < 0) return FLinearColor(0.5f, 0.5f, 0.5f, 1.f);         // Wildmen / unaligned: grey
	if (TribeId == PlayerTribeId) return FLinearColor(0.1f, 0.3f, 1.f, 1.f); // friendly: blue
	return FLinearColor(0.95f, 0.08f, 0.06f, 1.f);                           // enemy: red
}

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
