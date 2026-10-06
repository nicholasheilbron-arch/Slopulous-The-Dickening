#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TribeRegistrySubsystem.generated.h"

class UTribeComponent;
class UTribeMemberComponent;

/**
 * World-level lookup: tribe id -> tribe, and the list of every unit identity in the world.
 * Spell target queries go through GetMembersInRadius, so faction/unit type never depends on collision setup.
 * Handles any BeginPlay order between tribes and their members.
 */
UCLASS()
class SHAMAN_API UTribeRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	void RegisterTribe(UTribeComponent* Tribe);
	void UnregisterTribe(UTribeComponent* Tribe);
	UTribeComponent* FindTribe(int32 TribeId) const;

	void RegisterMember(UTribeMemberComponent* Member);
	void UnregisterMember(UTribeMemberComponent* Member);
	/** Called by a member after its TribeId or UnitKind changed. */
	void OnMemberChanged(UTribeMemberComponent* Member, int32 OldTribeId);

	/** Members whose owner is within Radius of Center, closest first. */
	void GetMembersInRadius(const FVector& Center, float Radius, TArray<UTribeMemberComponent*>& Out) const;
	int32 GetNumMembers() const { return Members.Num(); }

	static UTribeRegistrySubsystem* Get(const UObject* WorldContext);

	/** Identity to stamp onto the next spawned follower during a conversion, so the new actor's BeginPlay
	 *  (C++ and Blueprint) already sees its final tribe and kind. Scoped by UTribeMemberComponent::ConvertToTribe. */
	struct FPendingConversion { bool bActive = false; int32 TribeId = -1; int32 PopulationCost = 1; };
	void BeginPendingConversion(int32 TribeId, int32 PopulationCost) { Pending.bActive = true; Pending.TribeId = TribeId; Pending.PopulationCost = PopulationCost; }
	void EndPendingConversion() { Pending = FPendingConversion(); }
	const FPendingConversion& GetPendingConversion() const { return Pending; }

private:
	TMap<int32, TWeakObjectPtr<UTribeComponent>> Tribes;
	TArray<TWeakObjectPtr<UTribeMemberComponent>> Members;
	FPendingConversion Pending;
};
