#include "UI/ShamanSpellPanel.h"
#include "SpellComponent.h"
#include "SpellEffectDispatcherComponent.h"
#include "SpellProjectile.h"
#include "SpellRow.h"
#include "Engine/DataTable.h"

bool FShamanSpellPanelModel::IsSpellImplemented(const FSpellRow& Row, const USpellComponent* Spells, const USpellEffectDispatcherComponent* Effects)
{
	if (Row.Targeting == ESpellTargeting::Projectile)
	{
		// USpellComponent::CastSpell spawns nothing for a projectile row without a class: not castable in practice.
		const TSubclassOf<ASpellProjectile>* Cls = Spells ? Spells->ProjectileClasses.Find(Row.EffectId) : nullptr;
		return Cls && *Cls;
	}
	// Non-projectile rows only broadcast OnSpellCast: they do something only when the dispatcher has the effect.
	return Effects && Effects->FindEffect(Row.EffectId) != nullptr;
}

FShamanSpellPanelModel FShamanSpellPanelModel::Build(const USpellComponent* Spells, const USpellEffectDispatcherComponent* Effects, FName SelectedSpellId)
{
	FShamanSpellPanelModel M;
	if (!Spells || !Spells->SpellTable) return M;
	M.bHasSpellData = true;

	auto AddSlot = [&](FName Id, const FSpellRow* Row, EShamanSpellSlotState State)
	{
		FShamanSpellSlot S;
		S.SpellId = Id;
		S.State = State;
		S.DisplayName = Row ? Row->DisplayName : FText::FromName(Id);
		if (Row)
		{
			S.bSuperSpell = Row->bSuperSpell;
			S.ManaCost = Row->bSuperSpell ? 0.f : Row->ManaCost;
			S.CooldownTotal = Row->Cooldown;
		}
		S.bAffordable = Spells->Mana >= S.ManaCost;
		S.CooldownRemaining = Spells->GetCooldownRemaining(Id);
		S.Charges = Spells->GetCharges(Id);
		S.bSelected = State == EShamanSpellSlotState::Available && Id == SelectedSpellId;
		M.Slots.Add(S);
	};

	// Table order (Blast first, Convert second in Spells.csv).
	for (const FName& Id : Spells->SpellTable->GetRowNames())
	{
		const FSpellRow* Row = Spells->GetSpellRow(Id);
		if (!Row) continue;
		const bool bKnown = Spells->KnownSpellIds.Contains(Id);
		const bool bImplemented = IsSpellImplemented(*Row, Spells, Effects);
		if (bKnown) AddSlot(Id, Row, bImplemented ? EShamanSpellSlotState::Available : EShamanSpellSlotState::Unimplemented);
		else if (bImplemented) AddSlot(Id, Row, EShamanSpellSlotState::Discoverable);
		else ++M.NumHiddenUnimplemented;
	}
	// Known ids with no table row cannot be cast (CastSpell needs the row): show them as unavailable, never hide them.
	for (const FName& Id : Spells->KnownSpellIds)
		if (!Spells->GetSpellRow(Id)) AddSlot(Id, nullptr, EShamanSpellSlotState::Unimplemented);
	return M;
}

int32 FShamanSpellPanelModel::FindSlot(FName SpellId) const
{
	return Slots.IndexOfByPredicate([SpellId](const FShamanSpellSlot& S) { return S.SpellId == SpellId; });
}

void FShamanSpellPanelLayout::ComputeSlotRects(int32 NumSlots, float ScreenW, float ScreenH, float UIScale, TArray<FBox2D>& OutRects, FBox2D& OutPanel)
{
	OutRects.Reset();
	const float Tile = 68.f * UIScale, Gap = 6.f * UIScale, Pad = 8.f * UIScale, Margin = 16.f * UIScale;
	const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(NumSlots, 1), Columns));
	const float PanelW = Columns * Tile + (Columns - 1) * Gap + 2.f * Pad;
	const float PanelH = Rows * Tile + (Rows - 1) * Gap + 2.f * Pad + 22.f * UIScale; // + title strip
	const float X0 = ScreenW - Margin - PanelW;
	const float Y0 = FMath::Max(90.f * UIScale, ScreenH * 0.5f - PanelH * 0.5f); // below the mode banner and help line
	OutPanel = FBox2D(FVector2D(X0, Y0), FVector2D(X0 + PanelW, Y0 + PanelH));
	for (int32 i = 0; i < NumSlots; ++i)
	{
		const float X = X0 + Pad + (i % Columns) * (Tile + Gap);
		const float Y = Y0 + Pad + 22.f * UIScale + (i / Columns) * (Tile + Gap);
		OutRects.Add(FBox2D(FVector2D(X, Y), FVector2D(X + Tile, Y + Tile)));
	}
}

int32 FShamanSpellPanelLayout::HitTest(const TArray<FBox2D>& Rects, const FVector2D& Point)
{
	for (int32 i = 0; i < Rects.Num(); ++i) if (Rects[i].IsInside(Point)) return i;
	return INDEX_NONE;
}
