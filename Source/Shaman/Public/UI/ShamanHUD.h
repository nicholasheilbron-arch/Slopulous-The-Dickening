#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ShamanHUD.generated.h"

class AShamanCharacter;

/**
 * Phase 1 HUD drawn on the canvas (no widget assets needed): health, mana and regen, followers, selected spell
 * with cost/range/cooldown and cast-failure feedback, crosshair, interaction prompt, drowning warning, rebirth timer,
 * seed. Phase 3 replaces it with the UMG spellbook; the data it reads stays the same.
 */
UCLASS()
class SHAMAN_API AShamanHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;

private:
	void DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Color, const FString& Label);
	void DrawLine(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f, bool bCentered = false);
};
