#include "Misc/AutomationTest.h"
#include "SpellComponent.h"
#include "SpellRow.h"
#include "Core/ShamanTargetRules.h"
#include "Tribes/TribeSubsystem.h"
#include "Engine/DataTable.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ShamanRuleTestsPrivate
{
	static FSpellRow MakeBlast()
	{
		FSpellRow R; // mirrors the Blast (Taka) row in Spells.csv
		R.Targeting = ESpellTargeting::Projectile; R.ManaCost = 5.f; R.RangeRaw = 10.f; R.Cooldown = 1.f;
		R.TargetFilter = ESpellTargetFilter::AllUnits; R.bFriendlyFire = true;
		return R;
	}
	static FSpellRow MakeGround(float Mana, float Range)
	{
		FSpellRow R; R.Targeting = ESpellTargeting::Ground; R.ManaCost = Mana; R.RangeRaw = Range; R.Cooldown = 2.f;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanSpellManaRangeTest, "Shaman.Spells.ManaCooldownRange",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanSpellManaRangeTest::RunTest(const FString& Parameters)
{
	using namespace ShamanRuleTestsPrivate;
	const float UPP = 50.f; // RangeUnitsPerPoint default
	const FSpellRow Blast = MakeBlast();
	TestTrue(TEXT("Blast castable with exactly 5 mana"), USpellComponent::EvaluateCast(Blast, 5.f, 0, 10.0, -1.0, 100.f, UPP) == ESpellCastResult::Success);
	TestTrue(TEXT("Blast blocked at 4.9 mana"), USpellComponent::EvaluateCast(Blast, 4.9f, 0, 10.0, -1.0, 100.f, UPP) == ESpellCastResult::NotEnoughMana);
	TestTrue(TEXT("Blast blocked on cooldown"), USpellComponent::EvaluateCast(Blast, 50.f, 0, 10.0, 10.5, 100.f, UPP) == ESpellCastResult::OnCooldown);
	TestTrue(TEXT("Cooldown expires"), USpellComponent::EvaluateCast(Blast, 50.f, 0, 10.5, 10.5, 100.f, UPP) == ESpellCastResult::Success);
	TestEqual(TEXT("Projectile range is enforced by projectile lifetime, not the cast check"),
		USpellComponent::EvaluateCast(Blast, 50.f, 0, 10.0, -1.0, 99999.f, UPP), ESpellCastResult::Success);

	const FSpellRow Ground = MakeGround(30.f, 60.f); // 60 * 50 = 3000 uu
	TestTrue(TEXT("Ground spell inside range"), USpellComponent::EvaluateCast(Ground, 100.f, 0, 0.0, -1.0, 2999.f, UPP) == ESpellCastResult::Success);
	TestTrue(TEXT("Ground spell out of range"), USpellComponent::EvaluateCast(Ground, 100.f, 0, 0.0, -1.0, 3001.f, UPP) == ESpellCastResult::OutOfRange);

	FSpellRow Super = MakeGround(0.f, 100.f); Super.bSuperSpell = true; Super.ManaCost = 50.f;
	TestTrue(TEXT("Super spell ignores mana"), USpellComponent::EvaluateCast(Super, 0.f, 1, 0.0, -1.0, 10.f, UPP) == ESpellCastResult::Success);
	TestTrue(TEXT("Super spell needs a charge"), USpellComponent::EvaluateCast(Super, 100.f, 0, 0.0, -1.0, 10.f, UPP) == ESpellCastResult::NoCharges);

	// Range conversion on the component (0-100 design scale -> world units).
	USpellComponent* C = NewObject<USpellComponent>();
	TestEqual(TEXT("Default RangeUnitsPerPoint"), C->RangeUnitsPerPoint, 50.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanTargetFilterTest, "Shaman.Spells.TargetFiltering",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanTargetFilterTest::RunTest(const FString& Parameters)
{
	using R = FShamanTargetRules;
	TestTrue(TEXT("Same tribe = friendly"), R::GetRelation(0, 0, false) == ETribeRelation::Friendly);
	TestTrue(TEXT("Other tribe = hostile"), R::GetRelation(0, 1, false) == ETribeRelation::Hostile);
	TestTrue(TEXT("Wildmen = wild"), R::GetRelation(0, ShamanTribe::Wild, false) == ETribeRelation::Wild);
	TestTrue(TEXT("Props = neutral"), R::GetRelation(0, ShamanTribe::None, false) == ETribeRelation::Neutral);
	TestTrue(TEXT("Caster = self"), R::GetRelation(0, 0, true) == ETribeRelation::Self);
	TestFalse(TEXT("Wildmen are never hostile"), R::AreHostile(0, ShamanTribe::Wild));
	TestTrue(TEXT("Tribes 0 and 1 are hostile"), R::AreHostile(0, 1));

	// Convert: Wildmen only (spec: ignore enemy followers).
	TestTrue(TEXT("Convert affects Wildmen"), R::PassesFilter(ESpellTargetFilter::WildmenOnly, ETribeRelation::Wild, false));
	TestFalse(TEXT("Convert ignores enemies"), R::PassesFilter(ESpellTargetFilter::WildmenOnly, ETribeRelation::Hostile, false));
	TestFalse(TEXT("Convert ignores own followers"), R::PassesFilter(ESpellTargetFilter::WildmenOnly, ETribeRelation::Friendly, true));
	// Buffs.
	TestTrue(TEXT("FriendlyOnly hits friends"), R::PassesFilter(ESpellTargetFilter::FriendlyOnly, ETribeRelation::Friendly, false));
	TestFalse(TEXT("FriendlyOnly skips enemies"), R::PassesFilter(ESpellTargetFilter::FriendlyOnly, ETribeRelation::Hostile, false));
	// Melee / AI targeting.
	TestTrue(TEXT("EnemiesOnly hits hostiles"), R::PassesFilter(ESpellTargetFilter::EnemiesOnly, ETribeRelation::Hostile, false));
	TestFalse(TEXT("EnemiesOnly skips Wildmen"), R::PassesFilter(ESpellTargetFilter::EnemiesOnly, ETribeRelation::Wild, false));
	TestTrue(TEXT("EnemiesAndWildmen hits Wildmen"), R::PassesFilter(ESpellTargetFilter::EnemiesAndWildmen, ETribeRelation::Wild, true));
	TestFalse(TEXT("EnemiesAndWildmen never hits friends"), R::PassesFilter(ESpellTargetFilter::EnemiesAndWildmen, ETribeRelation::Friendly, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanFriendlyFireTest, "Shaman.Spells.FriendlyFire",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanFriendlyFireTest::RunTest(const FString& Parameters)
{
	using R = FShamanTargetRules;
	const FSpellRow Blast = ShamanRuleTestsPrivate::MakeBlast();
	TestTrue(TEXT("Blast hits enemies"), R::PassesFilter(Blast.TargetFilter, ETribeRelation::Hostile, Blast.bFriendlyFire));
	TestTrue(TEXT("Blast hits own followers (spec: friendly fire yes)"), R::PassesFilter(Blast.TargetFilter, ETribeRelation::Friendly, Blast.bFriendlyFire));
	TestTrue(TEXT("Blast hits Wildmen"), R::PassesFilter(Blast.TargetFilter, ETribeRelation::Wild, Blast.bFriendlyFire));
	TestFalse(TEXT("Blast never hits its caster"), R::PassesFilter(Blast.TargetFilter, ETribeRelation::Self, Blast.bFriendlyFire));
	TestFalse(TEXT("Friendly fire off spares own followers"), R::PassesFilter(ESpellTargetFilter::AllUnits, ETribeRelation::Friendly, false));
	TestTrue(TEXT("EnemiesOnly + friendly fire still hits friends"), R::PassesFilter(ESpellTargetFilter::EnemiesOnly, ETribeRelation::Friendly, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanPopulationFormulasTest, "Shaman.Tribes.PopulationFormulas",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanPopulationFormulasTest::RunTest(const FString& Parameters)
{
	// Mana regen = BaseRegen * (1 + RegenPerFollower * followers) * TemporaryModifier.
	USpellComponent* C = NewObject<USpellComponent>();
	C->BaseRegen = 1.f; C->RegenPerFollower = 0.25f; C->TemporaryModifier = 1.f;
	C->FollowerCount = 0; const float R0 = C->GetManaRegen();
	C->FollowerCount = 3; const float R3 = C->GetManaRegen();
	C->FollowerCount = 20; const float R20 = C->GetManaRegen();
	TestEqual(TEXT("No followers = base regen"), R0, 1.f);
	TestEqual(TEXT("3 followers"), R3, 1.75f);
	TestTrue(TEXT("More followers = faster regen"), R20 > R3 && R3 > R0);
	C->TemporaryModifier = 2.f; // e.g. Shaman-kill reward (Phase 4)
	TestEqual(TEXT("Temporary modifier multiplies"), C->GetManaRegen(), 2.f * R20);

	// Rebirth time = Clamp(Base / (1 + k * followers), Min, Max).
	FReincarnationConfig RC; RC.BaseTime = 12.f; RC.PerFollowerModifier = 0.08f; RC.MinTime = 3.f; RC.MaxTime = 30.f;
	const float T0 = UTribeSubsystem::ComputeRebirthTime(RC, 0);
	const float T3 = UTribeSubsystem::ComputeRebirthTime(RC, 3);
	const float T500 = UTribeSubsystem::ComputeRebirthTime(RC, 500);
	TestEqual(TEXT("No followers = base time"), T0, 12.f);
	TestTrue(TEXT("More followers = faster rebirth"), T3 < T0);
	TestEqual(TEXT("Rebirth clamps at MinTime"), T500, 3.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanTakaPreservedTest, "Shaman.Data.TakaPreserved",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanTakaPreservedTest::RunTest(const FString& Parameters)
{
	// Guards against silent rebalancing of TAKA (spec: report any change first).
	UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Spells.DT_Spells"));
	if (!Table) { AddWarning(TEXT("DT_Spells not imported yet; skipped.")); return true; }
	const FSpellRow* Blast = Table->FindRow<FSpellRow>(TEXT("Blast"), TEXT("Test"), false);
	if (!TestNotNull(TEXT("Blast row exists"), Blast)) return false;
	TestTrue(TEXT("Blast targeting"), Blast->Targeting == ESpellTargeting::Projectile);
	TestEqual(TEXT("Blast mana"), Blast->ManaCost, 5.f);
	TestEqual(TEXT("Blast range"), Blast->RangeRaw, 10.f);
	TestEqual(TEXT("Blast radius"), Blast->Radius, 300.f);
	TestEqual(TEXT("Blast damage"), Blast->Damage, 40.f);
	TestEqual(TEXT("Blast knockback"), Blast->Knockback, 1.f);
	TestTrue(TEXT("Blast homing"), Blast->bHoming);
	TestTrue(TEXT("Blast effect"), Blast->EffectId == FName(TEXT("FireBlast")));
	TestTrue(TEXT("Blast filter"), Blast->TargetFilter == ESpellTargetFilter::AllUnits);
	TestTrue(TEXT("Blast friendly fire"), Blast->bFriendlyFire);
	return true;
}

#endif
