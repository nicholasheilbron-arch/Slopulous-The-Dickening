#pragma once
#include "CoreMinimal.h"
#include "ShamanTypes.h"
#include "Tribes/TribeTaskTypes.h"
#include "TribeMana.generated.h"

/**
 * Populous-style tribe mana (spell panel slice, mana pass). Mana is made only by the tribe's living members, per minute,
 * by what they are doing; there is no passive background regeneration. Rates follow Populous: The Beginning
 * (community "scaled" units: Blast costs 100). Tune in DA_ShamanGameData > Mana.
 */
USTRUCT(BlueprintType)
struct SHAMAN_API FTribeManaConfig
{
	GENERATED_BODY()
	/** The tribe's Shaman while alive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mana") float ShamanPerMinute = 66.6f;
	/** A Brave that is working (task, building, gathering) or resting in a hut. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mana") float BraveWorkingPerMinute = 34.6f;
	/** Any follower that is moving, following, patrolling / guarding or fighting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mana") float MovingPerMinute = 11.3f;
	/** Any follower standing idle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mana") float IdlePerMinute = 9.1f;
	/** Mana in the pool at level start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mana", meta=(ClampMin="0")) float StartingMana = 0.f;
	/** Pool size. Mana nothing uses is banked here; once it is full, further unused mana is wasted (HUD flashes red). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mana", meta=(ClampMin="1")) float PoolCap = 1000.f;
	/** Share of new mana that goes to spells while units are also training (Populous: 50/50). With only one of the two
	 *  active, it gets 100%. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mana", meta=(ClampMin="0", ClampMax="1")) float SpellShareWhileTraining = 0.5f;
};

/** Who is making the tribe's mana right now (HUD breakdown and logs). */
struct SHAMAN_API FTribeManaBreakdown
{
	bool bShamanAlive = false;
	int32 Working = 0;   // Braves working / resting
	int32 Moving = 0;    // moving, following, guarding, fighting
	int32 Idle = 0;
	float PerMinute = 0.f;
};

/** What a member counts as for mana. */
enum class ETribeManaActivity : uint8 { None, Shaman, Working, Moving, Idle };

struct SHAMAN_API FTribeManaRules
{
	/** Dead / unavailable: None. The tribe's Shaman: Shaman. A Brave working, building, gathering or resting: Working.
	 *  Moving, following, guarding or fighting (any role; other roles' "work" counts as moving): Moving. Else Idle. */
	static ETribeManaActivity Classify(bool bIsTribeShaman, EUnitRole Role, EUnitSimState State, bool bAlive);
	static float RateFor(ETribeManaActivity Activity, const FTribeManaConfig& Config);
	/** Mana per minute of one tribe member. bIsTribeShaman: the tribe's own Shaman. Dead / unavailable members make none. */
	static float RatePerMinute(bool bIsTribeShaman, EUnitRole Role, EUnitSimState State, bool bAlive, const FTribeManaConfig& Config);
	/** Split one step's mana between the spell and training pipelines (Populous 50/50; 100% to whichever is the only
	 *  one active; nothing to either when neither is). */
	static void SplitPipelines(float Generated, bool bSpellsCharging, bool bTraining, float SpellShareWhileTraining,
		float& OutSpells, float& OutTraining);
};
