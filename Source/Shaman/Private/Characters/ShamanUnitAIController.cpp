#include "Characters/ShamanUnitAIController.h"
#include "Characters/ShamanUnitBase.h"
#include "Characters/ShamanCharacter.h"
#include "SpellComponent.h"
#include "Game/ShamanGameData.h"
#include "Tribes/TribeSubsystem.h"
#include "Buildings/BuildingActor.h"
#include "Core/ShamanTargetRules.h"
#include "Terrain/ShamanSpace.h"
#include "Terrain/ShamanTerrainSubsystem.h"
#include "Navigation/ShamanSurfaceNavigation.h"
#include "Navigation/PathFollowingComponent.h"
#include "TribeMemberComponent.h"
#include "Engine/World.h"

AShamanUnitAIController::AShamanUnitAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f; // decisions 4x per second; movement itself is per-frame
}

float AShamanUnitAIController::RandFloat()
{
	// SplitMix64 seeded from the spawn location: deterministic for a given world seed.
	uint64 Z = (RngState += 0x9E3779B97F4A7C15ull);
	Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
	Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
	Z ^= Z >> 31;
	return (float)(Z >> 40) / 16777216.f;
}

void AShamanUnitAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	const FVector L = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;
	RngState = (uint64)(int64)FMath::RoundToInt(L.X) * 73856093ull ^ (uint64)(int64)FMath::RoundToInt(L.Y) * 19349663ull;
	const float A = RandFloat() * 2.f * PI;
	FormationOffset = FVector(FMath::Cos(A), FMath::Sin(A), 0.f);
	NextWanderTime = 0.0;
}

void AShamanUnitAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AShamanUnitBase* U = Cast<AShamanUnitBase>(GetPawn());
	if (!U || !U->IsAlive() || U->IsRagdolling())
	{
		Target = nullptr;
		bHasSurfaceGoal = false;
		return;
	}
	FPlanetFrame Planet;
	if (FShamanSpace::GetPlanet(this, Planet))
	{
		// Planet: steer every frame (no path following component), think 4x per second as before.
		if (PrimaryActorTick.TickInterval > 0.f) SetActorTickInterval(0.f);
		DecisionTimer -= DeltaSeconds;
		if (DecisionTimer <= 0.f)
		{
			DecisionTimer = 0.25f;
			Think(U);
		}
		SteerOnSurface(U);
		return;
	}
	if (PrimaryActorTick.TickInterval == 0.f) SetActorTickInterval(0.25f); // planet went away: back to the flat cadence
	Think(U);
}

void AShamanUnitAIController::Think(AShamanUnitBase* U)
{
	UpdateTarget(U);
	if (AActor* T = Target.Get()) Engage(U, T);
	else FollowOrders(U);
	// Mirror what this AI is doing into the tribe simulation state (debug/inspection only; tasks are not executed yet).
	if (U->TribeMember)
	{
		EUnitSimState Activity = EUnitSimState::Idle;
		if (Target.IsValid()) Activity = EUnitSimState::Combat;
		else if (U->GetOrder() == EFollowerOrder::FollowShaman) Activity = EUnitSimState::Following;
		else if (U->GetOrder() == EFollowerOrder::HoldPosition || U->GetOrder() == EFollowerOrder::GuardHome) Activity = EUnitSimState::Guarding;
		U->TribeMember->SetActivity(Activity);
	}
}

void AShamanUnitAIController::StopMovement()
{
	bHasSurfaceGoal = false;
	Super::StopMovement();
}

void AShamanUnitAIController::SteerOnSurface(AShamanUnitBase* U)
{
	if (!bHasSurfaceGoal) return;
	const FVector Here = U->GetActorLocation();
	if (FShamanSpace::HorizontalDistance(this, Here, SurfaceGoal) <= SurfaceAcceptance)
	{
		bHasSurfaceGoal = false;
		return;
	}
	U->AddMovementInput(FShamanSpace::HorizontalDirection(this, Here, SurfaceGoal), 1.f);
}

FVector AShamanUnitAIController::GetLeashAnchor(AShamanUnitBase* U) const
{
	switch (U->GetOrder())
	{
	case EFollowerOrder::FollowShaman:
		if (const UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>())
			if (const AShamanUnitBase* S = Tribes->GetShaman(U->GetTribeId()))
				if (S->IsAlive()) return S->GetActorLocation();
		return U->GetHomeLocation();
	case EFollowerOrder::HoldPosition: return U->GetOrderLocation();
	default: return U->GetHomeLocation();
	}
}

void AShamanUnitAIController::UpdateTarget(AShamanUnitBase* U)
{
	const FUnitRow& Row = U->GetRow();
	if (!Row.bCanFight) { Target = nullptr; return; }
	const FVector Anchor = GetLeashAnchor(U);

	// Keep the current target while it is valid and inside the leash.
	if (AActor* T = Target.Get())
	{
		const bool bValid = FShamanTargetRules::CanAffect(U, T, ESpellTargetFilter::EnemiesOnly, false)
			&& FShamanSpace::HorizontalDistance(this, T->GetActorLocation(), Anchor) <= Row.LeashRadius;
		if (bValid) return;
		Target = nullptr;
	}

	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByObjectType(Hits, U->GetActorLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Row.AggroRadius));
	float BestD = TNumericLimits<float>::Max();
	for (const FOverlapResult& H : Hits)
	{
		AActor* A = H.GetActor();
		if (!A || !FShamanTargetRules::CanAffect(U, A, ESpellTargetFilter::EnemiesOnly, false)) continue;
		if (FShamanSpace::HorizontalDistance(this, A->GetActorLocation(), Anchor) > Row.LeashRadius) continue;
		const float D = FVector::DistSquared(A->GetActorLocation(), U->GetActorLocation());
		if (D < BestD) { BestD = D; Target = A; }
	}
}

bool AShamanUnitAIController::TryCastAt(AShamanUnitBase* U, AActor* T)
{
	AShamanCharacter* S = Cast<AShamanCharacter>(U);
	if (!S || !S->Spells) return false;
	const float Dist = FVector::Dist(S->GetActorLocation(), T->GetActorLocation());
	// Same spell definitions as the player. Phase 1 AI: cast the first known spell that is in range and affordable.
	for (const FName& Id : S->Spells->KnownSpellIds)
	{
		const FSpellRow* Row = S->Spells->GetSpellRow(Id);
		if (!Row || Row->bSuperSpell || Row->TargetFilter == ESpellTargetFilter::FriendlyOnly) continue;
		if (Dist > S->Spells->GetRangeUnits(Id)) continue;
		if (S->Spells->Mana < Row->ManaCost || S->Spells->GetCooldownRemaining(Id) > 0.f) continue;
		StopMovement();
		if (S->TryCastSpell(Id, T->GetActorLocation())) return true;
	}
	return false;
}

void AShamanUnitAIController::Engage(AShamanUnitBase* U, AActor* T)
{
	if (TryCastAt(U, T)) return;
	const float Reach = U->GetMeleeReach(T);
	if (FShamanSpace::HorizontalDistance(this, U->GetActorLocation(), T->GetActorLocation()) <= Reach)
	{
		StopMovement();
		LastMoveGoal = FVector(FLT_MAX);
		U->TryMeleeAttack(T);
	}
	else
	{
		MoveToward(T->GetActorLocation(), Reach * 0.6f);
	}
}

void AShamanUnitAIController::FollowOrders(AShamanUnitBase* U)
{
	const UShamanGameData* Data = UShamanGameData::Get(this);
	const FVector Here = U->GetActorLocation();
	switch (U->GetOrder())
	{
	case EFollowerOrder::FollowShaman:
	{
		const UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>();
		const AShamanUnitBase* S = Tribes ? Tribes->GetShaman(U->GetTribeId()) : nullptr;
		if (!S || !S->IsAlive())
		{
			// Shaman is reincarnating: wait at the circle.
			const ABuildingActor* Circle = Tribes ? Tribes->GetReincarnationCircle(U->GetTribeId()) : nullptr;
			const FVector Wait = Circle ? Circle->GetActorLocation() : U->GetHomeLocation();
			if (FShamanSpace::HorizontalDistance(this, Here, Wait) > 600.f) MoveToward(FShamanSpace::OffsetAlongGround(this, Wait, FVector2D(FormationOffset) * 400.f), 120.f);
			return;
		}
		const float FollowDist = Data->FollowDistance;
		if (FShamanSpace::HorizontalDistance(this, Here, S->GetActorLocation()) > FollowDist)
			MoveToward(FShamanSpace::OffsetAlongGround(this, S->GetActorLocation(), FVector2D(FormationOffset) * FollowDist * 0.7f), 100.f);
		return;
	}
	case EFollowerOrder::HoldPosition:
	{
		const FVector Spot = FShamanSpace::OffsetAlongGround(this, U->GetOrderLocation(), FVector2D(FormationOffset) * 180.f);
		if (FShamanSpace::HorizontalDistance(this, Here, Spot) > 150.f) MoveToward(Spot, 80.f);
		return;
	}
	case EFollowerOrder::GuardHome:
	{
		const FVector Spot = U->GetHomeLocation();
		if (FShamanSpace::HorizontalDistance(this, Here, Spot) > 400.f) MoveToward(Spot, 120.f);
		return;
	}
	case EFollowerOrder::Wander:
	default:
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now < NextWanderTime) return;
		NextWanderTime = Now + 4.0 + RandFloat() * 5.0;
		const float A = RandFloat() * 2.f * PI;
		const float Dist = RandFloat() * 450.f;
		const FVector Spot = FShamanSpace::OffsetAlongGround(this, U->GetHomeLocation(), FVector2D(FMath::Cos(A), FMath::Sin(A)) * Dist);
		MoveToward(Spot, 60.f);
		return;
	}
	}
}

void AShamanUnitAIController::MoveToward(const FVector& Dest, float Acceptance)
{
	if (const UShamanTerrainSubsystem* Terrain = UShamanTerrainSubsystem::Get(this))
		if (Terrain->IsPlanetActive())
		{
			if (bHasSurfaceGoal && FVector::DistSquared(Dest, LastMoveGoal) < FMath::Square(120.f)) return;
			LastMoveGoal = Dest;
			const FShamanSurfacePath Path = FShamanSurfaceNavigation::Get().FindPath(*Terrain, GetPawn()->GetActorLocation(), Dest);
			bHasSurfaceGoal = Path.bValid && Path.Points.Num() > 0;
			if (bHasSurfaceGoal) SurfaceGoal = Path.Points.Last();
			SurfaceAcceptance = Acceptance;
			return;
		}

	const bool bMoving = GetMoveStatus() == EPathFollowingStatus::Moving;
	if (bMoving && FVector::DistSquared(Dest, LastMoveGoal) < FMath::Square(120.f)) return;
	LastMoveGoal = Dest;
	EPathFollowingRequestResult::Type R = MoveToLocation(Dest, Acceptance, /*bStopOnOverlap*/ true, /*bUsePathfinding*/ true,
		/*bProjectDestinationToNavigation*/ true, /*bCanStrafe*/ false, nullptr, /*bAllowPartialPath*/ true);
	if (R == EPathFollowingRequestResult::Failed) // no navmesh here (or none at all): walk straight
		MoveToLocation(Dest, Acceptance, true, false, false, false, nullptr, true);
}
