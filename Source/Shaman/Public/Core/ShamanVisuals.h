#pragma once
#include "CoreMinimal.h"

class UStaticMesh;
class UStaticMeshComponent;

/** Helpers for data-driven placeholder art (engine basic shapes tinted through BasicShapeMaterial's "Color"). */
namespace ShamanVisuals
{
	/** Assigns Mesh (or Fallback when the soft pointer is empty), scale and tint, and lifts the component so the
	 *  mesh's bottom sits on the actor origin. */
	SHAMAN_API void ApplyMesh(UStaticMeshComponent* Comp, const TSoftObjectPtr<UStaticMesh>& Mesh, UStaticMesh* Fallback,
		const FVector& Scale, const FLinearColor& Tint);
	SHAMAN_API void SetTint(UStaticMeshComponent* Comp, const FLinearColor& Tint);

	/** Testing aid (console: ShamanTeamColors 0/1, cvar shaman.TeamColors, default on). When on, units are tinted by
	 *  relation to the player instead of by their tribe's data colour: player tribe blue, any other tribe red,
	 *  Wildmen (no tribe) grey. Visual only; no gameplay reads this. */
	SHAMAN_API bool TeamColorsEnabled();
	SHAMAN_API void SetTeamColorsEnabled(bool bEnabled);
	SHAMAN_API FLinearColor GetTeamColor(int32 TribeId, int32 PlayerTribeId);
}
