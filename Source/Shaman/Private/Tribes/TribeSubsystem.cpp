#include "Tribes/TribeSubsystem.h"
#include "Characters/ShamanUnitBase.h"
#include "Buildings/BuildingActor.h"
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "TribeRegistrySubsystem.h"
#include "Core/ShamanLog.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UTribeSubsystem::InitTribes(const TArray<FTribeDefinition>& Definitions, const FReincarnationConfig& InRebirth)
{
	if (UWorld* W = GetWorld())
	{
		for (FTribeState& T : Tribes) // no stale rebirths / death checks after a regenerate
		{
			W->GetTimerManager().ClearTimer(T.RebirthTimer);
			W->GetTimerManager().ClearTimer(T.DeathCheckTimer);
		}
	}
	Tribes.Reset();
	bLevelWon = false;
	Rebirth = InRebirth;
	for (int32 I = 0; I < Definitions.Num(); ++I)
	{
		FTribeState& T = Tribes.AddDefaulted_GetRef();
		T.TribeId = I;
		T.Def = Definitions[I];
	}
	for (int32 I = 0; I < Tribes.Num(); ++I) RefreshCapacity(I);
}

UTribeComponent* UTribeSubsystem::GetTribeComponent(int32 TribeId) const
{
	const UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(this);
	return R ? R->FindTribe(TribeId) : nullptr;
}

int32 UTribeSubsystem::ComputeCapacity(const FTribeState& T) const
{
	int32 Cap = T.Def.BaseCapacity;
	for (const TWeakObjectPtr<ABuildingActor>& B : T.Buildings) if (B.IsValid()) Cap += B->GetPopulationCapacity();
	return Cap;
}

void UTribeSubsystem::RefreshCapacity(int32 TribeId)
{
	const FTribeState* T = Find(TribeId);
	if (UTribeComponent* TC = T ? GetTribeComponent(TribeId) : nullptr) TC->SetPopulationCapacity(ComputeCapacity(*T));
}

void UTribeSubsystem::RegisterShaman(AShamanUnitBase* Shaman)
{
	FTribeState* T = Shaman ? Find(Shaman->GetTribeId()) : nullptr;
	if (!T) return;
	T->Shaman = Shaman;
	RefreshCapacity(T->TribeId); // the Shaman carries the tribe's UTribeComponent
}

void UTribeSubsystem::UnregisterShaman(AShamanUnitBase* Shaman)
{
	for (FTribeState& T : Tribes) if (T.Shaman.Get() == Shaman) T.Shaman = nullptr;
}

void UTribeSubsystem::RegisterBuilding(ABuildingActor* B)
{
	if (!B) return;
	FTribeState* T = Find(B->GetTribeId());
	if (!T) return;
	T->Buildings.AddUnique(B);
	if (B->IsReincarnationSite()) T->Circle = B;
	if (B->IsGatheringPoint()) T->Settlement.GatheringPoint = B;
	RefreshSettlement(*T);
	RefreshCapacity(T->TribeId);
}

void UTribeSubsystem::UnregisterBuilding(ABuildingActor* B)
{
	for (FTribeState& T : Tribes)
	{
		if (T.Buildings.Remove(B) > 0) RefreshCapacity(T.TribeId);
		if (T.Circle.Get() == B) T.Circle = nullptr;
		if (T.Settlement.GatheringPoint.Get() == B) T.Settlement.GatheringPoint = nullptr;
		RefreshSettlement(T);
	}
}

int32 UTribeSubsystem::GetFollowerCount(int32 TribeId) const
{
	const UTribeComponent* TC = GetTribeComponent(TribeId);
	return TC ? TC->GetFollowerCount() : 0;
}

int32 UTribeSubsystem::GetPopulationCapacity(int32 TribeId) const
{
	const FTribeState* T = Find(TribeId);
	return T ? ComputeCapacity(*T) : 0;
}

AShamanUnitBase* UTribeSubsystem::GetShaman(int32 TribeId) const
{
	const FTribeState* T = Find(TribeId);
	return T ? T->Shaman.Get() : nullptr;
}

ABuildingActor* UTribeSubsystem::GetReincarnationCircle(int32 TribeId) const
{
	const FTribeState* T = Find(TribeId);
	return T ? T->Circle.Get() : nullptr;
}

FLinearColor UTribeSubsystem::GetTribeColor(int32 TribeId) const
{
	const FTribeState* T = Find(TribeId);
	return T ? T->Def.Color : FLinearColor::White;
}

float UTribeSubsystem::GetRebirthRemaining(int32 TribeId) const
{
	const FTribeState* T = Find(TribeId);
	if (!T || T->RebirthAt < 0.0) return -1.f;
	return FMath::Max(0.f, (float)(T->RebirthAt - GetWorld()->GetTimeSeconds()));
}

TArray<AShamanUnitBase*> UTribeSubsystem::GetFollowers(int32 TribeId) const
{
	TArray<AShamanUnitBase*> Out;
	if (const UTribeComponent* TC = GetTribeComponent(TribeId))
		for (UTribeMemberComponent* M : TC->GetFollowers())
			if (AShamanUnitBase* U = Cast<AShamanUnitBase>(M->GetOwner()))
				if (U->IsAlive()) Out.Add(U);
	return Out;
}

float UTribeSubsystem::ComputeRebirthTime(const FReincarnationConfig& C, int32 Followers)
{
	const float Modifier = 1.f + FMath::Max(0.f, C.PerFollowerModifier) * FMath::Max(0, Followers);
	return FMath::Clamp(C.BaseTime / Modifier, C.MinTime, FMath::Max(C.MinTime, C.MaxTime));
}

void UTribeSubsystem::NotifyShamanDied(AShamanUnitBase* Shaman)
{
	FTribeState* T = Shaman ? Find(Shaman->GetTribeId()) : nullptr;
	if (!T) return;
	OnShamanDied.Broadcast(T->TribeId, Shaman);
	// Decide once this frame's other deaths are in (a Blast can kill the Shaman and its last followers together;
	// overlap order is arbitrary).
	FTimerDelegate D = FTimerDelegate::CreateUObject(this, &UTribeSubsystem::ResolveShamanDeath, T->TribeId);
	T->DeathCheckTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(D);
}

void UTribeSubsystem::ResolveShamanDeath(int32 TribeId)
{
	FTribeState* T = Find(TribeId);
	if (!T || T->bShamanEliminated) return;
	const AShamanUnitBase* S = T->Shaman.Get();
	if (S && S->IsAlive()) return; // already back (e.g. regenerate)

	const int32 Followers = GetFollowerCount(T->TribeId);
	const bool bPlayer = T->Def.bPlayerControlled;
	if (!T->Circle.IsValid()) { EliminateTribe(*T, TEXT("no Reincarnation Circle")); return; }
	if (!bPlayer && !Rebirth.bEnemyShamansReincarnate) { EliminateTribe(*T, TEXT("enemy reincarnation disabled")); return; }
	if (!bPlayer && Followers == 0) { EliminateTribe(*T, TEXT("Shaman died with 0 followers")); return; }

	const float Delay = ComputeRebirthTime(Rebirth, Followers);
	T->RebirthAt = GetWorld()->GetTimeSeconds() + Delay;
	FTimerDelegate D = FTimerDelegate::CreateUObject(this, &UTribeSubsystem::DoRebirth, T->TribeId);
	GetWorld()->GetTimerManager().SetTimer(T->RebirthTimer, D, Delay, false);
	UE_LOG(LogShaman, Log, TEXT("Shaman of tribe %d reborn in %.1fs (%d followers)."), T->TribeId, Delay, Followers);
}

void UTribeSubsystem::EliminateTribe(FTribeState& T, const TCHAR* Reason)
{
	T.bShamanEliminated = true;
	T.RebirthAt = -1.0;
	GetWorld()->GetTimerManager().ClearTimer(T.RebirthTimer);
	// The player's own circle is never destroyed here; an enemy circle goes with its tribe.
	if (!T.Def.bPlayerControlled)
		if (ABuildingActor* C = T.Circle.Get())
		{
			T.Circle = nullptr;
			C->Destroy(); // EndPlay unregisters the building
		}
	UE_LOG(LogShaman, Log, TEXT("Tribe %d eliminated (%s)%s."), T.TribeId, Reason, T.Def.bPlayerControlled ? TEXT("") : TEXT("; its Reincarnation Circle is destroyed"));
	OnTribeEliminated.Broadcast(T.TribeId);
	CheckLevelWon();
}

bool UTribeSubsystem::IsTribeEliminated(int32 TribeId) const
{
	const FTribeState* T = Find(TribeId);
	return T && T->bShamanEliminated;
}

void UTribeSubsystem::CheckLevelWon()
{
	if (bLevelWon) return;
	int32 Enemies = 0;
	for (const FTribeState& T : Tribes)
	{
		if (T.Def.bPlayerControlled) continue;
		++Enemies;
		if (!T.bShamanEliminated) return;
	}
	if (Enemies == 0) return;
	bLevelWon = true;
	UE_LOG(LogShaman, Log, TEXT("Level won: all %d enemy tribe(s) eliminated."), Enemies);
	OnLevelWon.Broadcast();
}

void UTribeSubsystem::DoRebirth(int32 TribeId)
{
	FTribeState* T = Find(TribeId);
	if (!T) return;
	T->RebirthAt = -1.0;
	AShamanUnitBase* S = T->Shaman.Get();
	ABuildingActor* C = T->Circle.Get();
	if (!S || !C) { EliminateTribe(*T, TEXT("Shaman or circle gone at rebirth")); return; }
	S->Reincarnate(C->GetRebirthLocation(), C->GetActorRotation());
	OnShamanReborn.Broadcast(TribeId, S);
}

// ---- Tribe simulation view ------------------------------------------------------------------------------------------

void UTribeSubsystem::RefreshSettlement(FTribeState& T)
{
	const ABuildingActor* A = T.Settlement.GatheringPoint.Get();
	if (!A) A = T.Circle.Get();
	T.Settlement.bHasAnchor = A != nullptr;
	if (A) T.Settlement.Anchor = A->GetActorLocation();
}

int32 UTribeSubsystem::GetLivingMemberCount(int32 TribeId) const
{
	const AShamanUnitBase* S = GetShaman(TribeId);
	return GetFollowers(TribeId).Num() + (S && S->IsAlive() ? 1 : 0);
}

TArray<UTribeMemberComponent*> UTribeSubsystem::GetAvailableWorkers(int32 TribeId) const
{
	const UTribeComponent* TC = GetTribeComponent(TribeId);
	return TC ? TC->GetAvailableWorkers() : TArray<UTribeMemberComponent*>();
}

int32 UTribeSubsystem::GetAvailableWorkerCount(int32 TribeId) const
{
	const UTribeComponent* TC = GetTribeComponent(TribeId);
	return TC ? TC->GetAvailableWorkerCount() : 0;
}

bool UTribeSubsystem::GetSettlementAnchor(int32 TribeId, FVector& OutAnchor) const
{
	const FTribeState* T = Find(TribeId);
	if (!T || !T->Settlement.bHasAnchor) return false;
	OutAnchor = T->Settlement.Anchor;
	return true;
}

TArray<ABuildingActor*> UTribeSubsystem::GetBuildings(int32 TribeId) const
{
	TArray<ABuildingActor*> Out;
	if (const FTribeState* T = Find(TribeId))
		for (const TWeakObjectPtr<ABuildingActor>& B : T->Buildings) if (B.IsValid()) Out.Add(B.Get());
	return Out;
}

FName UTribeSubsystem::GetFactionTag(int32 TribeId) const
{
	const FTribeState* T = Find(TribeId);
	return T ? T->Def.FactionTag : NAME_None;
}
