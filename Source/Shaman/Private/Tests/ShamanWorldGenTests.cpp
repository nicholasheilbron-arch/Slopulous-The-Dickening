#include "Misc/AutomationTest.h"
#include "World/WorldGenerator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ShamanWorldGenTestsPrivate
{
	static bool SameLayout(const FWorldLayout& A, const FWorldLayout& B)
	{
		return A.Seeds == B.Seeds && A.Heights == B.Heights && A.Markers == B.Markers
			&& A.PlayerStart == B.PlayerStart && A.EnemyStart == B.EnemyStart && A.FailMask == B.FailMask;
	}
	static int32 Count(const FWorldLayout& L, EWorldMarkerType T, int32 Tribe)
	{
		int32 N = 0;
		for (const FWorldMarker& M : L.Markers) if (M.Type == T && (Tribe == -99 || M.TribeIndex == Tribe)) ++N;
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanWorldGenDeterminismTest, "Shaman.World.Determinism",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanWorldGenDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ShamanWorldGenTestsPrivate;
	FWorldGenConfig C;
	for (int32 Seed : { 1, 1337, 424242 })
	{
		FWorldSeeds S; S.WorldSeed = Seed;
		const FWorldLayout A = FShamanWorldGenerator::Generate(C, S);
		const FWorldLayout B = FShamanWorldGenerator::Generate(C, S);
		TestTrue(FString::Printf(TEXT("Seed %d reproduces the same world"), Seed), SameLayout(A, B));
	}
	FWorldSeeds S1; S1.WorldSeed = 1;
	FWorldSeeds S2; S2.WorldSeed = 2;
	TestFalse(TEXT("Different seeds give different worlds"), SameLayout(FShamanWorldGenerator::Generate(C, S1), FShamanWorldGenerator::Generate(C, S2)));

	// Sub-seeds: derived deterministically, explicit overrides preserved, and they change the result.
	const FWorldSeeds D1 = FShamanWorldGenerator::DeriveSeeds(S1);
	TestTrue(TEXT("DeriveSeeds is stable"), D1 == FShamanWorldGenerator::DeriveSeeds(S1));
	TestTrue(TEXT("All sub-seeds non-zero"), D1.BiomeSeed && D1.ResourceSeed && D1.TribeSeed && D1.TerrainSeed);
	FWorldSeeds O = S1; O.ResourceSeed = 777;
	TestEqual(TEXT("Explicit ResourceSeed kept"), FShamanWorldGenerator::DeriveSeeds(O).ResourceSeed, 777);
	const FWorldLayout Base = FShamanWorldGenerator::Generate(C, S1);
	const FWorldLayout Res = FShamanWorldGenerator::Generate(C, O);
	TestTrue(TEXT("ResourceSeed override keeps terrain"), Base.Heights == Res.Heights);
	TestFalse(TEXT("ResourceSeed override moves resources"), Base.Markers == Res.Markers);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanWorldGenStartAreaTest, "Shaman.World.StartAreaConstraints",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FShamanWorldGenStartAreaTest::RunTest(const FString& Parameters)
{
	using namespace ShamanWorldGenTestsPrivate;
	FWorldGenConfig C;
	for (int32 I = 1; I <= 40; ++I)
	{
		FWorldSeeds S; S.WorldSeed = I * 7919 + 13;
		const FWorldLayout L = FShamanWorldGenerator::Generate(C, S);
		const FString P = FString::Printf(TEXT("Seed %d: "), S.WorldSeed);
		TestTrue(P + TEXT("start area valid"), L.bValid);
		TestEqual(P + TEXT("FailMask"), L.FailMask, 0);
		TestEqual(P + TEXT("one player Shaman"), Count(L, EWorldMarkerType::Shaman, 0), 1);
		TestEqual(P + TEXT("3 Braves"), Count(L, EWorldMarkerType::Brave, 0), C.PlayerBraves);
		TestEqual(P + TEXT("one House"), Count(L, EWorldMarkerType::House, 0), 1);
		TestEqual(P + TEXT("one Campfire"), Count(L, EWorldMarkerType::Campfire, 0), 1);
		TestEqual(P + TEXT("one Reincarnation Circle"), Count(L, EWorldMarkerType::ReincarnationCircle, 0), 1);
		TestEqual(P + TEXT("enemy Shaman"), Count(L, EWorldMarkerType::Shaman, 1), 1);
		TestTrue(P + TEXT("enemy settlement"), Count(L, EWorldMarkerType::ReincarnationCircle, 1) == 1 && Count(L, EWorldMarkerType::House, 1) == 1);
		TestTrue(P + TEXT("Wildmen"), Count(L, EWorldMarkerType::Wildman, -99) > 0);
		const float EnemyDist = FVector2D::Distance(L.PlayerStart, L.EnemyStart);
		TestTrue(P + TEXT("enemy nearby but not adjacent"), EnemyDist >= C.EnemyDistMin && EnemyDist <= C.EnemyDistMax);
		// Every placed object stands on dry land inside the map.
		bool bAllDry = true;
		for (const FWorldMarker& M : L.Markers)
			bAllDry &= FShamanWorldGenerator::IsInsideMap(L, M.Location) && FShamanWorldGenerator::GetHeightAt(L, M.Location) > 0.f;
		TestTrue(P + TEXT("all markers on dry land"), bAllDry);
	}
	return true;
}

#endif
