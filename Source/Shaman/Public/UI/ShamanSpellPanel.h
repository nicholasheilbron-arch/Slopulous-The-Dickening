#pragma once
#include "CoreMinimal.h"

class USpellComponent;
class USpellEffectDispatcherComponent;
class UTexture2D;
struct FSpellRow;

/** What a spell-panel slot shows. Nothing here ever casts: it only reflects USpellComponent state. */
enum class EShamanSpellSlotState : uint8
{
	Available,     // known (permanent or temporary charges) and implemented: selectable
	Discoverable,  // can be learned on this map but is not yet: drawn as "?" (no name, no icon) and not selectable
	Unimplemented, // known (e.g. via the unlock cheat) but its effect has no handler yet: shown, never castable
	Locked,        // a working spell the player does not have and cannot discover on this map: blank, non-interactive
};

struct SHAMAN_API FShamanSpellSlot
{
	FName SpellId;
	FText DisplayName;           // empty for Discoverable / Locked (not revealed)
	EShamanSpellSlotState State = EShamanSpellSlotState::Unimplemented;
	float ManaCost = 0.f;        // mana per charge (charge economy) / per cast (mana-at-cast)
	bool bSelected = false;
	bool bAffordable = true;     // castable now: a charge is ready (charge economy) / enough mana (mana-at-cast)
	float CooldownRemaining = 0.f;
	float CooldownTotal = 0.f;
	bool bSuperSpell = false;
	int32 Charges = 0;           // temporary charges (Phase 1 name)

	// Spell charges (spell panel slice)
	bool bChargeEconomy = false;
	bool bPermanent = false;     // learned permanently
	bool bImplemented = false;
	int32 MaxCharges = 0;        // permanent charge slots (0..4)
	int32 StoredCharges = 0;     // ready permanent charges
	int32 TemporaryCharges = 0;  // monument charges (red)
	float Progress01 = 0.f;      // next permanent charge
	float ProgressMana = 0.f;
	float ManaPerSecond = 0.f;   // drawn by this spell in the last step
	bool bCharging = false;      // charging switched on
	bool bCanToggleCharging = false;
	bool bFull = false;
	UTexture2D* Icon = nullptr;  // row icon (loaded), null = placeholder monogram
	FString Monogram;            // placeholder glyph text

	bool IsTemporaryOnly() const { return !bPermanent && TemporaryCharges > 0; }
	int32 GetAvailableCharges() const { return StoredCharges + TemporaryCharges; }
};

/**
 * Data model of the spell panel. Built every frame from the caster's USpellComponent (the only spell state) and its
 * USpellEffectDispatcherComponent, in DataTable row order. One slot per spell that is relevant here:
 *   - known (permanent or temporary charges): Available, or Unimplemented when its effect has no handler;
 *   - available to discover on this map (USpellComponent::IsSpellAvailableOnMap) and not learned: Discoverable ("?");
 *   - otherwise a working permanent spell: Locked (blank tile);
 *   - everything else (future spells without an effect, super spells without charges) is not shown at all; it is
 *     only counted in NumHiddenUnimplemented for tests and logs.
 */
struct SHAMAN_API FShamanSpellPanelModel
{
	TArray<FShamanSpellSlot> Slots;
	int32 NumHiddenUnimplemented = 0;
	bool bHasSpellData = false;
	bool bChargeEconomy = false;
	float ManaRegen = 0.f;          // tribe mana generation per second
	float ChargeDrawPerSecond = 0.f;
	int32 ActiveCharging = 0;
	float Mana = 0.f, MaxMana = 0.f;

	static bool IsSpellImplemented(const FSpellRow& Row, const USpellComponent* Spells, const USpellEffectDispatcherComponent* Effects);
	static FShamanSpellPanelModel Build(const USpellComponent* Spells, const USpellEffectDispatcherComponent* Effects, FName SelectedSpellId);
	int32 FindSlot(FName SpellId) const;
	/** Two-letter placeholder glyph from a display name ("Blast (Taka)" -> "Bl", "Magical Shield" -> "MS"). */
	static FString MakeMonogram(const FString& DisplayName);
};

/** Screen layout of the panel (3 columns, parchment tiles, right edge, footer for the mana line and hints). Shared by
 *  the HUD (drawing) and the controller (click hit test) so both always agree. */
struct SHAMAN_API FShamanSpellPanelLayout
{
	static constexpr int32 Columns = 3;
	static void ComputeSlotRects(int32 NumSlots, float ScreenW, float ScreenH, float UIScale, TArray<FBox2D>& OutRects, FBox2D& OutPanel);
	static int32 HitTest(const TArray<FBox2D>& Rects, const FVector2D& Point);
	/** Footer strip (inside OutPanel, below the tiles). */
	static float FooterHeight(float UIScale) { return 46.f * UIScale; }
};
