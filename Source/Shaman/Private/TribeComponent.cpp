#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "TribeRegistrySubsystem.h"
#include "SpellComponent.h"
#include "GameFramework/Actor.h"

UTribeComponent::UTribeComponent() { PrimaryComponentTick.bCanEverTick = false; }

void UTribeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (TribeId < 0) return; // -1 is reserved for unaligned units (Wildmen); such a component stays inactive
	if (UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(this)) R->RegisterTribe(this);
	NotifyChanged();
}

void UTribeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(this)) R->UnregisterTribe(this);
	Super::EndPlay(Reason);
}

void UTribeComponent::Prune()
{
	Followers.RemoveAll([](const TWeakObjectPtr<UTribeMemberComponent>& M) { return !M.IsValid(); });
}

int32 UTribeComponent::GetFollowerCount() const
{
	int32 N = 0;
	for (const TWeakObjectPtr<UTribeMemberComponent>& M : Followers) if (M.IsValid()) ++N;
	return N;
}

int32 UTribeComponent::GetPopulationUsed() const
{
	int32 Used = 0;
	for (const TWeakObjectPtr<UTribeMemberComponent>& M : Followers) if (M.IsValid()) Used += M->PopulationCost;
	return Used;
}

void UTribeComponent::SetPopulationCapacity(int32 NewCapacity)
{
	PopulationCapacity = FMath::Max(0, NewCapacity);
}

bool UTribeComponent::CanAddFollowers(int32 PopulationCostToAdd) const
{
	return GetPopulationUsed() + FMath::Max(0, PopulationCostToAdd) <= PopulationCapacity;
}

bool UTribeComponent::IsFollower(const UTribeMemberComponent* Member) const
{
	for (const TWeakObjectPtr<UTribeMemberComponent>& M : Followers) if (M.Get() == Member) return Member != nullptr;
	return false;
}

TArray<UTribeMemberComponent*> UTribeComponent::GetFollowers() const
{
	TArray<UTribeMemberComponent*> Out;
	for (const TWeakObjectPtr<UTribeMemberComponent>& M : Followers) if (UTribeMemberComponent* P = M.Get()) Out.Add(P);
	return Out;
}

bool UTribeComponent::RegisterFollower(UTribeMemberComponent* Member, bool bEnforceCapacity)
{
	if (!Member || !Member->IsFollower() || Member->TribeId != TribeId) return false;
	if (IsFollower(Member)) return true; // idempotent: never double count
	if (bEnforceCapacity && !CanAddFollowers(Member->PopulationCost)) return false;
	Prune();
	Followers.Add(Member);
	NotifyChanged();
	return true;
}

bool UTribeComponent::UnregisterFollower(UTribeMemberComponent* Member)
{
	const int32 Removed = Followers.RemoveAll([Member](const TWeakObjectPtr<UTribeMemberComponent>& M) { return M.Get() == Member; });
	Prune();
	if (Removed > 0) NotifyChanged();
	return Removed > 0;
}

void UTribeComponent::NotifyChanged()
{
	const int32 Count = GetFollowerCount();
	if (bDriveOwnerManaRegen)
		if (USpellComponent* Spells = GetOwner() ? GetOwner()->FindComponentByClass<USpellComponent>() : nullptr)
			Spells->FollowerCount = Count;
	OnFollowersChanged.Broadcast(TribeId, Count);
}

UTribeComponent* UTribeComponent::FindTribeFor(const AActor* Actor)
{
	if (!Actor) return nullptr;
	if (UTribeComponent* Own = Actor->FindComponentByClass<UTribeComponent>()) return Own;
	if (const UTribeMemberComponent* M = UTribeMemberComponent::FindOn(Actor))
		if (UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(Actor)) return R->FindTribe(M->TribeId);
	return nullptr;
}
