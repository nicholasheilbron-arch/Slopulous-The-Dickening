#pragma once
#include "CoreMinimal.h"
#include "Characters/ShamanUnitBase.h"
#include "ShamanCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class USpellComponent;
class UProgressionComponent;
class UTribeComponent;
class USpellEffectDispatcherComponent;

/**
 * The Shaman: a unit with spells, a third-person camera and player input. Enemy Shamans are the same class
 * driven by AShamanUnitAIController, so player and AI share one spell framework (spec section 9).
 */
UCLASS()
class SHAMAN_API AShamanCharacter : public AShamanUnitBase
{
	GENERATED_BODY()
public:
	AShamanCharacter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) USpringArmComponent* CameraBoom;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) UCameraComponent* FollowCamera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) USpellComponent* Spells;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) UProgressionComponent* Progression;
	/** The tribe this Shaman leads: follower roster + population capacity (Convert milestone). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) UTribeComponent* Tribe;
	/** Routes non-projectile spell EffectIds (ConvertWildmen, ...) to their effects. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) USpellEffectDispatcherComponent* EffectDispatcher;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BaseTurnRate = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BaseLookUpRate = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float AimTraceDistance = 12000.f;

	/** Faces the aim point then casts through the shared USpellComponent. Used by input and AI. */
	UFUNCTION(BlueprintCallable) bool TryCastSpell(FName SpellId, FVector AimPoint);
	UFUNCTION(BlueprintCallable) FName GetSelectedSpellId() const;
	UFUNCTION(BlueprintCallable) FVector GetAimPoint() const;

	/** HUD helpers. */
	AActor* GetFocusedInteractable() const { return FocusedInteractable.Get(); }
	FText GetFocusText() const;
	ESpellCastResult GetLastCastFeedback(float& OutAge) const;
	int32 GetSelectedSpellIndex() const { return SelectedSpellIndex; }

	virtual void Reincarnate(const FVector& Location, const FRotator& Rotation) override;

	/** Console (playtests): ShamanLearnSpell Convert */
	UFUNCTION(Exec) void ShamanLearnSpell(FName SpellId);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
	virtual void HandleDeath(AActor* Killer, AController* KillerController) override;

	void MoveForward(float V);
	void MoveRight(float V);
	void TurnAtRate(float V);
	void LookUpAtRate(float V);
	void InputCast();
	void InputMelee();
	void InputInteract();
	void InputNextSpell();
	void InputPrevSpell();
	void InputRally();
	void InputToggleDebug();
	void UpdateFocus();

	int32 SelectedSpellIndex = 0;
	TWeakObjectPtr<AActor> FocusedInteractable;
	double LastCastFeedbackTime = -100.0;
	ESpellCastResult LastCastFeedback = ESpellCastResult::Success;
};
