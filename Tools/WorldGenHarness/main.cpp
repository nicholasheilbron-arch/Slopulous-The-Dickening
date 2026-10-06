// Standalone verification of FShamanWorldGenerator: determinism + start-area constraints over many seeds.
#include "World/WorldGenerator.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <map>

static bool SameLayout(const FWorldLayout& A, const FWorldLayout& B)
{
	return A.Seeds == B.Seeds && A.Heights == B.Heights && A.Markers == B.Markers && A.PlayerStart == B.PlayerStart
		&& A.EnemyStart == B.EnemyStart && A.FailMask == B.FailMask;
}

int main(int argc, char** argv)
{
	const int Count = argc > 1 ? atoi(argv[1]) : 200;
	FWorldGenConfig C;
	int Valid = 0, Ponds = 0, Retries = 0, Fail = 0;
	std::map<int,int> FailBits;
	double TotalMs = 0, MaxMs = 0;
	for (int Seed = 1; Seed <= Count; ++Seed)
	{
		FWorldSeeds S; S.WorldSeed = Seed * 7919 + 13;
		auto T0 = std::chrono::steady_clock::now();
		FWorldLayout L = FShamanWorldGenerator::Generate(C, S);
		double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
		TotalMs += Ms; if (Ms > MaxMs) MaxMs = Ms;
		if (L.bValid) ++Valid; else { ++Fail; for (int b = 0; b < 12; ++b) if (L.FailMask & (1 << b)) FailBits[b]++; }
		if (L.bPondCarved) ++Ponds;
		if (L.TerrainAttempts > 1) ++Retries;
		if (Seed <= 5)
		{
			FWorldLayout L2 = FShamanWorldGenerator::Generate(C, S);
			if (!SameLayout(L, L2)) { printf("DETERMINISM FAILURE seed %d\n", S.WorldSeed); return 1; }
		}
		if (FShamanWorldGenerator::ValidateStartArea(L, C) != L.FailMask) { printf("validate mismatch\n"); return 1; }
	}
	// Different seeds must differ.
	FWorldSeeds A; A.WorldSeed = 1; FWorldSeeds B; B.WorldSeed = 2;
	if (SameLayout(FShamanWorldGenerator::Generate(C, A), FShamanWorldGenerator::Generate(C, B))) { printf("seeds 1 and 2 identical\n"); return 1; }
	// Pond fallback: no lakes/rivers and a tight water radius must still yield valid starts by carving ponds.
	{
		FWorldGenConfig D; D.RiverWidth = 0.f; D.LakeThreshold = 2.f; D.IslandFalloffStart = 3.f; D.IslandFalloffEnd = 4.f; D.WaterSearchRadius = 2500.f;
		int PV = 0, PP = 0;
		for (int Seed = 1; Seed <= 50; ++Seed) { FWorldSeeds S; S.WorldSeed = Seed; FWorldLayout L = FShamanWorldGenerator::Generate(D, S); PV += L.bValid; PP += L.bPondCarved; }
		printf("pond-config: valid=%d/50 pondsCarved=%d\n", PV, PP);
		if (PV != 50 || PP == 0) return 3;
	}
	printf("seeds=%d valid=%d failed=%d pondsCarved=%d neededRetry=%d avgMs=%.1f maxMs=%.1f\n", Count, Valid, Fail, Ponds, Retries, TotalMs / Count, MaxMs);
	for (auto& kv : FailBits) printf("  failbit %d: %d\n", kv.first, kv.second);
	return Fail == 0 ? 0 : 2;
}
