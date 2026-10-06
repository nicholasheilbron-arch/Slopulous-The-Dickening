#include "UI/ShamanHUD.h"
#include "Characters/ShamanCharacter.h"
#include "Characters/HealthComponent.h"
#include "SpellComponent.h"
#include "Game/ShamanGameMode.h"
#include "Tribes/TribeSubsystem.h"
#include "TribeComponent.h"
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
	case ESpellCastResult::UnknownSpell:   return TEXT("Spell not known");
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
	if (!S) return;

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
		DrawLine(FString::Printf(TEXT("Spell %d / %d   (mouse wheel or Q to change)"), S->GetSelectedSpellIndex() + 1, S->Spells->KnownSpellIds.Num()),
			SW * 0.5f, SH - 64.f * UI, FLinearColor(1, 1, 1, 0.6f), 0.75f * UI, true);
	}
	float Age = 0.f;
	const ESpellCastResult Feedback = S->GetLastCastFeedback(Age);
	if (Feedback != ESpellCastResult::Success && Age < 1.5f)
		DrawLine(CastResultText(Feedback), SW * 0.5f, SH - 150.f * UI, FLinearColor(1.f, 0.35f, 0.3f), 1.f * UI, true);

	// Crosshair.
	DrawRect(FLinearColor(1, 1, 1, 0.8f), SW * 0.5f - 1.f, SH * 0.5f - 8.f, 2.f, 16.f);
	DrawRect(FLinearColor(1, 1, 1, 0.8f), SW * 0.5f - 8.f, SH * 0.5f - 1.f, 16.f, 2.f);

	// Interaction prompt.
	const FText Focus = S->GetFocusText();
	if (!Focus.IsEmpty()) DrawLine(Focus.ToString(), SW * 0.5f, SH * 0.5f + 40.f * UI, FLinearColor::White, 1.f * UI, true);

	// Hazards and death.
	if (S->IsDrowning())
		DrawLine(FString::Printf(TEXT("Drowning! Get to shallow water (%.1fs)"), S->GetDrownRemaining()), SW * 0.5f, SH * 0.3f, FLinearColor(0.4f, 0.7f, 1.f), 1.4f * UI, true);
	if (!S->IsAlive())
	{
		const float T = Tribes ? Tribes->GetRebirthRemaining(MyTribe) : -1.f;
		const FString Msg = T >= 0.f
			? FString::Printf(TEXT("You have fallen. Reborn at the Reincarnation Circle in %.1fs"), T)
			: FString(TEXT("You have fallen and no Reincarnation Circle remains."));
		DrawLine(Msg, SW * 0.5f, SH * 0.4f, FLinearColor(1.f, 0.85f, 0.6f), 1.6f * UI, true);
	}

	// Enemy Shaman status (top-left).
	if (Tribes)
	{
		for (int32 Id = 0; Id < Tribes->GetNumTribes(); ++Id)
		{
			if (Id == MyTribe) continue;
			const AShamanUnitBase* E = Tribes->GetShaman(Id);
			const float T = Tribes->GetRebirthRemaining(Id);
			const FString State = !E ? TEXT("unknown") : E->IsAlive() ? TEXT("alive") : T >= 0.f ? FString::Printf(TEXT("reincarnating (%.0fs)"), T) : TEXT("eliminated");
			DrawLine(FString::Printf(TEXT("Enemy Shaman: %s   followers %d"), *State, Tribes->GetFollowerCount(Id)), 20.f, 12.f + 22.f * UI * (Id - 1), Tribes->GetTribeColor(Id), 0.85f * UI);
		}
	}
}
