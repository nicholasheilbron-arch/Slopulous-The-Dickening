#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Player/ShamanControlTypes.h"
#include "Player/ShamanPlayerController.h"
#include "Player/ShamanRTSCamera.h"
#include "Player/ShamanSelectionComponent.h"
#include "Terrain/PlanetFrame.h"
#include "Characters/ShamanUnitBase.h"
#include "Characters/ShamanCharacter.h"
#include "Characters/HealthComponent.h"
#include "SpellComponent.h"
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "EngineUtils.h"
#include "SpellEffectDispatcherComponent.h"
#include "SpellRow.h"
#include "Engine/DataTable.h"
#include "UI/ShamanSpellPanel.h"
#include "Tribes/TribeTaskTypes.h"
#include "GameplayTagContainer.h"
#include "ShamanTribeTestWorld.h"

/**
 * RTS milestone, Stage A: control foundation.
 *  - CameraRig / SelectionRules: pure math and rules (flat and planet frames).
 *  - Selection: UShamanSelectionComponent on real production units (death, destruction, tribe change).
 *  - ModeSwitch: AShamanPlayerController possessing a real Shaman in a test world (view target, input layer, no
 *    duplicate actors, the camera never moves the Shaman). No viewport here: actual mouse input, screen projection,
 *    HUD drawing and camera rendering are covered by the in-editor checks, not by these tests.
 */
namespace ShamanControlTestsPrivate
{
	using namespace ShamanTribeTestsPrivate;

	struct FControlFixture
	{
		FTribeTestWorld W;
		AShamanCharacter* S0 = nullptr;
		AShamanCharacter* S1 = nullptr;
		TArray<AShamanUnitBase*> Braves;
		AShamanUnitBase* EnemyBrave = nullptr;
		AShamanUnitBase* Wildman = nullptr;
		UTribeComponent* T0 = nullptr;

		explicit FControlFixture(int32 NumBraves)
		{
			S0 = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 0, FVector(0.f, 0.f, 100.f));
			S1 = W.SpawnUnit<AShamanCharacter>(W.Data->ShamanUnitId, 1, FVector(9000.f, 0.f, 100.f));
			for (int32 i = 0; i < NumBraves; ++i)
				Braves.Add(W.SpawnUnit<AShamanUnitBase>(W.Data->BraveUnitId, 0, FVector(300.f * (i + 1), 400.f, 100.f)));
			EnemyBrave = W.SpawnUnit<AShamanUnitBase>(W.Data->BraveUnitId, 1, FVector(9300.f, 0.f, 100.f));
			Wildman = W.SpawnUnit<AShamanUnitBase>(W.Data->WildmanUnitId, -1, FVector(-600.f, 0.f, 100.f));
			T0 = W.Tribes->GetTribeComponent(0);
		}
		UShamanSelectionComponent* MakeSelection()
		{
			AActor* Owner = W.World->SpawnActor<AActor>();
			UShamanSelectionComponent* Sel = NewObject<UShamanSelectionComponent>(Owner);
			Sel->RegisterComponent();
			Sel->SetPlayerTribe(0);
			return Sel;
		}
	};

	FShamanScreenCandidate Cand(AShamanUnitBase* U, FVector2D Feet, FVector2D Head, bool bOn = true)
	{
		FShamanScreenCandidate C; C.Unit = U; C.Feet = Feet; C.Head = Head; C.bOnScreen = bOn; return C;
	}
}
using namespace ShamanControlTestsPrivate;

#define SHAMAN_CONTROL_TEST(Class, Name) IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, Name, EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

SHAMAN_CONTROL_TEST(FShamanCameraRigTest, "Shaman.Controls.CameraRig")
bool FShamanCameraRigTest::RunTest(const FString& Parameters)
{
	FShamanRTSCameraSettings S;
	FShamanRTSCameraRig R;
	R.Focus = FVector(0.f, 0.f, 50.f);
	R.Forward = FVector(1.f, 0.f, 0.f);
	R.PitchDeg = 55.f;
	R.Distance = 4000.f;

	// Flat view: behind and above the focus, looking at it.
	FVector Loc; FRotator Rot;
	R.GetView(nullptr, Loc, Rot);
	TestTrue(TEXT("Flat: camera above the focus"), Loc.Z > R.Focus.Z + 3000.f);
	TestTrue(TEXT("Flat: camera behind the focus (opposite the heading)"), Loc.X < -2000.f);
	TestEqual(TEXT("Flat: camera at the zoom distance"), FVector::Dist(Loc, R.Focus), 4000.f, 1.f);
	TestTrue(TEXT("Flat: camera looks at the focus"), FVector::DotProduct(Rot.Vector(), (R.Focus - Loc).GetSafeNormal()) > 0.999f);

	R.Pan(nullptr, 100.f, 0.f);
	TestTrue(TEXT("Pan forward moves the focus along the heading"), R.Focus.Equals(FVector(100.f, 0.f, 50.f), 0.01f));
	R.Pan(nullptr, 0.f, 100.f);
	TestTrue(TEXT("Pan right moves the focus to the right (+Y)"), R.Focus.Equals(FVector(100.f, 100.f, 50.f), 0.01f));

	R.Orbit(nullptr, 90.f, 0.f, S);
	TestTrue(TEXT("Yaw +90 turns the heading right"), R.Forward.Equals(FVector(0.f, 1.f, 0.f), 1e-3f));
	R.GetView(nullptr, Loc, Rot);
	TestEqual(TEXT("Orbit keeps the distance"), FVector::Dist(Loc, R.Focus), 4000.f, 1.f);
	R.Orbit(nullptr, 0.f, 100.f, S);
	TestEqual(TEXT("Pitch clamped to MaxPitch"), R.PitchDeg, S.MaxPitch);
	R.Orbit(nullptr, 0.f, -200.f, S);
	TestEqual(TEXT("Pitch clamped to MinPitch"), R.PitchDeg, S.MinPitch);

	for (int32 i = 0; i < 100; ++i) R.Zoom(1.f, S);
	TestEqual(TEXT("Zoom in clamped to MinDistance"), R.Distance, S.MinDistance);
	for (int32 i = 0; i < 100; ++i) R.Zoom(-1.f, S);
	TestEqual(TEXT("Zoom out clamped to MaxDistance"), R.Distance, S.MaxDistance);

	// Planet: pan a long way around the globe. Focus stays on the sphere, heading stays tangent, never flips.
	const FPlanetFrame P(FVector(1000.f, -500.f, 200.f), 20000.f);
	FShamanRTSCameraRig G;
	G.Focus = P.GetPointAt(FVector(0.3f, 0.2f, 1.f), 0.f);
	G.Forward = FVector(1.f, 0.f, 0.f);
	G.Normalize(&P);
	G.Distance = 3000.f;
	bool bOnSphere = true, bTangent = true, bNoFlip = true, bAbove = true;
	FVector PrevForward = G.Forward;
	for (int32 i = 0; i < 2000; ++i) // 2000 x 100 uu = 200000 uu, more than one and a half circumferences
	{
		G.Pan(&P, 100.f, (i % 7 == 0) ? 30.f : 0.f);
		const FVector Up = P.GetUp(G.Focus);
		bOnSphere &= FMath::IsNearlyEqual(P.GetDistanceFromCenter(G.Focus), 20000.f, 1.f);
		bTangent &= FMath::Abs(FVector::DotProduct(G.Forward, Up)) < 1e-3f && FMath::IsNearlyEqual(G.Forward.Size(), 1.f, 1e-3f);
		bNoFlip &= FVector::DotProduct(PrevForward, G.Forward) > 0.99f;
		PrevForward = G.Forward;
		FVector L; FRotator Rr;
		G.GetView(&P, L, Rr);
		bAbove &= FVector::DotProduct(L - G.Focus, Up) > 0.f && !L.ContainsNaN();
	}
	TestTrue(TEXT("Planet: focus stays on the sphere"), bOnSphere);
	TestTrue(TEXT("Planet: heading stays tangent and unit"), bTangent);
	TestTrue(TEXT("Planet: heading never flips between steps"), bNoFlip);
	TestTrue(TEXT("Planet: camera always above the focus along the local up"), bAbove);
	return true;
}

SHAMAN_CONTROL_TEST(FShamanSelectionRulesTest, "Shaman.Controls.SelectionRules")
bool FShamanSelectionRulesTest::RunTest(const FString& Parameters)
{
	FControlFixture F(3);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	using R = FShamanSelectionRules;
	TestTrue(TEXT("Player Brave selectable"), R::IsSelectable(F.Braves[0], 0));
	TestTrue(TEXT("Player Shaman selectable"), R::IsSelectable(F.S0, 0));
	TestFalse(TEXT("Enemy Brave not selectable"), R::IsSelectable(F.EnemyBrave, 0));
	TestFalse(TEXT("Enemy Shaman not selectable"), R::IsSelectable(F.S1, 0));
	TestFalse(TEXT("Wildman not selectable"), R::IsSelectable(F.Wildman, 0));
	TestFalse(TEXT("Null not selectable"), R::IsSelectable(nullptr, 0));
	F.Braves[2]->Health->Kill(nullptr);
	TestFalse(TEXT("Dead Brave not selectable"), R::IsSelectable(F.Braves[2], 0));

	TestFalse(TEXT("Small move is a click"), R::IsDrag(FVector2D(100, 100), FVector2D(104, 103), 6.f));
	TestTrue(TEXT("Larger move is a drag"), R::IsDrag(FVector2D(100, 100), FVector2D(110, 100), 6.f));

	TArray<FShamanScreenCandidate> C;
	C.Add(Cand(F.Braves[0], FVector2D(100, 200), FVector2D(100, 150)));
	C.Add(Cand(F.Braves[1], FVector2D(300, 200), FVector2D(300, 150)));
	C.Add(Cand(F.S0, FVector2D(500, 200), FVector2D(500, 150), /*bOnScreen*/ false));
	TestEqual(TEXT("Click on a body picks that unit"), R::PickClosest(C, FVector2D(102, 170), 16.f), 0);
	TestEqual(TEXT("Click near the second unit picks it"), R::PickClosest(C, FVector2D(295, 190), 16.f), 1);
	TestEqual(TEXT("Click on empty ground picks nothing"), R::PickClosest(C, FVector2D(200, 400), 16.f), (int32)INDEX_NONE);
	TestEqual(TEXT("Off-screen units cannot be clicked"), R::PickClosest(C, FVector2D(500, 175), 16.f), (int32)INDEX_NONE);

	TArray<int32> Hits;
	R::BoxSelect(C, FVector2D(350, 250), FVector2D(50, 100), Hits); // corners in reverse order
	TestTrue(TEXT("Box selects units whose body centre is inside"), Hits.Num() == 2 && Hits.Contains(0) && Hits.Contains(1));
	R::BoxSelect(C, FVector2D(50, 100), FVector2D(150, 250), Hits);
	TestTrue(TEXT("Box selects only units inside"), Hits.Num() == 1 && Hits[0] == 0);
	R::BoxSelect(C, FVector2D(450, 100), FVector2D(550, 250), Hits);
	TestEqual(TEXT("Off-screen units are not box-selected"), Hits.Num(), 0);
	return true;
}

SHAMAN_CONTROL_TEST(FShamanSelectionComponentTest, "Shaman.Controls.Selection")
bool FShamanSelectionComponentTest::RunTest(const FString& Parameters)
{
	FControlFixture F(3);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	UShamanSelectionComponent* Sel = F.MakeSelection();
	AShamanUnitBase* B0 = F.Braves[0];
	AShamanUnitBase* B1 = F.Braves[1];
	AShamanUnitBase* B2 = F.Braves[2];
	const int32 WorkersBefore = F.T0->GetAvailableWorkerCount();

	Sel->ClickUnit(B0, false);
	TestTrue(TEXT("Click selects"), Sel->Num() == 1 && Sel->IsSelected(B0));
	Sel->ClickUnit(B0, false);
	TestEqual(TEXT("Click on the selected unit deselects it"), Sel->Num(), 0);
	Sel->ClickUnit(B0, false);
	Sel->ClickUnit(B1, false);
	TestTrue(TEXT("Click on another unit replaces the selection"), Sel->Num() == 1 && Sel->IsSelected(B1));
	Sel->ClickUnit(B0, true);
	TestTrue(TEXT("Shift-click adds"), Sel->Num() == 2 && Sel->IsSelected(B0) && Sel->IsSelected(B1));
	Sel->ClickUnit(F.EnemyBrave, false);
	TestEqual(TEXT("Click on an enemy acts as an empty click (clears)"), Sel->Num(), 0);
	Sel->SelectUnits({ B0, B1, F.S0, F.EnemyBrave, F.Wildman, F.S1 }, false);
	TestTrue(TEXT("Box keeps only the player's Braves and Shaman"), Sel->Num() == 3 && Sel->IsSelected(F.S0) && !Sel->IsSelected(F.EnemyBrave)
		&& !Sel->IsSelected(F.Wildman) && !Sel->IsSelected(F.S1));
	Sel->ClickEmpty(false);
	TestEqual(TEXT("Empty click clears"), Sel->Num(), 0);

	TestEqual(TEXT("Selecting never changes worker availability"), F.T0->GetAvailableWorkerCount(), WorkersBefore);
	TestEqual(TEXT("Selecting never creates tasks"), F.T0->GetOpenTaskCount(), 0);
	TestTrue(TEXT("Selecting never changes tribe membership"), B0->GetTribeId() == 0 && F.T0->IsFollower(B0->TribeMember));

	Sel->SelectUnits({ B0, B1, B2 }, false);
	B0->Health->Kill(nullptr);
	TestEqual(TEXT("Dead unit pruned"), Sel->PruneInvalid(), 1);
	B1->ChangeTribe(1);
	TestEqual(TEXT("Unit that left the tribe pruned"), Sel->PruneInvalid(), 1);
	B2->Destroy();
	Sel->PruneInvalid();
	TestEqual(TEXT("Destroyed unit pruned; selection empty"), Sel->Num(), 0);
	TestEqual(TEXT("No stale entries returned"), Sel->GetSelectedUnits().Num(), 0);
	return true;
}

SHAMAN_CONTROL_TEST(FShamanModeSwitchTest, "Shaman.Controls.ModeSwitch")
bool FShamanModeSwitchTest::RunTest(const FString& Parameters)
{
	FControlFixture F(2);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	AShamanPlayerController* PC = F.W.World->SpawnActor<AShamanPlayerController>();
	if (!TestNotNull(TEXT("Player controller"), PC)) return false;
	PC->CameraSettings.ModeBlendTime = 0.f; // view target changes immediately (no blend) so it can be checked here
	PC->Possess(F.S0);
	if (!TestTrue(TEXT("Controller possesses the Shaman"), PC->GetPawn() == F.S0)) return false;

	TestTrue(TEXT("RTS is the default mode"), PC->GetControlMode() == EShamanControlMode::RTS);
	AShamanRTSCamera* Cam = PC->GetRTSCamera();
	if (!TestNotNull(TEXT("Strategic camera spawned"), Cam)) return false;
	TestTrue(TEXT("RTS mode views through the strategic camera"), PC->GetViewTarget() == Cam);

	const FVector ShamanAt = F.S0->GetActorLocation();
	const FRotator ShamanRot = F.S0->GetActorRotation();
	const float Health = F.S0->Health->Health, Mana = F.S0->Spells->Mana;
	const FVector FocusBefore = Cam->GetRig().Focus;
	PC->PanCamera(1.f, 0.5f, 1.f);
	PC->OrbitCamera(45.f, 10.f);
	PC->ZoomCamera(3.f);
	TestFalse(TEXT("Panning moves the camera focus"), Cam->GetRig().Focus.Equals(FocusBefore, 1.f));
	TestTrue(TEXT("Camera movement never moves or turns the Shaman"),
		F.S0->GetActorLocation().Equals(ShamanAt, 0.01f) && F.S0->GetActorRotation().Equals(ShamanRot, 0.01f));

	auto CountShamans = [&F]() { int32 N = 0; for (TActorIterator<AShamanCharacter> It(F.W.World); It; ++It) ++N; return N; };
	auto CountCameras = [&F]() { int32 N = 0; for (TActorIterator<AShamanRTSCamera> It(F.W.World); It; ++It) ++N; return N; };
	const int32 Shamans = CountShamans();

	PC->ToggleControlMode();
	TestTrue(TEXT("Enter -> Shaman mode"), PC->GetControlMode() == EShamanControlMode::Shaman);
	TestTrue(TEXT("Shaman mode views through the Shaman"), PC->GetViewTarget() == F.S0);
	TestTrue(TEXT("Same Shaman actor still possessed (no duplicate)"), PC->GetPawn() == F.S0 && CountShamans() == Shamans);
	TestTrue(TEXT("Shaman state preserved"), F.S0->GetActorLocation().Equals(ShamanAt, 0.01f) && F.S0->Health->Health == Health
		&& F.S0->Spells->Mana == Mana && F.S0->GetTribeId() == 0 && F.S0->IsAlive());

	PC->ToggleControlMode();
	TestTrue(TEXT("Enter -> back to RTS mode"), PC->GetControlMode() == EShamanControlMode::RTS && PC->GetViewTarget() == PC->GetRTSCamera());
	TestEqual(TEXT("Still exactly one strategic camera"), CountCameras(), 1);
	TestEqual(TEXT("Still the same number of Shamans"), CountShamans(), Shamans);
	TestEqual(TEXT("Mode switching creates no tasks"), F.T0->GetOpenTaskCount(), 0);
	TestEqual(TEXT("Mode switching leaves the worker pool alone"), F.T0->GetAvailableWorkerCount(), 2);

	PC->UnPossess();
	PC->Destroy();
	return true;
}

SHAMAN_CONTROL_TEST(FShamanPlayerTribeLateTest, "Shaman.Controls.PlayerTribeLate")
bool FShamanPlayerTribeLateTest::RunTest(const FString& Parameters)
{
	// Regression: in PIE the player Shaman is possessed before world BeginPlay, i.e. before ApplyIdentity gives its
	// TribeMember the real tribe. The selection used to latch tribe -1 at OnPossess and then rejected every unit.
	FControlFixture F(2);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	AShamanPlayerController* PC = F.W.World->SpawnActor<AShamanPlayerController>();
	if (!TestNotNull(TEXT("Player controller"), PC)) return false;
	PC->CameraSettings.ModeBlendTime = 0.f;
	UShamanSelectionComponent* Sel = PC->Selection;
	AShamanUnitBase* B0 = F.Braves[0];
	AShamanUnitBase* B1 = F.Braves[1];

	F.S0->TribeMember->TribeId = ShamanTribes::NoTribe; // as before the Shaman's BeginPlay
	PC->Possess(F.S0);
	TestTrue(TEXT("An unknown pawn tribe is never pushed into the selection"), Sel->GetPlayerTribe() >= 0);
	Sel->SetPlayerTribe(-1);                        // the stale state the old OnPossess produced
	F.S0->TribeMember->TribeId = 0;                 // the Shaman's BeginPlay has now run
	PC->RefreshPlayerTribe();                       // PlayerTick does this every frame
	TestEqual(TEXT("Selection follows the Shaman's real tribe"), Sel->GetPlayerTribe(), 0);
	TestEqual(TEXT("Controller reports the real tribe"), PC->GetPlayerTribe(), 0);

	Sel->ClickUnit(B0, false);
	TestTrue(TEXT("Click selects a friendly Brave"), Sel->Num() == 1 && Sel->IsSelected(B0));
	Sel->ClickUnit(B1, false);
	TestTrue(TEXT("Click on another Brave changes the selection"), Sel->Num() == 1 && Sel->IsSelected(B1));
	Sel->ClickEmpty(false);
	TestEqual(TEXT("Click on empty ground deselects"), Sel->Num(), 0);
	Sel->SelectUnits({ B0, B1, F.EnemyBrave, F.Wildman }, false);
	TestTrue(TEXT("Drag selects all eligible Braves only"), Sel->Num() == 2 && Sel->IsSelected(B0) && Sel->IsSelected(B1));

	PC->ToggleControlMode();
	PC->RefreshPlayerTribe();
	PC->ToggleControlMode();
	PC->RefreshPlayerTribe();
	TestTrue(TEXT("Mode switches keep the tribe and the selection"), Sel->GetPlayerTribe() == 0 && Sel->Num() == 2);
	Sel->ClickEmpty(false);   // (a plain click on an already-selected unit deselects it, so clear first)
	Sel->ClickUnit(B0, false);
	TestTrue(TEXT("Selection still works after switching modes"), Sel->Num() == 1 && Sel->IsSelected(B0));

	PC->UnPossess();
	PC->Destroy();
	return true;
}

SHAMAN_CONTROL_TEST(FShamanSpellPanelTest, "Shaman.UI.SpellPanel")
bool FShamanSpellPanelTest::RunTest(const FString& Parameters)
{
	FControlFixture F(0);
	USpellComponent* Spells = F.S0->Spells;
	if (!TestNotNull(TEXT("Spell table resolved without DA_ShamanGameData"), Spells->SpellTable)) return false;
	const FSpellRow* Blast = Spells->GetSpellRow(TEXT("Blast"));
	if (!TestNotNull(TEXT("Blast row"), Blast)) return false;
	const FSpellRow BlastBefore = *Blast;
	const float ManaBefore = Spells->Mana;
	const int32 NumRows = Spells->SpellTable->GetRowNames().Num();
	// Spell panel slice: "?" now means "can be learned on this map" (was: any implemented spell). This test world has no
	// monuments, so the map offers Convert explicitly; off the map it must never show as "?" (checked below).
	Spells->SetSpellAvailableOnMap(TEXT("Convert"));

	FShamanSpellPanelModel M = FShamanSpellPanelModel::Build(Spells, F.S0->EffectDispatcher, F.S0->GetSelectedSpellId());
	TestTrue(TEXT("Panel has spell data"), M.bHasSpellData);
	const int32 IB = M.FindSlot(TEXT("Blast"));
	const int32 IC = M.FindSlot(TEXT("Convert"));
	if (!TestTrue(TEXT("Blast (TAKA) and Convert are on the panel"), IB != INDEX_NONE && IC != INDEX_NONE)) return false;
	TestTrue(TEXT("Blast is available and selected"), M.Slots[IB].State == EShamanSpellSlotState::Available && M.Slots[IB].bSelected);
	TestEqual(TEXT("Blast shows its real name"), M.Slots[IB].DisplayName.ToString(), BlastBefore.DisplayName.ToString());
	TestEqual(TEXT("Blast shows its real mana cost"), M.Slots[IB].ManaCost, BlastBefore.ManaCost);
	TestTrue(TEXT("Convert is discoverable (\"?\"), not selected"), M.Slots[IC].State == EShamanSpellSlotState::Discoverable && !M.Slots[IC].bSelected);
	TestEqual(TEXT("Unknown spells without an effect are not shown"), M.FindSlot(TEXT("Swarm")), (int32)INDEX_NONE);
	TestEqual(TEXT("Every table row is either shown or counted as not implemented"), M.Slots.Num() + M.NumHiddenUnimplemented, NumRows);
	int32 NumAvailable = 0;
	for (const FShamanSpellSlot& S : M.Slots) NumAvailable += S.State == EShamanSpellSlotState::Available ? 1 : 0;
	TestEqual(TEXT("Only known, implemented spells are available"), NumAvailable, 1);

	TestFalse(TEXT("A discoverable spell cannot be selected"), F.S0->SelectSpell(TEXT("Convert")));
	TestTrue(TEXT("Selection unchanged"), F.S0->GetSelectedSpellId() == FName(TEXT("Blast")));
	Spells->SetSpellAvailableOnMap(TEXT("Convert"), false);
	{
		const FShamanSpellPanelModel Off = FShamanSpellPanelModel::Build(Spells, F.S0->EffectDispatcher, F.S0->GetSelectedSpellId());
		const int32 IOff = Off.FindSlot(TEXT("Convert"));
		TestTrue(TEXT("Not offered by this map: never \"?\""), IOff == INDEX_NONE || Off.Slots[IOff].State != EShamanSpellSlotState::Discoverable);
		TestEqual(TEXT("Off the map: every table row is still either shown or counted"), Off.Slots.Num() + Off.NumHiddenUnimplemented, NumRows);
	}
	Spells->SetSpellAvailableOnMap(TEXT("Convert"));

	Spells->LearnSpell(TEXT("Convert"));
	M = FShamanSpellPanelModel::Build(Spells, F.S0->EffectDispatcher, F.S0->GetSelectedSpellId());
	TestTrue(TEXT("Learned Convert becomes available"), M.Slots[M.FindSlot(TEXT("Convert"))].State == EShamanSpellSlotState::Available);
	TestTrue(TEXT("Selecting a known spell works"), F.S0->SelectSpell(TEXT("Convert")) && F.S0->GetSelectedSpellId() == FName(TEXT("Convert")));
	M = FShamanSpellPanelModel::Build(Spells, F.S0->EffectDispatcher, F.S0->GetSelectedSpellId());
	TestTrue(TEXT("Panel highlights the new selection only"), M.Slots[M.FindSlot(TEXT("Convert"))].bSelected && !M.Slots[M.FindSlot(TEXT("Blast"))].bSelected);
	TestEqual(TEXT("Selecting never casts (mana unchanged)"), Spells->Mana, ManaBefore);
	TestEqual(TEXT("Selecting never casts (no cooldown)"), Spells->GetCooldownRemaining(TEXT("Convert")), 0.f);

	Spells->LearnSpell(TEXT("Swarm")); // console-learned, no effect handler yet
	const int32 Before = FShamanSpellPanelModel::Build(Spells, F.S0->EffectDispatcher, NAME_None).Slots.Num();
	M = FShamanSpellPanelModel::Build(Spells, F.S0->EffectDispatcher, NAME_None);
	const int32 IS = M.FindSlot(TEXT("Swarm"));
	TestTrue(TEXT("A known but unimplemented spell is shown as not implemented"), IS != INDEX_NONE && M.Slots[IS].State == EShamanSpellSlotState::Unimplemented && !M.Slots[IS].bSelected);
	TestEqual(TEXT("Rebuilding never duplicates slots"), M.Slots.Num(), Before);

	const FSpellRow* BlastAfter = Spells->GetSpellRow(TEXT("Blast"));
	TestTrue(TEXT("TAKA row untouched"), BlastAfter && BlastAfter->ManaCost == BlastBefore.ManaCost && BlastAfter->RangeRaw == BlastBefore.RangeRaw
		&& BlastAfter->Damage == BlastBefore.Damage && BlastAfter->bHoming == BlastBefore.bHoming && BlastAfter->EffectId == BlastBefore.EffectId);

	UDataTable* Table = Spells->SpellTable;
	Spells->SpellTable = nullptr;
	M = FShamanSpellPanelModel::Build(Spells, F.S0->EffectDispatcher, NAME_None);
	TestTrue(TEXT("No spell data: empty panel flagged, not fake slots"), !M.bHasSpellData && M.Slots.Num() == 0);
	Spells->SpellTable = Table;

	TArray<FBox2D> Rects; FBox2D Panel(ForceInit);
	FShamanSpellPanelLayout::ComputeSlotRects(5, 1920.f, 1080.f, 1.f, Rects, Panel);
	TestEqual(TEXT("One rectangle per slot"), Rects.Num(), 5);
	TestTrue(TEXT("Panel fits on screen"), Panel.Min.X >= 0.f && Panel.Max.X <= 1920.f && Panel.Min.Y >= 0.f && Panel.Max.Y <= 1080.f);
	TestEqual(TEXT("Hit test finds the slot under the cursor"), FShamanSpellPanelLayout::HitTest(Rects, Rects[4].GetCenter()), 4);
	TestEqual(TEXT("Hit test misses outside the slots"), FShamanSpellPanelLayout::HitTest(Rects, FVector2D(10.f, 10.f)), (int32)INDEX_NONE);
	return true;
}

SHAMAN_CONTROL_TEST(FShamanFormationRulesTest, "Shaman.Controls.FormationRules")
bool FShamanFormationRulesTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> O;
	FShamanCommandRules::FormationOffsets(0, 140.f, O);
	TestEqual(TEXT("No units, no slots"), O.Num(), 0);
	FShamanCommandRules::FormationOffsets(1, 140.f, O);
	TestTrue(TEXT("A single unit goes to the clicked point"), O.Num() == 1 && O[0].IsNearlyZero());
	for (int32 N : { 2, 7, 8, 20, 40 })
	{
		FShamanCommandRules::FormationOffsets(N, 140.f, O);
		float MinGap = TNumericLimits<float>::Max();
		for (int32 i = 0; i < O.Num(); ++i) for (int32 j = i + 1; j < O.Num(); ++j) MinGap = FMath::Min(MinGap, FVector2D::Distance(O[i], O[j]));
		TestEqual(FString::Printf(TEXT("%d units: one slot each"), N), O.Num(), N);
		TestTrue(FString::Printf(TEXT("%d units: slots at least the spacing apart"), N), MinGap >= 139.f);
		TArray<FVector2D> Again;
		FShamanCommandRules::FormationOffsets(N, 140.f, Again);
		TestTrue(FString::Printf(TEXT("%d units: deterministic"), N), Again == O);
	}
	return true;
}

SHAMAN_CONTROL_TEST(FShamanMoveCommandTest, "Shaman.Controls.Commands")
bool FShamanMoveCommandTest::RunTest(const FString& Parameters)
{
	FControlFixture F(3);
	if (!TestNotNull(TEXT("Tribe 0"), F.T0)) return false;
	AShamanPlayerController* PC = F.W.World->SpawnActor<AShamanPlayerController>();
	if (!TestNotNull(TEXT("Player controller"), PC)) return false;
	PC->CameraSettings.ModeBlendTime = 0.f;
	PC->Possess(F.S0);
	PC->RefreshPlayerTribe();

	TArray<AShamanUnitBase*> Units = { F.Braves[0], F.Braves[1], F.Braves[2], F.S0, F.EnemyBrave, F.Wildman };
	const FVector Ground(2500.f, 1500.f, 100.f);
	const int32 EnemyTasksBefore = F.W.Tribes->GetTribeComponent(1) ? F.W.Tribes->GetTribeComponent(1)->GetOpenTaskCount() : 0;
	TestEqual(TEXT("Move: the 3 Braves and the Shaman get the order (enemy / Wildman ignored)"), PC->CommandMove(Units, Ground), 4);
	TArray<int32> FirstTasks;
	TArray<FVector> Dests;
	for (AShamanUnitBase* B : F.Braves)
	{
		FTribeTask T;
		const int32 Id = B->TribeMember->GetCurrentTaskId();
		const bool bTask = F.T0->GetTask(Id, T);
		TestTrue(FString::Printf(TEXT("%s holds a PlayerCommand Task.MoveTo"), *B->GetName()), bTask && T.IsHeld()
			&& T.Type == FGameplayTag::RequestGameplayTag(TEXT("Task.MoveTo")) && T.Source == FName(TEXT("PlayerCommand")) && T.Assignee.Get() == B->TribeMember);
		TestTrue(FString::Printf(TEXT("%s will hold the spot afterwards"), *B->GetName()), B->GetOrder() == EFollowerOrder::HoldPosition
			&& FVector::Dist2D(B->GetOrderLocation(), Ground) < 1000.f);
		FirstTasks.Add(Id);
		Dests.Add(T.TargetLocation);
	}
	TestTrue(TEXT("Formation: every Brave gets its own spot"), Dests.Num() == 3 && FVector::Dist2D(Dests[0], Dests[1]) > 100.f
		&& FVector::Dist2D(Dests[0], Dests[2]) > 100.f && FVector::Dist2D(Dests[1], Dests[2]) > 100.f);
	TestTrue(TEXT("Enemy and Wildman untouched"), F.EnemyBrave->TribeMember->GetCurrentTaskId() == INDEX_NONE
		&& F.Wildman->TribeMember->GetCurrentTaskId() == INDEX_NONE
		&& (!F.W.Tribes->GetTribeComponent(1) || F.W.Tribes->GetTribeComponent(1)->GetOpenTaskCount() == EnemyTasksBefore));
	TestTrue(TEXT("The selected Shaman walks too"), PC->IsShamanMoving());

	// A new order replaces the old one: old tasks cancelled, never stacked.
	PC->CommandMove(Units, Ground + FVector(-1500.f, 0.f, 0.f));
	for (int32 i = 0; i < 3; ++i)
	{
		FTribeTask Old;
		TestTrue(TEXT("Previous move task cancelled by the new command"), F.T0->GetTask(FirstTasks[i], Old)
			&& Old.State == ETribeTaskState::Cancelled && Old.EndReason == FName(TEXT("PlayerCommand")));
	}
	TestEqual(TEXT("One open task per Brave"), F.T0->GetOpenTaskCount(), 3);

	// Right-click on the Shaman: the Braves guard (follow) him, their move tasks end.
	TestEqual(TEXT("Guard: the 3 Braves (the Shaman does not guard himself)"), PC->CommandGuardShaman(Units), 3);
	TestEqual(TEXT("Guard ends the move tasks"), F.T0->GetOpenTaskCount(), 0);
	bool bAllFollow = true;
	for (AShamanUnitBase* B : F.Braves) bAllFollow &= B->GetOrder() == EFollowerOrder::FollowShaman;
	TestTrue(TEXT("Guard: every Brave follows the Shaman"), bAllFollow);
	TestEqual(TEXT("Workers are free again"), F.T0->GetAvailableWorkerCount(), 3);

	// Switching to direct control stops the Shaman's walk (WASD takes over).
	PC->ToggleControlMode();
	TestFalse(TEXT("Shaman mode cancels the Shaman's move order"), PC->IsShamanMoving());
	PC->ToggleControlMode();

	PC->UnPossess();
	PC->Destroy();
	return true;
}

#undef SHAMAN_CONTROL_TEST

#endif
