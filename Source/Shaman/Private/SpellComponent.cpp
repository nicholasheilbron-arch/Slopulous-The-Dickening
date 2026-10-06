#include "SpellComponent.h"
#include "SpellProjectile.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"

USpellComponent::USpellComponent() { PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickInterval = 0.1f; }

float USpellComponent::GetManaRegen() const
{
	return BaseRegen * (1.f + RegenPerFollower * FollowerCount) * TemporaryModifier;
}

void USpellComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* F)
{
	Super::TickComponent(DeltaTime, TickType, F);
	Mana = FMath::Min(MaxMana, Mana + GetManaRegen() * DeltaTime);
}

void USpellComponent::LearnSpell(FName SpellId) { KnownSpellIds.AddUnique(SpellId); }

void USpellComponent::AddCharges(FName SpellId, int32 Charges)
{
	if (Charges <= 0) return;
	KnownSpellIds.AddUnique(SpellId);
	SpellCharges.FindOrAdd(SpellId) += Charges;
}

float USpellComponent::GetRangeUnits(FName SpellId) const
{
	const FSpellRow* Row = SpellTable ? SpellTable->FindRow<FSpellRow>(SpellId, TEXT("GetRangeUnits")) : nullptr;
	return Row ? Row->RangeRaw * RangeUnitsPerPoint : 0.f;
}

ESpellCastResult USpellComponent::EvaluateCast(const FSpellRow& Row, float CurrentMana, int32 Charges, double Now,
	double CooldownEndTime, float DistanceToTarget, float InRangeUnitsPerPoint)
{
	if (Now < CooldownEndTime) return ESpellCastResult::OnCooldown;
	const float Cost = Row.bSuperSpell ? 0.f : Row.ManaCost;
	if (CurrentMana < Cost) return ESpellCastResult::NotEnoughMana;
	if (Row.bSuperSpell && Charges <= 0) return ESpellCastResult::NoCharges;
	const float RangeUU = Row.RangeRaw * InRangeUnitsPerPoint;
	if (Row.Targeting != ESpellTargeting::Self && RangeUU > 0.f && Row.Targeting != ESpellTargeting::Projectile
		&& DistanceToTarget > RangeUU) return ESpellCastResult::OutOfRange;
	return ESpellCastResult::Success;
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

bool USpellComponent::CastSpell(FName SpellId, FVector AimTarget)
{
	LastCastResult = ESpellCastResult::UnknownSpell;
	if (!SpellTable || !KnownSpellIds.Contains(SpellId)) return false;
	const FSpellRow* Row = SpellTable->FindRow<FSpellRow>(SpellId, TEXT("CastSpell"));
	if (!Row) return false;

	const double Now = GetWorld()->GetTimeSeconds();
	AActor* Owner = GetOwner();
	const double* End = CooldownEnd.Find(SpellId);
	LastCastResult = EvaluateCast(*Row, Mana, GetCharges(SpellId), Now, End ? *End : -1.0,
		FVector::Dist(Owner->GetActorLocation(), AimTarget), RangeUnitsPerPoint);
	if (LastCastResult != ESpellCastResult::Success) return false;
	const float Cost = Row->bSuperSpell ? 0.f : Row->ManaCost;
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

	if (Row->bSuperSpell && --SpellCharges.FindOrAdd(SpellId) <= 0) { SpellCharges.Remove(SpellId); KnownSpellIds.Remove(SpellId); }
	return true;
}
