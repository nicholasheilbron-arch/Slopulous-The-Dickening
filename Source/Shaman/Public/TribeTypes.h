#pragma once
#include "CoreMinimal.h"
#include "TribeTypes.generated.h"

/** Tribe id conventions. Real tribes are 0..N (0 = player by convention). */
namespace ShamanTribes
{
	constexpr int32 NoTribe = -1; // Wildmen and other unaligned units
}

/** Gameplay identity of a unit for targeting and conversion. Kept as an enum for this milestone;
 *  maps 1:1 onto future Gameplay Tags (Target.Follower, Target.Wildman, Target.Shaman, Target.Building). */
UENUM(BlueprintType)
enum class EUnitKind : uint8
{
	Follower,   // belongs to a tribe and counts toward its population
	Wildman,    // unaligned, recruitable by Convert
	Shaman,     // never converted, never counted as a follower
	Building,   // never converted
	Other
};

/** How a unit changes sides. Only Recruit is implemented in the Convert milestone. */
UENUM(BlueprintType)
enum class EConversionKind : uint8
{
	Recruit,    // Wildman -> follower of the caster's tribe (Convert)
	Permanent,  // enemy follower -> own follower (e.g. converted by a hypnotized Preacher) - not implemented yet
	Hypnotize   // enemy follower -> temporarily own follower - not implemented yet
};
