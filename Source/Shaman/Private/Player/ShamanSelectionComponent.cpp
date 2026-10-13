#include "Player/ShamanSelectionComponent.h"
#include "Player/ShamanControlTypes.h"
#include "Characters/ShamanUnitBase.h"
#include "Core/ShamanLog.h"

UShamanSelectionComponent::UShamanSelectionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UShamanSelectionComponent::SetPlayerTribe(int32 TribeId)
{
	if (PlayerTribe == TribeId) return;
	PlayerTribe = TribeId;
	PruneInvalid();
}

bool UShamanSelectionComponent::IsSelected(const AShamanUnitBase* Unit) const
{
	if (!Unit) return false;
	for (const TWeakObjectPtr<AShamanUnitBase>& W : Selected) if (W.Get() == Unit) return true;
	return false;
}

void UShamanSelectionComponent::ClickUnit(AShamanUnitBase* Unit, bool bAdditive)
{
	if (!FShamanSelectionRules::IsSelectable(Unit, PlayerTribe)) { ClickEmpty(bAdditive); return; }
	if (IsSelected(Unit))
	{
		Selected.RemoveAll([Unit](const TWeakObjectPtr<AShamanUnitBase>& W) { return W.Get() == Unit; });
		Changed(TEXT("deselect"));
		return;
	}
	if (!bAdditive) Selected.Reset();
	Selected.Add(Unit);
	Changed(bAdditive ? TEXT("add") : TEXT("select"));
}

void UShamanSelectionComponent::ClickEmpty(bool bAdditive)
{
	if (bAdditive || Selected.Num() == 0) return;
	Selected.Reset();
	Changed(TEXT("clear"));
}

void UShamanSelectionComponent::SelectUnits(const TArray<AShamanUnitBase*>& Units, bool bAdditive)
{
	if (!bAdditive) Selected.Reset();
	for (AShamanUnitBase* U : Units)
		if (FShamanSelectionRules::IsSelectable(U, PlayerTribe) && !IsSelected(U)) Selected.Add(U);
	Changed(TEXT("box"));
}

void UShamanSelectionComponent::ClearSelection()
{
	if (Selected.Num() == 0) return;
	Selected.Reset();
	Changed(TEXT("clear"));
}

TArray<AShamanUnitBase*> UShamanSelectionComponent::GetSelectedUnits() const
{
	TArray<AShamanUnitBase*> Out;
	for (const TWeakObjectPtr<AShamanUnitBase>& W : Selected)
		if (AShamanUnitBase* U = W.Get())
			if (FShamanSelectionRules::IsSelectable(U, PlayerTribe)) Out.Add(U);
	return Out;
}

int32 UShamanSelectionComponent::PruneInvalid()
{
	const int32 Removed = Selected.RemoveAll([this](const TWeakObjectPtr<AShamanUnitBase>& W)
	{
		return !FShamanSelectionRules::IsSelectable(W.Get(), PlayerTribe);
	});
	if (Removed > 0) Changed(TEXT("pruned (dead, destroyed or left the tribe)"));
	return Removed;
}

void UShamanSelectionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	PruneInvalid();
}

void UShamanSelectionComponent::Changed(const TCHAR* Why)
{
	FString Names;
	for (const TWeakObjectPtr<AShamanUnitBase>& W : Selected)
		if (const AShamanUnitBase* U = W.Get()) Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + U->GetName();
	UE_LOG(LogShaman, Log, TEXT("[Select] %s: %d unit(s)%s%s"), Why, Selected.Num(), Names.IsEmpty() ? TEXT("") : TEXT(" - "), *Names);
	OnSelectionChanged.Broadcast();
}
