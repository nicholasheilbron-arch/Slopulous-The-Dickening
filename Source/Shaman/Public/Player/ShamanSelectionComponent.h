#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShamanSelectionComponent.generated.h"

class AShamanUnitBase;

DECLARE_MULTICAST_DELEGATE(FOnShamanSelectionChanged);

/**
 * The player's current unit selection (RTS mode). Owned by AShamanPlayerController. Holds weak references only and
 * never touches tasks, tribe membership or worker availability: selecting is presentation state, commands are issued
 * separately. Units that die, are destroyed or leave the player's tribe drop out on the next prune (10x per second,
 * over the selection only - no world scan).
 */
UCLASS(ClassGroup=(Shaman), meta=(BlueprintSpawnableComponent))
class SHAMAN_API UShamanSelectionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShamanSelectionComponent();

	void SetPlayerTribe(int32 TribeId);
	int32 GetPlayerTribe() const { return PlayerTribe; }

	/** Click on a unit: toggles it off if selected; otherwise replaces the selection (or adds, with bAdditive).
	 *  An unselectable unit counts as an empty click. */
	void ClickUnit(AShamanUnitBase* Unit, bool bAdditive);
	/** Click on nothing selectable: clears the selection unless bAdditive. */
	void ClickEmpty(bool bAdditive);
	/** Drag-box result: replaces the selection with the selectable units (or adds, with bAdditive). */
	void SelectUnits(const TArray<AShamanUnitBase*>& Units, bool bAdditive);
	void ClearSelection();

	bool IsSelected(const AShamanUnitBase* Unit) const;
	int32 Num() const { return Selected.Num(); }
	/** Valid, still-selectable units (does not modify the selection). */
	TArray<AShamanUnitBase*> GetSelectedUnits() const;
	/** Drops dead / destroyed / converted units. Returns how many were removed. */
	int32 PruneInvalid();

	FOnShamanSelectionChanged OnSelectionChanged;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Changed(const TCHAR* Why);
	TArray<TWeakObjectPtr<AShamanUnitBase>> Selected;
	int32 PlayerTribe = 0;
};
