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
}
