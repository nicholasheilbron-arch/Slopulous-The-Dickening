#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShamanTypes.h"
#include "SpellRow.generated.h"

class UParticleSystem;
class USoundBase;
class UTexture2D;

/** One row per spell in DT_Spells (import Content/Data/Spells.csv). Replaces the per-asset USpellDefinition.
 *  RangeRaw and ManaCost use the original 0-100 scales; world units = RangeRaw * USpellComponent::RangeUnitsPerPoint. */
USTRUCT(BlueprintType)
struct SHAMAN_API FSpellRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ESpellTargeting Targeting = ESpellTargeting::Ground;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ManaCost = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RangeRaw = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Radius = 300.f;      // uu, area of effect
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CastTime = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Cooldown = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Damage = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Knockback = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Duration = 0.f;      // seconds, for timed effects
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BuildingDamageMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjectileSpeed = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bHoming = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bIgnites = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bSuperSpell = false;  // costs no mana
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bOneShot = false;     // removed from known spells after casting
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName EffectId;            // key for the effect handler (see OnSpellCast)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName UnlockCondition;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName TerrainEffectId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UParticleSystem> ImpactVFX;
	// Added in Phase 1 completion: shared target rules (FShamanTargetRules). Defaults reproduce the original Blast behaviour.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ESpellTargetFilter TargetFilter = ESpellTargetFilter::AllUnits;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bFriendlyFire = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> CastSound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> ImpactSound;

	// ---- Spell charges (spell panel slice) -------------------------------------------------------------------------
	// Used when the caster's USpellComponent runs the charge economy (player tribe). ManaCost above is then the mana
	// needed to fill ONE charge (paid while charging, not at cast time). Values are explicit per spell; nothing is
	// inferred from rarity, icons or other columns.
	/** Stored charges a permanently learned spell can hold (1-4). 0 = cannot be learned permanently / never charges
	 *  (super spells that only come as temporary charges from monuments). Values above 4 are clamped to 4. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="4")) int32 MaxCharges = 0;
	/** Charging is switched on when the spell is learned (level start for starting spells). Off for everything but
	 *  Blast in Spells.csv; the player toggles charging with right-click on the panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bChargingByDefault = false;
	/** Panel icon. Empty = a placeholder monogram drawn by the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Icon;
};
