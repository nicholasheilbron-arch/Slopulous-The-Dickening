#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UI/ShamanSpellPanel.h"
#include "ShamanHUD.generated.h"

class AShamanCharacter;
class AShamanUnitBase;
class AShamanPlayerController;

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

	/** Spell panel hit testing against the rectangles drawn last frame (screen pixels, same space as the mouse). */
	bool IsOverSpellPanel(const FVector2D& ScreenPos) const;
	bool HitTestSpellPanel(const FVector2D& ScreenPos, FShamanSpellSlot& OutSlot) const;
	const FShamanSpellPanelModel& GetSpellPanelModel() const { return PanelModel; }

private:
	void DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Color, const FString& Label);
	void DrawLine(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f, bool bCentered = false);
	/** RTS milestone: mode banner, selection rings, drag box. */
	void DrawControlOverlay(const AShamanPlayerController* PC, float SW, float SH, float UI);
	/** Ground-aligned ring around a unit's feet (projected from its local up, so it fits slopes and planets). */
	void DrawSelectionRing(const AShamanUnitBase* Unit, float RadiusScale, const FLinearColor& Color);
	/** Ground-aligned circle at a world point (RTS command marker). */
	void DrawGroundCircle(const FVector& Centre, float Radius, const FLinearColor& Color, float Thickness);
	/** Data-driven spell panel (both modes): known spells, "?" for discoverable ones, unavailable ones marked. */
	void DrawSpellPanel(const AShamanCharacter* S, float SW, float SH, float UI, bool bRTS);
	void DrawSlotText(const FString& Text, const FBox2D& Box, float Y, const FLinearColor& Color, float MaxScale);

	FShamanSpellPanelModel PanelModel;
	TArray<FBox2D> PanelSlotRects;
	FBox2D PanelRect = FBox2D(ForceInit);
	bool bPanelDrawn = false;
};
