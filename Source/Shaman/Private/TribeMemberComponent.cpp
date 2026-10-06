#include "TribeMemberComponent.h"
#include "TribeComponent.h"
#include "TribeRegistrySubsystem.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UTribeMemberComponent::UTribeMemberComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
}

void UTribeMemberComponent::InitializeComponent()
{
	Super::InitializeComponent();
	if (const UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(this))
	{
		const UTribeRegistrySubsystem::FPendingConversion& P = R->GetPendingConversion();
		if (P.bActive)
		{
			UnitKind = EUnitKind::Follower;
			TribeId = P.TribeId;
			PopulationCost = P.PopulationCost;
		}
	}
}

UTribeMemberComponent* UTribeMemberComponent::FindOn(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UTribeMemberComponent>() : nullptr;
}

void UTribeMemberComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(this)) R->RegisterMember(this);
}

void UTribeMemberComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(this)) R->UnregisterMember(this);
	Super::EndPlay(Reason);
}

bool UTribeMemberComponent::IsConvertible() const
{
	const AActor* Owner = GetOwner();
	return bConvertible && !bConversionInProgress && Owner && !Owner->IsPendingKillPending()
		&& (UnitKind == EUnitKind::Wildman || UnitKind == EUnitKind::Follower);
}

bool UTribeMemberComponent::CanBeConvertedTo(int32 NewTribeId, EConversionKind Kind) const
{
	if (NewTribeId < 0 || NewTribeId == TribeId || !IsConvertible()) return false;
	switch (Kind)
	{
	case EConversionKind::Recruit: return IsWildman();   // Convert: Wildmen only, never another tribe's followers
	default: return false;                                // Permanent / Hypnotize: future milestones
	}
}

void UTribeMemberComponent::SetTribeId(int32 NewTribeId)
{
	if (NewTribeId == TribeId) return;
	const int32 Old = TribeId;
	TribeId = NewTribeId;
	if (HasBegunPlay())
		if (UTribeRegistrySubsystem* R = UTribeRegistrySubsystem::Get(this)) R->OnMemberChanged(this, Old);
}

UTribeMemberComponent* UTribeMemberComponent::ConvertToTribe(UTribeComponent* NewTribe, EConversionKind Kind)
{
	// 1) Validate before touching anything (transactional: failure leaves this unit unchanged).
	if (!NewTribe || !CanBeConvertedTo(NewTribe->TribeId, Kind)) return nullptr;
	if (!NewTribe->CanAddFollowers(PopulationCost)) return nullptr;
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner) return nullptr;
	bConversionInProgress = true;
	const bool bHadCollision = Owner->GetActorEnableCollision();

	UTribeMemberComponent* Result = nullptr;
	if (UClass* Cls = NewTribe->ConvertedFollowerClass.Get())
	{
		// 2a) Replace with the tribe's follower Blueprint (e.g. BP_Brave). The pending identity is applied in the new
		//     member's InitializeComponent, so its BeginPlay (C++ and Blueprint) already sees the final tribe.
		UTribeRegistrySubsystem* Registry = UTribeRegistrySubsystem::Get(this);
		Owner->SetActorEnableCollision(false); // let the follower spawn exactly where the Wildman stands
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (Registry) Registry->BeginPendingConversion(NewTribe->TribeId, PopulationCost);
		AActor* NewActor = World->SpawnActor<AActor>(Cls, Owner->GetActorTransform(), P);
		if (Registry) Registry->EndPendingConversion();
		UTribeMemberComponent* NewMember = FindOn(NewActor);
		if (!NewMember)
		{
			UE_LOG(LogTemp, Warning, TEXT("ConvertedFollowerClass %s has no UTribeMemberComponent; conversion aborted."), *GetNameSafe(Cls));
			if (NewActor) NewActor->Destroy();
			Owner->SetActorEnableCollision(bHadCollision);
			bConversionInProgress = false;
			return nullptr;
		}
		NewMember->UnitKind = EUnitKind::Follower;
		NewMember->PopulationCost = PopulationCost;
		NewMember->SetTribeId(NewTribe->TribeId);     // re-registers if the Blueprint default tribe differed
		NewTribe->RegisterFollower(NewMember, false);  // idempotent; covers Blueprints whose default kind was not Follower
		Result = NewMember;
	}
	else
	{
		// 2b) No follower class assigned yet: convert in place (stub until BP_Brave exists).
		UnitKind = EUnitKind::Follower;
		SetTribeId(NewTribe->TribeId);
		NewTribe->RegisterFollower(this, false);
		Result = this;
	}

	if (!NewTribe->IsFollower(Result))
	{
		// Should not happen (capacity was checked above); roll back so the population stays consistent.
		UE_LOG(LogTemp, Error, TEXT("Conversion of %s failed to register; rolling back."), *GetNameSafe(Owner));
		if (Result == this) { UnitKind = EUnitKind::Wildman; SetTribeId(ShamanTribes::NoTribe); }
		else { if (Result->GetOwner()) Result->GetOwner()->Destroy(); Owner->SetActorEnableCollision(bHadCollision); }
		bConversionInProgress = false;
		return nullptr;
	}

	// 3) Feedback hook fires while the original actor is still alive (Blueprint listeners on it still run).
	OnConverted.Broadcast(this, Result, Kind);
	if (Result == this)
	{
		bConversionInProgress = false;
		return Result;
	}
	if (!Owner->Destroy()) // the Wildman actor is replaced by the new follower
	{
		UE_LOG(LogTemp, Error, TEXT("Could not destroy %s after conversion; rolling back."), *GetNameSafe(Owner));
		if (Result->GetOwner()) Result->GetOwner()->Destroy();
		Owner->SetActorEnableCollision(bHadCollision);
		bConversionInProgress = false;
		return nullptr;
	}
	return Result;
}
