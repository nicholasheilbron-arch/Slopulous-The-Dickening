#include "UI/ShamanHUD.h"
#include "Characters/ShamanCharacter.h"
#include "Characters/HealthComponent.h"
#include "SpellComponent.h"
#include "SpellEffectDispatcherComponent.h"
#include "Game/ShamanGameMode.h"
#include "Tribes/TribeSubsystem.h"
#include "TribeComponent.h"
#include "Player/ShamanPlayerController.h"
#include "Player/ShamanSelectionComponent.h"
#include "Terrain/ShamanSpace.h"
#include "Components/CapsuleComponent.h"
#include "Terrain/PlanetFrame.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

void AShamanHUD::DrawLine(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale, bool bCentered)
{
	UFont* Font = GEngine->GetMediumFont();
	if (bCentered)
	{
		float W = 0.f, H = 0.f;
		GetTextSize(Text, W, H, Font, Scale);
		X -= W * 0.5f;
	}
	DrawText(Text, FLinearColor(0.f, 0.f, 0.f, 0.7f), X + 1.f, Y + 1.f, Font, Scale); // drop shadow for readability
	DrawText(Text, Color, X, Y, Font, Scale);
}

void AShamanHUD::DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Color, const FString& Label)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 2.f, Y - 2.f, W + 4.f, H + 4.f);
	DrawRect(Color, X, Y, W * FMath::Clamp(Fraction, 0.f, 1.f), H);
	DrawLine(Label, X + 6.f, Y + 1.f, FLinearColor::White, 0.9f);
}

static FString CastResultText(ESpellCastResult R)
{
	switch (R)
	{
	case ESpellCastResult::OnCooldown:     return TEXT("Not ready yet");
	case ESpellCastResult::NotEnoughMana:  return TEXT("Not enough mana");
	case ESpellCastResult::NoCharges:      return TEXT("No charges left");
	case ESpellCastResult::OutOfRange:     return TEXT("Out of range");
	case ESpellCastResult::UnknownSpell:   return TEXT("Spell not learned (or no spell data loaded)");
	case ESpellCastResult::CasterDisabled: return TEXT("Cannot cast right now");
	default: return FString();
	}
}

void AShamanHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas) return;
	const float SW = Canvas->ClipX, SH = Canvas->ClipY;
	const float UI = FMath::Clamp(SH / 1080.f, 0.75f, 2.f); // scale with resolution

	AShamanCharacter* S = Cast<AShamanCharacter>(GetOwningPawn());
	const AShamanGameMode* GM = GetWorld()->GetAuthGameMode<AShamanGameMode>();
	if (GM) DrawLine(FString::Printf(TEXT("Seed %d"), GM->GetLayout().Seeds.WorldSeed), SW - 140.f * UI, 12.f, FLinearColor(1, 1, 1, 0.6f), 0.8f * UI);
	const AShamanPlayerController* SPC = Cast<AShamanPlayerController>(PlayerOwner);
	const bool bRTS = SPC && SPC->GetControlMode() == EShamanControlMode::RTS;
	if (SPC) DrawControlOverlay(SPC, SW, SH, UI);
	bPanelDrawn = false;
	if (!S) return;
	DrawSpellPanel(S, SW, SH, UI, bRTS);

	UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>();
	const int32 MyTribe = S->GetTribeId();
	const UTribeComponent* MyTribeComp = Tribes ? Tribes->GetTribeComponent(MyTribe) : nullptr;
	const int32 Followers = MyTribeComp ? MyTribeComp->GetPopulationUsed() : 0; // same units as the capacity check
	const int32 Cap = Tribes ? Tribes->GetPopulationCapacity(MyTribe) : 0;

	// Bars (bottom-left).
	const float BX = 30.f * UI, BW = 320.f * UI, BH = 22.f * UI;
	float BY = SH - 110.f * UI;
	DrawBar(BX, BY, BW, BH, S->Health->GetHealthFraction(), FLinearColor(0.75f, 0.1f, 0.1f),
		FString::Printf(TEXT("Health %.0f / %.0f"), S->Health->Health, S->Health->MaxHealth));
	BY += BH + 10.f * UI;
	DrawBar(BX, BY, BW, BH, S->Spells->Mana / FMath::Max(1.f, S->Spells->MaxMana), FLinearColor(0.15f, 0.35f, 0.95f),
		FString::Printf(TEXT("Mana %.0f / %.0f   +%.2f/s"), S->Spells->Mana, S->Spells->MaxMana, S->Spells->GetManaRegen()));
	BY += BH + 10.f * UI;
	DrawLine(FString::Printf(TEXT("Followers %d / %d   (more followers = faster mana & rebirth)"), Followers, Cap), BX, BY, FLinearColor::White, 0.85f * UI);

	if (!S->Spells->SpellTable)
		DrawLine(TEXT("No spell data loaded: import Content/Data/Spells.csv as a DataTable (row type SpellRow)"), SW * 0.5f, SH - 120.f * UI, FLinearColor(1.f, 0.3f, 0.3f), 1.f * UI, true);

	// Selected spell (bottom-centre).
	const FName SpellId = S->GetSelectedSpellId();
	if (const FSpellRow* Row = S->Spells->GetSpellRow(SpellId))
	{
		const float CD = S->Spells->GetCooldownRemaining(SpellId);
		const bool bAffordable = S->Spells->Mana >= (Row->bSuperSpell ? 0.f : Row->ManaCost);
		const FLinearColor C = (CD > 0.f || !bAffordable) ? FLinearColor(0.6f, 0.6f, 0.6f) : FLinearColor(1.f, 0.8f, 0.3f);
		DrawLine(Row->DisplayName.ToString(), SW * 0.5f, SH - 120.f * UI, C, 1.3f * UI, true);
		FString Info = FString::Printf(TEXT("%.0f mana   range %.0f (%.0fm)   %s"), Row->ManaCost, Row->RangeRaw,
			S->Spells->GetRangeUnits(SpellId) / 100.f, CD > 0.f ? *FString::Printf(TEXT("ready in %.1fs"), CD) : TEXT("ready"));
		if (Row->bFriendlyFire) Info += TEXT("   ! hits your followers too");
		DrawLine(Info, SW * 0.5f, SH - 88.f * UI, FLinearColor::White, 0.85f * UI, true);
		if (!bRTS) // wheel / Q drive the camera in RTS mode
			DrawLine(FString::Printf(TEXT("Spell %d / %d   (mouse wheel or Q to change)"), S->GetSelectedSpellIndex() + 1, S->Spells->KnownSpellIds.Num()),
				SW * 0.5f, SH - 64.f * UI, FLinearColor(1, 1, 1, 0.6f), 0.75f * UI, true);
		else // honest: the panel selects; casting from RTS mode (targeting) is not implemented yet
			DrawLine(TEXT("Selected for casting in Shaman mode (Enter, then LMB). RTS-mode casting is not implemented yet."),
				SW * 0.5f, SH - 64.f * UI, FLinearColor(1, 1, 1, 0.6f), 0.75f * UI, true);
	}
	float Age = 0.f;
	const ESpellCastResult Feedback = S->GetLastCastFeedback(Age);
	if (Feedback != ESpellCastResult::Success && Age < 1.5f)
		DrawLine(CastResultText(Feedback), SW * 0.5f, SH - 150.f * UI, FLinearColor(1.f, 0.35f, 0.3f), 1.f * UI, true);

	// Crosshair and interaction prompt: direct Shaman control only.
	if (!bRTS)
	{
		DrawRect(FLinearColor(1, 1, 1, 0.8f), SW * 0.5f - 1.f, SH * 0.5f - 8.f, 2.f, 16.f);
		DrawRect(FLinearColor(1, 1, 1, 0.8f), SW * 0.5f - 8.f, SH * 0.5f - 1.f, 16.f, 2.f);
		const FText Focus = S->GetFocusText();
		if (!Focus.IsEmpty()) DrawLine(Focus.ToString(), SW * 0.5f, SH * 0.5f + 40.f * UI, FLinearColor::White, 1.f * UI, true);
	}

	// Hazards and death.
	if (S->IsDrowning())
		DrawLine(FString::Printf(TEXT("Drowning! Get to shallow water (%.1fs)"), S->GetDrownRemaining()), SW * 0.5f, SH * 0.3f, FLinearColor(0.4f, 0.7f, 1.f), 1.4f * UI, true);
	if (!S->IsAlive())
	{
		const float T = Tribes ? Tribes->GetRebirthRemaining(MyTribe) : -1.f;
		const FString Msg = T >= 0.f
			? FString::Printf(TEXT("You have fallen. Reborn at the Reincarnation Circle in %.1fs"), T)
			: (Tribes && Tribes->IsTribeEliminated(MyTribe)) ? FString(TEXT("You have fallen and no Reincarnation Circle remains."))
			: FString(TEXT("You have fallen."));
		DrawLine(Msg, SW * 0.5f, SH * 0.4f, FLinearColor(1.f, 0.85f, 0.6f), 1.6f * UI, true);
	}

	// Level complete (every enemy tribe eliminated). Minimal presentation: a banner; no end-of-level flow yet.
	if (Tribes && Tribes->IsLevelWon())
		DrawLine(TEXT("VICTORY - every enemy tribe is eliminated"), SW * 0.5f, SH * 0.2f, FLinearColor(1.f, 0.85f, 0.2f), 2.f * UI, true);

	// Enemy Shaman status (top-left).
	if (Tribes)
	{
		for (int32 Id = 0; Id < Tribes->GetNumTribes(); ++Id)
		{
			if (Id == MyTribe) continue;
			const AShamanUnitBase* E = Tribes->GetShaman(Id);
			const float T = Tribes->GetRebirthRemaining(Id);
			const FString State = Tribes->IsTribeEliminated(Id) ? TEXT("eliminated (circle destroyed)") : !E ? TEXT("unknown") : E->IsAlive() ? TEXT("alive")
				: T >= 0.f ? FString::Printf(TEXT("reincarnating (%.0fs)"), T) : TEXT("dead");
			DrawLine(FString::Printf(TEXT("Enemy Shaman: %s   followers %d"), *State, Tribes->GetFollowerCount(Id)), 20.f, 12.f + 22.f * UI * (Id - 1), Tribes->GetTribeColor(Id), 0.85f * UI);
		}
	}
}

// ---- RTS controls overlay ----------------------------------------------------------------------------------------------

void AShamanHUD::DrawSelectionRing(const AShamanUnitBase* Unit, float RadiusScale, const FLinearColor& Color)
{
	if (!Unit || !PlayerOwner) return;
	const FVector L = Unit->GetActorLocation();
	const FVector Up = FShamanSpace::GetUp(this, L);
	FPlanetFrame Planet;
	if (PlayerOwner->PlayerCameraManager && FShamanSpace::GetPlanet(this, Planet)
		&& FVector::DotProduct(Up, PlayerOwner->PlayerCameraManager->GetCameraLocation() - L) < 0.f) return; // far side of the planet
	const UCapsuleComponent* Cap = Unit->GetCapsuleComponent();
	const float Half = Cap ? Cap->GetScaledCapsuleHalfHeight() : 90.f;
	const float R = (Cap ? Cap->GetScaledCapsuleRadius() : 40.f) * RadiusScale;
	const FVector Centre = L - Up * (Half - 4.f);
	const FVector Hint = FMath::Abs(Up.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
	const FVector X = (Hint - Up * FVector::DotProduct(Hint, Up)).GetSafeNormal();
	const FVector Y = FVector::CrossProduct(Up, X);

	const int32 N = 24;
	FVector2D Prev;
	bool bPrev = false;
	for (int32 i = 0; i <= N; ++i)
	{
		const float A = 2.f * PI * i / N;
		FVector2D P;
		const bool bOk = PlayerOwner->ProjectWorldLocationToScreen(Centre + (X * FMath::Cos(A) + Y * FMath::Sin(A)) * R, P, false);
		if (bOk && bPrev)
		{
			AHUD::DrawLine(Prev.X, Prev.Y, P.X, P.Y, FLinearColor(0.f, 0.f, 0.f, 0.6f), 5.f); // dark outline: readable on any ground
			AHUD::DrawLine(Prev.X, Prev.Y, P.X, P.Y, Color, 2.5f);
		}
		Prev = P;
		bPrev = bOk;
	}
	// Chevron above the head: still visible when the feet are hidden behind terrain.
	FVector2D H;
	if (PlayerOwner->ProjectWorldLocationToScreen(L + Up * (Half + 45.f), H, false))
	{
		AHUD::DrawLine(H.X - 7.f, H.Y - 7.f, H.X, H.Y, Color, 2.5f);
		AHUD::DrawLine(H.X + 7.f, H.Y - 7.f, H.X, H.Y, Color, 2.5f);
	}
}

void AShamanHUD::DrawControlOverlay(const AShamanPlayerController* PC, float SW, float SH, float UI)
{
	const bool bRTS = PC->GetControlMode() == EShamanControlMode::RTS;
	const FShamanSelectionSettings& Sel = PC->SelectionSettings;

	// Mode banner (top centre).
	DrawLine(bRTS ? TEXT("RTS MODE") : TEXT("SHAMAN MODE"), SW * 0.5f, 10.f * UI,
		bRTS ? FLinearColor(1.f, 0.85f, 0.25f) : FLinearColor(0.55f, 0.85f, 1.f), 1.4f * UI, true);
	DrawLine(bRTS
		? TEXT("Enter: Shaman mode   LMB: select / drag box (Shift adds)   RMB: move there (on your Shaman: guard him)   WASD: pan   Q/E or MMB drag: rotate   Wheel: zoom   F: focus   Home: Shaman   Backspace: clear")
		: TEXT("Enter: RTS mode"), SW * 0.5f, 40.f * UI, FLinearColor(1.f, 1.f, 1.f, 0.65f), 0.7f * UI, true);

	// Selection rings (shown in both modes; selection is kept across switches).
	if (const UShamanSelectionComponent* Selection = PC->Selection)
	{
		const TArray<AShamanUnitBase*> Units = Selection->GetSelectedUnits();
		for (const AShamanUnitBase* U : Units) DrawSelectionRing(U, Sel.RingRadiusScale, Sel.RingColor);
		if (bRTS && Units.Num() > 0)
			DrawLine(FString::Printf(TEXT("Selected: %d"), Units.Num()), SW * 0.5f, 62.f * UI, Sel.RingColor, 0.85f * UI, true);
	}

	// Command marker: a shrinking ring where the last order went (gold around the Shaman for "guard").
	FVector CmdAt; float CmdAge = 0.f; bool bGuard = false;
	if (bRTS && PC->GetLastCommand(CmdAt, CmdAge, bGuard) && CmdAge < 0.9f)
	{
		const float T = CmdAge / 0.9f;
		FLinearColor C = bGuard ? Sel.GuardMarkerColor : Sel.MoveMarkerColor;
		C.A = 1.f - T;
		DrawGroundCircle(CmdAt, FMath::Lerp(bGuard ? 160.f : 110.f, 40.f, T), C, 3.f);
	}

	// Drag box: translucent fill with a glowing edge.
	if (bRTS && PC->IsDragSelecting())
	{
		FVector2D A, B;
		PC->GetDragRect(A, B);
		const float X0 = FMath::Min(A.X, B.X), Y0 = FMath::Min(A.Y, B.Y), X1 = FMath::Max(A.X, B.X), Y1 = FMath::Max(A.Y, B.Y);
		DrawRect(Sel.BoxFillColor, X0, Y0, X1 - X0, Y1 - Y0);
		const FLinearColor Glow(Sel.BoxEdgeColor.R, Sel.BoxEdgeColor.G, Sel.BoxEdgeColor.B, 0.3f);
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const FLinearColor C = Pass == 0 ? Glow : Sel.BoxEdgeColor;
			const float T = Pass == 0 ? 6.f : 2.f;
			AHUD::DrawLine(X0, Y0, X1, Y0, C, T); AHUD::DrawLine(X1, Y0, X1, Y1, C, T);
			AHUD::DrawLine(X1, Y1, X0, Y1, C, T); AHUD::DrawLine(X0, Y1, X0, Y0, C, T);
		}
	}
}

// ---- Spell panel -------------------------------------------------------------------------------------------------------

bool AShamanHUD::IsOverSpellPanel(const FVector2D& ScreenPos) const
{
	return bPanelDrawn && PanelRect.bIsValid && PanelRect.IsInside(ScreenPos);
}

bool AShamanHUD::HitTestSpellPanel(const FVector2D& ScreenPos, FShamanSpellSlot& OutSlot) const
{
	if (!bPanelDrawn) return false;
	const int32 I = FShamanSpellPanelLayout::HitTest(PanelSlotRects, ScreenPos);
	if (!PanelModel.Slots.IsValidIndex(I)) return false;
	OutSlot = PanelModel.Slots[I];
	return true;
}

void AShamanHUD::DrawSlotText(const FString& Text, const FBox2D& Box, float Y, const FLinearColor& Color, float MaxScale)
{
	UFont* Font = GEngine->GetMediumFont();
	float W = 0.f, H = 0.f;
	GetTextSize(Text, W, H, Font, 1.f);
	const float Avail = Box.GetSize().X - 6.f;
	const float Scale = W > 0.f ? FMath::Min(MaxScale, Avail / W) : MaxScale; // shrink long names to fit the tile
	const float X = Box.GetCenter().X - W * Scale * 0.5f;
	DrawText(Text, Color, X, Y, Font, Scale);
}

void AShamanHUD::DrawSpellPanel(const AShamanCharacter* S, float SW, float SH, float UI, bool bRTS)
{
	PanelModel = FShamanSpellPanelModel::Build(S->Spells, S->EffectDispatcher, S->GetSelectedSpellId());
	FShamanSpellPanelLayout::ComputeSlotRects(PanelModel.Slots.Num(), SW, SH, UI, PanelSlotRects, PanelRect);
	bPanelDrawn = true;

	const FLinearColor Frame(0.10f, 0.07f, 0.03f, 0.78f), Ink(0.22f, 0.12f, 0.04f, 1.f);
	DrawRect(Frame, PanelRect.Min.X, PanelRect.Min.Y, PanelRect.GetSize().X, PanelRect.GetSize().Y);
	DrawLine(TEXT("SPELLS"), PanelRect.GetCenter().X, PanelRect.Min.Y + 6.f * UI, FLinearColor(1.f, 0.85f, 0.45f), 0.8f * UI, true);

	if (!PanelModel.bHasSpellData)
	{
		DrawLine(TEXT("No spell data"), PanelRect.GetCenter().X, PanelRect.Max.Y + 4.f * UI, FLinearColor(1.f, 0.3f, 0.3f), 0.75f * UI, true);
		return;
	}

	for (int32 i = 0; i < PanelModel.Slots.Num(); ++i)
	{
		const FShamanSpellSlot& Slot = PanelModel.Slots[i];
		const FBox2D& B = PanelSlotRects[i];
		const FVector2D Sz = B.GetSize();
		const float Top = B.Min.Y;
		switch (Slot.State)
		{
		case EShamanSpellSlotState::Available:
		{
			DrawRect(FLinearColor(0.86f, 0.68f, 0.36f, 1.f), B.Min.X, B.Min.Y, Sz.X, Sz.Y); // parchment tile
			DrawRect(FLinearColor(0.15f, 0.35f, 0.95f, 1.f), B.Min.X + 5.f * UI, Top + 5.f * UI, 7.f * UI, 7.f * UI); // mana pip
			const FString Full = Slot.DisplayName.ToString();
			FString Name = Full, Alias;
			if (Full.Split(TEXT(" ("), &Name, &Alias)) Alias = TEXT("(") + Alias; // "Blast (Taka)" -> two lines
			DrawSlotText(Name, B, Top + Sz.Y * 0.22f, Ink, 0.85f * UI);
			if (!Alias.IsEmpty()) DrawSlotText(Alias, B, Top + Sz.Y * 0.44f, Ink, 0.7f * UI);
			const FString Cost = Slot.bSuperSpell ? FString::Printf(TEXT("x%d"), Slot.Charges) : FString::Printf(TEXT("%.0f mana"), Slot.ManaCost);
			DrawSlotText(Cost, B, Top + Sz.Y * 0.70f, Slot.bAffordable ? Ink : FLinearColor(0.75f, 0.05f, 0.05f), 0.65f * UI);
			if (Slot.CooldownRemaining > 0.f && Slot.CooldownTotal > 0.f) // recharge shade, shrinking as it recovers
			{
				const float F = FMath::Clamp(Slot.CooldownRemaining / Slot.CooldownTotal, 0.f, 1.f);
				DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), B.Min.X, B.Min.Y, Sz.X, Sz.Y * F);
			}
			if (!Slot.bAffordable) DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.25f), B.Min.X, B.Min.Y, Sz.X, Sz.Y);
			break;
		}
		case EShamanSpellSlotState::Discoverable:
			DrawRect(FLinearColor(0.55f, 0.43f, 0.24f, 1.f), B.Min.X, B.Min.Y, Sz.X, Sz.Y);
			DrawSlotText(TEXT("?"), B, Top + Sz.Y * 0.08f, Ink, 2.2f * UI);
			DrawSlotText(TEXT("undiscovered"), B, Top + Sz.Y * 0.72f, Ink, 0.55f * UI);
			break;
		case EShamanSpellSlotState::Unimplemented:
			DrawRect(FLinearColor(0.33f, 0.33f, 0.33f, 0.95f), B.Min.X, B.Min.Y, Sz.X, Sz.Y);
			DrawSlotText(Slot.DisplayName.ToString(), B, Top + Sz.Y * 0.25f, FLinearColor(0.85f, 0.85f, 0.85f), 0.7f * UI);
			DrawSlotText(TEXT("not implemented"), B, Top + Sz.Y * 0.62f, FLinearColor(1.f, 0.55f, 0.45f), 0.55f * UI);
			break;
		}
		if (Slot.bSelected) // selected: bright border
		{
			const FLinearColor Gold(1.f, 0.9f, 0.3f, 1.f);
			const float T = 3.f * UI;
			AHUD::DrawLine(B.Min.X, B.Min.Y, B.Max.X, B.Min.Y, Gold, T); AHUD::DrawLine(B.Max.X, B.Min.Y, B.Max.X, B.Max.Y, Gold, T);
			AHUD::DrawLine(B.Max.X, B.Max.Y, B.Min.X, B.Max.Y, Gold, T); AHUD::DrawLine(B.Min.X, B.Max.Y, B.Min.X, B.Min.Y, Gold, T);
		}
	}

	float Y = PanelRect.Max.Y + 4.f * UI;
	if (PanelModel.NumHiddenUnimplemented > 0)
	{
		DrawLine(FString::Printf(TEXT("+%d spells not implemented yet"), PanelModel.NumHiddenUnimplemented), PanelRect.GetCenter().X, Y,
			FLinearColor(1.f, 1.f, 1.f, 0.55f), 0.6f * UI, true);
		Y += 16.f * UI;
	}
	DrawLine(bRTS ? TEXT("Click a spell to select it") : TEXT("Wheel / Q: change spell"), PanelRect.GetCenter().X, Y,
		FLinearColor(1.f, 1.f, 1.f, 0.55f), 0.6f * UI, true);
}

void AShamanHUD::DrawGroundCircle(const FVector& Centre, float Radius, const FLinearColor& Color, float Thickness)
{
	if (!PlayerOwner) return;
	const FVector Up = FShamanSpace::GetUp(this, Centre);
	const FVector Hint = FMath::Abs(Up.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
	const FVector X = (Hint - Up * FVector::DotProduct(Hint, Up)).GetSafeNormal();
	const FVector Y = FVector::CrossProduct(Up, X);
	const FVector Lifted = Centre + Up * 6.f; // just above the ground so it is not buried in it
	FVector2D Prev;
	bool bPrev = false;
	for (int32 i = 0; i <= 24; ++i)
	{
		const float A = 2.f * PI * i / 24;
		FVector2D P;
		const bool bOk = PlayerOwner->ProjectWorldLocationToScreen(Lifted + (X * FMath::Cos(A) + Y * FMath::Sin(A)) * Radius, P, false);
		if (bOk && bPrev) AHUD::DrawLine(Prev.X, Prev.Y, P.X, P.Y, Color, Thickness);
		Prev = P;
		bPrev = bOk;
	}
}
