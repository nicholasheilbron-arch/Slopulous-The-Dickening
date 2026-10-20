#include "UI/ShamanSpellPanel.h"
#include "SpellComponent.h"
#include "SpellEffectDispatcherComponent.h"
#include "SpellProjectile.h"
#include "SpellRow.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"

bool FShamanSpellPanelModel::IsSpellImplemented(const FSpellRow& Row, const USpellComponent* Spells, const USpellEffectDispatcherComponent* Effects)
{
	return USpellComponent::IsRowImplemented(Row, Spells, Effects); // one rule for the panel, charging and the cheats
}

FString FShamanSpellPanelModel::MakeMonogram(const FString& DisplayName)
{
	FString Name = DisplayName, Alias;
	DisplayName.Split(TEXT(" ("), &Name, &Alias); // "Blast (Taka)" -> "Blast"
	TArray<FString> Words;
	Name.ParseIntoArrayWS(Words);
	if (Words.Num() >= 2) return Words[0].Left(1).ToUpper() + Words[1].Left(1).ToUpper();
	if (Words.Num() == 1) return Words[0].Left(1).ToUpper() + Words[0].Mid(1, 1).ToLower();
	return TEXT("?");
}

FShamanSpellPanelModel FShamanSpellPanelModel::Build(const USpellComponent* Spells, const USpellEffectDispatcherComponent* Effects, FName SelectedSpellId)
{
	FShamanSpellPanelModel M;
	if (!Spells || !Spells->SpellTable) return M;
	M.bHasSpellData = true;
	M.bChargeEconomy = Spells->bUseChargeEconomy;
	M.ManaRegen = Spells->GetManaRegen();
	M.ChargeDrawPerSecond = Spells->GetChargeDrawPerSecond();
	M.WastedPerSecond = Spells->GetWastedPerSecond();
	M.bTribeMana = Spells->bTribeManaGeneration;
	M.ActiveCharging = Spells->GetActiveChargingCount();
	M.Mana = Spells->Mana;
	M.MaxMana = Spells->MaxMana;

	auto AddSlot = [&](FName Id, const FSpellRow* Row, EShamanSpellSlotState State)
	{
		FShamanSpellSlot S;
		S.SpellId = Id;
		S.State = State;
		S.bChargeEconomy = M.bChargeEconomy;
		const bool bReveal = State == EShamanSpellSlotState::Available || State == EShamanSpellSlotState::Unimplemented;
		S.DisplayName = !bReveal ? FText::GetEmpty() : Row ? Row->DisplayName : FText::FromName(Id);
		S.Monogram = bReveal ? MakeMonogram(S.DisplayName.ToString()) : FString();
		if (Row)
		{
			S.bSuperSpell = Row->bSuperSpell;
			S.ManaCost = (Row->bSuperSpell && !M.bChargeEconomy) ? 0.f : Row->ManaCost;
			S.ChargeCost = FSpellChargeRules::GetChargeCost(*Row);
			S.CooldownTotal = Row->Cooldown;
			S.bImplemented = IsSpellImplemented(*Row, Spells, Effects);
			if (bReveal && !Row->Icon.IsNull()) S.Icon = Row->Icon.LoadSynchronous();
		}
		if (bReveal)
		{
			S.bPermanent = Spells->IsPermanentSpell(Id);
			S.MaxCharges = Spells->GetMaxCharges(Id);
			S.StoredCharges = Spells->GetStoredCharges(Id);
			S.TemporaryCharges = Spells->GetTemporaryCharges(Id);
			S.Charges = S.TemporaryCharges;
			S.Progress01 = Spells->GetChargeProgress01(Id);
			if (const FSpellChargeState* CS = Spells->GetChargeState(Id))
			{
				S.ProgressMana = S.bPermanent ? CS->ProgressMana : 0.f;
				S.ManaPerSecond = CS->LastManaPerSecond;
			}
			S.bCharging = Spells->IsCharging(Id);
			S.bCanToggleCharging = Spells->CanToggleCharging(Id);
			S.bFull = S.bPermanent && S.MaxCharges > 0 && S.StoredCharges >= S.MaxCharges;
			S.CooldownRemaining = Spells->GetCooldownRemaining(Id);
			S.bAffordable = M.bChargeEconomy ? S.GetAvailableCharges() > 0 : Spells->Mana >= S.ManaCost;
		}
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
		else if (Spells->IsSpellAvailableOnMap(Id)) AddSlot(Id, Row, EShamanSpellSlotState::Discoverable);
		else if (bImplemented && FSpellChargeRules::IsChargeable(*Row)) AddSlot(Id, Row, EShamanSpellSlotState::Locked);
		else ++M.NumHiddenUnimplemented; // future spells: never listed just because a row exists
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
	const float Tile = 64.f * UIScale, Gap = 5.f * UIScale, Pad = 7.f * UIScale, Margin = 16.f * UIScale, Title = 20.f * UIScale;
	const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(NumSlots, 1), Columns));
	const float PanelW = Columns * Tile + (Columns - 1) * Gap + 2.f * Pad;
	const float PanelH = Title + Rows * Tile + (Rows - 1) * Gap + 2.f * Pad + FooterHeight(UIScale);
	const float X0 = ScreenW - Margin - PanelW;
	// Right edge, vertically centred, below the mode banner / help line and clear of the bottom-left bars.
	const float Y0 = FMath::Clamp(ScreenH * 0.5f - PanelH * 0.5f, 90.f * UIScale, FMath::Max(90.f * UIScale, ScreenH - PanelH - 16.f * UIScale));
	OutPanel = FBox2D(FVector2D(X0, Y0), FVector2D(X0 + PanelW, Y0 + PanelH));
	for (int32 i = 0; i < NumSlots; ++i)
	{
		const float X = X0 + Pad + (i % Columns) * (Tile + Gap);
		const float Y = Y0 + Pad + Title + (i / Columns) * (Tile + Gap);
		OutRects.Add(FBox2D(FVector2D(X, Y), FVector2D(X + Tile, Y + Tile)));
	}
}

int32 FShamanSpellPanelLayout::HitTest(const TArray<FBox2D>& Rects, const FVector2D& Point)
{
	for (int32 i = 0; i < Rects.Num(); ++i) if (Rects[i].IsInside(Point)) return i;
	return INDEX_NONE;
}
