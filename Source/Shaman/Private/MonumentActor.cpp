#include "MonumentActor.h"
#include "SpellComponent.h"
#include "ProgressionComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

AMonumentActor::AMonumentActor() { PrimaryActorTick.bCanEverTick = false; }

const FMonumentRow* AMonumentActor::GetRow() const
{
	return MonumentTable ? MonumentTable->FindRow<FMonumentRow>(MonumentRowName, TEXT("Monument")) : nullptr;
}

bool AMonumentActor::CanWorship(AActor* W) const
{
	const FMonumentRow* Row = GetRow();
	if (!Row || !W || bSpent) return false;
	if (Row->WorshipRule == EWorshipRule::ShamanAndBraves) return true;
	return W->FindComponentByClass<USpellComponent>() != nullptr; // only a Shaman has a SpellComponent
}

float AMonumentActor::GetProgress01() const
{
	const FMonumentRow* Row = GetRow();
	return Row ? FMath::Clamp(Progress / FMath::Max(1.f, Row->WorshipRequired), 0.f, 1.f) : 0.f;
}

bool AMonumentActor::Worship(AActor* Worshipper, AActor* Beneficiary, float DeltaTime)
{
	const FMonumentRow* Row = GetRow();
	if (!Row || bSpent || !Worshipper) return false;
	if (ChargePool < 0) ChargePool = RewardCountOverride >= 0 ? RewardCountOverride : Row->DefaultRewardCount;

	const double Now = GetWorld()->GetTimeSeconds();
	for (auto It = ActiveWorshippers.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || Now - It.Value() > 0.5) It.RemoveCurrent();
	if (!ActiveWorshippers.Contains(Worshipper) && ActiveWorshippers.Num() >= Row->MaxWorshippers) return false;
	ActiveWorshippers.Add(Worshipper, Now);

	if (!CanWorship(Worshipper)) return Row->bIneligibleStillAnimate; // e.g. Gargoyle: followers go through the motions

	Progress += Row->ProgressPerWorshipperPerSec * DeltaTime;
	OnProgress.Broadcast(GetProgress01());
	if (Progress >= Row->WorshipRequired) CompleteCycle(Beneficiary, *Row);
	return true;
}

void AMonumentActor::CompleteCycle(AActor* Ben, const FMonumentRow& Row)
{
	const EMonumentRewardType Type = bOverrideRewardType ? RewardTypeOverride : Row.DefaultRewardType;
	const int32 Count = RewardCountOverride >= 0 ? RewardCountOverride : Row.DefaultRewardCount;
	USpellComponent* Spells = Ben ? Ben->FindComponentByClass<USpellComponent>() : nullptr;
	UProgressionComponent* Prog = Ben ? Ben->FindComponentByClass<UProgressionComponent>() : nullptr;
	Progress = 0.f;

	switch (Type)
	{
	case EMonumentRewardType::UnlockSpell:
		if (Spells) Spells->LearnSpell(RewardId);
		if (Prog && Row.bPersistsAcrossMaps) Prog->UnlockSpell(RewardId);
		bSpent = true;
		break;
	case EMonumentRewardType::UnlockBuilding:
		if (Prog) Prog->UnlockBuilding(RewardId); // building list also persists if bPersistsAcrossMaps, per Progression state
		bSpent = true;
		break;
	case EMonumentRewardType::GrantCharges:
	{
		const int32 Give = FMath::Min(Row.ChargesPerCycle, ChargePool);
		if (Spells) Spells->AddCharges(RewardId, Give);
		ChargePool -= Give;
		if (ChargePool <= 0) { bSpent = true; }
		break;
	}
	case EMonumentRewardType::WorldEvent:
		OnWorldEvent.Broadcast(RewardId, Count, Ben);
		bSpent = true;
		break;
	}

	OnCompleted.Broadcast(this, Ben);
	if (bSpent && Row.bSinksWhenDepleted) { OnDepleted.Broadcast(); SetLifeSpan(4.f); } // sinks and disappears
}
