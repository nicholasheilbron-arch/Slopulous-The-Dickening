#include "Core/ShamanTargetRules.h"
#include "Core/ShamanInterfaces.h"
#include "GameFramework/Actor.h"

ETribeRelation FShamanTargetRules::GetRelation(int32 CasterTribe, int32 TargetTribe, bool bTargetIsCaster)
{
	if (bTargetIsCaster) return ETribeRelation::Self;
	if (TargetTribe == ShamanTribe::None) return ETribeRelation::Neutral;
	if (TargetTribe == ShamanTribe::Wild) return ETribeRelation::Wild;
	if (CasterTribe < 0) return ETribeRelation::Neutral; // unaligned caster (e.g. environment)
	return CasterTribe == TargetTribe ? ETribeRelation::Friendly : ETribeRelation::Hostile;
}

bool FShamanTargetRules::PassesFilter(ESpellTargetFilter Filter, ETribeRelation Relation, bool bFriendlyFire)
{
	if (Relation == ETribeRelation::Self) return false; // casters never hit themselves via filters
	switch (Filter)
	{
	case ESpellTargetFilter::AllUnits:
		return Relation == ETribeRelation::Friendly ? bFriendlyFire : true;
	case ESpellTargetFilter::EnemiesOnly:
		return Relation == ETribeRelation::Hostile || (Relation == ETribeRelation::Friendly && bFriendlyFire);
	case ESpellTargetFilter::FriendlyOnly:
		return Relation == ETribeRelation::Friendly;
	case ESpellTargetFilter::WildmenOnly:
		return Relation == ETribeRelation::Wild;
	case ESpellTargetFilter::EnemiesAndWildmen:
		return Relation == ETribeRelation::Hostile || Relation == ETribeRelation::Wild;
	default:
		return false;
	}
}

bool FShamanTargetRules::AreHostile(int32 TribeA, int32 TribeB)
{
	return TribeA >= 0 && TribeB >= 0 && TribeA != TribeB;
}

int32 FShamanTargetRules::GetTribeOf(const AActor* Actor)
{
	const ITribeOwned* T = Cast<ITribeOwned>(const_cast<AActor*>(Actor)); // interface Cast needs a non-const UObject
	return T ? T->GetTribeId() : ShamanTribe::None;
}

ETribeRelation FShamanTargetRules::GetActorRelation(const AActor* Caster, const AActor* Target)
{
	return GetRelation(GetTribeOf(Caster), GetTribeOf(Target), Caster != nullptr && Caster == Target);
}

bool FShamanTargetRules::CanAffect(const AActor* Caster, const AActor* Target, ESpellTargetFilter Filter, bool bFriendlyFire)
{
	if (!Target) return false;
	if (const IShamanDamageReceiver* R = Cast<IShamanDamageReceiver>(const_cast<AActor*>(Target)))
		if (!R->IsAlive()) return false;
	return PassesFilter(Filter, GetActorRelation(Caster, Target), bFriendlyFire);
}
