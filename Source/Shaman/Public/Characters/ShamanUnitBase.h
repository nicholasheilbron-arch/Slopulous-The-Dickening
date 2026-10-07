#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Core/ShamanInterfaces.h"
#include "GameplayTagAssetInterface.h"
#include "Characters/UnitRow.h"
#include "TribeTypes.h"
#include "ShamanUnitBase.generated.h"

class UHealthComponent;
class URagdollReactionComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UTribeMemberComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMeleeAttack, AActor*, Target);

/**
 * Every unit in the game (Shaman, Brave, Warrior, Wildman, enemy units) is this class plus an FUnitRow.
 * It owns health, hit reactions, tribe membership, melee, drowning and standing orders. AI decisions live
 * in AShamanUnitAIController; the player Shaman adds camera/input/spells in AShamanCharacter.
 */
UCLASS()
class SHAMAN_API AShamanUnitBase : public ACharacter, public ITribeOwned, public IShamanInteractable,
	public IShamanDamageReceiver, public ISpellTarget, public IGameplayTagAssetInterface
{
	GENERATED_BODY()
public:
	AShamanUnitBase(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) UHealthComponent* Health;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) URagdollReactionComponent* HitReaction;
	/** Tinted cylinder shown when the unit row has no skeletal mesh yet. */
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* PlaceholderBody;
	/** Gameplay identity for targeting and conversion (Convert milestone). Its TribeId is the authoritative tribe. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) UTribeMemberComponent* TribeMember;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ExposeOnSpawn=true)) FName UnitId;
	/** Tribe at spawn (-1 = Wildman). Kept in sync with TribeMember; read GetTribeId() for the live value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ExposeOnSpawn=true)) int32 TribeId = -1; // ShamanTribe::Wild

	const FUnitRow& GetRow() const { return Row; }
	UFUNCTION(BlueprintCallable) bool IsShaman() const { return Row.Role == EUnitRole::Shaman; }
	UFUNCTION(BlueprintCallable) bool CountsAsFollower() const { return Row.bCountsAsFollower; }
	UFUNCTION(BlueprintCallable) bool IsRagdolling() const;
	UFUNCTION(BlueprintCallable) bool IsDrowning() const { return DrownTimer > 0.f; }
	UFUNCTION(BlueprintCallable) float GetDrownRemaining() const { return FMath::Max(0.f, Row.DrownTime - DrownTimer); }

	// Orders
	UFUNCTION(BlueprintCallable) void SetOrder(EFollowerOrder NewOrder, FVector Location);
	EFollowerOrder GetOrder() const { return Order; }
	FVector GetOrderLocation() const { return OrderLocation; }
	FVector GetHomeLocation() const { return HomeLocation; }

	// Combat
	/** Melee the target (or the best hostile in front when null). Returns true if a hit was delivered. */
	UFUNCTION(BlueprintCallable) bool TryMeleeAttack(AActor* Target);
	float GetMeleeReach(const AActor* Target) const;
	bool IsMeleeReady() const;
	UPROPERTY(BlueprintAssignable) FOnMeleeAttack OnMeleeAttack; // animation / VFX hook

	/** Body position (the ragdoll's when ragdolling). */
	FVector GetBodyLocation() const;

	/** Move to a new tribe (Convert / Hypnotize later). Re-registers and recolours. */
	UFUNCTION(BlueprintCallable) void ChangeTribe(int32 NewTribeId);

	/** Bring a dead unit back (used by reincarnation). */
	virtual void Reincarnate(const FVector& Location, const FRotator& Rotation);

	// ITribeOwned
	virtual int32 GetTribeId() const override;
	// IShamanInteractable
	virtual FText GetInteractionText(const AActor* Interactor) const override;
	virtual bool CanInteract(const AActor* Interactor) const override;
	virtual void Interact(AActor* Interactor) override;
	// IShamanDamageReceiver
	virtual bool IsAlive() const override;
	// IGameplayTagAssetInterface: Faction.*, Unit.*, State.Dead / State.Ragdoll / State.InWater (derived, not stored)
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION() virtual void HandleDeath(AActor* Killer, AController* KillerController);
	/** In-place conversion (e.g. Convert with no ConvertedFollowerClass): become a Brave of the new tribe. */
	UFUNCTION() void HandleConverted(UTribeMemberComponent* From, UTribeMemberComponent* To, EConversionKind Kind);
	void LoadRow();
	void ApplyIdentity();
	void ApplyRowStats();
	void ApplyVisuals();
	AActor* FindMeleeTarget() const;
	void UpdateWater(float DeltaSeconds);

	FUnitRow Row;
	EFollowerOrder Order = EFollowerOrder::Wander;
	FVector OrderLocation = FVector::ZeroVector;
	FVector HomeLocation = FVector::ZeroVector;
	double LastMeleeTime = -1000.0;
	float DrownTimer = 0.f;
	FTransform PlaceholderRestTransform;

	UPROPERTY() UStaticMesh* PlaceholderMesh = nullptr;
};
