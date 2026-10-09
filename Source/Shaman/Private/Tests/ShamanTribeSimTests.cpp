#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameplayTagContainer.h"
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "Tribes/TribeTaskTypes.h"
#include "Characters/ShamanUnitBase.h"
#include "Characters/ShamanCharacter.h"
#include "Characters/HealthComponent.h"
#include "ShamanTribeTestWorld.h"

/**
 * Phase 2.1 tribe simulation foundation: membership lifecycle, task lifecycle, one-primary-task assignment and the
 * worker pool, on real production unit/building rows. Flat test world, no movement; asserts authoritative state.
 */
namespace ShamanTribeSimTestsPrivate
{
	using namespace ShamanTribeTestsPrivate;

	/** Player Shaman (tribe 0, owns the roster) + enemy Shaman (tribe 1) + NumBraves player Braves. */
	struct FSimFixture
	{
		FTribeTestWorld W;
		AShamanCharacter* S0 = nullptr;
		AShamanCharacter* S1 = nullptr;
		TArray<AShamanUnitBase*> Braves;
		UTribeComponent* T0 = nullptr;
		UTribeComponent* T1 = nullptr;

		explicit FSimFixture(int32 NumBraves)
		{
			S0 = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 0, FVector(0.f, 0.f, 100.f));
			S1 = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 1, FVector(9000.f, 0.f, 100.f));
			for (int32 i = 0; i < NumBraves; ++i)
				Braves.Add(W.SpawnUnit<AShamanUnitBase>(W.Data->BraveUnitId, 0, FVector(300.f * (i + 1), 400.f, 100.f)));
			T0 = W.Tribes->GetTribeComponent(0);
			T1 = W.Tribes->GetTribeComponent(1);
		}
		static FGameplayTag DebugTag() { return FGameplayTag::RequestGameplayTag(TEXT("Task.Debug"), false); }
		int32 NewTask(UTribeComponent* T, ETribeTaskPriority P = ETribeTaskPriority::Normal)
		{
			return T->CreateTask(DebugTag(), P, nullptr, FVector(100.f, 200.f, 300.f), true, TEXT("Test"));
		}
		ETribeTaskState StateOf(const UTribeComponent* T, int32 Id) const
		{
			const FTribeTask* Task = T->FindTask(Id);
			return Task ? Task->State : ETribeTaskState::Unassigned;
		}
	};
}
using namespace ShamanTribeSimTestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanTribeMemberLifecycleTest, "Shaman.Tribes.MemberLifecycle",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanTribeMemberLifecycleTest::RunTest(const FString& Parameters)
{
	FSimFixture F(3);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0) || !TestNotNull(TEXT("Tribe 1"), F.T1)) return false;
	UTribeMemberComponent* M0 = F.Braves[0]->TribeMember;
	UTribeMemberComponent* M1 = F.Braves[1]->TribeMember;
	UTribeMemberComponent* M2 = F.Braves[2]->TribeMember;

	TestEqual(TEXT("3 Braves -> tribe count 3"), F.T0->GetFollowerCount(), 3);
	TestFalse(TEXT("The Shaman is not a follower of its own roster"), F.T0->IsFollower(F.S0->TribeMember));
	TestEqual(TEXT("Living members = 3 followers + Shaman"), F.W.Tribes->GetLivingMemberCount(0), 4);

	TestTrue(TEXT("Re-registering is idempotent"), F.T0->RegisterFollower(M0));
	TestEqual(TEXT("No double registration"), F.T0->GetFollowerCount(), 3);

	TestTrue(TEXT("Remove member"), F.T0->UnregisterFollower(M0));
	TestEqual(TEXT("Remove -> count decreases"), F.T0->GetFollowerCount(), 2);
	TestFalse(TEXT("Second removal reports nothing removed"), F.T0->UnregisterFollower(M0));
	TestEqual(TEXT("No double decrement"), F.T0->GetFollowerCount(), 2);
	TestTrue(TEXT("Add member back"), F.T0->RegisterFollower(M0));
	TestEqual(TEXT("Add -> count increases"), F.T0->GetFollowerCount(), 3);

	F.Braves[1]->Health->Kill(nullptr);
	TestEqual(TEXT("Death -> count decreases"), F.T0->GetFollowerCount(), 2);
	TestFalse(TEXT("A dead unit cannot rejoin"), F.T0->RegisterFollower(M1));
	TestEqual(TEXT("Dead unit not counted"), F.T0->GetFollowerCount(), 2);
	TestEqual(TEXT("Living members after death"), F.W.Tribes->GetLivingMemberCount(0), 3);

	F.Braves[2]->ChangeTribe(1);
	TestEqual(TEXT("Change of tribe: old tribe loses the member"), F.T0->GetFollowerCount(), 1);
	TestEqual(TEXT("Change of tribe: new tribe gains the member"), F.T1->GetFollowerCount(), 1);
	TestTrue(TEXT("Member now in tribe 1's roster"), F.T1->IsFollower(M2) && !F.T0->IsFollower(M2));

	AShamanUnitBase* Wild = F.W.SpawnUnit<AShamanUnitBase>(F.W.Data->WildmanUnitId, -1, FVector(-500.f, 0.f, 100.f));
	TestEqual(TEXT("A Wildman is nobody's follower"), F.T0->GetFollowerCount(), 1);
	TestNotNull(TEXT("Conversion succeeds"), Wild->TribeMember->ConvertToTribe(F.T0, EConversionKind::Recruit));
	TestEqual(TEXT("Conversion: tribe gains the member"), F.T0->GetFollowerCount(), 2);

	F.Braves[0]->Destroy(); // stale reference: the roster must not keep counting it
	TestEqual(TEXT("Destroyed member removed"), F.T0->GetFollowerCount(), 1);
	TestEqual(TEXT("Roster holds no stale entries"), F.T0->GetFollowers().Num(), F.T0->GetFollowerCount());
	TestFalse(TEXT("Null member is ignored (register)"), F.T0->RegisterFollower(nullptr));
	TestFalse(TEXT("Null member is ignored (unregister)"), F.T0->UnregisterFollower(nullptr));
	TestEqual(TEXT("Count unchanged by invalid input"), F.T0->GetFollowerCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanTribeTaskLifecycleTest, "Shaman.Tribes.TaskLifecycle",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanTribeTaskLifecycleTest::RunTest(const FString& Parameters)
{
	using S = ETribeTaskState;
	// The pure state machine: every allowed transition, nothing else.
	const S All[] = { S::Unassigned, S::Assigned, S::Active, S::Completed, S::Failed, S::Cancelled };
	auto Allowed = [](S From, S To)
	{
		return (From == S::Unassigned && (To == S::Assigned || To == S::Cancelled))
			|| (From == S::Assigned && (To == S::Active || To == S::Failed || To == S::Cancelled))
			|| (From == S::Active && (To == S::Completed || To == S::Failed || To == S::Cancelled));
	};
	for (S From : All) for (S To : All)
		TestTrue(FString::Printf(TEXT("Transition %s -> %s %s"), FTribeTaskRules::StateName(From), FTribeTaskRules::StateName(To),
			Allowed(From, To) ? TEXT("allowed") : TEXT("refused")), FTribeTaskRules::CanTransition(From, To) == Allowed(From, To));

	FSimFixture F(1);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	TestTrue(TEXT("Task.Debug tag registered"), FSimFixture::DebugTag().IsValid());
	UTribeMemberComponent* M = F.Braves[0]->TribeMember;

	// Create -> Assigned -> Active -> Completed, with every illegal shortcut refused.
	const int32 Id = F.NewTask(F.T0, ETribeTaskPriority::High);
	FTribeTask T;
	TestTrue(TEXT("Created task is queryable"), F.T0->GetTask(Id, T));
	TestTrue(TEXT("Created: Unassigned"), T.State == S::Unassigned);
	TestTrue(TEXT("Records type, priority, tribe, target, source"), T.Type == FSimFixture::DebugTag() && T.Priority == ETribeTaskPriority::High
		&& T.TribeId == 0 && T.bHasTargetLocation && T.TargetLocation.Equals(FVector(100.f, 200.f, 300.f)) && T.Source == FName(TEXT("Test")) && T.CreatedTime >= 0.f);
	TestFalse(TEXT("Cannot start an unassigned task"), F.T0->StartTask(Id));
	TestFalse(TEXT("Cannot complete an unassigned task"), F.T0->CompleteTask(Id));
	TestTrue(TEXT("Assign"), F.T0->AssignTask(Id, M));
	F.T0->GetTask(Id, T);
	TestTrue(TEXT("Assigned state, assignee and time recorded"), T.State == S::Assigned && T.Assignee.Get() == M && T.AssignedTime >= 0.f);
	TestFalse(TEXT("Cannot complete before it is active"), F.T0->CompleteTask(Id));
	TestTrue(TEXT("Start"), F.T0->StartTask(Id));
	TestTrue(TEXT("Active"), F.StateOf(F.T0, Id) == S::Active);
	TestFalse(TEXT("Cannot start twice"), F.T0->StartTask(Id));
	TestTrue(TEXT("Complete"), F.T0->CompleteTask(Id));
	F.T0->GetTask(Id, T);
	TestTrue(TEXT("Completed and still inspectable"), T.State == S::Completed && T.EndedTime >= 0.f);
	TestFalse(TEXT("Completed is final (complete)"), F.T0->CompleteTask(Id));
	TestFalse(TEXT("Completed is final (cancel)"), F.T0->CancelTask(Id, TEXT("Test")));
	TestFalse(TEXT("Completed is final (fail)"), F.T0->FailTask(Id, TEXT("Test")));
	TestTrue(TEXT("Completing frees the unit"), !M->HasTask() && M->IsAvailableWorker());
	TestTrue(TEXT("Completing does not kill or move the unit to another tribe"), F.Braves[0]->IsAlive() && M->TribeId == 0 && F.T0->IsFollower(M));

	// Fail and cancel paths.
	const int32 IdF = F.NewTask(F.T0);
	F.T0->AssignTask(IdF, M);
	TestTrue(TEXT("Fail from Assigned"), F.T0->FailTask(IdF, TEXT("Blocked")));
	F.T0->GetTask(IdF, T);
	TestTrue(TEXT("Failed with reason"), T.State == S::Failed && T.EndReason == FName(TEXT("Blocked")));
	const int32 IdC = F.NewTask(F.T0);
	TestTrue(TEXT("Cancel an unassigned task"), F.T0->CancelTask(IdC, TEXT("NotNeeded")) && F.StateOf(F.T0, IdC) == S::Cancelled);
	const int32 IdA = F.NewTask(F.T0);
	F.T0->AssignTask(IdA, M);
	F.T0->StartTask(IdA);
	TestTrue(TEXT("Cancel an active task"), F.T0->CancelTask(IdA, TEXT("Interrupted")) && F.StateOf(F.T0, IdA) == S::Cancelled);
	TestEqual(TEXT("No open tasks left"), F.T0->GetOpenTaskCount(), 0);
	TestFalse(TEXT("Unknown task id refused"), F.T0->StartTask(123456) || F.T0->CompleteTask(123456) || F.T0->CancelTask(123456, NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanTribeTaskAssignmentTest, "Shaman.Tribes.TaskAssignment",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanTribeTaskAssignmentTest::RunTest(const FString& Parameters)
{
	using S = ETribeTaskState;
	FSimFixture F(4);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0) || !TestNotNull(TEXT("Tribe 1"), F.T1)) return false;
	UTribeMemberComponent* Idle = F.Braves[0]->TribeMember;

	TestTrue(TEXT("New Brave is Idle and available"), Idle->GetSimState() == EUnitSimState::Idle && Idle->IsAvailableWorker());
	const int32 A = F.NewTask(F.T0);
	TestTrue(TEXT("Idle unit accepts a task"), F.T0->AssignTask(A, Idle));
	TestTrue(TEXT("Unit holds the task and shows Working"), Idle->GetCurrentTaskId() == A && Idle->GetSimState() == EUnitSimState::Working);
	const int32 B = F.NewTask(F.T0);
	TestFalse(TEXT("Busy unit rejects a second exclusive task"), F.T0->AssignTask(B, Idle));
	TestTrue(TEXT("Rejected task stays unassigned, unit keeps its task"), F.StateOf(F.T0, B) == S::Unassigned && Idle->GetCurrentTaskId() == A);
	TestFalse(TEXT("A task cannot go to two units"), F.T0->AssignTask(A, F.Braves[3]->TribeMember));

	UTribeMemberComponent* Off = F.Braves[1]->TribeMember;
	Off->SetUnavailable(true);
	TestTrue(TEXT("Unavailable state"), Off->GetSimState() == EUnitSimState::Unavailable);
	TestFalse(TEXT("Unavailable unit rejects a task"), F.T0->AssignTask(B, Off));
	Off->SetUnavailable(false);
	TestTrue(TEXT("Available again accepts"), F.T0->AssignTask(B, Off));

	UTribeMemberComponent* Dead = F.Braves[2]->TribeMember;
	F.Braves[2]->Health->Kill(nullptr);
	const int32 C = F.NewTask(F.T0);
	TestTrue(TEXT("Dead state"), Dead->GetSimState() == EUnitSimState::Dead);
	TestFalse(TEXT("Dead unit rejects a task"), F.T0->AssignTask(C, Dead));
	TestFalse(TEXT("The Shaman never takes worker tasks"), F.T0->AssignTask(C, F.S0->TribeMember));
	AShamanUnitBase* EnemyBrave = F.W.SpawnUnit<AShamanUnitBase>(F.W.Data->BraveUnitId, 1, FVector(9300.f, 0.f, 100.f));
	TestFalse(TEXT("Another tribe's unit rejects this tribe's task"), F.T0->AssignTask(C, EnemyBrave->TribeMember));
	TestTrue(TEXT("Refused task still unassigned"), F.StateOf(F.T0, C) == S::Unassigned);

	// Ending a task frees the unit, whatever the outcome.
	F.T0->StartTask(A);
	F.T0->CompleteTask(A);
	TestTrue(TEXT("Completed task frees the unit"), !Idle->HasTask() && Idle->GetSimState() != EUnitSimState::Working && F.T0->AssignTask(C, Idle));
	TestTrue(TEXT("Cancelled task frees the unit"), F.T0->CancelTask(C, TEXT("Test")) && !Idle->HasTask() && Idle->IsAvailableWorker());
	TestTrue(TEXT("Failed task frees the unit"), F.T0->FailTask(B, TEXT("Test")) && !Off->HasTask() && Off->IsAvailableWorker());

	// Losing the unit ends its task (no task held by a dead / departed unit).
	const int32 D = F.NewTask(F.T0);
	F.T0->AssignTask(D, Idle);
	F.T0->StartTask(D);
	F.Braves[0]->Health->Kill(nullptr);
	TestTrue(TEXT("Assigned unit dies -> task Failed"), F.StateOf(F.T0, D) == S::Failed && !Idle->HasTask());
	const int32 E = F.NewTask(F.T0);
	F.T0->AssignTask(E, Off);
	F.Braves[1]->ChangeTribe(1);
	TestTrue(TEXT("Assigned unit changes tribe -> task Failed, unit free in its new tribe"),
		F.StateOf(F.T0, E) == S::Failed && !Off->HasTask() && F.T1->CanAssignTo(Off));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanTribeWorkerPoolTest, "Shaman.Tribes.WorkerPool",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanTribeWorkerPoolTest::RunTest(const FString& Parameters)
{
	FSimFixture F(3);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0) || !TestNotNull(TEXT("Tribe 1"), F.T1)) return false;
	auto InPool = [](const UTribeComponent* T, const UTribeMemberComponent* M) { return T->GetAvailableWorkers().Contains(const_cast<UTribeMemberComponent*>(M)); };
	UTribeMemberComponent* M0 = F.Braves[0]->TribeMember;

	TestEqual(TEXT("3 available workers"), F.T0->GetAvailableWorkerCount(), 3);
	TestEqual(TEXT("Subsystem view agrees"), F.W.Tribes->GetAvailableWorkerCount(0), 3);
	TestFalse(TEXT("The Shaman is never in the worker pool"), InPool(F.T0, F.S0->TribeMember));

	const int32 Id = F.NewTask(F.T0);
	F.T0->AssignTask(Id, M0);
	TestTrue(TEXT("Assigned worker leaves the pool"), F.T0->GetAvailableWorkerCount() == 2 && !InPool(F.T0, M0));
	F.T0->StartTask(Id);
	TestFalse(TEXT("Active worker not available"), InPool(F.T0, M0));
	F.T0->CompleteTask(Id);
	TestTrue(TEXT("Completed worker returns"), F.T0->GetAvailableWorkerCount() == 3 && InPool(F.T0, M0));

	M0->SetUnavailable(true);
	TestTrue(TEXT("Unavailable worker leaves the pool"), F.T0->GetAvailableWorkerCount() == 2 && !InPool(F.T0, M0));
	M0->SetUnavailable(false);
	TestEqual(TEXT("Available again"), F.T0->GetAvailableWorkerCount(), 3);

	UTribeMemberComponent* M1 = F.Braves[1]->TribeMember;
	F.Braves[1]->Health->Kill(nullptr);
	TestTrue(TEXT("Dead worker disappears"), F.T0->GetAvailableWorkerCount() == 2 && !InPool(F.T0, M1));

	UTribeMemberComponent* M2 = F.Braves[2]->TribeMember;
	F.Braves[2]->ChangeTribe(1);
	TestTrue(TEXT("Transferred worker leaves the old pool"), F.T0->GetAvailableWorkerCount() == 1 && !InPool(F.T0, M2));
	TestTrue(TEXT("...and joins the new tribe's pool"), F.T1->GetAvailableWorkerCount() == 1 && InPool(F.T1, M2));

	AShamanUnitBase* Wild = F.W.SpawnUnit<AShamanUnitBase>(F.W.Data->WildmanUnitId, -1, FVector(-500.f, 0.f, 100.f));
	TestFalse(TEXT("Wildmen are not workers"), Wild->TribeMember->IsAvailableWorker());
	UTribeMemberComponent* Converted = Wild->TribeMember->ConvertToTribe(F.T0, EConversionKind::Recruit);
	TestTrue(TEXT("Converted worker appears in the new tribe's pool"), Converted && InPool(F.T0, Converted) && F.T0->GetAvailableWorkerCount() == 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanTribeSettlementTest, "Shaman.Tribes.SettlementAnchor",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanTribeSettlementTest::RunTest(const FString& Parameters)
{
	FTribeTestWorld W;
	if (!TestNotNull(TEXT("Building table (production data)"), W.Data->BuildingTable)) return false;
	FVector Anchor;
	TestFalse(TEXT("No buildings: no settlement anchor"), W.Tribes->GetSettlementAnchor(0, Anchor));
	const FVector CircleAt(0.f, 0.f, 0.f), FireAt(480.f, 0.f, 0.f);
	W.SpawnCircle(0, CircleAt);
	TestTrue(TEXT("Circle anchors the settlement"), W.Tribes->GetSettlementAnchor(0, Anchor) && Anchor.Equals(CircleAt));
	ABuildingActor* Fire = W.SpawnBuilding(W.Data->CampfireBuildingId, 0, FireAt);
	TestTrue(TEXT("Gathering point (campfire) becomes the anchor"), W.Tribes->GetSettlementAnchor(0, Anchor) && Anchor.Equals(FireAt));
	TestEqual(TEXT("Tribe owns 2 buildings"), W.Tribes->GetBuildings(0).Num(), 2);
	TestEqual(TEXT("Other tribe owns none"), W.Tribes->GetBuildings(1).Num(), 0);
	Fire->Destroy();
	TestTrue(TEXT("Campfire gone: back to the circle"), W.Tribes->GetSettlementAnchor(0, Anchor) && Anchor.Equals(CircleAt));
	TestEqual(TEXT("Tribe owns 1 building"), W.Tribes->GetBuildings(0).Num(), 1);
	return true;
}

#endif
