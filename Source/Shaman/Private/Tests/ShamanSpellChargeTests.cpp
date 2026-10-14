#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SpellComponent.h"
#include "SpellEffectDispatcherComponent.h"
#include "SpellProjectile.h"
#include "SpellRow.h"
#include "MonumentActor.h"
#include "MonumentRow.h"
#include "ProgressionComponent.h"
#include "Characters/ShamanCharacter.h"
#include "Player/ShamanPlayerController.h"
#include "UI/ShamanSpellPanel.h"
#include "Engine/DataTable.h"
#include "GameFramework/HUD.h"
#include "EngineUtils.h"
#include "ShamanTribeTestWorld.h"

/**
 * Spell panel slice: spell charges, shared mana charging, discovery and temporary (monument) charges.
 *  - "Bare" tests: a plain actor with the real USpellComponent + USpellEffectDispatcherComponent and a runtime spell
 *    table (exact numbers: 4 mana/s generation in 0.25 s steps = 1 mana per step).
 *  - "Production" tests: real AShamanCharacter / AMonumentActor / AShamanPlayerController on the production spell
 *    table. They need Content/Data/Spells.csv re-imported with the charge columns (they say so when it is not).
 */
namespace ShamanSpellChargeTestsPrivate
{
	struct FChargeTestWorld
	{
		UWorld* World = nullptr;
		FChargeTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SpellChargeTestWorld"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FChargeTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			World->RemoveFromRoot();
		}
	};

	static FSpellRow MakeChargeRow(ESpellTargeting Targeting, float Cost, int32 MaxCharges, const TCHAR* EffectId,
		float Cooldown = 0.f, float RangeRaw = 100.f, bool bChargingByDefault = false)
	{
		FSpellRow R;
		R.Targeting = Targeting; R.ManaCost = Cost; R.MaxCharges = MaxCharges; R.EffectId = EffectId; R.Cooldown = Cooldown;
		R.RangeRaw = RangeRaw; R.Radius = 300.f; R.bChargingByDefault = bChargingByDefault;
		return R;
	}

	/** Blast (5 mana, 4 charges, charging by default) and Convert (5, 4) mirror Spells.csv; the others are test spells. */
	static UDataTable* MakeChargeTable()
	{
		UDataTable* T = NewObject<UDataTable>();
		T->RowStruct = FSpellRow::StaticStruct();
		auto Add = [T](const TCHAR* Id, FSpellRow R) { R.DisplayName = FText::FromString(Id); T->AddRow(FName(Id), R); };
		Add(TEXT("Blast"),   MakeChargeRow(ESpellTargeting::Projectile, 5.f, 4, TEXT("FireBlast"), 1.f, 10.f, true));
		Add(TEXT("Convert"), MakeChargeRow(ESpellTargeting::Ground, 5.f, 4, TEXT("ConvertWildmen"), 2.f, 100.f));
		Add(TEXT("Heavy"),   MakeChargeRow(ESpellTargeting::Projectile, 10.f, 2, TEXT("FireBlast")));
		Add(TEXT("Quick"),   MakeChargeRow(ESpellTargeting::Projectile, 5.f, 3, TEXT("FireBlast"))); // no cooldown
		Add(TEXT("One"),     MakeChargeRow(ESpellTargeting::Projectile, 1.f, 1, TEXT("FireBlast")));
		Add(TEXT("Two"),     MakeChargeRow(ESpellTargeting::Projectile, 1.f, 2, TEXT("FireBlast")));
		Add(TEXT("Three"),   MakeChargeRow(ESpellTargeting::Projectile, 1.f, 3, TEXT("FireBlast")));
		Add(TEXT("Four"),    MakeChargeRow(ESpellTargeting::Projectile, 1.f, 4, TEXT("FireBlast")));
		Add(TEXT("Nine"),    MakeChargeRow(ESpellTargeting::Projectile, 1.f, 9, TEXT("FireBlast"))); // clamped to 4
		Add(TEXT("Swarm"),   MakeChargeRow(ESpellTargeting::Ground, 50.f, 3, TEXT("InsectSwarm"), 4.f, 30.f)); // no effect handler
		FSpellRow Arm = MakeChargeRow(ESpellTargeting::Ground, 0.f, 0, TEXT("Armageddon"));
		Arm.bSuperSpell = true;
		Add(TEXT("Armageddon"), Arm);
		return T;
	}

	struct FChargeCaster { AActor* Actor = nullptr; USpellComponent* Spells = nullptr; USpellEffectDispatcherComponent* Dispatcher = nullptr; };

	/** Charge-economy caster: RegenPerSecond mana/s, 0.25 s steps, empty pool (Max 1000). */
	static FChargeCaster SpawnChargeCaster(UWorld* World, UDataTable* Table, float RegenPerSecond = 4.f)
	{
		FChargeCaster C;
		C.Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector::ZeroVector));
		USceneComponent* Root = NewObject<USceneComponent>(C.Actor, TEXT("Root"));
		C.Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		C.Spells = NewObject<USpellComponent>(C.Actor, TEXT("Spells"));
		C.Spells->SpellTable = Table;
		C.Spells->bUseChargeEconomy = true;
		C.Spells->ChargeStepSeconds = 0.25f;
		C.Spells->BaseRegen = RegenPerSecond;
		C.Spells->RegenPerFollower = 0.f;
		C.Spells->FollowerCount = 0;
		C.Spells->TemporaryModifier = 1.f;
		C.Spells->Mana = 0.f;
		C.Spells->MaxMana = 1000.f;
		C.Spells->ProjectileClasses.Add(TEXT("FireBlast"), ASpellProjectile::StaticClass());
		C.Spells->RegisterComponent();
		C.Dispatcher = NewObject<USpellEffectDispatcherComponent>(C.Actor, TEXT("Dispatcher"));
		C.Dispatcher->RegisterComponent();
		return C;
	}

	static float ProgressMana(const USpellComponent* S, FName Id)
	{
		const FSpellChargeState* St = S->GetChargeState(Id);
		return St ? St->ProgressMana : -1.f;
	}

	/** Mana held in charges + partial progress (used to check that charging never creates mana). */
	static float ManaInCharges(const USpellComponent* S)
	{
		float Sum = 0.f;
		for (const FName& Id : S->KnownSpellIds)
			if (const FSpellRow* Row = S->GetSpellRow(Id))
				Sum += S->GetStoredCharges(Id) * Row->ManaCost + FMath::Max(0.f, ProgressMana(S, Id));
		return Sum;
	}

	static const FShamanSpellSlot* FindSlotPtr(const FShamanSpellPanelModel& M, FName Id)
	{
		const int32 I = M.FindSlot(Id);
		return M.Slots.IsValidIndex(I) ? &M.Slots[I] : nullptr;
	}

	static int32 CountProjectiles(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<ASpellProjectile> It(World); It; ++It) ++N;
		return N;
	}

	static const FName Blast(TEXT("Blast")), Convert(TEXT("Convert")), Heavy(TEXT("Heavy")), Quick(TEXT("Quick")),
		Swarm(TEXT("Swarm")), Armageddon(TEXT("Armageddon"));
}

#define SHAMAN_CHARGE_TEST(Class, Name) IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, Name, EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// 2, 3: right-click toggling; only learned, implemented, chargeable permanent spells can charge.
SHAMAN_CHARGE_TEST(FShamanChargeToggleTest, "Shaman.Spells.Charges.ToggleCharging")
bool FShamanChargeToggleTest::RunTest(const FString& Parameters)
{
	using namespace ShamanSpellChargeTestsPrivate;
	FChargeTestWorld TW;
	FChargeCaster C = SpawnChargeCaster(TW.World, MakeChargeTable());
	USpellComponent* S = C.Spells;
	S->LearnSpell(Blast);
	S->LearnSpell(Convert);
	TestTrue(TEXT("Blast charges by default"), S->IsCharging(Blast));
	TestFalse(TEXT("A newly learned spell starts idle"), S->IsCharging(Convert));
	TestTrue(TEXT("Right-click: Convert charging on"), S->ToggleSpellCharging(Convert) && S->IsCharging(Convert));
	TestEqual(TEXT("Two spells draw mana"), S->GetActiveChargingCount(), 2);
	TestTrue(TEXT("Right-click again: Convert charging off"), S->ToggleSpellCharging(Convert) && !S->IsCharging(Convert));
	TestEqual(TEXT("One spell draws mana"), S->GetActiveChargingCount(), 1);

	TestFalse(TEXT("Locked (not learned) spell cannot charge"), S->SetSpellCharging(Heavy, true));
	S->SetSpellAvailableOnMap(Heavy);
	TestFalse(TEXT("Undiscovered (\"?\") spell cannot charge"), S->SetSpellCharging(Heavy, true));
	S->LearnSpell(Swarm);
	TestFalse(TEXT("Spell without an effect cannot charge"), S->SetSpellCharging(Swarm, true));
	S->AddCharges(Quick, 2);
	TestFalse(TEXT("Temporary-only spell cannot charge"), S->SetSpellCharging(Quick, true));
	S->AddCharges(Armageddon, 1);
	TestFalse(TEXT("Super spell (no permanent charges) cannot charge"), S->SetSpellCharging(Armageddon, true));
	TestTrue(TEXT("None of them is charging"), !S->IsCharging(Heavy) && !S->IsCharging(Swarm) && !S->IsCharging(Quick) && !S->IsCharging(Armageddon));

	S->AdvanceChargeEconomy(2.f);
	TestTrue(TEXT("Ineligible spells never progress"), S->GetStoredCharges(Heavy) == 0 && S->GetChargeProgress01(Swarm) == 0.f
		&& S->GetStoredCharges(Swarm) == 0 && S->GetStoredCharges(Quick) == 0 && S->GetTemporaryCharges(Quick) == 2);
	TestEqual(TEXT("Only Blast took mana (2 s x 4/s = 8 = one charge + 3)"), ManaInCharges(S), 8.f, 1e-3f);

	const FShamanSpellPanelModel M = FShamanSpellPanelModel::Build(S, C.Dispatcher, NAME_None);
	auto CanToggle = [&M](FName Id) { const int32 I = M.FindSlot(Id); return I != INDEX_NONE && M.Slots[I].bCanToggleCharging; };
	TestTrue(TEXT("Panel: Blast and Convert accept right-click"), CanToggle(Blast) && CanToggle(Convert));
	TestFalse(TEXT("Panel: unimplemented / temporary / undiscovered tiles do not"), CanToggle(Swarm) || CanToggle(Quick) || CanToggle(Heavy));

	S->bUseChargeEconomy = false;
	TestFalse(TEXT("No toggling when the charge economy is off"), S->ToggleSpellCharging(Blast));
	return true;
}

// 4, 8, 11: progress needs charging + generation; partial progress is kept; full spells stop drawing mana.
SHAMAN_CHARGE_TEST(FShamanChargeProgressTest, "Shaman.Spells.Charges.ProgressAndPersistence")
bool FShamanChargeProgressTest::RunTest(const FString& Parameters)
{
	using namespace ShamanSpellChargeTestsPrivate;
	FChargeTestWorld TW;
	FChargeCaster C = SpawnChargeCaster(TW.World, MakeChargeTable());
	USpellComponent* S = C.Spells;
	S->LearnSpell(Blast);
	TestEqual(TEXT("Learned with zero charges"), S->GetStoredCharges(Blast), 0);
	S->AdvanceChargeEconomy(1.25f); // 5 steps x 1 mana = one 5-mana charge
	TestEqual(TEXT("Blast: first charge after 1.25 s"), S->GetStoredCharges(Blast), 1);
	TestEqual(TEXT("All generation went into the charge"), S->Mana, 0.f, 1e-3f);

	S->LearnSpell(Convert); // idle
	S->AdvanceChargeEconomy(1.f);
	TestEqual(TEXT("Idle Convert: no progress"), ProgressMana(S, Convert), 0.f, 1e-4f);
	TestEqual(TEXT("Charging Blast: 4 of 5 toward charge 2"), ProgressMana(S, Blast), 4.f, 1e-3f);

	S->SetSpellCharging(Blast, false);
	S->AdvanceChargeEconomy(1.f);
	TestEqual(TEXT("Switched off: progress kept, not advancing"), ProgressMana(S, Blast), 4.f, 1e-3f);
	TestEqual(TEXT("Switched off: no mana drawn (it stays in the pool)"), S->Mana, 4.f, 1e-3f);
	TestEqual(TEXT("Switched off: no draw reported"), S->GetChargeDrawPerSecond(), 0.f);
	S->SetSpellCharging(Blast, true);
	S->AdvanceChargeEconomy(0.25f);
	TestTrue(TEXT("Switched on again: resumes from 4/5 and completes charge 2 in one step"),
		S->GetStoredCharges(Blast) == 2 && FMath::IsNearlyZero(ProgressMana(S, Blast), 1e-3f));

	const float BaseRegen = S->BaseRegen;
	S->BaseRegen = 0.f; // no followers, no generation
	const float ManaBefore = S->Mana;
	S->AdvanceChargeEconomy(5.f);
	TestTrue(TEXT("Zero generation: no progress and no mana created"), S->GetStoredCharges(Blast) == 2
		&& FMath::IsNearlyZero(ProgressMana(S, Blast), 1e-3f) && FMath::IsNearlyEqual(S->Mana, ManaBefore, 1e-3f));
	S->BaseRegen = BaseRegen;

	S->SetStoredChargesForTest(Blast, 4);
	const float ManaFull = S->Mana;
	S->AdvanceChargeEconomy(1.f);
	TestEqual(TEXT("Full: stays at 4"), S->GetStoredCharges(Blast), 4);
	TestEqual(TEXT("Full: no progress past the maximum"), S->GetChargeProgress01(Blast), 0.f);
	TestEqual(TEXT("Full: all generation stays in the pool"), S->Mana, ManaFull + 4.f, 1e-3f);
	TestEqual(TEXT("Full: not counted as charging"), S->GetActiveChargingCount(), 0);
	TestTrue(TEXT("Full: charging setting itself is kept"), S->IsCharging(Blast));
	return true;
}

// 5, 6: one shared pool; more spells charge slower; cost decides charge time; deterministic and frame-rate independent.
SHAMAN_CHARGE_TEST(FShamanChargeSharedPoolTest, "Shaman.Spells.Charges.SharedManaPool")
bool FShamanChargeSharedPoolTest::RunTest(const FString& Parameters)
{
	using namespace ShamanSpellChargeTestsPrivate;
	FChargeTestWorld TW;
	UDataTable* Table = MakeChargeTable();

	FChargeCaster Alone = SpawnChargeCaster(TW.World, Table);
	Alone.Spells->LearnSpell(Blast);
	Alone.Spells->AdvanceChargeEconomy(1.25f);
	TestEqual(TEXT("Blast alone: 1 charge at 1.25 s"), Alone.Spells->GetStoredCharges(Blast), 1);

	FChargeCaster Two = SpawnChargeCaster(TW.World, Table);
	USpellComponent* S2 = Two.Spells;
	S2->LearnSpell(Blast);
	S2->LearnSpell(Convert);
	S2->SetSpellCharging(Convert, true);
	S2->AdvanceChargeEconomy(1.25f);
	TestTrue(TEXT("Blast + Convert: no charge yet at 1.25 s (half the mana each)"),
		S2->GetStoredCharges(Blast) == 0 && FMath::IsNearlyEqual(ProgressMana(S2, Blast), 2.5f, 1e-3f) && FMath::IsNearlyEqual(ProgressMana(S2, Convert), 2.5f, 1e-3f));
	TestEqual(TEXT("Each gets half the generation"), S2->GetChargeState(Blast)->LastManaPerSecond, 2.f, 1e-3f);
	S2->AdvanceChargeEconomy(1.25f);
	TestTrue(TEXT("Blast + Convert: both charged at 2.5 s"), S2->GetStoredCharges(Blast) == 1 && S2->GetStoredCharges(Convert) == 1);
	TestEqual(TEXT("Shared pool: nothing created (charges = generated mana)"), ManaInCharges(S2) + S2->Mana, 4.f * 2.5f, 1e-3f);

	FChargeCaster HeavyAlone = SpawnChargeCaster(TW.World, Table);
	HeavyAlone.Spells->LearnSpell(Heavy);
	HeavyAlone.Spells->SetSpellCharging(Heavy, true);
	HeavyAlone.Spells->AdvanceChargeEconomy(2.25f);
	TestEqual(TEXT("10-mana spell alone: not ready at 2.25 s"), HeavyAlone.Spells->GetStoredCharges(Heavy), 0);
	HeavyAlone.Spells->AdvanceChargeEconomy(0.25f);
	TestEqual(TEXT("10-mana spell alone: ready at 2.5 s (5-mana Blast took 1.25 s)"), HeavyAlone.Spells->GetStoredCharges(Heavy), 1);

	FChargeCaster Mixed = SpawnChargeCaster(TW.World, Table);
	USpellComponent* SM = Mixed.Spells;
	SM->LearnSpell(Blast);
	SM->LearnSpell(Heavy);
	SM->SetSpellCharging(Heavy, true);
	SM->AdvanceChargeEconomy(2.5f);
	TestTrue(TEXT("Same share, different cost: Blast charged, Heavy half way at 2.5 s"),
		SM->GetStoredCharges(Blast) == 1 && SM->GetStoredCharges(Heavy) == 0 && FMath::IsNearlyEqual(ProgressMana(SM, Heavy), 5.f, 1e-3f));
	SM->AdvanceChargeEconomy(2.5f);
	TestTrue(TEXT("At 5 s: Blast 2, Heavy 1"), SM->GetStoredCharges(Blast) == 2 && SM->GetStoredCharges(Heavy) == 1);

	FChargeCaster Fill = SpawnChargeCaster(TW.World, Table);
	USpellComponent* SF = Fill.Spells;
	SF->LearnSpell(Blast);
	SF->SetStoredChargesForTest(Blast, 3);
	SF->LearnSpell(Convert);
	SF->SetSpellCharging(Convert, true);
	SF->AdvanceChargeEconomy(2.5f);
	TestTrue(TEXT("At 2.5 s Blast is full and Convert has 1"), SF->GetStoredCharges(Blast) == 4 && SF->GetStoredCharges(Convert) == 1);
	SF->AdvanceChargeEconomy(1.25f);
	TestEqual(TEXT("Once Blast is full Convert gets all the mana (2 charges at 3.75 s)"), SF->GetStoredCharges(Convert), 2);

	// Same game time in different frame slices -> identical state.
	const float Slices[] = { 0.0625f, 0.25f, 0.5f, 5.f };
	FString Reference;
	for (float Dt : Slices)
	{
		FChargeCaster F = SpawnChargeCaster(TW.World, Table);
		F.Spells->LearnSpell(Blast);
		F.Spells->LearnSpell(Heavy);
		F.Spells->SetSpellCharging(Heavy, true);
		for (int32 i = 0, N = FMath::RoundToInt(5.f / Dt); i < N; ++i) F.Spells->AdvanceChargeEconomy(Dt);
		const FString State = FString::Printf(TEXT("%d %.4f %d %.4f %.4f"), F.Spells->GetStoredCharges(Blast), ProgressMana(F.Spells, Blast),
			F.Spells->GetStoredCharges(Heavy), ProgressMana(F.Spells, Heavy), F.Spells->Mana);
		if (Reference.IsEmpty()) Reference = State;
		TestEqual(*FString::Printf(TEXT("Frame slice %.4f s gives the same result"), Dt), State, Reference);
	}
	return true;
}

// 7: maximum charges 1-4 are enforced (and values above 4 are clamped).
SHAMAN_CHARGE_TEST(FShamanChargeMaxTest, "Shaman.Spells.Charges.MaxCharges")
bool FShamanChargeMaxTest::RunTest(const FString& Parameters)
{
	using namespace ShamanSpellChargeTestsPrivate;
	FChargeTestWorld TW;
	FChargeCaster C = SpawnChargeCaster(TW.World, MakeChargeTable(), 40.f); // 10 mana per step
	USpellComponent* S = C.Spells;
	const TCHAR* Ids[] = { TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four"), TEXT("Nine") };
	for (const TCHAR* Id : Ids) { S->LearnSpell(Id); S->SetSpellCharging(Id, true); }
	S->AdvanceChargeEconomy(10.f);
	TestEqual(TEXT("Max 1: never a second charge"), S->GetStoredCharges(TEXT("One")), 1);
	TestEqual(TEXT("Max 2"), S->GetStoredCharges(TEXT("Two")), 2);
	TestEqual(TEXT("Max 3"), S->GetStoredCharges(TEXT("Three")), 3);
	TestEqual(TEXT("Max 4: stops at 4"), S->GetStoredCharges(TEXT("Four")), 4);
	TestEqual(TEXT("Row value 9 is clamped to 4"), S->GetMaxCharges(TEXT("Nine")), 4);
	TestEqual(TEXT("Clamped spell stops at 4"), S->GetStoredCharges(TEXT("Nine")), 4);
	TestEqual(TEXT("Exactly 14 mana went into charges, the rest is in the pool"), S->Mana, 400.f - 14.f, 1e-2f);
	bool bNoExcess = true;
	for (const TCHAR* Id : Ids) bNoExcess &= S->GetChargeProgress01(Id) == 0.f && FMath::IsNearlyZero(ProgressMana(S, Id));
	TestTrue(TEXT("No progress beyond the maximum"), bNoExcess);
	return true;
}

// 9, 10: a successful cast uses exactly one charge (no mana); failed casts use nothing; the charge comes back.
SHAMAN_CHARGE_TEST(FShamanChargeCastTest, "Shaman.Spells.Charges.CastConsumesCharge")
bool FShamanChargeCastTest::RunTest(const FString& Parameters)
{
	using namespace ShamanSpellChargeTestsPrivate;
	FChargeTestWorld TW;
	FChargeCaster C = SpawnChargeCaster(TW.World, MakeChargeTable());
	USpellComponent* S = C.Spells;
	S->LearnSpell(Blast);
	S->SetStoredChargesForTest(Blast, 2);
	S->Mana = 50.f;
	const int32 ProjectilesBefore = CountProjectiles(TW.World);
	TestTrue(TEXT("Blast casts with a charge"), S->CastSpell(Blast, FVector(1000.f, 0.f, 0.f)));
	TestEqual(TEXT("Exactly one charge used"), S->GetStoredCharges(Blast), 1);
	TestEqual(TEXT("No mana spent at cast time (paid while charging)"), S->Mana, 50.f);
	TestEqual(TEXT("TAKA projectile spawned as before"), CountProjectiles(TW.World), ProjectilesBefore + 1);
	TestFalse(TEXT("Recast inside the 1 s cooldown fails"), S->CastSpell(Blast, FVector(1000.f, 0.f, 0.f)));
	TestTrue(TEXT("... reported as cooldown, no charge used"), S->LastCastResult == ESpellCastResult::OnCooldown && S->GetStoredCharges(Blast) == 1);

	S->LearnSpell(Convert);
	TestFalse(TEXT("No charge: cast fails"), S->CastSpell(Convert, FVector(2000.f, 0.f, 0.f)));
	TestTrue(TEXT("... as NoCharges"), S->LastCastResult == ESpellCastResult::NoCharges);
	S->SetStoredChargesForTest(Convert, 1);
	TestFalse(TEXT("Out of range (6000 uu > 5000) fails"), S->CastSpell(Convert, FVector(6000.f, 0.f, 0.f)));
	TestTrue(TEXT("... no charge used"), S->LastCastResult == ESpellCastResult::OutOfRange && S->GetStoredCharges(Convert) == 1);
	TestTrue(TEXT("In range: Convert casts"), S->CastSpell(Convert, FVector(2000.f, 0.f, 0.f)));
	TestEqual(TEXT("... using its only charge"), S->GetStoredCharges(Convert), 0);
	TestFalse(TEXT("Unknown spell fails"), S->CastSpell(Heavy, FVector(1000.f, 0.f, 0.f)));
	TestTrue(TEXT("... as UnknownSpell"), S->LastCastResult == ESpellCastResult::UnknownSpell);
	TestEqual(TEXT("Failed casts never touched mana"), S->Mana, 50.f);

	S->AdvanceChargeEconomy(1.25f); // Blast (charging) refills the spent charge; Convert is idle
	TestEqual(TEXT("Spent Blast charge recharges"), S->GetStoredCharges(Blast), 2);
	TestEqual(TEXT("Idle Convert stays empty"), S->GetStoredCharges(Convert), 0);
	return true;
}

// 12, 13: "?" only for spells this map offers; learning reveals the spell and leaves the other spells alone.
SHAMAN_CHARGE_TEST(FShamanChargeDiscoveryTest, "Shaman.Spells.Charges.DiscoveryAndLearning")
bool FShamanChargeDiscoveryTest::RunTest(const FString& Parameters)
{
	using namespace ShamanSpellChargeTestsPrivate;
	FChargeTestWorld TW;
	FChargeCaster C = SpawnChargeCaster(TW.World, MakeChargeTable());
	USpellComponent* S = C.Spells;
	S->LearnSpell(Blast);
	S->AdvanceChargeEconomy(1.75f); // 7 steps: 1 charge + 2/5
	const int32 BlastCharges = S->GetStoredCharges(Blast);
	const float BlastProgress = ProgressMana(S, Blast);
	TestTrue(TEXT("Blast has 1 charge and 2/5 progress"), BlastCharges == 1 && FMath::IsNearlyEqual(BlastProgress, 2.f, 1e-3f));

	FShamanSpellPanelModel M = FShamanSpellPanelModel::Build(S, C.Dispatcher, Blast);
	int32 I = M.FindSlot(Convert);
	TestTrue(TEXT("Convert not offered by this map: locked blank tile, never \"?\""), I != INDEX_NONE && M.Slots[I].State == EShamanSpellSlotState::Locked);
	TestEqual(TEXT("Future spell without an effect is not listed"), M.FindSlot(Swarm), (int32)INDEX_NONE);
	TestEqual(TEXT("Super spell without charges is not listed"), M.FindSlot(Armageddon), (int32)INDEX_NONE);

	S->SetSpellAvailableOnMap(Convert);
	M = FShamanSpellPanelModel::Build(S, C.Dispatcher, Blast);
	I = M.FindSlot(Convert);
	if (!TestTrue(TEXT("Map offers Convert: \"?\" tile"), I != INDEX_NONE && M.Slots[I].State == EShamanSpellSlotState::Discoverable)) return false;
	TestTrue(TEXT("\"?\" reveals no name or icon"), M.Slots[I].DisplayName.IsEmpty() && M.Slots[I].Monogram.IsEmpty() && M.Slots[I].Icon == nullptr);
	TestFalse(TEXT("\"?\" cannot charge"), M.Slots[I].bCanToggleCharging);

	S->LearnSpell(Convert);
	M = FShamanSpellPanelModel::Build(S, C.Dispatcher, Blast);
	const FShamanSpellSlot* Learned = FindSlotPtr(M, Convert);
	if (!TestTrue(TEXT("Learned: the real tile replaces the \"?\""), Learned && Learned->State == EShamanSpellSlotState::Available
		&& Learned->DisplayName.ToString() == TEXT("Convert") && Learned->bPermanent)) return false;
	TestTrue(TEXT("Learned: zero charges, charging off"), S->GetStoredCharges(Convert) == 0 && !S->IsCharging(Convert)
		&& FMath::IsNearlyZero(ProgressMana(S, Convert)));
	TestTrue(TEXT("Learning Convert left Blast's charges, progress and charging alone"), S->GetStoredCharges(Blast) == BlastCharges
		&& FMath::IsNearlyEqual(ProgressMana(S, Blast), BlastProgress) && S->IsCharging(Blast));
	S->LearnSpell(Blast);
	TestTrue(TEXT("Learning a known spell again resets nothing"), S->GetStoredCharges(Blast) == BlastCharges
		&& FMath::IsNearlyEqual(ProgressMana(S, Blast), BlastProgress) && S->IsCharging(Blast));
	const FShamanSpellSlot* BlastSlot = FindSlotPtr(M, Blast);
	TestTrue(TEXT("Panel keeps Blast selected"), BlastSlot && BlastSlot->bSelected && !Learned->bSelected);
	return true;
}

// 14, 15, 16: temporary charges are finite, never charge, coexist with permanent ones and run out cleanly.
SHAMAN_CHARGE_TEST(FShamanChargeTemporaryTest, "Shaman.Spells.Charges.TemporaryCharges")
bool FShamanChargeTemporaryTest::RunTest(const FString& Parameters)
{
	using namespace ShamanSpellChargeTestsPrivate;
	FChargeTestWorld TW;
	FChargeCaster C = SpawnChargeCaster(TW.World, MakeChargeTable());
	USpellComponent* S = C.Spells;
	const FVector Aim(1000.f, 0.f, 0.f);

	S->AddCharges(Quick, 2); // monument reward for a spell the player does not know
	TestTrue(TEXT("Temporary spell is castable"), S->KnownSpellIds.Contains(Quick) && S->GetAvailableCharges(Quick) == 2);
	TestTrue(TEXT("... but not learned permanently and not charging"), !S->IsPermanentSpell(Quick) && !S->IsCharging(Quick) && !S->CanToggleCharging(Quick));
	S->AdvanceChargeEconomy(2.f);
	TestTrue(TEXT("Temporary charges never recharge or draw mana"), S->GetTemporaryCharges(Quick) == 2 && FMath::IsNearlyEqual(S->Mana, 8.f, 1e-3f)
		&& S->GetActiveChargingCount() == 0);
	FShamanSpellPanelModel M = FShamanSpellPanelModel::Build(S, C.Dispatcher, NAME_None);
	int32 I = M.FindSlot(Quick);
	TestTrue(TEXT("Panel: temporary tile with 2 red charges, no permanent slots"), I != INDEX_NONE && M.Slots[I].State == EShamanSpellSlotState::Available
		&& M.Slots[I].IsTemporaryOnly() && M.Slots[I].TemporaryCharges == 2 && M.Slots[I].StoredCharges == 0 && !M.Slots[I].bPermanent);

	TestTrue(TEXT("Cast 1 uses a temporary charge"), S->CastSpell(Quick, Aim) && S->GetTemporaryCharges(Quick) == 1);
	TestTrue(TEXT("Cast 2 uses the last one"), S->CastSpell(Quick, Aim) && S->GetTemporaryCharges(Quick) == 0);
	TestFalse(TEXT("Exhausted temporary-only spell is gone from the spellbook"), S->KnownSpellIds.Contains(Quick));
	M = FShamanSpellPanelModel::Build(S, C.Dispatcher, NAME_None);
	I = M.FindSlot(Quick);
	TestTrue(TEXT("... and its slot returns to its empty (locked) state"), I != INDEX_NONE && M.Slots[I].State == EShamanSpellSlotState::Locked);
	TestFalse(TEXT("... and cannot be cast"), S->CastSpell(Quick, Aim));
	TestFalse(TEXT("Temporary charges never made it permanent"), S->IsPermanentSpell(Quick));

	// Permanent + temporary on the same spell.
	S->LearnSpell(Quick);
	S->SetStoredChargesForTest(Quick, 2);
	S->AddCharges(Quick, 2);
	S->LearnSpell(Quick); // a second learn (e.g. Vault) must not wipe either source
	TestTrue(TEXT("Both sources kept: 2 permanent + 2 temporary"), S->GetStoredCharges(Quick) == 2 && S->GetTemporaryCharges(Quick) == 2
		&& S->GetAvailableCharges(Quick) == 4);
	TestTrue(TEXT("Temporary charges are used first"), S->CastSpell(Quick, Aim) && S->GetTemporaryCharges(Quick) == 1 && S->GetStoredCharges(Quick) == 2);
	TestTrue(TEXT("Last temporary charge"), S->CastSpell(Quick, Aim) && S->GetTemporaryCharges(Quick) == 0 && S->GetStoredCharges(Quick) == 2);
	TestTrue(TEXT("Exhausting temporary charges leaves the permanent spell intact"), S->IsPermanentSpell(Quick) && S->KnownSpellIds.Contains(Quick)
		&& S->GetMaxCharges(Quick) == 3);
	TestTrue(TEXT("Then permanent charges are used"), S->CastSpell(Quick, Aim) && S->GetStoredCharges(Quick) == 1);

	S->SetSpellCharging(Quick, true);
	S->AddCharges(Quick, 1);
	const float Before = ProgressMana(S, Quick);
	S->AdvanceChargeEconomy(0.5f);
	TestTrue(TEXT("Permanent progress continues while a temporary charge waits"), ProgressMana(S, Quick) > Before && S->GetTemporaryCharges(Quick) == 1);
	M = FShamanSpellPanelModel::Build(S, C.Dispatcher, NAME_None);
	I = M.FindSlot(Quick);
	TestTrue(TEXT("Panel shows both: blue permanent + red temporary"), I != INDEX_NONE && M.Slots[I].bPermanent && M.Slots[I].StoredCharges == 1
		&& M.Slots[I].TemporaryCharges == 1 && M.Slots[I].MaxCharges == 3 && !M.Slots[I].IsTemporaryOnly());
	return true;
}

// 1 (+ the unlock cheat): production Shaman at level start.
SHAMAN_CHARGE_TEST(FShamanChargeInitialStateTest, "Shaman.Spells.Charges.InitialLevelState")
bool FShamanChargeInitialStateTest::RunTest(const FString& Parameters)
{
	using namespace ShamanSpellChargeTestsPrivate;
	using namespace ShamanTribeTestsPrivate;
	FTribeTestWorld W;
	if (!TestTrue(TEXT("Production spell table has the charge columns (reimport Content/Data/Spells.csv into the Spells DataTable)"),
		USpellComponent::TableHasChargeData(W.Data->SpellTable))) return false;
	AShamanCharacter* S0 = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 0, FVector(0.f, 0.f, 100.f));
	AShamanCharacter* S1 = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 1, FVector(9000.f, 0.f, 100.f));
	USpellComponent* S = S0->Spells;
	TestTrue(TEXT("Player Shaman uses spell charges"), S->bUseChargeEconomy && !S0->IsSpellChargeDataMissing());
	TestFalse(TEXT("AI Shaman keeps the Phase 1 mana-at-cast rule"), S1->Spells->bUseChargeEconomy);
	TestTrue(TEXT("Only Blast is learned"), S->KnownSpellIds.Num() == 1 && S->KnownSpellIds[0] == Blast && S->IsPermanentSpell(Blast));
	TestTrue(TEXT("Blast is charging, with no stored charge yet"), S->IsCharging(Blast) && S->GetStoredCharges(Blast) == 0);
	TestEqual(TEXT("Blast holds 4 charges"), S->GetMaxCharges(Blast), 4);
	TestEqual(TEXT("Exactly one spell draws mana"), S->GetActiveChargingCount(), 1);
	bool bOthersIdle = true;
	for (const FName& Id : S->SpellTable->GetRowNames())
		if (Id != Blast) bOthersIdle &= !S->IsCharging(Id) && !S->CanToggleCharging(Id) && !S->IsPermanentSpell(Id);
	TestTrue(TEXT("Every other spell is unlearned and idle"), bOthersIdle);
	TestTrue(TEXT("Blast selected"), S0->GetSelectedSpellId() == Blast);

	FShamanSpellPanelModel M = FShamanSpellPanelModel::Build(S, S0->EffectDispatcher, S0->GetSelectedSpellId());
	int32 NumDiscoverable = 0, NumAvailable = 0;
	for (const FShamanSpellSlot& Slot : M.Slots)
	{
		NumDiscoverable += Slot.State == EShamanSpellSlotState::Discoverable ? 1 : 0;
		NumAvailable += Slot.State == EShamanSpellSlotState::Available ? 1 : 0;
	}
	TestEqual(TEXT("No \"?\" without a map offering the spell"), NumDiscoverable, 0);
	TestEqual(TEXT("One usable tile (Blast)"), NumAvailable, 1);

	S->FollowerCount = 0; // regen = BaseRegen
	S->AdvanceChargeEconomy(1.f);
	TestTrue(TEXT("Blast progresses from the shared mana"), S->GetChargeProgress01(Blast) > 0.f || S->GetStoredCharges(Blast) > 0);

	// Test cheat: unlock everything that can be learned permanently, charges full.
	S0->ShamanUnlockAllSpells();
	bool bAllFull = true;
	int32 NumChargeable = 0;
	for (const FName& Id : S->SpellTable->GetRowNames())
	{
		const FSpellRow* Row = S->GetSpellRow(Id);
		if (!Row || !FSpellChargeRules::IsChargeable(*Row)) continue;
		++NumChargeable;
		bAllFull &= S->IsPermanentSpell(Id) && S->GetStoredCharges(Id) == FSpellChargeRules::GetMaxCharges(*Row);
	}
	TestTrue(TEXT("Cheat: every permanent spell learned and full"), NumChargeable > 1 && bAllFull);
	TestTrue(TEXT("Cheat: charging settings untouched (Blast on, Convert off)"), S->IsCharging(Blast) && !S->IsCharging(Convert));
	TestTrue(TEXT("Cheat: selection unchanged"), S0->GetSelectedSpellId() == Blast);
	M = FShamanSpellPanelModel::Build(S, S0->EffectDispatcher, S0->GetSelectedSpellId());
	bool bHonest = true;
	for (const FShamanSpellSlot& Slot : M.Slots)
		if (!Slot.bImplemented) bHonest &= Slot.State == EShamanSpellSlotState::Unimplemented && !Slot.bCanToggleCharging;
	TestTrue(TEXT("Cheat: spells without an effect still show as not implemented"), bHonest);

	const FSpellRow* BlastRow = S->GetSpellRow(Blast);
	TestTrue(TEXT("TAKA row unchanged"), BlastRow && BlastRow->ManaCost == 5.f && BlastRow->RangeRaw == 10.f && BlastRow->Damage == 40.f
		&& BlastRow->Knockback == 1.f && BlastRow->bHoming && BlastRow->EffectId == FName(TEXT("FireBlast")));
	return true;
}

// 14 / 12 through the real monument code (Vault of Knowledge -> learn + discovery, Stone Head -> temporary charges).
SHAMAN_CHARGE_TEST(FShamanChargeMonumentTest, "Shaman.Spells.Charges.MonumentRewards")
bool FShamanChargeMonumentTest::RunTest(const FString& Parameters)
{
	using namespace ShamanSpellChargeTestsPrivate;
	using namespace ShamanTribeTestsPrivate;
	FTribeTestWorld W;
	if (!TestTrue(TEXT("Production spell table has the charge columns (reimport Content/Data/Spells.csv)"),
		USpellComponent::TableHasChargeData(W.Data->SpellTable))) return false;
	AShamanCharacter* S0 = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 0, FVector(0.f, 0.f, 100.f));
	USpellComponent* S = S0->Spells;

	UDataTable* MT = NewObject<UDataTable>();
	MT->RowStruct = FMonumentRow::StaticStruct();
	FMonumentRow Vault;
	Vault.MonumentType = EMonumentType::VaultOfKnowledge; Vault.WorshipRule = EWorshipRule::ShamanOnly; Vault.WorshipRequired = 30.f;
	Vault.ProgressPerWorshipperPerSec = 1.f; Vault.MaxWorshippers = 1; Vault.DefaultRewardType = EMonumentRewardType::UnlockSpell;
	Vault.bPersistsAcrossMaps = true;
	MT->AddRow(TEXT("Vault"), Vault);
	FMonumentRow Head;
	Head.MonumentType = EMonumentType::StoneHead; Head.WorshipRule = EWorshipRule::ShamanAndBraves; Head.WorshipRequired = 10.f;
	Head.ProgressPerWorshipperPerSec = 1.f; Head.MaxWorshippers = 8; Head.DefaultRewardType = EMonumentRewardType::GrantCharges;
	Head.DefaultRewardCount = 3; Head.ChargesPerCycle = 1;
	MT->AddRow(TEXT("StoneHead"), Head);
	auto SpawnMonument = [&W, MT](FName Row, FName Reward)
	{
		const FTransform At(FVector(600.f, 0.f, 100.f));
		AMonumentActor* Mon = W.World->SpawnActorDeferred<AMonumentActor>(AMonumentActor::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Mon->MonumentTable = MT;
		Mon->MonumentRowName = Row;
		Mon->RewardId = Reward;
		Mon->FinishSpawning(At);
		if (!Mon->HasActorBegunPlay()) Mon->DispatchBeginPlay();
		return Mon;
	};

	TestFalse(TEXT("Before the Vault: Convert is not offered here"), S->IsSpellAvailableOnMap(Convert));
	AMonumentActor* VaultActor = SpawnMonument(TEXT("Vault"), Convert);
	TestTrue(TEXT("A Vault teaching Convert makes it discoverable for existing Shamans"), S->IsSpellAvailableOnMap(Convert));
	AShamanCharacter* Late = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 1, FVector(9000.f, 0.f, 100.f));
	TestTrue(TEXT("... and for Shamans that spawn later"), Late->Spells->IsSpellAvailableOnMap(Convert));
	FShamanSpellPanelModel M = FShamanSpellPanelModel::Build(S, S0->EffectDispatcher, S0->GetSelectedSpellId());
	const FShamanSpellSlot* Q = FindSlotPtr(M, Convert);
	TestTrue(TEXT("Panel: \"?\" tile for Convert"), Q && Q->State == EShamanSpellSlotState::Discoverable);

	S->FollowerCount = 0;
	S->AdvanceChargeEconomy(1.f);
	const int32 BlastCharges = S->GetStoredCharges(Blast);
	const float BlastProgress = S->GetChargeProgress01(Blast);
	VaultActor->Worship(S0, S0, 31.f); // one full worship cycle
	TestTrue(TEXT("Vault worshipped: Convert learned permanently"), VaultActor->IsSpent() && S->IsPermanentSpell(Convert));
	TestTrue(TEXT("... with zero charges and charging off"), S->GetStoredCharges(Convert) == 0 && !S->IsCharging(Convert));
	TestTrue(TEXT("... carried to future maps"), S0->Progression->GetPermanentSpells().Contains(Convert));
	TestTrue(TEXT("... without touching Blast"), S->GetStoredCharges(Blast) == BlastCharges && FMath::IsNearlyEqual(S->GetChargeProgress01(Blast), BlastProgress)
		&& S->IsCharging(Blast));
	M = FShamanSpellPanelModel::Build(S, S0->EffectDispatcher, S0->GetSelectedSpellId());
	const FShamanSpellSlot* Real = FindSlotPtr(M, Convert);
	TestTrue(TEXT("Panel: the \"?\" became the Convert tile"), Real && Real->State == EShamanSpellSlotState::Available);

	AMonumentActor* HeadActor = SpawnMonument(TEXT("StoneHead"), Blast);
	for (int32 i = 0; i < 3; ++i) HeadActor->Worship(S0, S0, 11.f);
	TestTrue(TEXT("Stone Head: 3 temporary Blast charges, then spent"), S->GetTemporaryCharges(Blast) == 3 && HeadActor->IsSpent());
	TestTrue(TEXT("... permanent Blast charges untouched"), S->GetStoredCharges(Blast) == BlastCharges && S->IsPermanentSpell(Blast));
	TestEqual(TEXT("... temporary charges do not add a charging spell"), S->GetActiveChargingCount(), 1);
	return true;
}

// 17: switching control modes keeps the spellbook and never duplicates the HUD / panel.
SHAMAN_CHARGE_TEST(FShamanChargeModeSwitchTest, "Shaman.Spells.Charges.ModeSwitchKeepsSpellbook")
bool FShamanChargeModeSwitchTest::RunTest(const FString& Parameters)
{
	using namespace ShamanSpellChargeTestsPrivate;
	using namespace ShamanTribeTestsPrivate;
	FTribeTestWorld W;
	if (!TestTrue(TEXT("Production spell table has the charge columns (reimport Content/Data/Spells.csv)"),
		USpellComponent::TableHasChargeData(W.Data->SpellTable))) return false;
	AShamanCharacter* S0 = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 0, FVector(0.f, 0.f, 100.f));
	USpellComponent* S = S0->Spells;
	S->FollowerCount = 0;

	// Selection follows the spell, not the list index, when temporary spells come and go.
	S->AddCharges(Convert, 1);
	TestTrue(TEXT("Select the temporary Convert"), S0->SelectSpell(Convert) && S0->GetSelectedSpellId() == Convert);
	TestTrue(TEXT("Cast it (uses the temporary charge)"), S0->TryCastSpell(Convert, S0->GetActorLocation() + FVector(1000.f, 0.f, 0.f)));
	TestTrue(TEXT("Convert gone, selection falls back to Blast"), !S->KnownSpellIds.Contains(Convert) && S0->GetSelectedSpellId() == Blast);

	S->LearnSpell(Convert);
	S->SetSpellCharging(Convert, true);
	S->AdvanceChargeEconomy(1.f);
	S->AddCharges(Blast, 1);
	S->SetSpellAvailableOnMap(Swarm);
	S0->SelectSpell(Convert);
	auto Snapshot = [S, S0]()
	{
		FString Out;
		for (const FName& Id : S->KnownSpellIds)
			Out += FString::Printf(TEXT("%s:%d:%d:%.5f:%d:%d|"), *Id.ToString(), S->IsPermanentSpell(Id) ? 1 : 0, S->GetStoredCharges(Id),
				S->GetChargeProgress01(Id), S->IsCharging(Id) ? 1 : 0, S->GetTemporaryCharges(Id));
		for (const FName& Id : S->GetMapSpellIds()) Out += Id.ToString() + TEXT(",");
		return Out + S0->GetSelectedSpellId().ToString() + FString::Printf(TEXT(" %.4f"), S->Mana);
	};
	auto CountHUDs = [&W]() { int32 N = 0; for (TActorIterator<AHUD> It(W.World); It; ++It) ++N; return N; };
	const FString Before = Snapshot();
	const int32 SlotsBefore = FShamanSpellPanelModel::Build(S, S0->EffectDispatcher, S0->GetSelectedSpellId()).Slots.Num();

	AShamanPlayerController* PC = W.World->SpawnActor<AShamanPlayerController>();
	if (!TestNotNull(TEXT("Player controller"), PC)) return false;
	PC->CameraSettings.ModeBlendTime = 0.f;
	PC->Possess(S0);
	const int32 HUDs = CountHUDs();
	for (int32 i = 0; i < 4; ++i)
	{
		PC->ToggleControlMode();
		TestEqual(*FString::Printf(TEXT("Switch %d: spellbook unchanged"), i + 1), Snapshot(), Before);
		TestEqual(*FString::Printf(TEXT("Switch %d: no extra HUD"), i + 1), CountHUDs(), HUDs);
	}
	TestEqual(TEXT("Panel slots unchanged (no duplicates)"),
		FShamanSpellPanelModel::Build(S, S0->EffectDispatcher, S0->GetSelectedSpellId()).Slots.Num(), SlotsBefore);
	PC->UnPossess();
	PC->Destroy();
	return true;
}

#endif
