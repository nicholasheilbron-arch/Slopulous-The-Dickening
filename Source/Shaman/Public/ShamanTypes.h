#pragma once
#include "CoreMinimal.h"
#include "ShamanTypes.generated.h"

UENUM(BlueprintType)
enum class EHitTier : uint8 { None, Stagger, Knockdown, Ragdoll, Launch };

UENUM(BlueprintType)
enum class ESpellTargeting : uint8 { Projectile, Ground, Self, Area };

UENUM(BlueprintType)
enum class EMonumentType : uint8 { VaultOfKnowledge, StoneHead, Obelisk, Gargoyle };

UENUM(BlueprintType)
enum class EWorshipRule : uint8 { ShamanOnly, ShamanAndBraves };

UENUM(BlueprintType)
enum class EMonumentRewardType : uint8 { UnlockSpell, UnlockBuilding, GrantCharges, WorldEvent };

// ---------------------------------------------------------------------------------------------
// Phase 1 completion additions (shared vocabulary for tribes, units, targeting).
// ---------------------------------------------------------------------------------------------

/** Who a spell may affect. Combined with FSpellRow::bFriendlyFire by FShamanTargetRules. */
UENUM(BlueprintType)
enum class ESpellTargetFilter : uint8
{
	AllUnits,          // hostile + wild (+ friendly when bFriendlyFire)
	EnemiesOnly,       // hostile (+ friendly when bFriendlyFire)
	FriendlyOnly,      // own tribe only (buffs)
	WildmenOnly,       // unaligned Wildmen only (Convert)
	EnemiesAndWildmen  // hostile + wild, never friendly
};

/** Result of a cast attempt; also used for HUD feedback. */
UENUM(BlueprintType)
enum class ESpellCastResult : uint8 { Success, UnknownSpell, OnCooldown, NotEnoughMana, NoCharges, OutOfRange, CasterDisabled };

/** Relationship of a target to the caster's tribe. */
UENUM(BlueprintType)
enum class ETribeRelation : uint8 { Self, Friendly, Hostile, Wild, Neutral };

UENUM(BlueprintType)
enum class EUnitRole : uint8 { Shaman, Brave, Warrior, Wildman };

UENUM(BlueprintType)
enum class EResourceType : uint8 { Wood, Food, Stone, Ore };

/** Simple standing orders. Phase 2 adds work orders (gather/build). New values are appended (saved / Blueprint values
 *  of the first four never change). RTS guard orders: the unit walks a ring around its guard centre and fights enemies
 *  that come near it (AShamanUnitAIController). */
UENUM(BlueprintType)
enum class EFollowerOrder : uint8
{
	FollowShaman,
	HoldPosition,
	GuardHome,
	Wander,
	GuardShaman, // circle the tribe's Shaman and follow her (G)
	GuardTarget, // circle an actor: a guard flag or one of the tribe's buildings (RMB on it)
	GuardSpot,   // circle a spot on the ground (Ctrl+Alt double right-click; also a guarded flag that was removed)
	Patrol       // walk a loop of points (Ctrl+RMB flags, Ctrl+Alt+RMB ground points)
};
