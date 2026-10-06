#include "TribeRegistrySubsystem.h"
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UTribeRegistrySubsystem* UTribeRegistrySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UTribeRegistrySubsystem>() : nullptr;
}

void UTribeRegistrySubsystem::RegisterTribe(UTribeComponent* Tribe)
{
	if (!Tribe) return;
	if (UTribeComponent* Existing = FindTribe(Tribe->TribeId))
	{
		if (Existing != Tribe)
			UE_LOG(LogTemp, Warning, TEXT("Tribe id %d already belongs to %s; %s ignored (ids must be unique)."), Tribe->TribeId,
				*GetNameSafe(Existing->GetOwner()), *GetNameSafe(Tribe->GetOwner()));
		return; // keep the first tribe so two rosters never share followers
	}
	Tribes.Add(Tribe->TribeId, Tribe);

	// Adopt followers that began play before their tribe did. Iterate a copy: roster callbacks may spawn units.
	const TArray<TWeakObjectPtr<UTribeMemberComponent>> Snapshot = Members;
	for (const TWeakObjectPtr<UTribeMemberComponent>& M : Snapshot)
		if (M.IsValid() && M->IsFollower() && M->TribeId == Tribe->TribeId)
			Tribe->RegisterFollower(M.Get(), /*bEnforceCapacity*/ false);
}

void UTribeRegistrySubsystem::UnregisterTribe(UTribeComponent* Tribe)
{
	if (Tribe && FindTribe(Tribe->TribeId) == Tribe) Tribes.Remove(Tribe->TribeId);
}

UTribeComponent* UTribeRegistrySubsystem::FindTribe(int32 TribeId) const
{
	const TWeakObjectPtr<UTribeComponent>* T = Tribes.Find(TribeId);
	return T ? T->Get() : nullptr;
}

void UTribeRegistrySubsystem::RegisterMember(UTribeMemberComponent* Member)
{
	if (!Member) return;
	Members.AddUnique(Member);
	if (Member->IsFollower())
		if (UTribeComponent* T = FindTribe(Member->TribeId))
			T->RegisterFollower(Member, /*bEnforceCapacity*/ false); // level-placed / spawned followers always join
}

void UTribeRegistrySubsystem::UnregisterMember(UTribeMemberComponent* Member)
{
	if (!Member) return;
	Members.Remove(Member);
	if (UTribeComponent* T = FindTribe(Member->TribeId)) T->UnregisterFollower(Member);
}

void UTribeRegistrySubsystem::OnMemberChanged(UTribeMemberComponent* Member, int32 OldTribeId)
{
	if (!Member) return;
	if (OldTribeId != Member->TribeId)
		if (UTribeComponent* Old = FindTribe(OldTribeId)) Old->UnregisterFollower(Member);
	if (UTribeComponent* New = FindTribe(Member->TribeId))
	{
		if (Member->IsFollower()) New->RegisterFollower(Member, false);
		else New->UnregisterFollower(Member);
	}
}

void UTribeRegistrySubsystem::GetMembersInRadius(const FVector& Center, float Radius, TArray<UTribeMemberComponent*>& Out) const
{
	Out.Reset();
	const float R2 = Radius * Radius;
	for (const TWeakObjectPtr<UTribeMemberComponent>& W : Members)
	{
		UTribeMemberComponent* M = W.Get();
		const AActor* A = M ? M->GetOwner() : nullptr;
		if (!A || A->IsPendingKillPending()) continue;
		if (FVector::DistSquared(A->GetActorLocation(), Center) <= R2) Out.Add(M);
	}
	Out.StableSort([&Center](const UTribeMemberComponent& A, const UTribeMemberComponent& B)
	{
		return FVector::DistSquared(A.GetOwner()->GetActorLocation(), Center) < FVector::DistSquared(B.GetOwner()->GetActorLocation(), Center);
	});
}
