#include "Tribes/TribeMana.h"

ETribeManaActivity FTribeManaRules::Classify(bool bIsTribeShaman, EUnitRole Role, EUnitSimState State, bool bAlive)
{
	if (!bAlive || State == EUnitSimState::Dead || State == EUnitSimState::Unavailable) return ETribeManaActivity::None;
	if (bIsTribeShaman) return ETribeManaActivity::Shaman;
	switch (State)
	{
	case EUnitSimState::Working:
	case EUnitSimState::Building:
	case EUnitSimState::Gathering:
	case EUnitSimState::Sleeping: // resting in a hut
		return Role == EUnitRole::Brave ? ETribeManaActivity::Working : ETribeManaActivity::Moving;
	case EUnitSimState::Moving:
	case EUnitSimState::Following:
	case EUnitSimState::Guarding:
	case EUnitSimState::Combat:
		return ETribeManaActivity::Moving;
	default: // Idle, Eating, Drinking
		return ETribeManaActivity::Idle;
	}
}

float FTribeManaRules::RateFor(ETribeManaActivity Activity, const FTribeManaConfig& Config)
{
	switch (Activity)
	{
	case ETribeManaActivity::Shaman:  return Config.ShamanPerMinute;
	case ETribeManaActivity::Working: return Config.BraveWorkingPerMinute;
	case ETribeManaActivity::Moving:  return Config.MovingPerMinute;
	case ETribeManaActivity::Idle:    return Config.IdlePerMinute;
	default:                          return 0.f;
	}
}

float FTribeManaRules::RatePerMinute(bool bIsTribeShaman, EUnitRole Role, EUnitSimState State, bool bAlive, const FTribeManaConfig& Config)
{
	return RateFor(Classify(bIsTribeShaman, Role, State, bAlive), Config);
}

void FTribeManaRules::SplitPipelines(float Generated, bool bSpellsCharging, bool bTraining, float SpellShareWhileTraining,
	float& OutSpells, float& OutTraining)
{
	OutSpells = OutTraining = 0.f;
	if (Generated <= 0.f) return;
	if (bSpellsCharging && bTraining)
	{
		OutSpells = Generated * FMath::Clamp(SpellShareWhileTraining, 0.f, 1.f);
		OutTraining = Generated - OutSpells;
	}
	else if (bSpellsCharging) OutSpells = Generated;
	else if (bTraining) OutTraining = Generated;
}
