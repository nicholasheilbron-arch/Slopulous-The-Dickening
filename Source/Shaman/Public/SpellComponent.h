#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpellRow.h"
#include "Tribes/TribeMana.h"
#include "SpellComponent.generated.h"

class UDataTable;
class ASpellProjectile;
class USpellEffectDispatcherComponent;

/** Charge state of one spell in a caster's spellbook (spell panel slice). Owned by USpellComponent only. */
USTRUCT(BlueprintType)
struct SHAMAN_API FSpellChargeState
{
	GENERATED_BODY()
	/** Learned permanently (LearnSpell / Vault of Knowledge / starting spell). Temporary-only spells are not. */
	UPROPERTY(BlueprintReadOnly) bool bPermanent = false;
	/** Player wants this spell to draw mana and charge (right-click on the panel). Only acts on chargeable spells. */
	UPROPERTY(BlueprintReadOnly) bool bCharging = false;
	/** Ready permanent charges (0..MaxCharges). Temporary (monument) charges are counted separately. */
	UPROPERTY(BlueprintReadOnly) int32 StoredCharges = 0;
	/** Mana already paid toward the next charge (0..ManaCost). Kept while charging is switched off. */
	UPROPERTY(BlueprintReadOnly) float ProgressMana = 0.f;
	/** Mana per second this spell received in the last charging step (HUD only). */
	UPROPERTY(BlueprintReadOnly) float LastManaPerSecond = 0.f;
};

/** Pure charge rules (no world). Shared by USpellComponent, the panel and the tests. */
struct SHAMAN_API FSpellChargeRules
{
	static constexpr int32 MaxChargesLimit = 4;
	/** Row MaxCharges clamped to 0..4 (0 = cannot hold permanent charges). */
	static int32 GetMaxCharges(const FSpellRow& Row);
	/** Mana to fill one charge (FSpellRow::ChargeManaCost). */
	static float GetChargeCost(const FSpellRow& Row) { return Row.ChargeManaCost; }
	/** A permanently known spell can charge when the row defines charges and a positive mana cost per charge. */
	static bool IsChargeable(const FSpellRow& Row);
	/**
	 * Splits one step's mana Budget between the spells that are charging (max-min fair "water filling"):
	 * every spell gets an equal share, a spell that needs less than its share takes only what it needs and the rest
	 * is shared among the others. Needs[i] = mana spell i can still absorb. Deterministic (ties keep input order);
	 * sum(OutGiven) <= Budget, OutGiven[i] <= Needs[i].
	 */
	static void SplitBudget(float Budget, const TArray<float>& Needs, TArray<float>& OutGiven);
};

/** Fired when the spellbook changes: learned / temporary charges / charge gained or spent / charging toggled. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSpellbookChanged, FName /*SpellId (None = several)*/);

/** Owns known spell ids, mana, cooldowns. ManaRegen = BaseRegen * FollowerMultiplier * TemporaryModifier.
 *  Projectile spells spawn a projectile; every other spell fires OnSpellCast for an effect handler to implement.
 *
 *  Spell charges (bUseChargeEconomy, on for the player tribe's Shaman): the single source of truth for the spell panel.
 *   - Permanent spells hold up to FSpellRow::MaxCharges (1-4) charges; one charge costs FSpellRow::ChargeManaCost.
 *   - Each fixed step (ChargeStepSeconds) the step's new mana (GetManaRegen * step) is split between the spell and
 *     training pipelines (FTribeManaRules::SplitPipelines: 50/50, or 100% to the only active one). The spell share is
 *     divided fairly between the charging spells (FSpellChargeRules::SplitBudget). Whatever nothing uses is banked in
 *     Mana up to MaxMana and wasted past that. Banked mana is not drawn back by charging: only new mana charges spells.
 *   - Casting then needs a charge instead of mana: a successful cast uses one temporary charge if there is one,
 *     otherwise one permanent charge. Failed casts use nothing. Mana is not spent at cast time.
 *   - Temporary charges (AddCharges: Stone Heads, test fixtures) never recharge and never draw mana. A spell known only
 *     through temporary charges is forgotten when they run out; a permanent spell keeps its own charges untouched.
 *  With bUseChargeEconomy off (enemy Shamans, older tests) every rule above is skipped and casting spends ManaCost as
 *  before.
 */
UCLASS(ClassGroup=(Shaman), meta=(BlueprintSpawnableComponent))
class SHAMAN_API USpellComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USpellComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite) UDataTable* SpellTable = nullptr;       // rows of FSpellRow
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> KnownSpellIds;            // castable now: permanent + temporary
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName, TSubclassOf<ASpellProjectile>> ProjectileClasses; // by EffectId
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RangeUnitsPerPoint = 50.f;        // 100 raw range = 5000 uu
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxMana = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Mana = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BaseRegen = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RegenPerFollower = 0.25f;
	UPROPERTY(BlueprintReadWrite) int32 FollowerCount = 3;
	UPROPERTY(BlueprintReadWrite) float TemporaryModifier = 1.f;

	/** Spell charges instead of mana-at-cast (see class comment). Set by AShamanCharacter for the player tribe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charges") bool bUseChargeEconomy = false;
	/** Populous tribe mana: generation comes only from the owner's living tribe members by activity
	 *  (FTribeManaRules); GetManaRegen() then ignores BaseRegen / RegenPerFollower. Set with the charge economy for the
	 *  player tribe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charges") bool bTribeManaGeneration = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charges") FTribeManaConfig ManaConfig;
	/** Training huts currently training (0 until training exists). While > 0, new mana is split between spells and
	 *  training (FTribeManaConfig::SpellShareWhileTraining). */
	UPROPERTY(BlueprintReadWrite, Category="Charges") int32 ActiveTrainingConsumers = 0;
	/** Fixed economy step: results depend only on elapsed game time, never on frame rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charges", meta=(ClampMin="0.01")) float ChargeStepSeconds = 0.1f;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnSpellCast, FName, SpellId, FName, EffectId, FVector, Target, float, Radius, float, Duration);
	UPROPERTY(BlueprintAssignable) FOnSpellCast OnSpellCast;
	FOnSpellbookChanged OnSpellbookChanged;

	UFUNCTION(BlueprintCallable) bool CastSpell(FName SpellId, FVector AimTarget);
	/** Mana per second (tribe mana: sum of member rates / 60; otherwise the Phase 1 formula). */
	UFUNCTION(BlueprintCallable) float GetManaRegen() const;
	/** Who makes the tribe's mana now (tribe mana only; empty otherwise). */
	FTribeManaBreakdown GetManaBreakdown() const;
	/** Permanent learn. New entries start with 0 stored charges and charging = row bChargingByDefault. Learning a spell
	 *  that is already permanent changes nothing; other spells are never touched. */
	UFUNCTION(BlueprintCallable) void LearnSpell(FName SpellId);
	/** Temporary charges (Stone Heads / monument rewards; super spells run on these). Never recharge. */
	UFUNCTION(BlueprintCallable) void AddCharges(FName SpellId, int32 Charges);
	/** Temporary charges (name kept from Phase 1). */
	UFUNCTION(BlueprintCallable) int32 GetCharges(FName SpellId) const { const int32* C = SpellCharges.Find(SpellId); return C ? *C : 0; }
	UFUNCTION(BlueprintCallable) float GetRangeUnits(FName SpellId) const;

	// --- Added in Phase 1 completion (additive; CastSpell behaviour unchanged) ---
	/** Pure rule check used by CastSpell, AI and tests. Check order matches the original CastSpell. */
	static ESpellCastResult EvaluateCast(const FSpellRow& Row, float CurrentMana, int32 Charges, double Now,
		double CooldownEndTime, float DistanceToTarget, float RangeUnitsPerPoint);
	UFUNCTION(BlueprintCallable) float GetCooldownRemaining(FName SpellId) const;
	/** Row lookup (null if unknown). */
	const FSpellRow* GetSpellRow(FName SpellId) const;
	/** Why the last CastSpell call failed (Success if it worked). For HUD feedback. */
	UPROPERTY(BlueprintReadOnly) ESpellCastResult LastCastResult = ESpellCastResult::Success;
	const TMap<FName, int32>& GetAllCharges() const { return SpellCharges; }

	// --- Spell charges (spell panel slice) ---
	/** Charge-economy cast check: cooldown, then a charge (temporary or permanent), then range. No mana check. */
	static ESpellCastResult EvaluateChargedCast(const FSpellRow& Row, int32 AvailableCharges, double Now,
		double CooldownEndTime, float DistanceToTarget, float RangeUnitsPerPoint);
	/** True when casting the row does something: a projectile class for its EffectId, or a dispatcher effect. */
	static bool IsRowImplemented(const FSpellRow& Row, const USpellComponent* Spells, const USpellEffectDispatcherComponent* Effects);
	bool IsSpellImplemented(FName SpellId) const;
	/** True when at least one row of Table defines MaxCharges > 0 (the table was imported with the charge columns). */
	static bool TableHasChargeData(const UDataTable* Table);

	const FSpellChargeState* GetChargeState(FName SpellId) const { return ChargeStates.Find(SpellId); }
	UFUNCTION(BlueprintCallable, Category="Charges") bool IsPermanentSpell(FName SpellId) const;
	UFUNCTION(BlueprintCallable, Category="Charges") int32 GetMaxCharges(FName SpellId) const;
	UFUNCTION(BlueprintCallable, Category="Charges") int32 GetStoredCharges(FName SpellId) const;
	UFUNCTION(BlueprintCallable, Category="Charges") int32 GetTemporaryCharges(FName SpellId) const { return GetCharges(SpellId); }
	/** Temporary + permanent charges usable right now (charge economy). */
	UFUNCTION(BlueprintCallable, Category="Charges") int32 GetAvailableCharges(FName SpellId) const;
	/** Progress toward the next permanent charge, 0..1 (0 when full). */
	UFUNCTION(BlueprintCallable, Category="Charges") float GetChargeProgress01(FName SpellId) const;
	/** Charging switched on (whether or not it can progress right now). */
	UFUNCTION(BlueprintCallable, Category="Charges") bool IsCharging(FName SpellId) const;
	/** Permanent, chargeable, implemented spell under the charge economy: charging may be switched on. */
	UFUNCTION(BlueprintCallable, Category="Charges") bool CanToggleCharging(FName SpellId) const;
	/** Switch charging. Returns false (nothing changed) for spells that cannot charge (locked, undiscovered,
	 *  temporary-only, unimplemented, no charge data, economy off). Progress is kept either way. */
	UFUNCTION(BlueprintCallable, Category="Charges") bool SetSpellCharging(FName SpellId, bool bCharge);
	UFUNCTION(BlueprintCallable, Category="Charges") bool ToggleSpellCharging(FName SpellId);
	/** Spells currently drawing mana (charging, below max). */
	UFUNCTION(BlueprintCallable, Category="Charges") int32 GetActiveChargingCount() const;
	/** Mana per second spent on charges in the last step (HUD). */
	UFUNCTION(BlueprintCallable, Category="Charges") float GetChargeDrawPerSecond() const { return LastDrawPerSecond; }
	/** Last step's flow, per second: generated, to training, banked in the pool, wasted (pool full). */
	float GetGeneratedPerSecond() const { return LastGeneratedPerSecond; }
	float GetTrainingPerSecond() const { return LastTrainingPerSecond; }
	float GetBankedPerSecond() const { return LastBankedPerSecond; }
	float GetWastedPerSecond() const { return LastWastedPerSecond; }
	/** Test / cheat: set permanent stored charges directly (clamped to 0..Max). */
	void SetStoredChargesForTest(FName SpellId, int32 Charges);

	/** Map-specific discovery: spells that can be learned on this map (monuments, scripted rewards). Drawn as "?"
	 *  until learned. A spell defined in the table but not listed here never shows as discoverable. */
	UFUNCTION(BlueprintCallable, Category="Charges") void SetSpellAvailableOnMap(FName SpellId, bool bAvailable = true);
	UFUNCTION(BlueprintCallable, Category="Charges") bool IsSpellAvailableOnMap(FName SpellId) const { return MapSpellIds.Contains(SpellId); }
	const TArray<FName>& GetMapSpellIds() const { return MapSpellIds; }

	/** Runs the charge economy for Seconds of game time in fixed ChargeStepSeconds steps (TickComponent calls this;
	 *  tests call it directly). Leftover time is carried to the next call. */
	void AdvanceChargeEconomy(float Seconds);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void ChargeStep(float Step, float RegenPerSecond);
	void ConsumeCharge(FName SpellId);
	void NotifySpellbookChanged(FName SpellId) { OnSpellbookChanged.Broadcast(SpellId); }

	TMap<FName, double> CooldownEnd;
	TMap<FName, int32> SpellCharges;          // temporary charges
	TMap<FName, FSpellChargeState> ChargeStates;
	TArray<FName> MapSpellIds;
	float ChargeTimeAccumulator = 0.f;
	float LastDrawPerSecond = 0.f;
	float LastGeneratedPerSecond = 0.f, LastTrainingPerSecond = 0.f, LastBankedPerSecond = 0.f, LastWastedPerSecond = 0.f;
};
