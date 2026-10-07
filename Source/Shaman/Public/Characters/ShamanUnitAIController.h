#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "ShamanUnitAIController.generated.h"

class AShamanUnitBase;

/**
 * Phase 1 unit brain: a small C++ state machine (no Behavior Tree assets needed).
 *   1. If hostile units are within AggroRadius and the leash allows it: engage (Shamans try spells first, then melee).
 *   2. Otherwise follow the standing order: FollowShaman / HoldPosition / GuardHome / Wander.
 * Uses the navmesh when present and falls back to straight-line moves when it is not.
 * On planets (no navmesh possible) it asks FShamanSurfaceNavigation for a path and steers along the surface
 * every frame; decisions still run 4x per second.
 * Phase 2 replaces step 2 with work orders (gather, build) without changing step 1.
 */
UCLASS()
class SHAMAN_API AShamanUnitAIController : public AAIController
{
	GENERATED_BODY()
public:
	AShamanUnitAIController();
	virtual void Tick(float DeltaSeconds) override;
	virtual void StopMovement() override;
	/** Planet steering goal (debug/tests). */
	bool HasSurfaceGoal() const { return bHasSurfaceGoal; }
	AActor* GetCurrentTarget() const { return Target.Get(); }

protected:
	virtual void OnPossess(APawn* InPawn) override;

	void UpdateTarget(AShamanUnitBase* U);
	void Engage(AShamanUnitBase* U, AActor* T);
	void FollowOrders(AShamanUnitBase* U);
	bool TryCastAt(AShamanUnitBase* U, AActor* T);
	void MoveToward(const FVector& Dest, float Acceptance);
	FVector GetLeashAnchor(AShamanUnitBase* U) const;
	void Think(AShamanUnitBase* U);
	void SteerOnSurface(AShamanUnitBase* U);

	TWeakObjectPtr<AActor> Target;
	FVector LastMoveGoal = FVector(FLT_MAX);
	FVector FormationOffset = FVector::ZeroVector;   // stable per unit, so followers spread out
	double NextWanderTime = 0.0;
	uint64 RngState = 1;
	// Planet movement (prototype navigation, see FShamanSurfaceNavigation)
	bool bHasSurfaceGoal = false;
	FVector SurfaceGoal = FVector::ZeroVector;
	float SurfaceAcceptance = 100.f;
	float DecisionTimer = 0.f;
	float RandFloat();
};
