#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "Tribes/TribeTaskExecution.h"
#include "ShamanUnitAIController.generated.h"

class AShamanUnitBase;

/**
 * Phase 1 unit brain: a small C++ state machine (no Behavior Tree assets needed).
 *   1. If hostile units are within AggroRadius and the leash allows it: engage (Shamans try spells first, then melee).
 *   2. Otherwise follow the standing order: FollowShaman / HoldPosition / GuardHome / Wander.
 * Uses the navmesh when present and falls back to straight-line moves when it is not.
 * On planets (no navmesh possible) it asks FShamanSurfaceNavigation for a path and steers along the surface
 * every frame; decisions still run 4x per second.
 *
 * Phase 2.2: an Active executable task (Task.MoveTo / MoveToActor / Guard) on the pawn's tribe member comes first.
 * While FUnitTaskExecutor has control, steps 1 and 2 are skipped, so autonomous combat / follow / wander never
 * overwrite task movement (a guard on station still melees enemies in reach without moving). The controller is the
 * executor's movement layer (IUnitTaskMover, via TaskMover): tasks move through the same navmesh / planet-surface
 * movement as everything else.
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
	/** Last goal handed to the movement layer (FVector(FLT_MAX) = none). Debug/tests. */
	FVector GetLastMoveGoal() const { return LastMoveGoal; }
	const FUnitTaskExecutor& GetTaskExecutor() const { return TaskExecutor; }

	/** Movement layer for task execution (see IUnitTaskMover). Flat worlds: navmesh path (to GoalActor itself when given,
	 *  partial paths allowed so big actors are reached at their edge), or a straight-line move when there is no usable
	 *  navmesh (the Phase 1 convention); a usable navmesh with no path -> Unreachable.
	 *  Planets: FShamanSurfaceNavigation; a partial path that ends short of the goal -> Unreachable. */
	ETaskMoveRequest RequestTaskMove(const FVector& Goal, AActor* GoalActor, float Acceptance);
	void StopTaskMove();
	bool IsTaskMoveInProgress() const;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

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

	/** Executes the pawn's Active task (Phase 2.2). */
	FUnitTaskExecutor TaskExecutor;
	/** Adapter handing this controller's movement to TaskExecutor. */
	struct FControllerTaskMover : public IUnitTaskMover
	{
		AShamanUnitAIController* Owner = nullptr;
		virtual ETaskMoveRequest RequestTaskMove(const FVector& Goal, AActor* GoalActor, float Acceptance) override { return Owner->RequestTaskMove(Goal, GoalActor, Acceptance); }
		virtual void StopTaskMove() override { Owner->StopTaskMove(); }
		virtual bool IsTaskMoveInProgress() const override { return Owner->IsTaskMoveInProgress(); }
	} TaskMover;
};
