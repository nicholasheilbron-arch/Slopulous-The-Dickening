#pragma once
#include "CoreMinimal.h"
#include "World/WorldGenTypes.h"

/** Small deterministic RNG (SplitMix64). Used instead of FRandomStream so the generator's output
 *  depends only on this file and can be verified outside the engine. */
struct SHAMAN_API FShamanRng
{
	uint64 State;
	explicit FShamanRng(uint64 Seed) : State(Seed) {}
	uint64 Next64()
	{
		uint64 Z = (State += 0x9E3779B97F4A7C15ull);
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
		return Z ^ (Z >> 31);
	}
	float NextFloat() { return (float)(Next64() >> 40) / 16777216.f; }        // [0,1)
	float Range(float A, float B) { return A + (B - A) * NextFloat(); }
	int32 RangeInt(int32 Min, int32 Max) { return Min + (int32)(Next64() % (uint64)(Max - Min + 1)); } // inclusive
};

/**
 * Pure, engine-independent world generation (no UWorld, no actors).
 * Same seeds + same config => identical FWorldLayout. Spawning lives in AShamanGameMode.
 * Generation is "controlled": terrain is noise, but the start area is chosen and repaired until it
 * satisfies the gameplay constraints (water, food, wood, stone, ore, Wildmen, enemy tribe, reachable).
 */
class SHAMAN_API FShamanWorldGenerator
{
public:
	/** Fills any zero sub-seed from WorldSeed. Non-zero sub-seeds are kept (so a single one can be overridden). */
	static FWorldSeeds DeriveSeeds(const FWorldSeeds& In);

	static FWorldLayout Generate(const FWorldGenConfig& Config, const FWorldSeeds& Seeds);

	/** Re-checks the start-area rules on a finished layout. Returns EStartAreaFail bit mask (0 = valid). */
	static int32 ValidateStartArea(const FWorldLayout& Layout, const FWorldGenConfig& Config);

	// --- Layout queries (also used by spawning) ---
	static float GetHeightAt(const FWorldLayout& L, const FVector2D& P);   // bilinear
	static bool IsInsideMap(const FWorldLayout& L, const FVector2D& P, float Margin = 0.f);
	static FVector2D VertexToWorld(const FWorldLayout& L, int32 X, int32 Y);
	static float HeightAtVertex(const FWorldLayout& L, int32 X, int32 Y) { return L.Heights[X * L.GridVertices + Y]; }

	// --- Noise (exposed for tests) ---
	static float GradientNoise(float X, float Y, uint32 Seed);   // ~[-1,1]
	static float Fbm(float X, float Y, uint32 Seed, int32 Octaves);
};
