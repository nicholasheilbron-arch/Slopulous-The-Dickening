#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "SpellComponent.h"
#include "SpellRow.h"
#include "SpellProjectile.h"
#include "SpellEffectDispatcherComponent.h"
#include "SpellEffect_ConvertUnits.h"
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "TribeRegistrySubsystem.h"

/**
 * Convert milestone tests. Each test builds a throwaway game world, spawns plain AActors and adds the real
 * components at runtime (no Blueprints or assets needed), then casts through USpellComponent::CastSpell so the
 * whole path SpellComponent -> OnSpellCast -> dispatcher -> ConvertUnits -> tribe is exercised.
 */
namespace ConvertTestsPrivate
{
	struct FTestWorld
	{
		UWorld* World = nullptr;
		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ConvertTestWorld"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			// No GameMode in this world, so begin play is not flagged automatically; do it so spawned actors/components BeginPlay.
			if (!World->HasBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			World->RemoveFromRoot();
		}

		AActor* SpawnBare(const FVector& Loc)
		{
			AActor* A = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Loc));
			USceneComponent* Root = NewObject<USceneComponent>(A, TEXT("Root"));
			A->SetRootComponent(Root);
			Root->RegisterComponent();
			A->SetActorLocation(Loc);
			return A;
		}

		template<class T> T* AddComp(AActor* A, const TCHAR* Name)
		{
			T* C = NewObject<T>(A, Name);
			return C;
		}

		/** Unit with identity. Registering the component after setup triggers its BeginPlay (actor already playing). */
		UTribeMemberComponent* SpawnUnit(const FVector& Loc, EUnitKind Kind, int32 TribeId)
		{
			AActor* A = SpawnBare(Loc);
			UTribeMemberComponent* M = AddComp<UTribeMemberComponent>(A, TEXT("TribeMember"));
			M->UnitKind = Kind;
			M->TribeId = TribeId;
			M->RegisterComponent();
			return M;
		}

		struct FCaster { AActor* Actor; USpellComponent* Spells; UTribeComponent* Tribe; USpellEffectDispatcherComponent* Dispatcher; };

		/** Shaman stand-in: SpellComponent (with a runtime spell table) + tribe + effect dispatcher. */
		FCaster SpawnCaster(int32 TribeId, int32 Capacity, UDataTable* Table)
		{
			FCaster C;
			C.Actor = SpawnBare(FVector::ZeroVector);
			C.Spells = AddComp<USpellComponent>(C.Actor, TEXT("Spells"));
			C.Spells->SpellTable = Table;
			C.Spells->Mana = 100.f;
			C.Spells->BaseRegen = 0.f; // no regen during tests
			C.Spells->RegisterComponent();
			C.Tribe = AddComp<UTribeComponent>(C.Actor, TEXT("Tribe"));
			C.Tribe->TribeId = TribeId;
			C.Tribe->PopulationCapacity = Capacity;
			C.Tribe->RegisterComponent();
			C.Dispatcher = AddComp<USpellEffectDispatcherComponent>(C.Actor, TEXT("Dispatcher"));
			C.Dispatcher->RegisterComponent();
			return C;
		}
	};

	/** Values copied from the existing Spells.csv rows (Convert and Blast) - unchanged balance. */
	static FSpellRow ConvertRow()
	{
		FSpellRow R;
		R.Targeting = ESpellTargeting::Ground; R.ManaCost = 5.f; R.RangeRaw = 100.f; R.Radius = 500.f;
		R.CastTime = 0.6f; R.Cooldown = 2.f; R.Damage = 0.f; R.Knockback = 0.f; R.Duration = 0.f;
		R.EffectId = TEXT("ConvertWildmen"); R.UnlockCondition = TEXT("Start");
		return R;
	}
	static FSpellRow BlastRow()
	{
		FSpellRow R;
		R.Targeting = ESpellTargeting::Projectile; R.ManaCost = 5.f; R.RangeRaw = 10.f; R.Radius = 300.f; R.CastTime = 0.4f;
		R.Cooldown = 1.f; R.Damage = 40.f; R.Knockback = 1.f; R.ProjectileSpeed = 2500.f; R.bHoming = true; R.bIgnites = true;
		R.EffectId = TEXT("FireBlast"); R.UnlockCondition = TEXT("Start");
		return R;
	}
	static UDataTable* MakeTable()
	{
		UDataTable* T = NewObject<UDataTable>();
		T->RowStruct = FSpellRow::StaticStruct();
		T->AddRow(TEXT("Convert"), ConvertRow());
		T->AddRow(TEXT("Blast"), BlastRow());
		return T;
	}
	static const FVector Aim(2000.f, 0.f, 0.f); // inside Convert range (100 * 50 = 5000 uu)
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertOneWildmanTest, "Shaman.Spells.Convert.OneWildman",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertOneWildmanTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 10, MakeTable());
	C.Spells->LearnSpell(TEXT("Convert"));
	UTribeMemberComponent* W = TW.SpawnUnit(Aim + FVector(100, 0, 0), EUnitKind::Wildman, ShamanTribes::NoTribe);
	TestEqual(TEXT("Population before"), C.Tribe->GetFollowerCount(), 0);
	TestTrue(TEXT("Cast succeeds"), C.Spells->CastSpell(TEXT("Convert"), Aim));
	TestEqual(TEXT("Population +1"), C.Tribe->GetFollowerCount(), 1);
	TestTrue(TEXT("Wildman is now a follower"), W->IsFollower());
	TestEqual(TEXT("...of the player tribe"), W->GetTribeId(), 0);
	TestTrue(TEXT("Registered with the tribe"), C.Tribe->IsFollower(W));
	TestEqual(TEXT("Mana follower count updated"), C.Spells->FollowerCount, 1);
	TestEqual(TEXT("Dispatcher affected 1"), C.Dispatcher->LastResult.Affected, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertMultipleWildmenTest, "Shaman.Spells.Convert.MultipleWildmen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertMultipleWildmenTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 10, MakeTable());
	C.Spells->LearnSpell(TEXT("Convert"));
	TArray<UTribeMemberComponent*> Wild;
	for (int32 I = 0; I < 4; ++I) Wild.Add(TW.SpawnUnit(Aim + FVector(0, -300 + I * 200, 0), EUnitKind::Wildman, ShamanTribes::NoTribe));
	TestTrue(TEXT("Cast succeeds"), C.Spells->CastSpell(TEXT("Convert"), Aim));
	for (UTribeMemberComponent* W : Wild) TestTrue(TEXT("Every Wildman in radius converted"), W->IsFollower() && W->GetTribeId() == 0);
	TestEqual(TEXT("Population +4"), C.Tribe->GetFollowerCount(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertCapacityTest, "Shaman.Spells.Convert.PopulationCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertCapacityTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 3, MakeTable());
	C.Spells->LearnSpell(TEXT("Convert"));
	TW.SpawnUnit(FVector(-3000, 0, 0), EUnitKind::Follower, 0); // existing Brave, far away -> population 1 / 3
	TArray<UTribeMemberComponent*> Wild;
	for (int32 I = 0; I < 4; ++I) Wild.Add(TW.SpawnUnit(Aim + FVector(50.f * (I + 1), 0, 0), EUnitKind::Wildman, ShamanTribes::NoTribe));
	TestEqual(TEXT("Population before"), C.Tribe->GetFollowerCount(), 1);
	C.Spells->CastSpell(TEXT("Convert"), Aim);
	TestEqual(TEXT("Population capped at capacity"), C.Tribe->GetFollowerCount(), 3);
	TestEqual(TEXT("Two converted"), C.Dispatcher->LastResult.Affected, 2);
	TestEqual(TEXT("Two skipped for capacity"), C.Dispatcher->LastResult.SkippedForCapacity, 2);
	TestTrue(TEXT("Closest two converted"), Wild[0]->IsFollower() && Wild[1]->IsFollower());
	TestTrue(TEXT("Excess Wildmen untouched"), Wild[2]->IsWildman() && Wild[3]->IsWildman()
		&& Wild[2]->GetTribeId() == ShamanTribes::NoTribe && Wild[3]->GetTribeId() == ShamanTribes::NoTribe);

	// Full tribe: a second cast (after cooldown) creates nobody.
	C.Spells->Mana = 100.f;
	C.Dispatcher->Dispatch([&]{ FSpellEffectContext X; X.Caster = C.Actor; X.SpellComponent = C.Spells; X.SpellId = TEXT("Convert");
		X.EffectId = TEXT("ConvertWildmen"); X.Target = Aim; X.Radius = 500.f; return X; }());
	TestEqual(TEXT("Still capped"), C.Tribe->GetFollowerCount(), 3);
	TestTrue(TEXT("Still Wildmen"), Wild[2]->IsWildman() && Wild[3]->IsWildman());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertIgnoresEnemyTest, "Shaman.Spells.Convert.IgnoresEnemyFollowers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertIgnoresEnemyTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 10, MakeTable());
	auto E = TW.SpawnCaster(1, 10, MakeTable()); // enemy tribe 1
	C.Spells->LearnSpell(TEXT("Convert"));
	UTribeMemberComponent* Enemy = TW.SpawnUnit(Aim, EUnitKind::Follower, 1);
	TestEqual(TEXT("Enemy population"), E.Tribe->GetFollowerCount(), 1);
	C.Spells->CastSpell(TEXT("Convert"), Aim);
	TestEqual(TEXT("Enemy follower keeps its tribe"), Enemy->GetTribeId(), 1);
	TestTrue(TEXT("Enemy still registered with enemy tribe"), E.Tribe->IsFollower(Enemy));
	TestEqual(TEXT("Player population unchanged"), C.Tribe->GetFollowerCount(), 0);
	TestEqual(TEXT("Enemy population unchanged"), E.Tribe->GetFollowerCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertIgnoresOwnAndSpecialTest, "Shaman.Spells.Convert.IgnoresOwnBraveShamanBuilding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertIgnoresOwnAndSpecialTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 10, MakeTable());
	C.Spells->LearnSpell(TEXT("Convert"));
	UTribeMemberComponent* Brave = TW.SpawnUnit(Aim, EUnitKind::Follower, 0);
	UTribeMemberComponent* EnemyShaman = TW.SpawnUnit(Aim + FVector(50, 0, 0), EUnitKind::Shaman, 1);
	UTribeMemberComponent* Hut = TW.SpawnUnit(Aim + FVector(-50, 0, 0), EUnitKind::Building, ShamanTribes::NoTribe);
	TestEqual(TEXT("Population before"), C.Tribe->GetFollowerCount(), 1);
	C.Spells->CastSpell(TEXT("Convert"), Aim);
	TestTrue(TEXT("Own Brave unchanged"), Brave->IsFollower() && Brave->GetTribeId() == 0);
	TestEqual(TEXT("Own Brave counted once"), C.Tribe->GetFollowerCount(), 1);
	TestTrue(TEXT("Shaman unchanged"), EnemyShaman->GetUnitKind() == EUnitKind::Shaman && EnemyShaman->GetTribeId() == 1);
	TestTrue(TEXT("Building unchanged"), Hut->GetUnitKind() == EUnitKind::Building);
	TestEqual(TEXT("No candidates"), C.Dispatcher->LastResult.Candidates, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertOutsideRadiusTest, "Shaman.Spells.Convert.OutsideRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertOutsideRadiusTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 10, MakeTable());
	C.Spells->LearnSpell(TEXT("Convert"));
	UTribeMemberComponent* Inside = TW.SpawnUnit(Aim + FVector(499, 0, 0), EUnitKind::Wildman, ShamanTribes::NoTribe);
	UTribeMemberComponent* Outside = TW.SpawnUnit(Aim + FVector(0, 501, 0), EUnitKind::Wildman, ShamanTribes::NoTribe);
	C.Spells->CastSpell(TEXT("Convert"), Aim);
	TestTrue(TEXT("Inside radius (499) converted"), Inside->IsFollower());
	TestTrue(TEXT("Outside radius (501) unchanged"), Outside->IsWildman() && Outside->GetTribeId() == ShamanTribes::NoTribe);
	TestEqual(TEXT("Population +1"), C.Tribe->GetFollowerCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertNoTargetsTest, "Shaman.Spells.Convert.NoValidTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertNoTargetsTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 10, MakeTable());
	C.Spells->LearnSpell(TEXT("Convert"));
	TestTrue(TEXT("Cast with nobody there does not fail or crash"), C.Spells->CastSpell(TEXT("Convert"), Aim));
	TestEqual(TEXT("No population change"), C.Tribe->GetFollowerCount(), 0);
	TestTrue(TEXT("Effect handled"), C.Dispatcher->LastResult.bHandled);
	TestEqual(TEXT("Nothing affected"), C.Dispatcher->LastResult.Affected, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertManaTest, "Shaman.Spells.Convert.ManaCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertManaTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 10, MakeTable());
	C.Spells->LearnSpell(TEXT("Convert"));
	C.Spells->Mana = 50.f;
	TestTrue(TEXT("Cast succeeds"), C.Spells->CastSpell(TEXT("Convert"), Aim));
	TestEqual(TEXT("Exactly 5 mana spent"), C.Spells->Mana, 45.f);

	auto D = TW.SpawnCaster(1, 10, MakeTable()); // tribe ids must be unique per world
	D.Spells->LearnSpell(TEXT("Convert"));
	D.Spells->Mana = 4.f;
	TestFalse(TEXT("Not enough mana fails"), D.Spells->CastSpell(TEXT("Convert"), Aim));
	TestEqual(TEXT("No mana spent on failure"), D.Spells->Mana, 4.f);
	D.Spells->Mana = 50.f;
	TestFalse(TEXT("Out of range (5001 uu) fails"), D.Spells->CastSpell(TEXT("Convert"), FVector(5001.f, 0.f, 0.f)));
	TestEqual(TEXT("No mana spent when out of range"), D.Spells->Mana, 50.f);
	TestTrue(TEXT("Exactly at range (5000 uu) works"), D.Spells->CastSpell(TEXT("Convert"), FVector(5000.f, 0.f, 0.f)));

	auto U = TW.SpawnCaster(2, 10, MakeTable()); // never learned Convert
	U.Spells->Mana = 50.f;
	TestFalse(TEXT("Unknown spell fails"), U.Spells->CastSpell(TEXT("Convert"), Aim));
	TestEqual(TEXT("No mana spent for unknown spell"), U.Spells->Mana, 50.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertCooldownTest, "Shaman.Spells.Convert.Cooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertCooldownTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 10, MakeTable());
	C.Spells->LearnSpell(TEXT("Convert"));
	UTribeMemberComponent* W = TW.SpawnUnit(Aim, EUnitKind::Wildman, ShamanTribes::NoTribe);
	TestTrue(TEXT("First cast"), C.Spells->CastSpell(TEXT("Convert"), Aim + FVector(0, 2000, 0))); // empty area
	const float ManaAfterFirst = C.Spells->Mana;
	TestFalse(TEXT("Immediate recast blocked by 2s cooldown"), C.Spells->CastSpell(TEXT("Convert"), Aim));
	TestEqual(TEXT("No mana spent on blocked recast"), C.Spells->Mana, ManaAfterFirst);
	TestTrue(TEXT("Wildman untouched by blocked cast"), W->IsWildman());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertBlastRegressionTest, "Shaman.Spells.Convert.BlastUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertBlastRegressionTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 10, MakeTable());
	C.Spells->LearnSpell(TEXT("Blast"));
	C.Spells->ProjectileClasses.Add(TEXT("FireBlast"), ASpellProjectile::StaticClass());
	UTribeMemberComponent* W = TW.SpawnUnit(FVector(300, 0, 0), EUnitKind::Wildman, ShamanTribes::NoTribe);
	C.Dispatcher->LastResult = FSpellEffectResult();
	TestTrue(TEXT("Blast casts"), C.Spells->CastSpell(TEXT("Blast"), FVector(1000, 0, 0)));
	TestEqual(TEXT("Blast costs 5 mana"), C.Spells->Mana, 95.f);
	TestFalse(TEXT("Projectile spells do not go through the effect dispatcher"), C.Dispatcher->LastResult.bHandled);
	TestTrue(TEXT("Blast converts nobody"), W->IsWildman());
	TestEqual(TEXT("Population unchanged"), C.Tribe->GetFollowerCount(), 0);
	TestFalse(TEXT("Blast cooldown still applies"), C.Spells->CastSpell(TEXT("Blast"), FVector(1000, 0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConvertRosterConsistencyTest, "Shaman.Spells.Convert.RosterConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConvertRosterConsistencyTest::RunTest(const FString& Parameters)
{
	using namespace ConvertTestsPrivate;
	FTestWorld TW;
	auto C = TW.SpawnCaster(0, 10, MakeTable());
	UTribeMemberComponent* B = TW.SpawnUnit(FVector(100, 0, 0), EUnitKind::Follower, 0);
	TestTrue(TEXT("Double register is a no-op"), C.Tribe->RegisterFollower(B) && C.Tribe->RegisterFollower(B));
	TestEqual(TEXT("Counted once"), C.Tribe->GetFollowerCount(), 1);
	UTribeMemberComponent* W = TW.SpawnUnit(FVector(200, 0, 0), EUnitKind::Wildman, ShamanTribes::NoTribe);
	TestNotNull(TEXT("Direct conversion works"), W->ConvertToTribe(C.Tribe, EConversionKind::Recruit));
	TestNull(TEXT("Converting twice does nothing"), W->ConvertToTribe(C.Tribe, EConversionKind::Recruit));
	TestNull(TEXT("Hypnotize not implemented yet"), B->ConvertToTribe(C.Tribe, EConversionKind::Hypnotize));
	TestEqual(TEXT("Count is 2"), C.Tribe->GetFollowerCount(), 2);
	B->GetOwner()->Destroy();
	TestEqual(TEXT("Destroyed follower leaves the roster"), C.Tribe->GetFollowerCount(), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
