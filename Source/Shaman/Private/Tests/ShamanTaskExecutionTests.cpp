#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameplayTagContainer.h"
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "Tribes/TribeTaskTypes.h"
#include "Tribes/TribeTaskExecution.h"
#include "Characters/ShamanUnitBase.h"
#include "Characters/ShamanCharacter.h"
#include "Characters/ShamanUnitAIController.h"
#include "Characters/HealthComponent.h"
#include "World/SurfaceProbeActor.h"
#include "ShamanTribeTestWorld.h"
#include <limits>

/**
 * Phase 2.2 task execution.
 *  - ExecutionRules: the pure decision rules (no world).
 *  - MoveToLocation / MoveToActor / MoveFailures / Guard / EndingAndEligibility: FUnitTaskExecutor on real tribes, task
 *    books and production unit rows, driven through a recording fake movement layer (IUnitTaskMover). Arrival is
 *    produced by teleporting the unit; time is passed explicitly. Real UE4 path following is NOT exercised here.
 *  - AutonomyYields: the real AShamanUnitAIController possessing a Brave (decision step only, no movement simulation).
 * Flat test world throughout.
 */
namespace ShamanTaskExecTestsPrivate
{
	using namespace ShamanTribeTestsPrivate;

	/** Records what the executor asked for; answers with Answer. */
	struct FFakeMover : public IUnitTaskMover
	{
		int32 Requests = 0;
		int32 Stops = 0;
		FVector LastGoal = FVector::ZeroVector;
		AActor* LastGoalActor = nullptr;
		ETaskMoveRequest Answer = ETaskMoveRequest::Accepted;
		bool bInProgress = true;
		virtual ETaskMoveRequest RequestTaskMove(const FVector& Goal, AActor* GoalActor, float) override { ++Requests; LastGoal = Goal; LastGoalActor = GoalActor; return Answer; }
		virtual void StopTaskMove() override { ++Stops; }
		virtual bool IsTaskMoveInProgress() const override { return bInProgress; }
	};

	/** Player Shaman (tribe 0) + enemy Shaman (tribe 1) + NumBraves player Braves; Exec drives Braves[0]. */
	struct FExecFixture
	{
		FTribeTestWorld W;
		AShamanCharacter* S0 = nullptr;
		AShamanCharacter* S1 = nullptr;
		TArray<AShamanUnitBase*> Braves;
		UTribeComponent* T0 = nullptr;
		UTribeComponent* T1 = nullptr;
		FTribeTaskExecutionConfig Cfg;
		double T = 0.0;
		FFakeMover Mover;
		FUnitTaskExecutor Exec; // declared after W: unbinds before the world goes away

		explicit FExecFixture(int32 NumBraves, bool bBindExecutor = true)
		{
			S0 = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 0, FVector(0.f, 0.f, 100.f));
			S1 = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 1, FVector(9000.f, 0.f, 100.f));
			for (int32 i = 0; i < NumBraves; ++i)
				Braves.Add(W.SpawnUnit<AShamanUnitBase>(W.Data->BraveUnitId, 0, FVector(300.f * (i + 1), 400.f, 100.f)));
			T0 = W.Tribes->GetTribeComponent(0);
			T1 = W.Tribes->GetTribeComponent(1);
			Cfg = W.Data->TaskExecution;
			T = W.World->GetTimeSeconds();
			if (bBindExecutor && Braves.Num() > 0) Exec.Bind(Braves[0]->TribeMember, &Mover);
		}
		static FGameplayTag Tag(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(Name, false); }
		int32 NewTask(UTribeComponent* Tribe, const TCHAR* TagName, AActor* Actor, const FVector& Where, bool bHasWhere)
		{
			return Tribe->CreateTask(Tag(TagName), ETribeTaskPriority::Normal, Actor, Where, bHasWhere, TEXT("Test"));
		}
		/** Create + assign + start a MoveTo for Braves[0]. */
		int32 StartMove(const FVector& Where, UTribeComponent* Tribe = nullptr, AShamanUnitBase* Unit = nullptr)
		{
			Tribe = Tribe ? Tribe : T0;
			Unit = Unit ? Unit : Braves[0];
			const int32 Id = NewTask(Tribe, TEXT("Task.MoveTo"), nullptr, Where, true);
			Tribe->AssignTask(Id, Unit->TribeMember);
			Tribe->StartTask(Id);
			return Id;
		}
		static ETribeTaskState StateOf(const UTribeComponent* Tribe, int32 Id)
		{
			const FTribeTask* Task = Tribe->FindTask(Id);
			return Task ? Task->State : ETribeTaskState::Unassigned;
		}
		static FName ReasonOf(const UTribeComponent* Tribe, int32 Id)
		{
			const FTribeTask* Task = Tribe->FindTask(Id);
			return Task ? Task->EndReason : NAME_None;
		}
		static void Teleport(AActor* A, const FVector& Where) { A->SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics); }
		FVector Ahead(const AActor* A, float Dist) const { return A->GetActorLocation() + FVector(Dist, 0.f, 0.f); }
	};

	bool IsReleased(const UTribeMemberComponent* M) { return M && !M->HasTask() && M->IsAvailableWorker(); }
}
using namespace ShamanTaskExecTestsPrivate;

#define SHAMAN_TASK_TEST(Class, Name) IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, Name, EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

SHAMAN_TASK_TEST(FShamanTaskExecRulesTest, "Shaman.Tasks.ExecutionRules")
bool FShamanTaskExecRulesTest::RunTest(const FString& Parameters)
{
	using R = FTribeTaskExecutionRules;
	const FGameplayTag MoveTo = FExecFixture::Tag(TEXT("Task.MoveTo"));
	const FGameplayTag MoveToActor = FExecFixture::Tag(TEXT("Task.MoveToActor"));
	const FGameplayTag Guard = FExecFixture::Tag(TEXT("Task.Guard"));
	if (!TestTrue(TEXT("Task.MoveTo / MoveToActor / Guard tags registered"), MoveTo.IsValid() && MoveToActor.IsValid() && Guard.IsValid())) return false;
	TestTrue(TEXT("Executable kinds"), R::KindOf(MoveTo) == ETaskExecKind::MoveToLocation && R::KindOf(MoveToActor) == ETaskExecKind::MoveToActor
		&& R::KindOf(Guard) == ETaskExecKind::Guard);
	TestTrue(TEXT("Task.Debug stays bookkeeping-only"), R::KindOf(FExecFixture::Tag(TEXT("Task.Debug"))) == ETaskExecKind::None);
	TestTrue(TEXT("Reserved tags are not executed"), R::KindOf(FExecFixture::Tag(TEXT("Task.Gather.Wood"))) == ETaskExecKind::None
		&& R::KindOf(FExecFixture::Tag(TEXT("Task.Build"))) == ETaskExecKind::None && R::KindOf(FGameplayTag()) == ETaskExecKind::None);

	FTribeTaskExecutionConfig C;
	FTribeTask Task;
	Task.Type = MoveTo;
	TestFalse(TEXT("MoveTo without a location is invalid"), R::HasValidTarget(Task, ETaskExecKind::MoveToLocation));
	Task.bHasTargetLocation = true;
	Task.TargetLocation = FVector(1.f, 2.f, 3.f);
	TestTrue(TEXT("MoveTo with a location is valid"), R::HasValidTarget(Task, ETaskExecKind::MoveToLocation));
	Task.TargetLocation = FVector(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);
	TestFalse(TEXT("NaN location is invalid"), R::HasValidTarget(Task, ETaskExecKind::MoveToLocation));
	TestFalse(TEXT("MoveToActor without an actor is invalid"), R::HasValidTarget(Task, ETaskExecKind::MoveToActor));
	TestEqual(TEXT("Default arrival radius from config"), R::AcceptanceFor(Task, ETaskExecKind::MoveToLocation, C), C.ArrivalRadius);
	TestEqual(TEXT("Guard radius from config"), R::AcceptanceFor(Task, ETaskExecKind::Guard, C), C.GuardRadius);
	Task.AcceptanceRadius = 42.f;
	TestEqual(TEXT("Per-task override"), R::AcceptanceFor(Task, ETaskExecKind::MoveToActor, C), 42.f);

	// Move: moving, then complete on arrival.
	FTaskProgress P; P.Reset(0.0);
	TestTrue(TEXT("Far: keep moving"), R::Step(ETaskExecKind::MoveToLocation, P, 1000.f, 150.f, true, 0.0, C).Action == ETaskStepAction::Move);
	const FTaskStepResult Arr = R::Step(ETaskExecKind::MoveToLocation, P, 100.f, 150.f, true, 1.0, C);
	TestTrue(TEXT("Within radius: complete (Arrived)"), Arr.Action == ETaskStepAction::Complete && Arr.Result == ETaskMoveResult::Arrived);

	// Watchdog: progress resets it, no progress trips it.
	P.Reset(0.0);
	R::Step(ETaskExecKind::MoveToLocation, P, 1000.f, 150.f, true, 0.0, C);
	TestTrue(TEXT("Progress keeps it alive"), R::Step(ETaskExecKind::MoveToLocation, P, 900.f, 150.f, true, C.StuckTimeout - 1.0, C).Action == ETaskStepAction::Move);
	TestTrue(TEXT("Still inside the window"), R::Step(ETaskExecKind::MoveToLocation, P, 895.f, 150.f, true, 2.0 * C.StuckTimeout - 2.0, C).Action == ETaskStepAction::Move);
	const FTaskStepResult Stuck = R::Step(ETaskExecKind::MoveToLocation, P, 890.f, 150.f, true, 2.0 * C.StuckTimeout, C);
	TestTrue(TEXT("No progress for StuckTimeout: Failed(Stuck)"), Stuck.Action == ETaskStepAction::Fail && Stuck.Result == ETaskMoveResult::Stuck && Stuck.Reason == FName(TEXT("Stuck")));

	FTribeTaskExecutionConfig NoStuck = C;
	NoStuck.StuckTimeout = 0.f;
	NoStuck.MaxMoveDuration = 10.f;
	P.Reset(0.0);
	const FTaskStepResult Late = R::Step(ETaskExecKind::MoveToLocation, P, 1000.f, 150.f, true, 11.0, NoStuck);
	TestTrue(TEXT("Move over MaxMoveDuration: Failed(TimedOut)"), Late.Action == ETaskStepAction::Fail && Late.Result == ETaskMoveResult::TimedOut);

	// Guard: approach, hold (never stuck / timed out while holding), tolerate drift inside the leash, return beyond it.
	P.Reset(0.0);
	TestTrue(TEXT("Guard far: move"), R::Step(ETaskExecKind::Guard, P, 1000.f, C.GuardRadius, true, 0.0, NoStuck).Action == ETaskStepAction::Move);
	const FTaskStepResult Hold = R::Step(ETaskExecKind::Guard, P, 100.f, C.GuardRadius, true, 1.0, NoStuck);
	TestTrue(TEXT("Guard at post: hold, on station"), Hold.Action == ETaskStepAction::Hold && Hold.Result == ETaskMoveResult::OnStation && P.bOnStation);
	TestTrue(TEXT("Guard holds indefinitely (no timeout)"), R::Step(ETaskExecKind::Guard, P, 100.f, C.GuardRadius, true, 10000.0, C).Action == ETaskStepAction::Hold);
	TestTrue(TEXT("Small drift inside the leash: hold"), R::Step(ETaskExecKind::Guard, P, C.GuardLeashRadius - 10.f, C.GuardRadius, true, 10001.0, C).Action == ETaskStepAction::Hold);
	const FTaskStepResult Back = R::Step(ETaskExecKind::Guard, P, C.GuardLeashRadius + 500.f, C.GuardRadius, true, 10002.0, C);
	TestTrue(TEXT("Pushed beyond the leash: walk back"), Back.Action == ETaskStepAction::Move && !P.bOnStation);

	const FTaskStepResult Lost = R::Step(ETaskExecKind::MoveToActor, P, 10.f, 200.f, false, 0.0, C);
	TestTrue(TEXT("Target gone: Failed(TargetLost)"), Lost.Action == ETaskStepAction::Fail && Lost.Result == ETaskMoveResult::TargetLost);
	return true;
}

SHAMAN_TASK_TEST(FShamanTaskMoveToLocationTest, "Shaman.Tasks.MoveToLocation")
bool FShamanTaskMoveToLocationTest::RunTest(const FString& Parameters)
{
	using S = ETribeTaskState;
	FExecFixture F(3);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	AShamanUnitBase* B = F.Braves[0];
	UTribeMemberComponent* M = B->TribeMember;
	const FVector Goal = F.Ahead(B, 2000.f);
	int32 Completions = 0;
	const FDelegateHandle H = M->OnTaskStateChanged.AddLambda([&Completions](UTribeMemberComponent*, int32, ETribeTaskState St)
	{
		if (St == ETribeTaskState::Completed) ++Completions;
	});

	const int32 Id = F.NewTask(F.T0, TEXT("Task.MoveTo"), nullptr, Goal, true);
	TestTrue(TEXT("Eligible Brave is assigned"), F.T0->AssignTask(Id, M));
	TestTrue(TEXT("Assignment makes the unit aware (pending), nothing moves yet"),
		F.Exec.GetPendingTaskId() == Id && !F.Exec.HasControl() && F.Mover.Requests == 0);
	TestEqual(TEXT("Assigned worker left the pool"), F.T0->GetAvailableWorkerCount(), 2);

	TestTrue(TEXT("Start"), F.T0->StartTask(Id));
	TestTrue(TEXT("Start triggers execution: executor in control, one move request to the goal"),
		F.Exec.HasControl() && F.Exec.GetTaskId() == Id && F.Mover.Requests == 1 && F.Mover.LastGoal.Equals(Goal));
	TestTrue(TEXT("Unit shows Moving"), M->GetSimState() == EUnitSimState::Moving && F.StateOf(F.T0, Id) == S::Active);
	F.Exec.Update(F.T + 1.0);
	TestEqual(TEXT("No repeated move requests while under way"), F.Mover.Requests, 1);

	// Wrong unit: a task started for another Brave is not executed by this executor.
	const int32 Other = F.NewTask(F.T0, TEXT("Task.MoveTo"), nullptr, Goal, true);
	F.T0->AssignTask(Other, F.Braves[1]->TribeMember);
	F.T0->StartTask(Other);
	TestTrue(TEXT("Another unit's task does not take over this executor"), F.Exec.GetTaskId() == Id && F.Mover.Requests == 1);
	F.T0->CancelTask(Other, TEXT("Test"));

	FExecFixture::Teleport(B, Goal + FVector(50.f, 0.f, 0.f));
	F.Exec.Update(F.T + 2.0);
	TestTrue(TEXT("Arrival completes the task"), F.StateOf(F.T0, Id) == S::Completed && F.Exec.GetLastResult() == ETaskMoveResult::Arrived);
	TestTrue(TEXT("Completion releases the executor and stops task movement"), !F.Exec.HasControl() && F.Mover.Stops >= 1);
	TestTrue(TEXT("Completion releases the worker"), IsReleased(M) && F.T0->GetAvailableWorkerCount() == 3);
	F.Exec.Update(F.T + 3.0);
	TestFalse(TEXT("Completed is final"), F.T0->CompleteTask(Id));
	TestEqual(TEXT("Completed exactly once"), Completions, 1);
	M->OnTaskStateChanged.Remove(H);
	return true;
}

SHAMAN_TASK_TEST(FShamanTaskMoveToActorTest, "Shaman.Tasks.MoveToActor")
bool FShamanTaskMoveToActorTest::RunTest(const FString& Parameters)
{
	using S = ETribeTaskState;
	FExecFixture F(2);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	AShamanUnitBase* B = F.Braves[0];
	UTribeMemberComponent* M = B->TribeMember;
	ASurfaceProbeActor* Marker = F.W.World->SpawnActor<ASurfaceProbeActor>(ASurfaceProbeActor::StaticClass(), FTransform(F.Ahead(B, 3000.f)));
	if (!TestNotNull(TEXT("Target actor"), Marker)) return false;

	const int32 Id = F.NewTask(F.T0, TEXT("Task.MoveToActor"), Marker, FVector::ZeroVector, false);
	F.T0->AssignTask(Id, M);
	F.T0->StartTask(Id);
	TestTrue(TEXT("Moves toward the target actor (the actor itself is handed to the movement layer)"), F.Exec.HasControl() && F.Mover.Requests == 1
		&& F.Mover.LastGoal.Equals(Marker->GetActorLocation()) && F.Mover.LastGoalActor == Marker);
	FExecFixture::Teleport(Marker, Marker->GetActorLocation() + FVector(0.f, 1000.f, 0.f));
	F.Exec.Update(F.T + 1.0);
	TestTrue(TEXT("Target moved far: move re-issued to its new position"), F.Mover.Requests == 2 && F.Mover.LastGoal.Equals(Marker->GetActorLocation()));
	FExecFixture::Teleport(Marker, Marker->GetActorLocation() + FVector(0.f, 50.f, 0.f));
	F.Exec.Update(F.T + 1.5);
	TestEqual(TEXT("Small target drift: no new request"), F.Mover.Requests, 2);
	FExecFixture::Teleport(B, Marker->GetActorLocation() + FVector(100.f, 0.f, 0.f));
	F.Exec.Update(F.T + 2.0);
	TestTrue(TEXT("Within reach of the target: Completed, worker released"), F.StateOf(F.T0, Id) == S::Completed && IsReleased(M));

	// Target destroyed while approaching.
	FExecFixture::Teleport(B, Marker->GetActorLocation() + FVector(-2000.f, 0.f, 0.f));
	const int32 Id2 = F.NewTask(F.T0, TEXT("Task.MoveToActor"), Marker, FVector::ZeroVector, false);
	F.T0->AssignTask(Id2, M);
	F.T0->StartTask(Id2);
	TestTrue(TEXT("Second approach under way"), F.Exec.HasControl() && F.StateOf(F.T0, Id2) == S::Active);
	const int32 StopsBefore = F.Mover.Stops;
	Marker->Destroy();
	F.Exec.Update(F.T + 3.0);
	TestTrue(TEXT("Destroyed target: Failed(TargetLost)"), F.StateOf(F.T0, Id2) == S::Failed && F.ReasonOf(F.T0, Id2) == FName(TEXT("TargetLost"))
		&& F.Exec.GetLastResult() == ETaskMoveResult::TargetLost);
	TestTrue(TEXT("...movement stopped, worker released"), F.Mover.Stops > StopsBefore && !F.Exec.HasControl() && IsReleased(M));

	// No target at all.
	const int32 Id3 = F.NewTask(F.T0, TEXT("Task.MoveToActor"), nullptr, FVector::ZeroVector, false);
	F.T0->AssignTask(Id3, M);
	F.T0->StartTask(Id3);
	TestTrue(TEXT("Missing target: Failed(InvalidTarget) on start, worker released"),
		F.StateOf(F.T0, Id3) == S::Failed && F.ReasonOf(F.T0, Id3) == FName(TEXT("InvalidTarget")) && IsReleased(M) && !F.Exec.HasControl());
	return true;
}

SHAMAN_TASK_TEST(FShamanTaskMoveFailuresTest, "Shaman.Tasks.MoveFailures")
bool FShamanTaskMoveFailuresTest::RunTest(const FString& Parameters)
{
	using S = ETribeTaskState;
	FExecFixture F(2);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	AShamanUnitBase* B = F.Braves[0];
	UTribeMemberComponent* M = B->TribeMember;
	const FVector Far = F.Ahead(B, 5000.f);

	F.Mover.Answer = ETaskMoveRequest::Failed;
	const int32 A = F.StartMove(Far);
	TestTrue(TEXT("Movement layer cannot move: Failed(NoPath)"), F.StateOf(F.T0, A) == S::Failed && F.ReasonOf(F.T0, A) == FName(TEXT("NoPath"))
		&& F.Exec.GetLastResult() == ETaskMoveResult::RequestFailed);
	TestTrue(TEXT("...worker released"), IsReleased(M) && !F.Exec.HasControl());

	F.Mover.Answer = ETaskMoveRequest::Unreachable;
	const int32 U = F.StartMove(Far);
	TestTrue(TEXT("Unreachable goal: Failed(Unreachable), worker released"), F.StateOf(F.T0, U) == S::Failed
		&& F.ReasonOf(F.T0, U) == FName(TEXT("Unreachable")) && IsReleased(M));

	F.Mover.Answer = ETaskMoveRequest::Accepted;
	const int32 St = F.StartMove(Far);
	F.Exec.Update(F.T + 1.0);
	TestTrue(TEXT("Accepted move, no progress yet: still active"), F.StateOf(F.T0, St) == S::Active);
	F.Exec.Update(F.T + F.Cfg.StuckTimeout + 1.0);
	TestTrue(TEXT("Never gets closer: Failed(Stuck), not busy forever"), F.StateOf(F.T0, St) == S::Failed && F.ReasonOf(F.T0, St) == FName(TEXT("Stuck"))
		&& IsReleased(M) && F.T0->GetAvailableWorkerCount() == 2);

	const int32 Inv = F.NewTask(F.T0, TEXT("Task.MoveTo"), nullptr, FVector::ZeroVector, false);
	F.T0->AssignTask(Inv, M);
	F.T0->StartTask(Inv);
	TestTrue(TEXT("No destination: Failed(InvalidTarget) on start"), F.StateOf(F.T0, Inv) == S::Failed && F.ReasonOf(F.T0, Inv) == FName(TEXT("InvalidTarget")) && IsReleased(M));

	const int32 Off = F.StartMove(Far);
	M->SetUnavailable(true);
	F.Exec.Update(F.T + 2.0);
	TestTrue(TEXT("Unit becomes unavailable mid-task: Failed(UnitUnavailable)"), F.StateOf(F.T0, Off) == S::Failed
		&& F.ReasonOf(F.T0, Off) == FName(TEXT("UnitUnavailable")) && !M->HasTask() && !F.Exec.HasControl());
	M->SetUnavailable(false);
	TestTrue(TEXT("...and is a worker again once available"), IsReleased(M));
	return true;
}

SHAMAN_TASK_TEST(FShamanTaskGuardTest, "Shaman.Tasks.Guard")
bool FShamanTaskGuardTest::RunTest(const FString& Parameters)
{
	using S = ETribeTaskState;
	FExecFixture F(2);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	AShamanUnitBase* B = F.Braves[0];
	UTribeMemberComponent* M = B->TribeMember;
	const FVector Post = F.Ahead(B, 1500.f);

	const int32 Id = F.NewTask(F.T0, TEXT("Task.Guard"), nullptr, Post, true);
	F.T0->AssignTask(Id, M);
	F.T0->StartTask(Id);
	TestTrue(TEXT("Guard: moves to the post"), F.Exec.HasControl() && F.Mover.Requests == 1 && F.Mover.LastGoal.Equals(Post) && M->GetSimState() == EUnitSimState::Moving);

	FExecFixture::Teleport(B, Post + FVector(50.f, 0.f, 0.f));
	F.Exec.Update(F.T + 1.0);
	TestTrue(TEXT("At the post: on station, still Active, movement stopped"), F.Exec.IsOnStation() && F.StateOf(F.T0, Id) == S::Active
		&& F.Mover.Stops >= 1 && M->GetSimState() == EUnitSimState::Guarding);
	F.Exec.Update(F.T + 1000.0);
	TestTrue(TEXT("Stays there indefinitely (no stuck / timeout while guarding)"), F.StateOf(F.T0, Id) == S::Active && F.Mover.Requests == 1);
	TestFalse(TEXT("Guarding Brave is not an available worker"), M->IsAvailableWorker());

	FExecFixture::Teleport(B, Post + FVector(F.Cfg.GuardLeashRadius - 50.f, 0.f, 0.f));
	F.Exec.Update(F.T + 1001.0);
	TestTrue(TEXT("Nudged inside the leash: holds"), F.Exec.IsOnStation() && F.Mover.Requests == 1);
	FExecFixture::Teleport(B, Post + FVector(F.Cfg.GuardLeashRadius + 800.f, 0.f, 0.f));
	F.Exec.Update(F.T + 1002.0);
	TestTrue(TEXT("Pushed off the post: walks back"), !F.Exec.IsOnStation() && F.Mover.Requests == 2 && F.StateOf(F.T0, Id) == S::Active);

	const int32 StopsBefore = F.Mover.Stops;
	TestTrue(TEXT("Cancel"), F.T0->CancelTask(Id, TEXT("Test")));
	TestTrue(TEXT("Cancellation stops guarding and task movement"), !F.Exec.HasControl() && F.Mover.Stops > StopsBefore
		&& F.Exec.GetLastResult() == ETaskMoveResult::Interrupted);
	TestTrue(TEXT("Cancellation releases the worker"), IsReleased(M) && F.T0->GetAvailableWorkerCount() == 2);
	F.Exec.Update(F.T + 1003.0);
	TestEqual(TEXT("No movement after cancellation"), F.Mover.Requests, 2);
	return true;
}

SHAMAN_TASK_TEST(FShamanTaskEndingTest, "Shaman.Tasks.EndingAndEligibility")
bool FShamanTaskEndingTest::RunTest(const FString& Parameters)
{
	using S = ETribeTaskState;
	FExecFixture F(3);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0) || !TestNotNull(TEXT("Tribe 1"), F.T1)) return false;
	UTribeMemberComponent* M0 = F.Braves[0]->TribeMember;
	const FVector Far = F.Ahead(F.Braves[0], 4000.f);

	// Ended from the task book while executing.
	const int32 Fa = F.StartMove(Far);
	int32 Stops = F.Mover.Stops;
	F.T0->FailTask(Fa, TEXT("Console"));
	TestTrue(TEXT("Failure (external) stops execution and releases the worker"), !F.Exec.HasControl() && F.Mover.Stops > Stops
		&& F.Exec.GetLastReason() == FName(TEXT("Console")) && IsReleased(M0));
	const int32 Co = F.StartMove(Far);
	Stops = F.Mover.Stops;
	F.T0->CompleteTask(Co);
	TestTrue(TEXT("Completion (external) stops execution and releases the worker"), !F.Exec.HasControl() && F.Mover.Stops > Stops && IsReleased(M0));
	const int32 Ca = F.StartMove(Far);
	F.T0->CancelTask(Ca, TEXT("Console"));
	TestTrue(TEXT("Cancellation releases the worker"), !F.Exec.HasControl() && IsReleased(M0) && F.T0->GetAvailableWorkerCount() == 3);
	TestEqual(TEXT("No open tasks left"), F.T0->GetOpenTaskCount(), 0);

	// Death while executing.
	const int32 D = F.StartMove(Far);
	Stops = F.Mover.Stops;
	F.Braves[0]->Health->Kill(nullptr);
	TestTrue(TEXT("Death fails the task and stops execution"), F.StateOf(F.T0, D) == S::Failed && !F.Exec.HasControl() && F.Mover.Stops > Stops && !M0->HasTask());
	TestTrue(TEXT("Death updates followers and the worker pool"), F.T0->GetFollowerCount() == 2 && F.T0->GetAvailableWorkerCount() == 2);
	F.Exec.Update(F.T + 5.0);
	TestFalse(TEXT("Dead unit is not driven again"), F.Exec.HasControl());

	// Tribe transfer while executing (a second executor drives Braves[1]).
	FFakeMover Mover1;
	FUnitTaskExecutor Exec1;
	AShamanUnitBase* B1 = F.Braves[1];
	UTribeMemberComponent* M1 = B1->TribeMember;
	Exec1.Bind(M1, &Mover1);
	const int32 Tr = F.StartMove(Far, F.T0, B1);
	TestTrue(TEXT("Second Brave executing"), Exec1.HasControl() && F.T0->GetAvailableWorkerCount() == 1);
	const int32 T1Followers = F.T1->GetFollowerCount(), T1Workers = F.T1->GetAvailableWorkerCount();
	B1->ChangeTribe(1);
	TestTrue(TEXT("Transfer fails the old tribe's task and stops execution"), F.StateOf(F.T0, Tr) == S::Failed && !Exec1.HasControl() && Mover1.Stops >= 1);
	TestTrue(TEXT("Old tribe: one follower and one worker left"), F.T0->GetFollowerCount() == 1 && F.T0->GetAvailableWorkerCount() == 1);
	TestTrue(TEXT("New tribe gains the unit as a free worker"), F.T1->GetFollowerCount() == T1Followers + 1 && F.T1->GetAvailableWorkerCount() == T1Workers + 1);
	const int32 New = F.StartMove(F.Ahead(B1, 3000.f), F.T1, B1);
	TestTrue(TEXT("The new tribe's tasks are executed"), Exec1.HasControl() && Exec1.GetTaskId() == New && Mover1.Requests == 2);
	F.T1->CancelTask(New, TEXT("Test"));
	Exec1.Unbind(NAME_None);

	// The Shaman is never a worker.
	const int32 Sh = F.NewTask(F.T0, TEXT("Task.MoveTo"), nullptr, Far, true);
	TestFalse(TEXT("The Shaman cannot be assigned a task"), F.T0->AssignTask(Sh, F.S0->TribeMember));
	TestFalse(TEXT("The Shaman is not in the worker pool"), F.T0->GetAvailableWorkers().Contains(F.S0->TribeMember));
	return true;
}

SHAMAN_TASK_TEST(FShamanTaskAutonomyTest, "Shaman.Tasks.AutonomyYields")
bool FShamanTaskAutonomyTest::RunTest(const FString& Parameters)
{
	using S = ETribeTaskState;
	FExecFixture F(1, /*bBindExecutor*/ false);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	AShamanUnitBase* B = F.Braves[0];
	UTribeMemberComponent* M = B->TribeMember;
	FExecFixture::Teleport(F.S0, FVector(-3000.f, 0.f, 100.f)); // Shaman far away: a follower wants to walk to it
	AShamanUnitAIController* Ctrl = F.W.World->SpawnActor<AShamanUnitAIController>();
	if (!TestNotNull(TEXT("AI controller"), Ctrl)) return false;
	Ctrl->Possess(B);
	if (!TestTrue(TEXT("Controller possesses the Brave"), Ctrl->GetPawn() == B)) return false;
	TestTrue(TEXT("Brave follows its Shaman by default"), B->GetOrder() == EFollowerOrder::FollowShaman);

	Ctrl->Tick(0.3f);
	TestTrue(TEXT("Without a task, follow behaviour issues a move"), Ctrl->GetLastMoveGoal() != FVector(FLT_MAX));

	// Guard task at the Brave's own position: on station at once, no move needed.
	const int32 Id = F.NewTask(F.T0, TEXT("Task.Guard"), nullptr, B->GetActorLocation(), true);
	F.T0->AssignTask(Id, M);
	F.T0->StartTask(Id);
	TestTrue(TEXT("The controller's executor runs the task"), Ctrl->GetTaskExecutor().HasControl() && Ctrl->GetTaskExecutor().IsOnStation());
	TestTrue(TEXT("Starting the task stopped the follow move already under way"), Ctrl->GetLastMoveGoal() == FVector(FLT_MAX));
	Ctrl->Tick(0.3f);
	Ctrl->Tick(0.3f);
	TestTrue(TEXT("Active task: follow behaviour does not override it (no autonomous move)"), Ctrl->GetLastMoveGoal() == FVector(FLT_MAX)
		&& F.StateOf(F.T0, Id) == S::Active && M->GetSimState() == EUnitSimState::Guarding);

	F.T0->CancelTask(Id, TEXT("Test"));
	TestFalse(TEXT("Cancelled: executor released"), Ctrl->GetTaskExecutor().HasControl());
	Ctrl->Tick(0.3f);
	TestTrue(TEXT("Autonomous follow resumes once the task ends"), Ctrl->GetLastMoveGoal() != FVector(FLT_MAX) && M->GetSimState() == EUnitSimState::Following);
	TestTrue(TEXT("Worker released"), IsReleased(M));

	Ctrl->UnPossess();
	Ctrl->Destroy();
	return true;
}

#undef SHAMAN_TASK_TEST

#endif
