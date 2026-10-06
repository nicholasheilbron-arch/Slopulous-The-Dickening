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
		for (FTribeState& T : Tribes) W->GetTimerManager().ClearTimer(T.RebirthTimer); // no stale rebirths after a regenerate
	Tribes.Reset();
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
	RefreshCapacity(T->TribeId);
}

void UTribeSubsystem::UnregisterBuilding(ABuildingActor* B)
{
	for (FTribeState& T : Tribes)
	{
		if (T.Buildings.Remove(B) > 0) RefreshCapacity(T.TribeId);
		if (T.Circle.Get() == B) T.Circle = nullptr;
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

	const bool bMayReincarnate = T->Def.bPlayerControlled || Rebirth.bEnemyShamansReincarnate;
	if (!T->Circle.IsValid() || !bMayReincarnate)
	{
		T->bShamanEliminated = true; // Phase 4: tribe defeat / Shaman-kill reward hooks go here
		UE_LOG(LogShaman, Log, TEXT("Shaman of tribe %d permanently eliminated."), T->TribeId);
		return;
	}
	const float Delay = ComputeRebirthTime(Rebirth, GetFollowerCount(T->TribeId));
	T->RebirthAt = GetWorld()->GetTimeSeconds() + Delay;
	FTimerDelegate D = FTimerDelegate::CreateUObject(this, &UTribeSubsystem::DoRebirth, T->TribeId);
	GetWorld()->GetTimerManager().SetTimer(T->RebirthTimer, D, Delay, false);
	UE_LOG(LogShaman, Log, TEXT("Shaman of tribe %d reborn in %.1fs."), T->TribeId, Delay);
}

void UTribeSubsystem::DoRebirth(int32 TribeId)
{
	FTribeState* T = Find(TribeId);
	if (!T) return;
	T->RebirthAt = -1.0;
	AShamanUnitBase* S = T->Shaman.Get();
	ABuildingActor* C = T->Circle.Get();
	if (!S || !C) { T->bShamanEliminated = true; return; }
	S->Reincarnate(C->GetRebirthLocation(), C->GetActorRotation());
	OnShamanReborn.Broadcast(TribeId, S);
}
