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
#include "Engine/Texture2D.h"

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
	case ESpellCastResult::NoCharges:      return TEXT("No charge ready (right-click the spell in RTS mode to charge it)");
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

	// Bars (bottom-left). Tribe mana adds two flow lines, so the block starts higher.
	const bool bTribeMana = S->Spells->bTribeManaGeneration;
	const float BX = 30.f * UI, BW = 320.f * UI, BH = 22.f * UI;
	float BY = SH - (bTribeMana ? 150.f : 110.f) * UI;
	DrawBar(BX, BY, BW, BH, S->Health->GetHealthFraction(), FLinearColor(0.75f, 0.1f, 0.1f),
		FString::Printf(TEXT("Health %.0f / %.0f"), S->Health->Health, S->Health->MaxHealth));
	BY += BH + 10.f * UI;
	if (!bTribeMana)
	{
		DrawBar(BX, BY, BW, BH, S->Spells->Mana / FMath::Max(1.f, S->Spells->MaxMana), FLinearColor(0.15f, 0.35f, 0.95f),
			FString::Printf(TEXT("Mana %.0f / %.0f   +%.2f/s"), S->Spells->Mana, S->Spells->MaxMana, S->Spells->GetManaRegen()));
		BY += BH + 10.f * UI;
	}
	else
	{
		// Populous mana: who makes it, where it goes this moment, and a red flash when the full pool throws it away.
		const USpellComponent* Sp = S->Spells;
		const FTribeManaBreakdown Made = Sp->GetManaBreakdown();
		const bool bWasting = Sp->GetWastedPerSecond() > 0.f;
		const bool bFlash = bWasting && FMath::Fmod((float)GetWorld()->GetRealTimeSeconds(), 0.8f) < 0.4f;
		DrawBar(BX, BY, BW, BH, Sp->Mana / FMath::Max(1.f, Sp->MaxMana), bFlash ? FLinearColor(0.9f, 0.1f, 0.08f) : FLinearColor(0.15f, 0.35f, 0.95f),
			FString::Printf(TEXT("Mana pool %.0f / %.0f      +%.1f / min"), Sp->Mana, Sp->MaxMana, Made.PerMinute));
		BY += BH + 6.f * UI;
		const float ToSpells = Sp->GetChargeDrawPerSecond() * 60.f, ToTraining = Sp->GetTrainingPerSecond() * 60.f;
		const FString Flow = bWasting
			? FString::Printf(TEXT("POOL FULL: %.1f / min wasted - right-click a spell to charge it"), Sp->GetWastedPerSecond() * 60.f)
			: FString::Printf(TEXT("-> Spells %.1f / min (%d charging)   Training %.1f / min   Pool +%.1f / min"),
				ToSpells, Sp->GetActiveChargingCount(), ToTraining, Sp->GetBankedPerSecond() * 60.f);
		DrawLine(Flow, BX, BY, bWasting ? FLinearColor(1.f, 0.3f, 0.25f) : FLinearColor(0.75f, 0.85f, 1.f), 0.75f * UI);
		BY += 18.f * UI;
		const FTribeManaConfig& C = Sp->ManaConfig;
		DrawLine(FString::Printf(TEXT("Made by: Shaman %.1f   %d working x%.1f   %d moving x%.1f   %d idle x%.1f"),
			Made.bShamanAlive ? C.ShamanPerMinute : 0.f, Made.Working, C.BraveWorkingPerMinute, Made.Moving, C.MovingPerMinute,
			Made.Idle, C.IdlePerMinute), BX, BY, FLinearColor(1.f, 1.f, 1.f, 0.6f), 0.7f * UI);
		BY += 22.f * UI;
	}
	DrawLine(FString::Printf(TEXT("Followers %d / %d   (more followers = faster mana & rebirth)"), Followers, Cap), BX, BY, FLinearColor::White, 0.85f * UI);

	if (!S->Spells->SpellTable)
		DrawLine(TEXT("No spell data loaded: import Content/Data/Spells.csv as a DataTable (row type SpellRow)"), SW * 0.5f, SH - 120.f * UI, FLinearColor(1.f, 0.3f, 0.3f), 1.f * UI, true);

	// Selected spell (bottom-centre).
	const FName SpellId = S->GetSelectedSpellId();
	if (const FSpellRow* Row = S->Spells->GetSpellRow(SpellId))
	{
		const float CD = S->Spells->GetCooldownRemaining(SpellId);
		const bool bCharges = S->Spells->bUseChargeEconomy;
		const bool bAffordable = bCharges ? S->Spells->GetAvailableCharges(SpellId) > 0 : S->Spells->Mana >= (Row->bSuperSpell ? 0.f : Row->ManaCost);
		const FLinearColor C = (CD > 0.f || !bAffordable) ? FLinearColor(0.6f, 0.6f, 0.6f) : FLinearColor(1.f, 0.8f, 0.3f);
		DrawLine(Row->DisplayName.ToString(), SW * 0.5f, SH - 120.f * UI, C, 1.3f * UI, true);
		const FString Cost = !bCharges ? FString::Printf(TEXT("%.0f mana"), Row->ManaCost)
			: FString::Printf(TEXT("charges %d/%d%s"), S->Spells->GetStoredCharges(SpellId), S->Spells->GetMaxCharges(SpellId),
				S->Spells->GetTemporaryCharges(SpellId) > 0 ? *FString::Printf(TEXT(" +%d temp"), S->Spells->GetTemporaryCharges(SpellId)) : TEXT(""));
		FString Info = FString::Printf(TEXT("%s   range %.0f (%.0fm)   %s"), *Cost, Row->RangeRaw,
			S->Spells->GetRangeUnits(SpellId) / 100.f, CD > 0.f ? *FString::Printf(TEXT("ready in %.1fs"), CD) : (bCharges && !bAffordable) ? TEXT("no charge ready") : TEXT("ready"));
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
		? TEXT("Enter: Shaman mode   LMB: select / drag box (Shift adds)   RMB: move there (on your Shaman: guard him)   Spell panel: LMB select, RMB charge   WASD: pan   Q/E or MMB drag: rotate   Wheel: zoom   F: focus   Home: Shaman   Backspace: clear")
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

namespace ShamanSpellPanelStyle
{
	static const FLinearColor Frame(0.10f, 0.07f, 0.03f, 0.82f);
	static const FLinearColor Parchment(0.87f, 0.71f, 0.42f, 1.f);
	static const FLinearColor ParchmentDim(0.62f, 0.50f, 0.30f, 1.f);
	static const FLinearColor Ink(0.24f, 0.13f, 0.05f, 1.f);
	static const FLinearColor Border(0.18f, 0.10f, 0.04f, 1.f);
	static const FLinearColor Blue(0.22f, 0.48f, 1.f, 1.f);      // permanent charges
	static const FLinearColor Red(0.92f, 0.16f, 0.10f, 1.f);     // temporary (monument) charges
	static const FLinearColor Amber(0.98f, 0.68f, 0.12f, 1.f);   // charging progress
	static const FLinearColor Paused(0.56f, 0.53f, 0.48f, 1.f);  // progress kept while charging is off
	static const FLinearColor Gold(1.f, 0.88f, 0.28f, 1.f);      // selection
	static const FLinearColor Lamp(0.35f, 0.92f, 0.35f, 1.f);    // charging lamp
}

void AShamanHUD::DrawBoxOutline(const FBox2D& B, const FLinearColor& Color, float T)
{
	AHUD::DrawLine(B.Min.X, B.Min.Y, B.Max.X, B.Min.Y, Color, T); AHUD::DrawLine(B.Max.X, B.Min.Y, B.Max.X, B.Max.Y, Color, T);
	AHUD::DrawLine(B.Max.X, B.Max.Y, B.Min.X, B.Max.Y, Color, T); AHUD::DrawLine(B.Min.X, B.Max.Y, B.Min.X, B.Min.Y, Color, T);
}

void AShamanHUD::DrawChargeDot(float CX, float CY, float R, const FLinearColor& Fill, bool bFilled)
{
	auto Blob = [this, CX, CY](float Rad, const FLinearColor& C)
	{
		DrawRect(C, CX - Rad, CY - Rad * 0.6f, 2.f * Rad, 1.2f * Rad); // two crossed bars read as a round dot
		DrawRect(C, CX - Rad * 0.6f, CY - Rad, 1.2f * Rad, 2.f * Rad);
	};
	Blob(R + 1.f, ShamanSpellPanelStyle::Border);
	Blob(R, bFilled ? Fill : FLinearColor(0.70f, 0.56f, 0.32f, 1.f)); // empty socket
	if (bFilled) DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.35f), CX - R * 0.55f, CY - R * 0.55f, R * 0.5f, R * 0.5f); // glint
}

void AShamanHUD::DrawPlaceholderIcon(const FString& Monogram, const FVector2D& C, float R, const FLinearColor& Ink, float UI)
{
	FVector2D Prev = C + FVector2D(R, 0.f);
	for (int32 i = 1; i <= 8; ++i) // octagonal ink ring
	{
		const float A = 2.f * PI * i / 8.f;
		const FVector2D P = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
		AHUD::DrawLine(Prev.X, Prev.Y, P.X, P.Y, Ink, 1.5f * UI);
		Prev = P;
	}
	UFont* Font = GEngine->GetMediumFont();
	float W = 0.f, H = 0.f;
	const float Scale = 0.95f * UI;
	GetTextSize(Monogram, W, H, Font, Scale);
	DrawText(Monogram, Ink, C.X - W * 0.5f, C.Y - H * 0.5f, Font, Scale);
}

void AShamanHUD::DrawSpellTile(const FShamanSpellSlot& Slot, const FBox2D& B, float UI)
{
	using namespace ShamanSpellPanelStyle;
	const FVector2D Sz = B.GetSize();
	const float X = B.Min.X, Y = B.Min.Y, W = Sz.X, H = Sz.Y;
	const FVector2D Centre(X + W * 0.5f, Y + H * 0.46f);

	switch (Slot.State)
	{
	case EShamanSpellSlotState::Locked: // blank, dim, padlock: nothing revealed, nothing to click
	{
		DrawRect(FLinearColor(0.40f, 0.32f, 0.20f, 0.85f), X, Y, W, H);
		const FLinearColor Faded(0.20f, 0.13f, 0.06f, 0.75f);
		const float CX = Centre.X, CY = Centre.Y + 3.f * UI;
		DrawRect(Faded, CX - 7.f * UI, CY - 2.f * UI, 14.f * UI, 11.f * UI);
		AHUD::DrawLine(CX - 4.f * UI, CY - 2.f * UI, CX - 4.f * UI, CY - 8.f * UI, Faded, 2.f * UI);
		AHUD::DrawLine(CX - 4.f * UI, CY - 8.f * UI, CX + 4.f * UI, CY - 8.f * UI, Faded, 2.f * UI);
		AHUD::DrawLine(CX + 4.f * UI, CY - 8.f * UI, CX + 4.f * UI, CY - 2.f * UI, Faded, 2.f * UI);
		DrawBoxOutline(B, Border, 1.5f * UI);
		return;
	}
	case EShamanSpellSlotState::Discoverable: // "?" only: no name, no icon until learned
		DrawRect(ParchmentDim, X, Y, W, H);
		DrawSlotText(TEXT("?"), B, Y + H * 0.12f, Ink, 2.0f * UI);
		DrawSlotText(TEXT("undiscovered"), B, Y + H * 0.74f, Ink, 0.5f * UI);
		DrawBoxOutline(B, Border, 1.5f * UI);
		return;
	case EShamanSpellSlotState::Unimplemented:
		DrawRect(FLinearColor(0.33f, 0.33f, 0.33f, 0.95f), X, Y, W, H);
		DrawPlaceholderIcon(Slot.Monogram, Centre - FVector2D(0.f, 4.f * UI), H * 0.2f, FLinearColor(0.75f, 0.75f, 0.75f, 0.8f), UI);
		DrawSlotText(Slot.DisplayName.ToString(), B, Y + H * 0.66f, FLinearColor(0.88f, 0.88f, 0.88f), 0.5f * UI);
		DrawSlotText(TEXT("no effect yet"), B, Y + H * 0.80f, FLinearColor(1.f, 0.55f, 0.45f), 0.45f * UI);
		DrawBoxOutline(B, Border, 1.5f * UI);
		return;
	default:
		break;
	}

	// Available: permanent and / or temporary charges.
	const bool bTempOnly = Slot.IsTemporaryOnly();
	const bool bChargeSlots = Slot.bChargeEconomy && Slot.bPermanent && Slot.MaxCharges > 0;
	const bool bIdle = bChargeSlots && !Slot.bCharging && !Slot.bFull;
	DrawRect(Parchment, X, Y, W, H);

	// Charge dots along the top: blue = ready permanent charge (empty socket = not yet), red = temporary charges.
	const float DotR = 3.2f * UI, DotStep = 2.f * DotR + 3.f * UI, DotY = Y + 5.f * UI + DotR;
	float DotX = X + 5.f * UI + DotR;
	if (bChargeSlots)
		for (int32 i = 0; i < Slot.MaxCharges; ++i) { DrawChargeDot(DotX, DotY, DotR, Blue, i < Slot.StoredCharges); DotX += DotStep; }
	if (Slot.TemporaryCharges > 0)
	{
		if (bChargeSlots) DotX += 3.f * UI;
		const int32 Shown = FMath::Min(Slot.TemporaryCharges, 5);
		for (int32 i = 0; i < Shown; ++i) { DrawChargeDot(DotX, DotY, DotR, Red, true); DotX += DotStep; }
		if (Slot.TemporaryCharges > Shown) DrawText(FString::Printf(TEXT("+%d"), Slot.TemporaryCharges - Shown), Red, DotX - DotR, DotY - 6.f * UI, GEngine->GetMediumFont(), 0.45f * UI);
	}
	if (bChargeSlots && Slot.bCharging && !Slot.bFull && DotX < X + W - 9.f * UI) // charging lamp, top-right
		DrawRect(Lamp, X + W - 9.f * UI, Y + 4.f * UI, 5.f * UI, 5.f * UI);

	// Icon (row texture) or placeholder monogram; faded while nothing is ready to cast.
	const FLinearColor InkNow = Slot.bAffordable ? Ink : FLinearColor(Ink.R, Ink.G, Ink.B, 0.45f);
	const float R = H * 0.21f;
	if (Slot.Icon) DrawTexture(Slot.Icon, Centre.X - R, Centre.Y - R, 2.f * R, 2.f * R, 0.f, 0.f, 1.f, 1.f,
		Slot.bAffordable ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.45f));
	else DrawPlaceholderIcon(Slot.Monogram, Centre, R, InkNow, UI);
	FString Name = Slot.DisplayName.ToString(), Alias;
	Name.Split(TEXT(" ("), &Name, &Alias);
	DrawSlotText(Name, B, Y + H * 0.67f, InkNow, 0.5f * UI);

	// Bottom: progress toward the next permanent charge (amber while charging, grey while switched off).
	const float BarX = X + 5.f * UI, BarW = W - 10.f * UI, BarH = 5.f * UI, BarY = Y + H - BarH - 4.f * UI;
	if (bChargeSlots)
	{
		DrawRect(FLinearColor(0.18f, 0.11f, 0.04f, 0.9f), BarX - 1.f, BarY - 1.f, BarW + 2.f, BarH + 2.f);
		if (!Slot.bFull && Slot.Progress01 > 0.f) DrawRect(Slot.bCharging ? Amber : Paused, BarX, BarY, BarW * Slot.Progress01, BarH);
	}
	else if (bTempOnly)
		DrawSlotText(TEXT("temporary"), B, BarY - 5.f * UI, Red, 0.42f * UI);
	else if (!Slot.bChargeEconomy) // mana-at-cast fallback (no charge data): the Phase 1 cost label
		DrawSlotText(Slot.bSuperSpell ? FString::Printf(TEXT("x%d"), Slot.Charges) : FString::Printf(TEXT("%.0f mana"), Slot.ManaCost),
			B, BarY - 6.f * UI, Slot.bAffordable ? Ink : FLinearColor(0.75f, 0.05f, 0.05f), 0.45f * UI);

	if (bIdle) // not charging: dimmed, "off" on the bar
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.20f), X, Y, W, H);
		DrawSlotText(TEXT("off"), FBox2D(FVector2D(X, BarY), FVector2D(X + W, BarY)), BarY - 3.f * UI, FLinearColor(1.f, 1.f, 1.f, 0.85f), 0.42f * UI);
	}
	if (Slot.CooldownRemaining > 0.f && Slot.CooldownTotal > 0.f) // recast shade, shrinking as it recovers
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.40f), X, Y, W, H * FMath::Clamp(Slot.CooldownRemaining / Slot.CooldownTotal, 0.f, 1.f));

	DrawBoxOutline(B, bTempOnly ? Red : Border, bTempOnly ? 2.f * UI : 1.5f * UI);
	if (Slot.bSelected)
	{
		DrawBoxOutline(FBox2D(B.Min - FVector2D(2.f, 2.f) * UI, B.Max + FVector2D(2.f, 2.f) * UI), FLinearColor(0.f, 0.f, 0.f, 0.6f), 1.f * UI);
		DrawBoxOutline(B, Gold, 3.f * UI);
	}
}

void AShamanHUD::DrawSpellTooltip(const FShamanSpellSlot& Slot, const FBox2D& Tile, float UI, bool bRTS)
{
	TArray<FString> Lines;
	switch (Slot.State)
	{
	case EShamanSpellSlotState::Locked:
		Lines.Add(TEXT("Locked"));
		Lines.Add(TEXT("Not available on this map"));
		break;
	case EShamanSpellSlotState::Discoverable:
		Lines.Add(TEXT("Undiscovered spell"));
		Lines.Add(TEXT("Can be learned on this map"));
		break;
	case EShamanSpellSlotState::Unimplemented:
		Lines.Add(Slot.DisplayName.ToString());
		Lines.Add(TEXT("Effect not implemented yet: cannot be cast or charged"));
		break;
	default:
	{
		Lines.Add(Slot.DisplayName.ToString());
		if (!Slot.bChargeEconomy)
		{
			Lines.Add(FString::Printf(TEXT("%.0f mana per cast"), Slot.ManaCost));
			break;
		}
		if (Slot.bPermanent && Slot.MaxCharges > 0)
		{
			Lines.Add(FString::Printf(TEXT("Charges %d / %d%s"), Slot.StoredCharges, Slot.MaxCharges,
				Slot.TemporaryCharges > 0 ? *FString::Printf(TEXT("   +%d temporary"), Slot.TemporaryCharges) : TEXT("")));
			Lines.Add(FString::Printf(TEXT("%.0f mana per charge"), Slot.ChargeCost));
			if (Slot.bFull) Lines.Add(TEXT("Full: not drawing mana"));
			else if (!Slot.bCharging) Lines.Add(FString::Printf(TEXT("Charging OFF (%.0f%% kept)%s"), Slot.Progress01 * 100.f, bRTS ? TEXT(" - right-click to charge") : TEXT("")));
			else if (Slot.ManaPerSecond > 0.f)
				Lines.Add(FString::Printf(TEXT("Charging: %.1f mana / min, next charge in %.0fs"), Slot.ManaPerSecond * 60.f,
					FMath::Max(0.f, Slot.ChargeCost - Slot.ProgressMana) / Slot.ManaPerSecond));
			else Lines.Add(TEXT("Charging: waiting for mana"));
		}
		else if (Slot.TemporaryCharges > 0)
			Lines.Add(FString::Printf(TEXT("%d temporary charge(s) from a monument - no recharge"), Slot.TemporaryCharges));
		Lines.Add(Slot.bAffordable ? TEXT("Ready to cast") : TEXT("No charge ready"));
		break;
	}
	}
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 0.62f * UI, LineH = 15.f * UI, Pad = 6.f * UI;
	float MaxW = 0.f;
	for (const FString& L : Lines) { float W = 0.f, H = 0.f; GetTextSize(L, W, H, Font, Scale); MaxW = FMath::Max(MaxW, W); }
	const float BoxW = MaxW + 2.f * Pad, BoxH = Lines.Num() * LineH + 2.f * Pad;
	const float BX = PanelRect.Min.X - 8.f * UI - BoxW;
	const float BY = FMath::Clamp(Tile.Min.Y, 0.f, FMath::Max(0.f, Canvas->ClipY - BoxH));
	DrawRect(FLinearColor(0.08f, 0.05f, 0.02f, 0.9f), BX, BY, BoxW, BoxH);
	DrawBoxOutline(FBox2D(FVector2D(BX, BY), FVector2D(BX + BoxW, BY + BoxH)), ShamanSpellPanelStyle::Parchment, 1.f);
	for (int32 i = 0; i < Lines.Num(); ++i)
		DrawText(Lines[i], i == 0 ? FLinearColor(1.f, 0.85f, 0.45f) : FLinearColor(1.f, 1.f, 1.f, 0.9f), BX + Pad, BY + Pad + i * LineH, Font, Scale);
}

void AShamanHUD::DrawSpellPanel(const AShamanCharacter* S, float SW, float SH, float UI, bool bRTS)
{
	using namespace ShamanSpellPanelStyle;
	PanelModel = FShamanSpellPanelModel::Build(S->Spells, S->EffectDispatcher, S->GetSelectedSpellId());
	for (const FShamanSpellSlot& Slot : PanelModel.Slots) if (Slot.Icon) PanelIcons.AddUnique(Slot.Icon);
	FShamanSpellPanelLayout::ComputeSlotRects(PanelModel.Slots.Num(), SW, SH, UI, PanelSlotRects, PanelRect);
	bPanelDrawn = true;

	DrawRect(Frame, PanelRect.Min.X, PanelRect.Min.Y, PanelRect.GetSize().X, PanelRect.GetSize().Y);
	DrawBoxOutline(PanelRect, FLinearColor(0.55f, 0.40f, 0.18f, 0.9f), 1.f);
	DrawLine(TEXT("SPELLS"), PanelRect.GetCenter().X, PanelRect.Min.Y + 4.f * UI, FLinearColor(1.f, 0.85f, 0.45f), 0.75f * UI, true);

	if (!PanelModel.bHasSpellData)
	{
		DrawLine(TEXT("No spell data"), PanelRect.GetCenter().X, PanelRect.Max.Y + 4.f * UI, FLinearColor(1.f, 0.3f, 0.3f), 0.75f * UI, true);
		return;
	}

	for (int32 i = 0; i < PanelModel.Slots.Num(); ++i) DrawSpellTile(PanelModel.Slots[i], PanelSlotRects[i], UI);

	// Footer: shared mana economy and what the clicks do.
	const float FootY = PanelRect.Max.Y - FShamanSpellPanelLayout::FooterHeight(UI) + 3.f * UI;
	const float FX = PanelRect.Min.X + 7.f * UI, FW = PanelRect.GetSize().X - 14.f * UI;
	if (PanelModel.bChargeEconomy)
	{
		const bool bWasting = PanelModel.WastedPerSecond > 0.f;
		DrawLine(bWasting ? FString::Printf(TEXT("Pool full: %.0f / min wasted"), PanelModel.WastedPerSecond * 60.f)
			: FString::Printf(TEXT("+%.1f / min -> %d charging"), PanelModel.ManaRegen * 60.f, PanelModel.ActiveCharging),
			PanelRect.GetCenter().X, FootY, bWasting ? FLinearColor(1.f, 0.3f, 0.25f) : FLinearColor(0.75f, 0.85f, 1.f), 0.58f * UI, true);
		// Share of this second's generation going into charges.
		const float Frac = PanelModel.ManaRegen > 0.f ? FMath::Clamp(PanelModel.ChargeDrawPerSecond / PanelModel.ManaRegen, 0.f, 1.f) : 0.f;
		const float BarY = FootY + 15.f * UI;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), FX, BarY, FW, 3.f * UI);
		DrawRect(Amber, FX, BarY, FW * Frac, 3.f * UI);
	}
	else
	{
		DrawLine(FString::Printf(TEXT("Mana %.0f / %.0f"), PanelModel.Mana, PanelModel.MaxMana), PanelRect.GetCenter().X, FootY,
			FLinearColor(0.75f, 0.85f, 1.f), 0.58f * UI, true);
	}
	DrawLine(bRTS ? TEXT("LMB select   RMB charge on/off") : TEXT("Wheel / Q change   LMB cast"), PanelRect.GetCenter().X, FootY + 22.f * UI,
		FLinearColor(1.f, 1.f, 1.f, 0.55f), 0.55f * UI, true);
	if (S->IsSpellChargeDataMissing())
		DrawLine(TEXT("Spell charges off: reimport Content/Data/Spells.csv"), PanelRect.GetCenter().X, PanelRect.Max.Y + 4.f * UI,
			FLinearColor(1.f, 0.35f, 0.3f), 0.55f * UI, true);

	// Hover details (RTS: the cursor is visible).
	float MX = 0.f, MY = 0.f;
	if (bRTS && PlayerOwner && PlayerOwner->GetMousePosition(MX, MY))
	{
		const int32 Hover = FShamanSpellPanelLayout::HitTest(PanelSlotRects, FVector2D(MX, MY));
		if (PanelModel.Slots.IsValidIndex(Hover)) DrawSpellTooltip(PanelModel.Slots[Hover], PanelSlotRects[Hover], UI, bRTS);
	}
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
