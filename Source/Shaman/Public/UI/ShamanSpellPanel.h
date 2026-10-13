#pragma once
#include "CoreMinimal.h"

class USpellComponent;
class USpellEffectDispatcherComponent;
struct FSpellRow;

/** What a spell-panel slot shows. Nothing here ever casts: it only reflects USpellComponent state. */
enum class EShamanSpellSlotState : uint8
{
	Available,     // known and implemented: selectable (may still be on cooldown or short of mana)
	Discoverable,  // implemented but not known on this level: drawn as "?" and not selectable
	Unimplemented, // known (e.g. via ShamanLearnSpell) but its effect has no handler yet: shown, explicitly unavailable
};

struct SHAMAN_API FShamanSpellSlot
{
	FName SpellId;
	FText DisplayName;
	EShamanSpellSlotState State = EShamanSpellSlotState::Unimplemented;
	float ManaCost = 0.f;
	bool bSelected = false;
	bool bAffordable = true;
	float CooldownRemaining = 0.f;
	float CooldownTotal = 0.f;
	bool bSuperSpell = false;
	int32 Charges = 0;
};

/**
 * Data model of the spell panel (RTS milestone acceptance fix; the full Stage C panel builds on it). Built every frame
 * from the caster's USpellComponent (SpellTable rows + KnownSpellIds) and its USpellEffectDispatcherComponent, in
 * DataTable row order. No assets needed: the table already resolves without DA_ShamanGameData (path / row-type lookup).
 *   - a spell is "implemented" when casting it does something: a Projectile row with a projectile class for its
 *     EffectId, or any other row whose EffectId has a dispatcher effect;
 *   - unknown + unimplemented spells are not shown (counted in NumHiddenUnimplemented).
 */
struct SHAMAN_API FShamanSpellPanelModel
{
	TArray<FShamanSpellSlot> Slots;
	int32 NumHiddenUnimplemented = 0;
	bool bHasSpellData = false;

	static bool IsSpellImplemented(const FSpellRow& Row, const USpellComponent* Spells, const USpellEffectDispatcherComponent* Effects);
	static FShamanSpellPanelModel Build(const USpellComponent* Spells, const USpellEffectDispatcherComponent* Effects, FName SelectedSpellId);
	int32 FindSlot(FName SpellId) const;
};

/** Screen layout of the panel (3 columns, parchment tiles, right edge). Shared by the HUD (drawing) and the controller
 *  (click hit test) so both always agree. */
struct SHAMAN_API FShamanSpellPanelLayout
{
	static constexpr int32 Columns = 3;
	static void ComputeSlotRects(int32 NumSlots, float ScreenW, float ScreenH, float UIScale, TArray<FBox2D>& OutRects, FBox2D& OutPanel);
	static int32 HitTest(const TArray<FBox2D>& Rects, const FVector2D& Point);
};
