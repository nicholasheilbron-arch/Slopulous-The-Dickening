// Off-engine verification of SHAMAN's engine-independent terrain core (FPlanetHeightField, FPlanetFrame,
// FPlanetTerrainQueries). Build: ./build.sh  (g++ with the CoreMinimal shim). Not part of the game build.
#include "CoreMinimal.h"
#include "Terrain/PlanetHeightField.h"
#include "Terrain/PlanetTerrainQueries.h"
#include <cstdio>
#include <chrono>
#include <thread>
#include <random>

static int GFail = 0, GPass = 0;
#define CHECK(cond, ...) do { if (cond) { ++GPass; } else { ++GFail; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static FVector FibDir(int i, int n)
{
	const float GA = 2.39996323f;
	const float Y = 1.f - 2.f * (i + 0.5f) / n;
	const float R = std::sqrt(std::max(0.f, 1.f - Y * Y));
	return FVector(std::cos(GA * i) * R, std::sin(GA * i) * R, Y);
}
static double NowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

int main()
{
	FPlanetSettings S;
	S.Center = FVector(1000.f, -2000.f, 500.f); // non-origin centre: nothing may assume (0,0,0)
	const int N = 20000;

	// 1. Frame basics
	{
		FPlanetFrame F(S.Center, S.Radius, S.SeaLevelOffset);
		const FVector P = S.Center + FVector(0, 0, -25000.f); // "south pole": up must point -Z
		CHECK((F.GetUp(P) - FVector(0, 0, -1)).Size() < 1e-5f, "up at south pole");
		CHECK(std::fabs(F.GetAltitude(P) - 5000.f) < 0.01f, "altitude %f", F.GetAltitude(P));
		FVector Fw, R, U; F.GetTangentBasis(P, FVector(1, 0, 0), Fw, R, U);
		CHECK(std::fabs(FVector::DotProduct(Fw, U)) < 1e-5f && std::fabs(FVector::DotProduct(R, U)) < 1e-5f && std::fabs(FVector::DotProduct(Fw, R)) < 1e-5f, "basis orthogonal");
		F.GetTangentBasis(P, FVector(0, 0, 1), Fw, R, U); // degenerate hint
		CHECK(std::fabs(Fw.Size() - 1.f) < 1e-4f, "degenerate hint fallback");
		const float D = F.GetSurfaceDistance(S.Center + FVector(1, 0, 0), S.Center + FVector(0, 1, 0));
		CHECK(std::fabs(D - S.Radius * PI * 0.5f) < 1.f, "quarter great circle %f", D);
		// Small angles: two points on the same radial line are 0 apart (acos lost ~7 uu here in float).
		const FVector Q = S.Center + FVector(0.31f, -0.52f, -0.79f).GetSafeNormal() * 20000.f;
		const FVector Q2 = S.Center + (Q - S.Center).GetSafeNormal() * 20311.7f;
		CHECK(F.GetSurfaceDistance(Q, Q2) < 0.01f, "radial-only surface distance %f", F.GetSurfaceDistance(Q, Q2));
		FVector QF, QR, QU; F.GetTangentBasis(Q, FVector(1, 0, 0), QF, QR, QU);
		const float D3 = F.GetSurfaceDistance(Q, Q + QF * 3.f);
		CHECK(std::fabs(D3 - 3.f) < 0.05f, "3 uu tangential step reads %f", D3);
	}

	// 2. Determinism + seed sensitivity
	FPlanetHeightField A(S), B(S);
	FPlanetSettings S2 = S; S2.Seed = 4242;
	FPlanetHeightField C(S2);
	{
		double Hash = 0; int Same = 0, Diff = 0;
		for (int i = 0; i < 4000; ++i)
		{
			const FVector D = FibDir(i, 4000);
			const float Ha = A.GetHeight(D), Hb = B.GetHeight(D), Hc = C.GetHeight(D);
			Same += (Ha == Hb); Diff += (std::fabs(Ha - Hc) > 1.f);
			Hash += Ha * (i % 97 + 1);
		}
		CHECK(Same == 4000, "same seed identical (%d)", Same);
		CHECK(Diff > 3000, "different seed differs (%d)", Diff);
		std::printf("height checksum seed %d: %.3f\n", S.Seed, Hash);
	}

	// 3. Shape statistics
	float MinH = 1e9f, MaxH = -1e9f; int Land = 0, Walk = 0; FVector LandDir;
	{
		const double T0 = NowMs();
		for (int i = 0; i < N; ++i)
		{
			const FVector D = FibDir(i, N);
			const float H = A.GetHeight(D);
			MinH = std::min(MinH, H); MaxH = std::max(MaxH, H);
			if (H > S.SeaLevelOffset) { ++Land; if (Land == 1) LandDir = D; }
			const FTerrainSample Sm = FPlanetTerrainQueries::SampleDirection(A, D);
			Walk += Sm.bWalkable;
			CHECK(Sm.bValid, "sample valid");
		}
		const double T1 = NowMs();
		std::printf("heights [%.0f, %.0f]  land %.1f%%  walkable %.1f%%  %d samples in %.1f ms (%.2f us/sample incl. normal)\n",
			MinH, MaxH, 100.0 * Land / N, 100.0 * Walk / N, N, T1 - T0, 1000.0 * (T1 - T0) / N);
		CHECK(Land > N * 0.25 && Land < N * 0.65, "land fraction %.2f", (double)Land / N);
		CHECK(MinH > -S.SeabedDepth - 200.f && MaxH < S.LandHeight * 1.8f + S.MountainHeight + 200.f, "height range");
		CHECK(MinH >= A.GetMinHeightBound() && MaxH <= A.GetMaxHeightBound(), "bounds conservative");
		CHECK(Walk > N * 0.2, "walkable fraction");
	}

	// 4. Normals: unit, mostly facing up, and agree with the SDF gradient
	{
		int Ok = 0, Bad = 0;
		for (int i = 0; i < 2000; ++i)
		{
			const FVector D = FibDir(i * 7 + 3, 14003);
			const FVector Nrm = A.GetSurfaceNormal(D);
			if (std::fabs(Nrm.Size() - 1.f) > 1e-3f || FVector::DotProduct(Nrm, D) <= 0.f) ++Bad;
			const FVector P = A.GetSurfacePoint(D) - S.Center;
			const float e = 20.f;
			FVector G(A.GetSignedDistance(P + FVector(e, 0, 0)) - A.GetSignedDistance(P - FVector(e, 0, 0)),
			          A.GetSignedDistance(P + FVector(0, e, 0)) - A.GetSignedDistance(P - FVector(0, e, 0)),
			          A.GetSignedDistance(P + FVector(0, 0, e)) - A.GetSignedDistance(P - FVector(0, 0, e)));
			if (FVector::DotProduct(G.GetSafeNormal(), Nrm) > 0.9f) ++Ok;
		}
		CHECK(Bad == 0, "normals unit & outward (%d bad)", Bad);
		CHECK(Ok > 1900, "normals match SDF gradient (%d/2000)", Ok);
	}

	// 5. Signed distance sign
	{
		const FVector D = FibDir(123, 1000);
		const float R = A.GetSurfaceRadius(D);
		CHECK(A.GetSignedDistance(D * (R + 50.f)) > 0.f, "above ground positive");
		CHECK(A.GetSignedDistance(D * (R - 50.f)) < 0.f, "below ground negative");
		CHECK(std::fabs(A.GetSignedDistance(D * R)) < 5.f, "on surface ~0 (%f)", A.GetSignedDistance(D * R));
		CHECK(A.GetSignedDistance(D * 1000.f) < 0.f, "deep inside negative");
		CHECK(A.GetSignedDistance(D * 40000.f) > 0.f, "far outside positive");
	}

	// 6. Raycast down onto the surface
	{
		int Hits = 0, Close = 0;
		for (int i = 0; i < 500; ++i)
		{
			const FVector D = FibDir(i, 500);
			const FVector Start = S.Center + D * (S.Radius + 6000.f), End = S.Center + D * (S.Radius - 3000.f);
			FTerrainRaycastHit H;
			if (FPlanetTerrainQueries::Raycast(A, Start, End, H)) { ++Hits; if ((H.Location - A.GetSurfacePoint(D)).Size() < 10.f) ++Close; }
		}
		CHECK(Hits == 500, "radial raycasts hit (%d)", Hits);
		CHECK(Close >= 495, "radial hits on surface (%d)", Close);
		// Tangent ray far above the planet misses
		FTerrainRaycastHit H;
		CHECK(!FPlanetTerrainQueries::Raycast(A, S.Center + FVector(-40000, 0, 30000), S.Center + FVector(40000, 0, 30000), H), "high ray misses");
		// Oblique ray through the planet hits on the near side
		const FVector St = S.Center + FVector(-40000, 300, 200), En = S.Center + FVector(40000, 300, 200);
		CHECK(FPlanetTerrainQueries::Raycast(A, St, En, H) && H.Location.X - S.Center.X < 0.f, "through-ray hits near side");
		CHECK(std::fabs(A.GetSignedDistance(H.Location - S.Center)) < 10.f, "through-ray hit on surface");
	}

	// 7. Edits: raise / lower / flatten / paint, falloff, bounds, validation
	{
		const FVector D = LandDir;
		const FVector P = A.GetSurfacePoint(D);
		const float H0 = A.GetHeight(D);
		FTerrainModification M; M.Op = ETerrainOp::Raise; M.Center = P; M.Radius = 500.f; M.Strength = 300.f;
		const double T0 = NowMs();
		const int Idx = A.AddEdit(M);
		const double T1 = NowMs();
		CHECK(Idx == 0, "first edit index");
		CHECK(std::fabs(A.GetHeight(D) - (H0 + 300.f)) < 0.5f, "raise centre %f -> %f", H0, A.GetHeight(D));
		CHECK(A.IsEdited(D), "edited flag");
		CHECK(A.GetMaxHeightBound() >= MaxH + 300.f - 1.f, "raise bound grows");
		// outside radius untouched
		FVector Far = (D + FVector(0.3f, 0.2f, 0.1f)).GetSafeNormal();
		CHECK(A.GetHeight(Far) == B.GetHeight(Far), "far untouched");
		// edge: a direction ~600uu away along the surface is outside 500 radius
		FVector T, R, U; A.GetFrame().GetTangentBasis(P, FVector(1, 0, 0), T, R, U);
		const FVector Edge = (D + T * (600.f / S.Radius)).GetSafeNormal();
		CHECK(std::fabs(A.GetHeight(Edge) - B.GetHeight(Edge)) < 0.01f, "outside radius untouched");
		const FVector Mid = (D + T * (350.f / S.Radius)).GetSafeNormal();
		const float dMid = A.GetHeight(Mid) - B.GetHeight(Mid);
		CHECK(dMid > 0.f && dMid < 300.f, "falloff in between (%f)", dMid);
		std::printf("AddEdit took %.4f ms\n", T1 - T0);

		M.Op = ETerrainOp::Lower; M.Strength = 100.f;
		A.AddEdit(M);
		CHECK(std::fabs(A.GetHeight(D) - (H0 + 200.f)) < 0.5f, "lower");
		M.Op = ETerrainOp::Flatten; M.Strength = 1.f; M.TargetHeight = 50.f;
		A.AddEdit(M);
		CHECK(std::fabs(A.GetHeight(D) - 50.f) < 0.5f, "flatten to target (%f)", A.GetHeight(D));
		M.Op = ETerrainOp::Paint; M.Material = ETerrainMaterial::Scorched;
		A.AddEdit(M);
		CHECK(A.GetMaterial(D) == ETerrainMaterial::Scorched, "paint");
		CHECK(std::fabs(A.GetHeight(D) - 50.f) < 0.5f, "paint keeps height");
		CHECK(A.GetNumEdits() == 4, "edit count");

		FTerrainModification Bad = M; Bad.Radius = -5.f;
		CHECK(A.AddEdit(Bad) == -1, "negative radius rejected");
		Bad = M; Bad.Center = S.Center;
		CHECK(A.AddEdit(Bad) == -1, "centre-at-core rejected");
		Bad = M; Bad.Strength = NAN;
		CHECK(A.AddEdit(Bad) == -1, "NaN rejected");
		CHECK(A.GetNumEdits() == 4, "rejects not appended");

		// Replay determinism: rebuild from the edit log
		FPlanetHeightField Rep(S);
		for (int i = 0; i < A.GetNumEdits(); ++i) Rep.AddEdit(A.GetEdit(i).Request);
		int Same = 0;
		for (int i = 0; i < 3000; ++i) { const FVector Dd = (i < 1500) ? (D + FibDir(i, 1500) * 0.03f).GetSafeNormal() : FibDir(i, 3000); Same += A.GetHeight(Dd) == Rep.GetHeight(Dd); }
		CHECK(Same == 3000, "replay identical (%d)", Same);

		A.ResetEdits(false); // reader-safe reset: storage not reused
		CHECK(A.GetNumEdits() == 0 && A.GetHeight(D) == H0 && !A.IsEdited(D), "reset restores");
		CHECK(A.GetRemainingCapacity() == FPlanetHeightField::MaxEdits - 4, "reader-safe reset keeps slots used (%d)", A.GetRemainingCapacity());
		FTerrainModification M2; M2.Center = P; M2.Strength = 77.f;
		CHECK(A.AddEdit(M2) == 0 && std::fabs(A.GetHeight(D) - (H0 + 77.f)) < 0.5f && A.GetEdit(0).Strength == 77.f, "edit after reset");
		A.ResetEdits(true);
		CHECK(A.GetNumEdits() == 0 && A.GetRemainingCapacity() == FPlanetHeightField::MaxEdits && A.GetHeight(D) == H0, "reclaiming reset");

		// Footprint directions cover the edit: every direction with non-zero change is within the outer ring
		TArray<FVector, TInlineAllocator<40>> Fp;
		FTerrainModification M3; M3.Center = P; M3.Radius = 800.f;
		FPlanetTerrainQueries::GetFootprintDirections(A.GetFrame(), M3, Fp);
		float MaxDist = 0.f;
		for (int i = 0; i < Fp.Num(); ++i) MaxDist = std::max(MaxDist, A.GetFrame().GetSurfaceDistance(S.Center + Fp[i], S.Center + D));
		CHECK(Fp.Num() == 37 && std::fabs(MaxDist - 800.f) < 2.f, "footprint (%d dirs, max %f)", Fp.Num(), MaxDist);
	}

	// 8. Sample flags / underwater
	{
		int Under = 0, Consistent = 0;
		for (int i = 0; i < 4000; ++i)
		{
			const FTerrainSample Sm = FPlanetTerrainQueries::SampleDirection(A, FibDir(i, 4000));
			if (Sm.bUnderwater) ++Under;
			const bool FlagU = (Sm.Flags & ETerrainFlags::Underwater) != 0;
			const bool Ok = FlagU == Sm.bUnderwater && (Sm.bUnderwater == (Sm.WaterDepth > 0.f)) && std::fabs(Sm.WaterDepth - std::max(0.f, S.SeaLevelOffset - Sm.Height)) < 0.01f
				&& (Sm.Up - FibDir(i, 4000)).Size() < 1e-4f;
			Consistent += Ok;
		}
		CHECK(Consistent == 4000, "sample consistency (%d)", Consistent);
		CHECK(Under > 1000, "some ocean (%d)", Under);
		// World-location sample from a point high above returns the ground below it
		const FVector D = FibDir(77, 400);
		const FTerrainSample Sm = FPlanetTerrainQueries::Sample(A, S.Center + D * 30000.f);
		CHECK((Sm.Location - A.GetSurfacePoint(D)).Size() < 0.5f, "sample by location");
	}

	// 9. Protected-region overlap
	{
		FPlanetFrame F = A.GetFrame();
		const FVector D = FibDir(5, 50);
		FTerrainProtectedRegion Reg; Reg.Id = 1; Reg.Center = A.GetSurfacePoint(D); Reg.Radius = 400.f;
		FVector T, R, U; F.GetTangentBasis(Reg.Center, FVector(1, 0, 0), T, R, U);
		FTerrainModification M; M.Radius = 500.f;
		M.Center = S.Center + (D + T * (800.f / S.Radius)).GetSafeNormal() * (S.Radius + 3000.f); // altitude irrelevant
		CHECK(FPlanetTerrainQueries::Overlaps(F, Reg, M), "overlap at 800 < 900");
		M.Center = S.Center + (D + T * (1000.f / S.Radius)).GetSafeNormal() * S.Radius;
		CHECK(!FPlanetTerrainQueries::Overlaps(F, Reg, M), "no overlap at 1000 > 900");
		M.Op = ETerrainOp::RaisePath; M.Radius = 200.f;
		M.Center = S.Center + (D - T * (3000.f / S.Radius)).GetSafeNormal() * S.Radius;
		M.PathEnd = S.Center + (D + T * (3000.f / S.Radius)).GetSafeNormal() * S.Radius;
		CHECK(FPlanetTerrainQueries::Overlaps(F, Reg, M), "path crossing region overlaps");
		M.Center = S.Center + (D + R * (2000.f / S.Radius) - T * (3000.f / S.Radius)).GetSafeNormal() * S.Radius;
		M.PathEnd = S.Center + (D + R * (2000.f / S.Radius) + T * (3000.f / S.Radius)).GetSafeNormal() * S.Radius;
		CHECK(!FPlanetTerrainQueries::Overlaps(F, Reg, M), "parallel path does not overlap");
	}

	// 10. Concurrency: readers evaluate while the game thread appends edits (voxel meshing pattern)
	{
		FPlanetHeightField H(S);
		std::atomic<bool> Stop{false}; std::atomic<long> Reads{0}; std::atomic<int> Nan{0};
		std::vector<std::thread> Th;
		for (int t = 0; t < 4; ++t) Th.emplace_back([&, t] {
			std::mt19937 Rng(t); std::uniform_real_distribution<float> U(-1.f, 1.f);
			while (!Stop.load()) { const FVector D = FVector(U(Rng), U(Rng), U(Rng)).GetSafeNormal(); if (D.SizeSquared() < 0.5f) continue;
				const float V = H.GetSignedDistance(D * (S.Radius + U(Rng) * 2000.f)); if (!std::isfinite(V)) ++Nan; ++Reads; }
		});
		for (int i = 0; i < 2000; ++i)
		{
			FTerrainModification M; M.Op = (i & 1) ? ETerrainOp::Raise : ETerrainOp::Lower; M.Center = S.Center + FibDir(i, 2000) * S.Radius; M.Strength = 50.f;
			H.AddEdit(M);
			if ((i & 63) == 0) std::this_thread::sleep_for(std::chrono::microseconds(500)); // let readers overlap the appends
		}
		while (Reads.load() < 20000) std::this_thread::yield();
		H.ResetEdits(false); // concurrent-safe reset while readers run
		for (int i = 0; i < 200; ++i) { FTerrainModification M; M.Center = S.Center + FibDir(i, 200) * S.Radius; H.AddEdit(M); }
		Stop = true; for (auto& t : Th) t.join();
		CHECK(H.GetNumEdits() == 200, "edits after concurrent reset");
		H.ResetEdits(true);
		for (int i = 0; i < 2000; ++i) { FTerrainModification M; M.Center = S.Center + FibDir(i, 2000) * S.Radius; M.Strength = 50.f; H.AddEdit(M); }
		CHECK(Nan == 0 && Reads > 1000, "concurrent reads finite (%ld reads)", Reads.load());
		CHECK(H.GetNumEdits() == 2000, "concurrent edits appended");
		// capacity
		FTerrainModification M; M.Center = S.Center + FVector(0, 0, S.Radius);
		int Last = 0; for (int i = H.GetNumEdits(); i < FPlanetHeightField::MaxEdits + 5; ++i) Last = H.AddEdit(M);
		CHECK(Last == -1 && H.GetNumEdits() == FPlanetHeightField::MaxEdits, "capacity enforced");
		// cost of evaluating with a full edit log (worst case for meshing)
		const double T0 = NowMs(); double Acc = 0;
		for (int i = 0; i < 20000; ++i) Acc += H.GetSignedDistance(FibDir(i, 20000) * (S.Radius + 100.f));
		std::printf("SDF eval with %d edits: %.3f us/eval (acc %.1f)\n", H.GetNumEdits(), 1000.0 * (NowMs() - T0) / 20000, Acc);
	}

	std::printf("\n%d passed, %d failed\n", GPass, GFail);
	return GFail ? 1 : 0;
}
