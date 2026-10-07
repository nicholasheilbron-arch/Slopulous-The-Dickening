#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Terrain/ShamanTerrainSubsystem.h"
#include "Terrain/ShamanTerrainBackend.h"
#include "Terrain/PlanetHeightField.h"
#include "Terrain/PlanetTerrainQueries.h"
#include "Terrain/ShamanSpace.h"
#include "Navigation/ShamanSurfaceNavigation.h"
#include "Characters/ShamanUnitBase.h"
#include "Characters/ShamanCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"

/**
 * Spherical terrain foundation tests. They run on the analytic backend (pure math, no Voxel Plugin), so they are
 * fast, deterministic and independent of rendering/collision streaming. Voxel-backend behaviour (meshing,
 * collision, timing) is covered by the in-game acceptance commands (see SHAMAN_Spherical_Terrain_Handoff.md).
 */
namespace ShamanTerrainTestsPrivate
{
	struct FTestWorld
	{
		UWorld* World = nullptr;
		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ShamanTerrainTestWorld"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			World->RemoveFromRoot();
		}
		UShamanTerrainSubsystem* Terrain() const { return World->GetSubsystem<UShamanTerrainSubsystem>(); }
		UShamanTerrainSubsystem* MakePlanet(const FPlanetSettings& S) const
		{
			UShamanTerrainSubsystem* T = Terrain();
			return T && T->InitializeTerrain(UAnalyticPlanetTerrainBackend::StaticClass(), S) ? T : nullptr;
		}
	};

	static FPlanetSettings TestSettings(int32 Seed = 1337)
	{
		FPlanetSettings S;
		S.Seed = Seed;
		S.Center = FVector(1000.f, -2000.f, 500.f); // deliberately not the origin
		S.Radius = 20000.f;
		return S;
	}

	static FVector FibDir(int32 i, int32 n)
	{
		const float Y = 1.f - 2.f * (i + 0.5f) / n;
		const float R = FMath::Sqrt(FMath::Max(0.f, 1.f - Y * Y));
		return FVector(FMath::Cos(2.39996323f * i) * R, FMath::Sin(2.39996323f * i) * R, Y);
	}

	/** First walkable land direction satisfying Pred (deterministic). */
	template<class TPred>
	static bool FindLand(const UShamanTerrainSubsystem* T, TPred Pred, FVector& OutDir)
	{
		for (int32 i = 0; i < 4000; ++i)
		{
			const FVector D = FibDir(i, 4000);
			const FTerrainSample S = T->QueryTerrainDirection(D);
			if (S.bWalkable && !S.bUnderwater && S.Height > 60.f && Pred(D, S)) { OutDir = D; return true; }
		}
		return false;
	}
}
using namespace ShamanTerrainTestsPrivate;

#define SHAMAN_TERRAIN_TEST(Class, Name) IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, Name, EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

SHAMAN_TERRAIN_TEST(FShamanTerrainPlanetFrameTest, "Shaman.Terrain.PlanetCenterRadius")
bool FShamanTerrainPlanetFrameTest::RunTest(const FString&)
{
	FTestWorld W;
	UShamanTerrainSubsystem* Flat = W.Terrain();
	TestNotNull(TEXT("Subsystem exists"), Flat);
	if (!Flat) return false;
	TestFalse(TEXT("No planet before InitializeTerrain (flat worlds unaffected)"), Flat->IsPlanetActive());
	TestFalse(TEXT("Flat world query is invalid"), Flat->QueryTerrain(FVector::ZeroVector).bValid);
	TestTrue(TEXT("Flat-world up is +Z"), FShamanSpace::GetUp(W.World, FVector(5, 6, 7)).Equals(FVector::UpVector));

	const FPlanetSettings S = TestSettings();
	UShamanTerrainSubsystem* T = W.MakePlanet(S);
	TestNotNull(TEXT("Planet initialised"), T);
	if (!T) return false;
	TestTrue(TEXT("Planet active"), T->IsPlanetActive());
	TestTrue(TEXT("Centre"), T->GetPlanetCenter().Equals(S.Center));
	TestTrue(TEXT("Radius"), FMath::IsNearlyEqual(T->GetPlanetRadius(), S.Radius));
	TestTrue(TEXT("Water level = radius + offset"), FMath::IsNearlyEqual(T->GetWaterLevel(), S.Radius + S.SeaLevelOffset));
	TestTrue(TEXT("Frame matches"), T->GetPlanetFrame().Center.Equals(S.Center) && FMath::IsNearlyEqual(T->GetPlanetFrame().Radius, S.Radius));
	AddExpectedError(TEXT("is too small"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid radius rejected"), W.Terrain()->InitializeTerrain(UAnalyticPlanetTerrainBackend::StaticClass(), [] { FPlanetSettings B; B.Radius = 0.f; return B; }()));
	return true;
}

SHAMAN_TERRAIN_TEST(FShamanTerrainRadialUpTest, "Shaman.Terrain.RadialUp")
bool FShamanTerrainRadialUpTest::RunTest(const FString&)
{
	FTestWorld W;
	const FPlanetSettings S = TestSettings();
	UShamanTerrainSubsystem* T = W.MakePlanet(S);
	if (!TestNotNull(TEXT("Planet"), T)) return false;
	const FPlanetFrame F = T->GetPlanetFrame();
	const FVector South = S.Center + FVector(0, 0, -25000.f), East = S.Center + FVector(0, 25000.f, 0);
	TestTrue(TEXT("Up at south pole points -Z"), F.GetUp(South).Equals(FVector(0, 0, -1), 1e-4f));
	TestTrue(TEXT("Up at east points +Y"), F.GetUp(East).Equals(FVector(0, 1, 0), 1e-4f));
	TestTrue(TEXT("Gravity is -Up"), F.GetGravityDir(East).Equals(FVector(0, -1, 0), 1e-4f));
	TestTrue(TEXT("FShamanSpace uses the planet"), FShamanSpace::GetUp(W.World, South).Equals(FVector(0, 0, -1), 1e-4f));
	FVector Fwd, Right, Up;
	F.GetTangentBasis(East, FVector(0, 1, 0) /*parallel to up*/, Fwd, Right, Up);
	TestTrue(TEXT("Tangent basis orthonormal even with a degenerate hint"),
		FMath::Abs(FVector::DotProduct(Fwd, Up)) < 1e-4f && FMath::Abs(FVector::DotProduct(Right, Up)) < 1e-4f && FMath::IsNearlyEqual(Fwd.Size(), 1.f, 1e-3f));
	TestTrue(TEXT("Quarter great circle"), FMath::IsNearlyEqual(F.GetSurfaceDistance(South, East), S.Radius * PI * 0.5f, 1.f));
	const FRotator R = FShamanSpace::UprightRotation(W.World, South, FVector(1, 0, 0));
	TestTrue(TEXT("Upright rotation at the south pole: actor up = radial up"), R.Quaternion().GetUpVector().Equals(FVector(0, 0, -1), 1e-3f));
	TestTrue(TEXT("Ground distance is great-circle on planets"), FMath::IsNearlyEqual(FShamanSpace::HorizontalDistance(W.World, South, East), S.Radius * PI * 0.5f, 1.f));
	return true;
}

SHAMAN_TERRAIN_TEST(FShamanTerrainSurfaceQueryTest, "Shaman.Terrain.SurfaceQuery")
bool FShamanTerrainSurfaceQueryTest::RunTest(const FString&)
{
	FTestWorld W;
	const FPlanetSettings S = TestSettings();
	UShamanTerrainSubsystem* T = W.MakePlanet(S);
	if (!TestNotNull(TEXT("Planet"), T)) return false;
	const FPlanetFrame F = T->GetPlanetFrame();
	int32 Bad = 0, Land = 0, RayOk = 0;
	for (int32 i = 0; i < 500; ++i)
	{
		const FVector D = FibDir(i, 500);
		const FTerrainSample Smp = T->QueryTerrainDirection(D);
		const FVector Above = S.Center + D * (S.Radius + 8000.f);
		const FTerrainSample ByLoc = T->QueryTerrain(Above);
		Bad += !Smp.bValid || !Smp.Up.Equals(D, 1e-3f) || FVector::DotProduct(Smp.Normal, Smp.Up) <= 0.f
			|| !FMath::IsNearlyEqual(F.GetAltitude(Smp.Location), Smp.Height, 0.5f)
			|| !ByLoc.Location.Equals(Smp.Location, 0.5f)
			|| !T->GetSurfaceLocation(D).Equals(Smp.Location, 0.5f)
			|| !FMath::IsNearlyEqual(T->GetTerrainHeight(Above), Smp.Height, 0.5f);
		Land += Smp.Height > S.SeaLevelOffset;
		FTerrainRaycastHit Hit;
		if (T->Raycast(Above, S.Center + D * (S.Radius - 4000.f), Hit) && Hit.Location.Equals(Smp.Location, 10.f)) ++RayOk;
	}
	TestEqual(TEXT("Samples consistent (valid, radial up, outward normal, height = altitude, by-location = by-direction)"), Bad, 0);
	TestTrue(TEXT("Planet has land and sea (25..75% land)"), Land > 125 && Land < 375);
	TestEqual(TEXT("Radial raycasts hit the surface"), RayOk, 500);
	FTerrainRaycastHit Miss;
	TestFalse(TEXT("Ray far above the planet misses"), T->Raycast(S.Center + FVector(-60000, 0, 40000), S.Center + FVector(60000, 0, 40000), Miss));
	return true;
}

SHAMAN_TERRAIN_TEST(FShamanTerrainDeterminismTest, "Shaman.Terrain.Determinism")
bool FShamanTerrainDeterminismTest::RunTest(const FString&)
{
	FPlanetSettings S = TestSettings(4242);
	FPlanetHeightField A(S), B(S);
	S.Seed = 4243;
	FPlanetHeightField C(S);
	int32 Same = 0, Diff = 0;
	for (int32 i = 0; i < 5000; ++i)
	{
		const FVector D = FibDir(i, 5000);
		Same += A.GetHeight(D) == B.GetHeight(D);
		Diff += !FMath::IsNearlyEqual(A.GetHeight(D), C.GetHeight(D), 1.f);
	}
	TestEqual(TEXT("Same seed -> bit-identical heights"), Same, 5000);
	TestTrue(TEXT("Different seed -> different planet"), Diff > 4000);

	// Two independent worlds with the same settings agree through the full subsystem path too.
	FTestWorld W1, W2;
	UShamanTerrainSubsystem* T1 = W1.MakePlanet(TestSettings(77));
	UShamanTerrainSubsystem* T2 = W2.MakePlanet(TestSettings(77));
	if (!TestNotNull(TEXT("Planets"), T1) || !TestNotNull(TEXT("Planets"), T2)) return false;
	int32 SameSamples = 0;
	for (int32 i = 0; i < 1000; ++i)
	{
		const FTerrainSample X = T1->QueryTerrainDirection(FibDir(i, 1000)), Y = T2->QueryTerrainDirection(FibDir(i, 1000));
		SameSamples += X.Height == Y.Height && X.Material == Y.Material && X.Flags == Y.Flags && X.bWalkable == Y.bWalkable;
	}
	TestEqual(TEXT("Same seed -> identical samples in separate worlds"), SameSamples, 1000);

	// Save/load: replaying the edit log reproduces the terrain exactly.
	FTerrainModification M; M.Center = T1->GetSurfaceLocation(FibDir(10, 1000)); M.Strength = 250.f;
	T1->ModifyTerrain(M);
	M.Op = ETerrainOp::Flatten; M.Strength = 1.f; M.TargetHeight = 40.f; M.Center = T1->GetSurfaceLocation(FibDir(11, 1000));
	T1->ModifyTerrain(M);
	TArray<FTerrainModification> Log; T1->GetEditLog(Log);
	TestTrue(TEXT("Replay on another world succeeds"), T2->ReplayEditLog(Log));
	int32 SameAfter = 0;
	for (int32 i = 0; i < 1000; ++i)
		SameAfter += T1->QueryTerrainDirection(FibDir(i, 1000)).Height == T2->QueryTerrainDirection(FibDir(i, 1000)).Height;
	TestEqual(TEXT("Edit-log replay is exact"), SameAfter, 1000);
	return true;
}

SHAMAN_TERRAIN_TEST(FShamanTerrainUnderwaterTest, "Shaman.Terrain.Underwater")
bool FShamanTerrainUnderwaterTest::RunTest(const FString&)
{
	FTestWorld W;
	const FPlanetSettings S = TestSettings();
	UShamanTerrainSubsystem* T = W.MakePlanet(S);
	if (!TestNotNull(TEXT("Planet"), T)) return false;
	FVector SeaDir, LandDir;
	bool bSea = false;
	for (int32 i = 0; i < 4000 && !bSea; ++i)
	{
		const FTerrainSample Smp = T->QueryTerrainDirection(FibDir(i, 4000));
		if (Smp.WaterDepth > 300.f) { SeaDir = FibDir(i, 4000); bSea = true; }
	}
	TestTrue(TEXT("Found deep sea"), bSea);
	TestTrue(TEXT("Found land"), FindLand(T, [](const FVector&, const FTerrainSample&) { return true; }, LandDir));
	if (!bSea) return false;
	const FPlanetFrame F = T->GetPlanetFrame();
	const FTerrainSample Sea = T->QueryTerrainDirection(SeaDir);
	TestTrue(TEXT("Seabed sample is underwater and flagged"), Sea.bUnderwater && (Sea.Flags & ETerrainFlags::Underwater) && !Sea.bWalkable);
	TestTrue(TEXT("Water depth = sea radius - ground radius"), FMath::IsNearlyEqual(Sea.WaterDepth, F.SeaLevelRadius - F.GetDistanceFromCenter(Sea.Location), 0.5f));
	TestTrue(TEXT("A point just below the sea surface is underwater"), T->IsUnderwater(S.Center + SeaDir * (F.SeaLevelRadius - 50.f)));
	TestFalse(TEXT("A point just above the sea surface is not"), T->IsUnderwater(S.Center + SeaDir * (F.SeaLevelRadius + 50.f)));
	TestTrue(TEXT("Depth below sea is radial"), FMath::IsNearlyEqual(T->GetWaterDepthAt(S.Center + SeaDir * (F.SeaLevelRadius - 120.f)), 120.f, 0.5f));
	// The same rule works on the far side of the planet (no world Z).
	const FVector Opp = -SeaDir;
	TestTrue(TEXT("Depth rule independent of world Z"), FMath::IsNearlyEqual(T->GetWaterDepthAt(S.Center + Opp * (F.SeaLevelRadius - 120.f)), 120.f, 0.5f));
	const FTerrainSample Land = T->QueryTerrainDirection(LandDir);
	TestTrue(TEXT("Land is dry"), !Land.bUnderwater && Land.WaterDepth == 0.f);
	return true;
}

SHAMAN_TERRAIN_TEST(FShamanTerrainModificationTest, "Shaman.Terrain.ModificationRequest")
bool FShamanTerrainModificationTest::RunTest(const FString&)
{
	FTestWorld W;
	UShamanTerrainSubsystem* T = W.MakePlanet(TestSettings());
	if (!TestNotNull(TEXT("Planet"), T)) return false;
	FVector D;
	TestTrue(TEXT("Land"), FindLand(T, [](const FVector&, const FTerrainSample&) { return true; }, D));
	const FVector P = T->GetSurfaceLocation(D);
	const float H0 = T->GetTerrainHeight(P);

	FTerrainModification M; M.Op = ETerrainOp::Raise; M.Center = P; M.Radius = 500.f; M.Strength = 300.f; M.Source = TEXT("Test");
	const FTerrainModificationResult R = T->ModifyTerrain(M);
	TestTrue(TEXT("Raise applied"), R.WasApplied());
	TestEqual(TEXT("First edit index"), R.EditIndex, 0);
	TestTrue(TEXT("Raise changed the height at the centre"), FMath::IsNearlyEqual(T->GetTerrainHeight(P), H0 + 300.f, 0.5f));
	TestTrue(TEXT("Edited flag"), (T->QueryTerrain(P).Flags & ETerrainFlags::Edited) != 0);
	TestTrue(TEXT("Bounds contain the edit"), FVector::Dist(R.BoundsCenter, P) <= R.BoundsRadius && R.BoundsRadius >= 500.f);
	const FVector Far = T->GetSurfaceLocation((D + FVector(0.2f, 0.2f, 0.2f)).GetSafeNormal());
	TestTrue(TEXT("Far terrain untouched"), FMath::IsNearlyEqual(T->GetTerrainHeight(Far), T->QueryTerrain(Far).Height));

	M.Op = ETerrainOp::Lower; M.Strength = 100.f;
	TestTrue(TEXT("Lower applied"), T->ModifyTerrain(M).WasApplied());
	TestTrue(TEXT("Lower"), FMath::IsNearlyEqual(T->GetTerrainHeight(P), H0 + 200.f, 0.5f));
	M.Op = ETerrainOp::Flatten; M.Strength = 1.f; M.TargetHeight = 20.f;
	TestTrue(TEXT("Flatten applied"), T->ModifyTerrain(M).WasApplied());
	TestTrue(TEXT("Flatten"), FMath::IsNearlyEqual(T->GetTerrainHeight(P), 20.f, 0.5f));
	M.Op = ETerrainOp::Paint; M.Material = ETerrainMaterial::Scorched;
	TestTrue(TEXT("Paint applied"), T->ModifyTerrain(M).WasApplied());
	TestTrue(TEXT("Paint"), T->GetTerrainMaterial(P) == ETerrainMaterial::Scorched);

	M.Op = ETerrainOp::RaisePath; // future op: backend says unsupported, nothing changes
	TestTrue(TEXT("Unsupported op rejected"), T->ModifyTerrain(M).Status == ETerrainModifyStatus::RejectedUnsupported);
	FTerrainModification Bad = M; Bad.Op = ETerrainOp::Raise; Bad.Radius = -1.f;
	TestTrue(TEXT("Invalid request rejected"), T->ModifyTerrain(Bad).Status == ETerrainModifyStatus::RejectedInvalid);
	TArray<FTerrainModification> Log; T->GetEditLog(Log);
	TestEqual(TEXT("Edit log holds only applied edits"), Log.Num(), 4);
	TestEqual(TEXT("Stats"), T->GetStats().EditsApplied, 4);
	return true;
}

SHAMAN_TERRAIN_TEST(FShamanTerrainProtectedTest, "Shaman.Terrain.ProtectedRejection")
bool FShamanTerrainProtectedTest::RunTest(const FString&)
{
	FTestWorld W;
	UShamanTerrainSubsystem* T = W.MakePlanet(TestSettings());
	if (!TestNotNull(TEXT("Planet"), T)) return false;
	FVector D;
	FindLand(T, [](const FVector&, const FTerrainSample&) { return true; }, D);
	const FVector CircleLoc = T->GetSurfaceLocation(D);
	const int32 Id = T->RegisterProtectedRegion(CircleLoc, 700.f, TEXT("ReincarnationCircle"));
	TestTrue(TEXT("Region id"), Id > 0);
	TestTrue(TEXT("Sample inside is flagged protected"), (T->QueryTerrain(CircleLoc).Flags & ETerrainFlags::Protected) != 0);

	int32 Events = 0;
	T->OnTerrainChangedNative().AddLambda([&Events](const FTerrainChangeEvent&) { ++Events; });
	const float H0 = T->GetTerrainHeight(CircleLoc);
	FTerrainModification M; M.Center = CircleLoc; M.Radius = 300.f; M.Strength = 500.f;
	const FTerrainModificationResult R = T->ModifyTerrain(M);
	TestTrue(TEXT("Edit on the circle rejected"), R.Status == ETerrainModifyStatus::RejectedProtected);
	TestEqual(TEXT("Blocking region reported"), R.BlockingRegionId, Id);
	TestEqual(TEXT("Height unchanged"), T->GetTerrainHeight(CircleLoc), H0);
	TestEqual(TEXT("No change event for a rejected edit"), Events, 0);

	// Footprint touching the edge (900 uu away, radius 300 -> overlaps a 700 uu region) is rejected too.
	const FPlanetFrame F = T->GetPlanetFrame();
	FVector Fwd, Right, Up; F.GetTangentBasis(CircleLoc, FVector(1, 0, 0), Fwd, Right, Up);
	M.Center = CircleLoc + Fwd * 900.f;
	TestTrue(TEXT("Overlapping footprint rejected"), T->ModifyTerrain(M).Status == ETerrainModifyStatus::RejectedProtected);
	M.Center = CircleLoc + Fwd * 1100.f;
	TestTrue(TEXT("Footprint outside the region allowed"), T->ModifyTerrain(M).WasApplied());
	TestTrue(TEXT("Unregister"), T->UnregisterProtectedRegion(Id));
	M.Center = CircleLoc;
	TestTrue(TEXT("Allowed after unregistering"), T->ModifyTerrain(M).WasApplied());
	return true;
}

SHAMAN_TERRAIN_TEST(FShamanTerrainChangedEventTest, "Shaman.Terrain.ChangedNotification")
bool FShamanTerrainChangedEventTest::RunTest(const FString&)
{
	FTestWorld W;
	UShamanTerrainSubsystem* T = W.MakePlanet(TestSettings());
	if (!TestNotNull(TEXT("Planet"), T)) return false;
	FVector D;
	FindLand(T, [](const FVector&, const FTerrainSample&) { return true; }, D);
	const FVector P = T->GetSurfaceLocation(D);

	TArray<FTerrainChangeEvent> Changed, Completed;
	float HeightSeenInEvent = 0.f;
	T->OnTerrainChangedNative().AddLambda([&](const FTerrainChangeEvent& E) { Changed.Add(E); HeightSeenInEvent = T->GetTerrainHeight(P); });
	T->OnTerrainUpdateCompletedNative().AddLambda([&](const FTerrainChangeEvent& E) { Completed.Add(E); });
	const float H0 = T->GetTerrainHeight(P);

	FTerrainModification M; M.Center = P; M.Radius = 400.f; M.Strength = 150.f; M.Source = TEXT("EventTest");
	const FTerrainModificationResult R = T->ModifyTerrain(M);
	TestEqual(TEXT("Exactly one change event"), Changed.Num(), 1);
	if (Changed.Num() != 1) return false;
	TestEqual(TEXT("Event carries the edit index"), Changed[0].EditIndex, R.EditIndex);
	TestTrue(TEXT("Event carries op and source"), Changed[0].Op == ETerrainOp::Raise && Changed[0].Source == FName(TEXT("EventTest")));
	TestTrue(TEXT("Event area covers the edit"), FVector::Dist(Changed[0].Center, P) <= Changed[0].Radius);
	TestTrue(TEXT("Queries already see the new terrain inside the event"), FMath::IsNearlyEqual(HeightSeenInEvent, H0 + 150.f, 0.5f));

	// Completion fires once the backend reports no pending rebuild (analytic: after the settle frames).
	for (int32 i = 0; i < 3; ++i) T->Tick(1.f / 60.f);
	TestEqual(TEXT("One completion event"), Completed.Num(), 1);
	if (Completed.Num() == 1) TestTrue(TEXT("Completion has timing"), Completed[0].UpdateMs >= 0.f && Completed[0].EditIndex == R.EditIndex);
	TestFalse(TEXT("Nothing pending afterwards"), T->IsUpdatePending());
	return true;
}

SHAMAN_TERRAIN_TEST(FShamanTerrainNavigationPrototypeTest, "Shaman.Terrain.NavigationPrototype")
bool FShamanTerrainNavigationPrototypeTest::RunTest(const FString&)
{
	FTestWorld W;
	UShamanTerrainSubsystem* T = W.MakePlanet(TestSettings());
	if (!TestNotNull(TEXT("Planet"), T)) return false;
	FVector D;
	FindLand(T, [](const FVector&, const FTerrainSample&) { return true; }, D);
	const FVector Start = T->GetSurfaceLocation(D);
	const IShamanSurfacePathfinder& Nav = FShamanSurfaceNavigation::Get();
	const FShamanSurfacePath Same = Nav.FindPath(*T, Start, Start);
	TestTrue(TEXT("Trivial path valid"), Same.bValid && !Same.bPartial && Same.Points.Num() == 1);
	// A goal across deep water gives a partial path ending on walkable ground before the water.
	FVector SeaDir; bool bSea = false;
	for (int32 i = 0; i < 4000 && !bSea; ++i)
		if (T->QueryTerrainDirection(FibDir(i, 4000)).WaterDepth > 500.f) { SeaDir = FibDir(i, 4000); bSea = true; }
	if (bSea)
	{
		const FShamanSurfacePath P = Nav.FindPath(*T, Start, T->GetSurfaceLocation(SeaDir));
		TestTrue(TEXT("Path into the sea is partial"), P.bValid && P.bPartial);
		TestTrue(TEXT("Partial path ends on walkable ground"), P.Points.Num() == 1 && T->QueryTerrain(P.Points[0]).bWalkable);
	}
	return true;
}

SHAMAN_TERRAIN_TEST(FShamanTerrainMovementTest, "Shaman.Terrain.SphericalMovement")
bool FShamanTerrainMovementTest::RunTest(const FString&)
{
	FTestWorld W;
	// Flat world first: the movement component must behave exactly like UCharacterMovementComponent.
	{
		AShamanUnitBase* U = W.World->SpawnActorDeferred<AShamanUnitBase>(AShamanUnitBase::StaticClass(), FTransform(FVector(0, 0, 5000)));
		U->AutoPossessAI = EAutoPossessAI::Disabled;
		U->FinishSpawning(FTransform(FVector(0, 0, 5000)));
		UShamanCharacterMovementComponent* M = Cast<UShamanCharacterMovementComponent>(U->GetCharacterMovement());
		TestNotNull(TEXT("Units use UShamanCharacterMovementComponent"), M);
		if (!M) return false;
		TestFalse(TEXT("Flat world: not on a planet"), M->IsOnPlanet());
		M->SetMovementMode(MOVE_Walking);
		TestTrue(TEXT("Flat world: MOVE_Walking stays MOVE_Walking"), M->MovementMode == MOVE_Walking);
		U->Destroy();
	}

	const FPlanetSettings S = TestSettings();
	UShamanTerrainSubsystem* T = W.MakePlanet(S);
	if (!TestNotNull(TEXT("Planet"), T)) return false;
	W.World->GetWorldSettings()->bEnableWorldBoundsChecks = false; // as AShamanPlanetGameMode does (KillZ is world -Z)
	// Southern-hemisphere land site, so "down" is far from world -Z.
	FVector D;
	const bool bFound = FindLand(T, [T](const FVector& Dir, const FTerrainSample& Smp)
	{
		if (Dir.Z > -0.3f) return false;
		const FPlanetFrame F = T->GetPlanetFrame();
		FVector Fwd, Right, Up; F.GetTangentBasis(Smp.Location, FVector(1, 0, 0), Fwd, Right, Up);
		for (int32 k = 1; k <= 8; ++k) // flat-ish walkable strip ahead
		{
			const FTerrainSample Q = T->QueryTerrain(Smp.Location + Fwd * 150.f * k);
			if (!Q.bWalkable || Q.bUnderwater || FMath::Abs(Q.Height - Smp.Height) > 250.f) return false;
		}
		return true;
	}, D);
	TestTrue(TEXT("Found a southern land strip"), bFound);
	if (!bFound) return false;

	const FPlanetFrame F = T->GetPlanetFrame();
	const FVector Ground = T->GetSurfaceLocation(D);
	const FVector SpawnLoc = Ground + D * 400.f; // drop from 400 uu
	AShamanUnitBase* U = W.World->SpawnActorDeferred<AShamanUnitBase>(AShamanUnitBase::StaticClass(), FTransform(FShamanSpace::UprightRotation(W.World, SpawnLoc, FVector(1, 0, 0)), SpawnLoc));
	U->AutoPossessAI = EAutoPossessAI::Disabled;
	U->FinishSpawning(FTransform(SpawnLoc));
	if (!U->HasActorBegunPlay()) U->DispatchBeginPlay();
	UShamanCharacterMovementComponent* M = Cast<UShamanCharacterMovementComponent>(U->GetCharacterMovement());
	if (!TestNotNull(TEXT("Movement"), M)) return false;
	M->bRunPhysicsWithNoController = true;
	M->SetMovementMode(MOVE_Falling);
	TestTrue(TEXT("Planet: falling is redirected to the planet mode"), M->IsFalling() && M->MovementMode == MOVE_Custom);

	// First UE4.27.2 run: driven through UWorld::Tick in this bare test world (no GameMode), the unit never moved.
	// Advance world time only, and step the movement component directly: it is the code under test.
	const float Dt = 1.f / 60.f;
	auto Step = [&](int32 Frames)
	{
		for (int32 i = 0; i < Frames; ++i)
		{
			W.World->Tick(LEVELTICK_TimeOnly, Dt);
			M->TickComponent(Dt, LEVELTICK_All, &M->PrimaryComponentTick);
		}
	};
	const FVector BeforeFall = U->GetActorLocation();
	Step(120); // 2 s: fall and land
	TestFalse(TEXT("Unit still alive (not killed by world bounds)"), U->IsPendingKill());
	TestTrue(TEXT("Movement component simulates (unit moved while falling)"), FVector::Dist(BeforeFall, U->GetActorLocation()) > 50.f);
	const float Half = U->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	auto Clearance = [&]() { return F.GetDistanceFromCenter(U->GetActorLocation()) - Half - (S.Radius + T->GetTerrainHeight(U->GetActorLocation())); };
	TestTrue(TEXT("Landed (walking on the planet)"), M->IsMovingOnGround());
	TestTrue(FString::Printf(TEXT("Standing on the ground (clearance %.1f)"), Clearance()), FMath::Abs(Clearance()) < 30.f);
	TestTrue(TEXT("Capsule up = radial up"), FVector::DotProduct(U->GetActorUpVector(), F.GetUp(U->GetActorLocation())) > 0.99f);

	// Walk "forward" along the surface for 3 s.
	FVector Fwd, Right, Up; F.GetTangentBasis(U->GetActorLocation(), FVector(1, 0, 0), Fwd, Right, Up);
	const FVector Start = U->GetActorLocation();
	float WorstClearance = 0.f;
	for (int32 i = 0; i < 180; ++i)
	{
		FVector F2, R2, U2; F.GetTangentBasis(U->GetActorLocation(), Fwd, F2, R2, U2);
		U->AddMovementInput(F2, 1.f);
		Step(1);
		WorstClearance = FMath::Max(WorstClearance, FMath::Abs(Clearance()));
	}
	const float Walked = F.GetSurfaceDistance(Start, U->GetActorLocation());
	TestTrue(FString::Printf(TEXT("Walked along the surface (%.0f uu)"), Walked), Walked > 400.f);
	TestTrue(FString::Printf(TEXT("Stayed on the ground while walking (worst clearance %.1f)"), WorstClearance), WorstClearance < 60.f && M->IsMovingOnGround());
	TestTrue(TEXT("Still upright after moving across curvature"), FVector::DotProduct(U->GetActorUpVector(), F.GetUp(U->GetActorLocation())) > 0.99f);
	TestTrue(TEXT("Facing the movement direction (orient to movement)"), FVector::DotProduct(U->GetActorForwardVector(), F.GetTangentDirectionTo(Start, U->GetActorLocation())) > 0.8f);

	// Jump: leaves the ground radially and lands again.
	U->Jump();
	Step(2);
	TestTrue(TEXT("Jump goes up (radially)"), Clearance() > 3.f && M->IsFalling());
	Step(120);
	TestTrue(TEXT("Lands after the jump"), M->IsMovingOnGround() && FMath::Abs(Clearance()) < 30.f);

	// Raising the terrain under the unit: it ends up on top, not inside.
	FTerrainModification Mod; Mod.Center = U->GetActorLocation(); Mod.Radius = 600.f; Mod.Strength = 300.f;
	TestTrue(TEXT("Raise under the unit"), T->ModifyTerrain(Mod).WasApplied());
	Step(30);
	TestTrue(FString::Printf(TEXT("On top of raised terrain (clearance %.1f)"), Clearance()), FMath::Abs(Clearance()) < 30.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
