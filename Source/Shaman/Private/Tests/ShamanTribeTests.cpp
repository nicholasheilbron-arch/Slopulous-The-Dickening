#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Game/ShamanGameData.h"
#include "Tribes/TribeSubsystem.h"
#include "TribeComponent.h"
#include "SpellComponent.h"
#include "Characters/ShamanUnitBase.h"
#include "Characters/ShamanCharacter.h"
#include "Characters/HealthComponent.h"
#include "Buildings/BuildingActor.h"

#include "ShamanTribeTestWorld.h"

using namespace ShamanTribeTestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanFollowerCountOnDeathTest, "Shaman.Tribes.FollowerCountOnDeath",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanFollowerCountOnDeathTest::RunTest(const FString& Parameters)
{
	FTribeTestWorld W;
	if (!TestNotNull(TEXT("Unit table (production data)"), W.Data->UnitTable)) return false;

	// Same order as the game: some followers begin play before their Shaman (adopted when the tribe registers).
	TArray<AShamanUnitBase*> Braves;
	for (int32 i = 0; i < 3; ++i) Braves.Add(W.SpawnUnit<AShamanUnitBase>(W.Data->BraveUnitId, 0, FVector(300.f * i, 500.f, 100.f)));
	AShamanCharacter* S = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 0, FVector(0.f, 0.f, 100.f));
	for (int32 i = 3; i < 6; ++i) Braves.Add(W.SpawnUnit<AShamanUnitBase>(W.Data->BraveUnitId, 0, FVector(300.f * i, 500.f, 100.f)));

	UTribeComponent* TC = W.Tribes->GetTribeComponent(0);
	if (!TestNotNull(TEXT("Player tribe registered"), TC)) return false;
	auto Check = [&](int32 Expected, const TCHAR* When)
	{
		TestEqual(FString::Printf(TEXT("%s: follower count"), When), TC->GetFollowerCount(), Expected);
		TestEqual(FString::Printf(TEXT("%s: HUD count (population used)"), When), TC->GetPopulationUsed(), Expected);
		TestEqual(FString::Printf(TEXT("%s: subsystem count"), When), W.Tribes->GetFollowerCount(0), Expected);
		TestEqual(FString::Printf(TEXT("%s: mana-regen follower count"), When), S->Spells->FollowerCount, Expected);
	};
	Check(6, TEXT("Start"));
	const float Regen6 = S->Spells->GetManaRegen();

	// A non-lethal hit (Blast centre damage 40 vs Brave health) does not change the roster.
	UGameplayStatics::ApplyDamage(Braves[0], 40.f, nullptr, S, UDamageType::StaticClass());
	TestTrue(TEXT("40 damage does not kill a Brave"), Braves[0]->IsAlive());
	Check(6, TEXT("After a non-lethal hit"));

	Braves[1]->Health->Kill(S);
	Check(5, TEXT("After 1 death"));
	TestTrue(TEXT("Mana regen drops with the follower count"), S->Spells->GetManaRegen() < Regen6);
	Braves[2]->Health->Kill(nullptr); // e.g. drowned
	Check(4, TEXT("After 2 deaths"));
	TestEqual(TEXT("Rebirth time uses the live count"),
		UTribeSubsystem::ComputeRebirthTime(W.Data->Reincarnation, W.Tribes->GetFollowerCount(0)),
		UTribeSubsystem::ComputeRebirthTime(W.Data->Reincarnation, 4));
	Braves[1]->Destroy(); // corpse removal changes nothing
	Check(4, TEXT("After a corpse is removed"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanTribeEliminationTest, "Shaman.Tribes.Elimination",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanTribeEliminationTest::RunTest(const FString& Parameters)
{
	{
		FTribeTestWorld W;
		if (!TestNotNull(TEXT("Building table (production data)"), W.Data->BuildingTable)) return false;
		ABuildingActor* PlayerCircle = W.SpawnCircle(0, FVector(0.f, 0.f, 0.f));
		ABuildingActor* EnemyCircle = W.SpawnCircle(1, FVector(9000.f, 0.f, 0.f));
		AShamanCharacter* Player = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 0, FVector(0.f, 400.f, 100.f));
		AShamanCharacter* Enemy = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 1, FVector(9000.f, 400.f, 100.f));
		AShamanUnitBase* EnemyBrave = W.SpawnUnit<AShamanUnitBase>(W.Data->BraveUnitId, 1, FVector(9300.f, 400.f, 100.f));
		TestTrue(TEXT("Circles registered"), W.Tribes->GetReincarnationCircle(0) == PlayerCircle && W.Tribes->GetReincarnationCircle(1) == EnemyCircle);
		TestEqual(TEXT("Enemy has 1 follower"), W.Tribes->GetFollowerCount(1), 1);

		// 1) Enemy Shaman dies while its tribe still has a follower: normal reincarnation, circle stays.
		Enemy->Health->Kill(Player);
		W.AdvanceTimers(0.01f);
		TestFalse(TEXT("With followers: not eliminated"), W.Tribes->IsTribeEliminated(1));
		TestTrue(TEXT("With followers: rebirth scheduled"), W.Tribes->GetRebirthRemaining(1) > 0.f);
		TestTrue(TEXT("With followers: circle stands"), IsValid(EnemyCircle) && !EnemyCircle->IsPendingKillPending());
		W.AdvanceTimers(60.f);
		TestTrue(TEXT("Enemy Shaman reincarnated"), Enemy->IsAlive());

		// 2) Its last follower dies, then the Shaman dies with 0 followers: circle destroyed, tribe eliminated, level won.
		EnemyBrave->Health->Kill(Player);
		TestEqual(TEXT("Enemy has 0 followers"), W.Tribes->GetFollowerCount(1), 0);
		Enemy->Health->Kill(Player);
		W.AdvanceTimers(0.01f);
		TestTrue(TEXT("0 followers: tribe eliminated"), W.Tribes->IsTribeEliminated(1));
		TestTrue(TEXT("0 followers: enemy circle destroyed"), !IsValid(EnemyCircle) || EnemyCircle->IsPendingKillPending());
		TestNull(TEXT("0 followers: no circle left for the tribe"), W.Tribes->GetReincarnationCircle(1));
		TestEqual(TEXT("0 followers: no rebirth scheduled"), W.Tribes->GetRebirthRemaining(1), -1.f);
		TestTrue(TEXT("Last enemy tribe eliminated: level won"), W.Tribes->IsLevelWon());
		W.AdvanceTimers(60.f);
		TestFalse(TEXT("Eliminated Shaman never comes back"), Enemy->IsAlive());

		// 3) The player's Shaman dies with 0 followers: player circle untouched, normal reincarnation.
		TestEqual(TEXT("Player has 0 followers"), W.Tribes->GetFollowerCount(0), 0);
		Player->Health->Kill(nullptr);
		W.AdvanceTimers(0.01f);
		TestFalse(TEXT("Player tribe never eliminated by this rule"), W.Tribes->IsTribeEliminated(0));
		TestTrue(TEXT("Player circle stands"), IsValid(PlayerCircle) && !PlayerCircle->IsPendingKillPending());
		TestTrue(TEXT("Player rebirth scheduled"), W.Tribes->GetRebirthRemaining(0) > 0.f);
		W.AdvanceTimers(60.f);
		TestTrue(TEXT("Player reincarnated"), Player->IsAlive());
	}
	{
		// 4) One event kills the enemy Shaman first and its last follower right after (Blast overlap order):
		//    decided after both deaths, so the tribe is eliminated.
		FTribeTestWorld W;
		ABuildingActor* EnemyCircle = W.SpawnCircle(1, FVector(9000.f, 0.f, 0.f));
		W.SpawnCircle(0, FVector(0.f, 0.f, 0.f));
		AShamanCharacter* Enemy = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 1, FVector(9000.f, 400.f, 100.f));
		AShamanUnitBase* EnemyBrave = W.SpawnUnit<AShamanUnitBase>(W.Data->BraveUnitId, 1, FVector(9300.f, 400.f, 100.f));
		Enemy->Health->Kill(nullptr);
		EnemyBrave->Health->Kill(nullptr);
		W.AdvanceTimers(0.01f);
		TestTrue(TEXT("Same event: eliminated"), W.Tribes->IsTribeEliminated(1));
		TestTrue(TEXT("Same event: circle destroyed"), !IsValid(EnemyCircle) || EnemyCircle->IsPendingKillPending());
	}
	return true;
}

#endif
