#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ShamanInterfaces.generated.h"

/** Anything that belongs to a tribe (units, buildings). Tribe ids: see ShamanTribe in ShamanTargetRules.h. */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UTribeOwned : public UInterface { GENERATED_BODY() };
class SHAMAN_API ITribeOwned
{
	GENERATED_BODY()
public:
	virtual int32 GetTribeId() const = 0;
};

/** Things the Shaman can interact with (E key). */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UShamanInteractable : public UInterface { GENERATED_BODY() };
class SHAMAN_API IShamanInteractable
{
	GENERATED_BODY()
public:
	virtual FText GetInteractionText(const AActor* Interactor) const = 0;
	virtual bool CanInteract(const AActor* Interactor) const { return true; }
	virtual void Interact(AActor* Interactor) = 0;
};

/** Anything with health. Damage itself arrives through Unreal's TakeDamage pipeline (UHealthComponent). */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UShamanDamageReceiver : public UInterface { GENERATED_BODY() };
class SHAMAN_API IShamanDamageReceiver
{
	GENERATED_BODY()
public:
	virtual bool IsAlive() const = 0;
	virtual bool IsInvulnerable() const { return false; }
};

/** Optional per-target spell rules (immunities, shields in Phase 3). */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class USpellTarget : public UInterface { GENERATED_BODY() };
class SHAMAN_API ISpellTarget
{
	GENERATED_BODY()
public:
	virtual bool IsImmuneToSpell(FName SpellId) const { return false; }
};
