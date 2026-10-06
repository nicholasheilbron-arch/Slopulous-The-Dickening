#pragma once
#include "CoreMinimal.h"
#include "ShamanTypes.h"
#include "TribeTypes.h"

class AActor;

/** Tribe id conventions. Real tribes are 0..N (0 = player). */
namespace ShamanTribe
{
	constexpr int32 Player = 0;
	constexpr int32 Wild = ShamanTribes::NoTribe;   // Wildmen: unaligned, convertible, never hostile (same id as the Convert system)
	constexpr int32 None = -2;   // not tribe-owned (props, terrain)
}

/**
 * Centralised "who can affect whom" rules. Every spell, melee hit and AI target query goes through here
 * so friendly fire, Wildmen-only spells, and future immunities stay consistent (spec section 14).
 * The two core functions are pure so they can be unit tested without a world.
 */
struct SHAMAN_API FShamanTargetRules
{
	static ETribeRelation GetRelation(int32 CasterTribe, int32 TargetTribe, bool bTargetIsCaster);
	static bool PassesFilter(ESpellTargetFilter Filter, ETribeRelation Relation, bool bFriendlyFire);
	static bool AreHostile(int32 TribeA, int32 TribeB);

	/** Actor helpers (use ITribeOwned). */
	static int32 GetTribeOf(const AActor* Actor);
	static ETribeRelation GetActorRelation(const AActor* Caster, const AActor* Target);
	static bool CanAffect(const AActor* Caster, const AActor* Target, ESpellTargetFilter Filter, bool bFriendlyFire);
};
