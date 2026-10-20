#include "SpellComponent.h"
#include "SpellProjectile.h"
#include "SpellEffectDispatcherComponent.h"
#include "Characters/ShamanUnitBase.h"
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"

// ---- Pure charge rules ------------------------------------------------------------------------------------------------

int32 FSpellChargeRules::GetMaxCharges(const FSpellRow& Row)
{
	return FMath::Clamp<int32>(Row.MaxCharges, 0, (int32)MaxChargesLimit); // (int32): no ODR-use under C++14
}

bool FSpellChargeRules::IsChargeable(const FSpellRow& Row)
{
	return !Row.bSuperSpell && GetMaxCharges(Row) > 0 && GetChargeCost(Row) > 0.f;
}

void FSpellChargeRules::SplitBudget(float Budget, const TArray<float>& Needs, TArray<float>& OutGiven)
{
	const int32 N = Needs.Num();
	OutGiven.Init(0.f, N);
	if (N == 0 || Budget <= 0.f) return;
	// Smallest need first so leftover from nearly-full spells flows to the others; stable on ties (input order).
	TArray<int32> Order;
	Order.Reserve(N);
	for (int32 i = 0; i < N; ++i) Order.Add(i);
	Order.StableSort([&Needs](int32 A, int32 B) { return Needs[A] < Needs[B]; });
	float Remaining = Budget;
	int32 Left = N;
	for (int32 Idx : Order)
	{
		const float Share = Remaining / Left;
		const float Give = FMath::Clamp(Needs[Idx], 0.f, Share);
		OutGiven[Idx] = Give;
		Remaining -= Give;
		--Left;
	}
}

// ---- Component ----------------------------------------------------------------------------------------------------------

USpellComponent::USpellComponent() { PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickInterval = 0.1f; }

FTribeManaBreakdown USpellComponent::GetManaBreakdown() const
{
	FTribeManaBreakdown B;
	if (!bTribeManaGeneration) return B;
	const AShamanUnitBase* Self = Cast<AShamanUnitBase>(GetOwner());
	B.bShamanAlive = Self && Self->IsAlive();
	if (B.bShamanAlive) B.PerMinute += FTribeManaRules::RatePerMinute(true, EUnitRole::Shaman, EUnitSimState::Idle, true, ManaConfig);
	const UTribeComponent* Tribe = GetOwner() ? GetOwner()->FindComponentByClass<UTribeComponent>() : nullptr;
	if (!Tribe) return B;
	for (const UTribeMemberComponent* M : Tribe->GetFollowers())
	{
		if (!M || M->GetOwner() == GetOwner()) continue; // the Shaman is counted above
		const AShamanUnitBase* U = Cast<AShamanUnitBase>(M->GetOwner());
		const EUnitRole Role = U ? U->GetRow().Role : EUnitRole::Brave;
		const ETribeManaActivity Activity = FTribeManaRules::Classify(false, Role, M->GetSimState(), M->IsAlive());
		B.PerMinute += FTribeManaRules::RateFor(Activity, ManaConfig);
		B.Working += Activity == ETribeManaActivity::Working ? 1 : 0;
		B.Moving += Activity == ETribeManaActivity::Moving ? 1 : 0;
		B.Idle += Activity == ETribeManaActivity::Idle ? 1 : 0;
	}
	return B;
}

float USpellComponent::GetManaRegen() const
{
	if (bTribeManaGeneration) return GetManaBreakdown().PerMinute / 60.f; // Populous: only living members make mana
	return BaseRegen * (1.f + RegenPerFollower * FollowerCount) * TemporaryModifier;
}

void USpellComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* F)
{
	Super::TickComponent(DeltaTime, TickType, F);
	if (!bUseChargeEconomy)
	{
		Mana = FMath::Min(MaxMana, Mana + GetManaRegen() * DeltaTime); // Phase 1 rule, unchanged
		return;
	}
	AdvanceChargeEconomy(DeltaTime);
}

void USpellComponent::AdvanceChargeEconomy(float Seconds)
{
	if (Seconds <= 0.f) return;
	const float Step = FMath::Max(0.01f, ChargeStepSeconds);
	const float Regen = GetManaRegen(); // the tribe is read once per call (units change state far slower than a step)
	ChargeTimeAccumulator += Seconds;
	for (int32 Guard = 0; ChargeTimeAccumulator >= Step && Guard < 1000; ++Guard) // 1000 steps = 100 s at the default step
	{
		ChargeTimeAccumulator -= Step;
		ChargeStep(Step, Regen);
	}
	if (ChargeTimeAccumulator >= Step) ChargeTimeAccumulator = 0.f; // pathological hitch: drop the backlog
}

void USpellComponent::ChargeStep(float Step, float RegenPerSecond)
{
	// 1. New mana this step (tribe members only; nothing is created anywhere else).
	const float Generated = FMath::Max(0.f, RegenPerSecond * Step);

	// 2. Spells charging this step, in spellbook order (deterministic).
	struct FActive { FName Id; FSpellChargeState* State; float Cost; int32 Max; };
	TArray<FActive> Active;
	TArray<float> Needs;
	for (const FName& Id : KnownSpellIds)
	{
		FSpellChargeState* S = ChargeStates.Find(Id);
		if (!S) continue;
		S->LastManaPerSecond = 0.f;
		if (!S->bPermanent || !S->bCharging) continue;
		const FSpellRow* Row = GetSpellRow(Id);
		if (!Row || !FSpellChargeRules::IsChargeable(*Row) || !IsSpellImplemented(Id)) continue;
		const int32 Max = FSpellChargeRules::GetMaxCharges(*Row);
		const float Cost = FSpellChargeRules::GetChargeCost(*Row);
		if (S->StoredCharges >= Max) { S->ProgressMana = 0.f; continue; } // full: draws nothing
		const float Need = (Max - S->StoredCharges) * Cost - S->ProgressMana;
		if (Need <= 0.f) continue;
		Active.Add({ Id, S, Cost, Max });
		Needs.Add(Need);
	}

	// 3. Populous split: spells / training 50-50, or 100% to the only active pipeline.
	float SpellBudget = 0.f, TrainingBudget = 0.f;
	FTribeManaRules::SplitPipelines(Generated, Active.Num() > 0, ActiveTrainingConsumers > 0, ManaConfig.SpellShareWhileTraining,
		SpellBudget, TrainingBudget);

	// 4. The spell share is divided fairly between the charging spells.
	TArray<float> Given;
	FSpellChargeRules::SplitBudget(SpellBudget, Needs, Given);
	float Spent = 0.f;
	bool bChargeGained = false;
	for (int32 i = 0; i < Active.Num(); ++i)
	{
		FSpellChargeState& S = *Active[i].State;
		S.ProgressMana += Given[i];
		S.LastManaPerSecond = Given[i] / Step;
		Spent += Given[i];
		while (S.StoredCharges < Active[i].Max && S.ProgressMana >= Active[i].Cost - 1e-3f)
		{
			++S.StoredCharges;
			S.ProgressMana -= Active[i].Cost; // may go a hair below 0 (float tolerance): carried, so no mana is created
			bChargeGained = true;
		}
		if (S.StoredCharges >= Active[i].Max) S.ProgressMana = 0.f; // never progress past the maximum
	}

	// 5. Anything unused (nothing charging / training, or spells topping out) is banked; past the cap it is wasted.
	const float Unused = FMath::Max(0.f, Generated - Spent - TrainingBudget);
	const float Banked = FMath::Clamp(MaxMana - Mana, 0.f, Unused);
	Mana += Banked;
	LastGeneratedPerSecond = Generated / Step;
	LastDrawPerSecond = Spent / Step;
	LastTrainingPerSecond = TrainingBudget / Step; // no training consumer exists yet (ActiveTrainingConsumers stays 0)
	LastBankedPerSecond = Banked / Step;
	LastWastedPerSecond = (Unused - Banked) / Step;
	if (bChargeGained) NotifySpellbookChanged(NAME_None);
}

void USpellComponent::LearnSpell(FName SpellId)
{
	KnownSpellIds.AddUnique(SpellId);
	FSpellChargeState& S = ChargeStates.FindOrAdd(SpellId);
	if (S.bPermanent) return; // already learned: keep its charges, progress and charging setting
	S.bPermanent = true;
	S.StoredCharges = 0;
	S.ProgressMana = 0.f;
	const FSpellRow* Row = GetSpellRow(SpellId);
	S.bCharging = Row && Row->bChargingByDefault && FSpellChargeRules::IsChargeable(*Row);
	NotifySpellbookChanged(SpellId);
}

void USpellComponent::AddCharges(FName SpellId, int32 Charges)
{
	if (Charges <= 0) return;
	KnownSpellIds.AddUnique(SpellId);
	SpellCharges.FindOrAdd(SpellId) += Charges;
	NotifySpellbookChanged(SpellId);
}

float USpellComponent::GetRangeUnits(FName SpellId) const
{
	const FSpellRow* Row = SpellTable ? SpellTable->FindRow<FSpellRow>(SpellId, TEXT("GetRangeUnits")) : nullptr;
	return Row ? Row->RangeRaw * RangeUnitsPerPoint : 0.f;
}

static bool IsBeyondCastRange(const FSpellRow& Row, float DistanceToTarget, float InRangeUnitsPerPoint)
{
	const float RangeUU = Row.RangeRaw * InRangeUnitsPerPoint;
	return Row.Targeting != ESpellTargeting::Self && RangeUU > 0.f && Row.Targeting != ESpellTargeting::Projectile
		&& DistanceToTarget > RangeUU;
}

ESpellCastResult USpellComponent::EvaluateCast(const FSpellRow& Row, float CurrentMana, int32 Charges, double Now,
	double CooldownEndTime, float DistanceToTarget, float InRangeUnitsPerPoint)
{
	if (Now < CooldownEndTime) return ESpellCastResult::OnCooldown;
	const float Cost = Row.bSuperSpell ? 0.f : Row.ManaCost;
	if (CurrentMana < Cost) return ESpellCastResult::NotEnoughMana;
	if (Row.bSuperSpell && Charges <= 0) return ESpellCastResult::NoCharges;
	if (IsBeyondCastRange(Row, DistanceToTarget, InRangeUnitsPerPoint)) return ESpellCastResult::OutOfRange;
	return ESpellCastResult::Success;
}

ESpellCastResult USpellComponent::EvaluateChargedCast(const FSpellRow& Row, int32 AvailableCharges, double Now,
	double CooldownEndTime, float DistanceToTarget, float InRangeUnitsPerPoint)
{
	if (Now < CooldownEndTime) return ESpellCastResult::OnCooldown;
	if (AvailableCharges <= 0) return ESpellCastResult::NoCharges;
	if (IsBeyondCastRange(Row, DistanceToTarget, InRangeUnitsPerPoint)) return ESpellCastResult::OutOfRange;
	return ESpellCastResult::Success;
}

bool USpellComponent::IsRowImplemented(const FSpellRow& Row, const USpellComponent* Spells, const USpellEffectDispatcherComponent* Effects)
{
	if (Row.Targeting == ESpellTargeting::Projectile)
	{
		// CastSpell spawns nothing for a projectile row without a class: not castable in practice.
		const TSubclassOf<ASpellProjectile>* Cls = Spells ? Spells->ProjectileClasses.Find(Row.EffectId) : nullptr;
		return Cls && *Cls;
	}
	// Non-projectile rows only broadcast OnSpellCast: they do something only when the dispatcher has the effect.
	return Effects && Effects->FindEffect(Row.EffectId) != nullptr;
}

bool USpellComponent::IsSpellImplemented(FName SpellId) const
{
	const FSpellRow* Row = GetSpellRow(SpellId);
	const USpellEffectDispatcherComponent* Effects = GetOwner() ? GetOwner()->FindComponentByClass<USpellEffectDispatcherComponent>() : nullptr;
	return Row && IsRowImplemented(*Row, this, Effects);
}

bool USpellComponent::TableHasChargeData(const UDataTable* Table)
{
	if (!Table || Table->GetRowStruct() != FSpellRow::StaticStruct()) return false;
	for (const TPair<FName, uint8*>& It : Table->GetRowMap())
	{
		const FSpellRow* Row = reinterpret_cast<const FSpellRow*>(It.Value);
		if (Row && Row->MaxCharges > 0 && Row->ChargeManaCost > 0.f) return true; // both charge columns imported
	}
	return false;
}

const FSpellRow* USpellComponent::GetSpellRow(FName SpellId) const
{
	return SpellTable ? SpellTable->FindRow<FSpellRow>(SpellId, TEXT("GetSpellRow"), false) : nullptr;
}

float USpellComponent::GetCooldownRemaining(FName SpellId) const
{
	const double* End = CooldownEnd.Find(SpellId);
	return End && GetWorld() ? FMath::Max(0.f, (float)(*End - GetWorld()->GetTimeSeconds())) : 0.f;
}

// ---- Charge queries / control ---------------------------------------------------------------------------------------

bool USpellComponent::IsPermanentSpell(FName SpellId) const
{
	const FSpellChargeState* S = ChargeStates.Find(SpellId);
	return S && S->bPermanent;
}

int32 USpellComponent::GetMaxCharges(FName SpellId) const
{
	const FSpellRow* Row = GetSpellRow(SpellId);
	return Row ? FSpellChargeRules::GetMaxCharges(*Row) : 0;
}

int32 USpellComponent::GetStoredCharges(FName SpellId) const
{
	const FSpellChargeState* S = ChargeStates.Find(SpellId);
	return S && S->bPermanent ? S->StoredCharges : 0;
}

int32 USpellComponent::GetAvailableCharges(FName SpellId) const
{
	return GetCharges(SpellId) + GetStoredCharges(SpellId);
}

float USpellComponent::GetChargeProgress01(FName SpellId) const
{
	const FSpellChargeState* S = ChargeStates.Find(SpellId);
	const FSpellRow* Row = GetSpellRow(SpellId);
	const float Cost = Row ? FSpellChargeRules::GetChargeCost(*Row) : 0.f;
	if (!S || !S->bPermanent || Cost <= 0.f || S->StoredCharges >= FSpellChargeRules::GetMaxCharges(*Row)) return 0.f;
	return FMath::Clamp(S->ProgressMana / Cost, 0.f, 1.f);
}

bool USpellComponent::IsCharging(FName SpellId) const
{
	const FSpellChargeState* S = ChargeStates.Find(SpellId);
	return S && S->bPermanent && S->bCharging;
}

bool USpellComponent::CanToggleCharging(FName SpellId) const
{
	if (!bUseChargeEconomy || !IsPermanentSpell(SpellId)) return false;
	const FSpellRow* Row = GetSpellRow(SpellId);
	return Row && FSpellChargeRules::IsChargeable(*Row) && IsSpellImplemented(SpellId);
}

bool USpellComponent::SetSpellCharging(FName SpellId, bool bCharge)
{
	if (!CanToggleCharging(SpellId)) return false;
	FSpellChargeState& S = ChargeStates.FindChecked(SpellId);
	if (S.bCharging != bCharge)
	{
		S.bCharging = bCharge;
		if (!bCharge) S.LastManaPerSecond = 0.f; // stops drawing immediately; ProgressMana is kept
		NotifySpellbookChanged(SpellId);
	}
	return true;
}

bool USpellComponent::ToggleSpellCharging(FName SpellId)
{
	return SetSpellCharging(SpellId, !IsCharging(SpellId));
}

int32 USpellComponent::GetActiveChargingCount() const
{
	int32 N = 0;
	for (const FName& Id : KnownSpellIds)
	{
		if (!CanToggleCharging(Id) || !IsCharging(Id)) continue;
		if (GetStoredCharges(Id) < GetMaxCharges(Id)) ++N;
	}
	return N;
}

void USpellComponent::SetStoredChargesForTest(FName SpellId, int32 Charges)
{
	FSpellChargeState* S = ChargeStates.Find(SpellId);
	if (!S || !S->bPermanent) return;
	S->StoredCharges = FMath::Clamp(Charges, 0, GetMaxCharges(SpellId));
	if (S->StoredCharges >= GetMaxCharges(SpellId)) S->ProgressMana = 0.f;
	NotifySpellbookChanged(SpellId);
}

void USpellComponent::SetSpellAvailableOnMap(FName SpellId, bool bAvailable)
{
	if (SpellId.IsNone()) return;
	bool bChanged = false;
	if (bAvailable) { bChanged = !MapSpellIds.Contains(SpellId); MapSpellIds.AddUnique(SpellId); }
	else bChanged = MapSpellIds.Remove(SpellId) > 0;
	if (bChanged) NotifySpellbookChanged(SpellId);
}

void USpellComponent::ConsumeCharge(FName SpellId)
{
	// Temporary (monument) charges first: they never come back, permanent ones recharge.
	if (int32* Temp = SpellCharges.Find(SpellId))
	{
		if (--(*Temp) <= 0)
		{
			SpellCharges.Remove(SpellId);
			if (!IsPermanentSpell(SpellId)) KnownSpellIds.Remove(SpellId); // a temporary-only spell is gone
		}
	}
	else if (FSpellChargeState* S = ChargeStates.Find(SpellId))
	{
		if (S->bPermanent && S->StoredCharges > 0) --S->StoredCharges;
	}
	NotifySpellbookChanged(SpellId);
}

// ---- Casting --------------------------------------------------------------------------------------------------------

bool USpellComponent::CastSpell(FName SpellId, FVector AimTarget)
{
	LastCastResult = ESpellCastResult::UnknownSpell;
	if (!SpellTable || !KnownSpellIds.Contains(SpellId)) return false;
	const FSpellRow* Row = SpellTable->FindRow<FSpellRow>(SpellId, TEXT("CastSpell"));
	if (!Row) return false;

	const double Now = GetWorld()->GetTimeSeconds();
	AActor* Owner = GetOwner();
	const double* End = CooldownEnd.Find(SpellId);
	const float Distance = FVector::Dist(Owner->GetActorLocation(), AimTarget);
	LastCastResult = bUseChargeEconomy
		? EvaluateChargedCast(*Row, GetAvailableCharges(SpellId), Now, End ? *End : -1.0, Distance, RangeUnitsPerPoint)
		: EvaluateCast(*Row, Mana, GetCharges(SpellId), Now, End ? *End : -1.0, Distance, RangeUnitsPerPoint);
	if (LastCastResult != ESpellCastResult::Success) return false;
	// Charge economy: the mana was paid while charging. Otherwise the Phase 1 rule (super spells are free).
	const float Cost = (bUseChargeEconomy || Row->bSuperSpell) ? 0.f : Row->ManaCost;
	const float RangeUU = Row->RangeRaw * RangeUnitsPerPoint;

	Mana -= Cost;
	CooldownEnd.Add(SpellId, Now + Row->Cooldown);
	if (USoundBase* Snd = Row->CastSound.LoadSynchronous()) UGameplayStatics::PlaySoundAtLocation(this, Snd, Owner->GetActorLocation());

	if (Row->Targeting == ESpellTargeting::Projectile)
	{
		if (const TSubclassOf<ASpellProjectile>* Cls = ProjectileClasses.Find(Row->EffectId))
		{
			const FVector Start = Owner->GetActorLocation() + Owner->GetActorForwardVector() * 100.f + FVector(0, 0, 60);
			FActorSpawnParameters P; P.Instigator = Cast<APawn>(Owner); P.Owner = Owner;
			if (ASpellProjectile* Proj = GetWorld()->SpawnActor<ASpellProjectile>(*Cls, Start, (AimTarget - Start).Rotation(), P))
				Proj->Init(*Row, RangeUU);
		}
	}
	else
	{
		OnSpellCast.Broadcast(SpellId, Row->EffectId, AimTarget, Row->Radius, Row->Duration);
	}

	if (bUseChargeEconomy) ConsumeCharge(SpellId); // exactly one charge per successful cast
	else if (Row->bSuperSpell && --SpellCharges.FindOrAdd(SpellId) <= 0) { SpellCharges.Remove(SpellId); KnownSpellIds.Remove(SpellId); }
	return true;
}
