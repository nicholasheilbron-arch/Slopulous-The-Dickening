#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UI/ShamanSpellPanel.h"
#include "ShamanHUD.generated.h"

class AShamanCharacter;
class AShamanUnitBase;
class AShamanPlayerController;
class UTexture2D;

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
	/** One spell tile (state, charge dots, icon / placeholder, progress bar, selection). */
	void DrawSpellTile(const FShamanSpellSlot& Slot, const FBox2D& B, float UI);
	/** Small round-ish charge dot (filled or empty socket). */
	void DrawChargeDot(float CX, float CY, float R, const FLinearColor& Fill, bool bFilled);
	void DrawBoxOutline(const FBox2D& B, const FLinearColor& Color, float Thickness);
	/** Placeholder icon: monogram inside an ink ring (no art assets yet). */
	void DrawPlaceholderIcon(const FString& Monogram, const FVector2D& Centre, float Radius, const FLinearColor& Ink, float UI);
	/** Details of the hovered tile, drawn to the left of the panel (RTS mode, cursor visible). */
	void DrawSpellTooltip(const FShamanSpellSlot& Slot, const FBox2D& Tile, float UI, bool bRTS);

	FShamanSpellPanelModel PanelModel;
	/** Keeps spell icons loaded by the panel alive between frames (the model holds raw pointers). */
	UPROPERTY(Transient) TArray<UTexture2D*> PanelIcons;
	TArray<FBox2D> PanelSlotRects;
	FBox2D PanelRect = FBox2D(ForceInit);
	bool bPanelDrawn = false;
};
