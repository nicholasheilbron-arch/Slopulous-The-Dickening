#include "World/WorldGenerator.h"

// NOTE: this file must stay engine-independent (only CoreMinimal types: TArray, FVector2D, FMath)
// so it can be verified outside Unreal by Tools/WorldGenHarness.

namespace ShamanWorldGenPrivate
{
	static const float SeaLevel = 0.f;
	static const float WalkableMinHeight = 20.f;   // vertices at or below this count as shore/water

	static uint32 Hash32(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352du;
		X ^= X >> 15; X *= 0x846ca68bu;
		X ^= X >> 16;
		return X;
	}
	static uint32 HashCell(int32 IX, int32 IY, uint32 Seed)
	{
		return Hash32(((uint32)IX * 0x8da6b343u) ^ Hash32(((uint32)IY * 0xd8163841u) ^ Seed));
	}
	static uint64 SplitMix(uint64 X)
	{
		X += 0x9E3779B97F4A7C15ull;
		X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
		X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
		return X ^ (X >> 31);
	}
	static float SmoothStep01(float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * (3.f - 2.f * T); }
	static float SmoothStepRange(float A, float B, float X) { return SmoothStep01((X - A) / (B - A)); }
	static FVector2D Dir(float AngleRad) { return FVector2D(FMath::Cos(AngleRad), FMath::Sin(AngleRad)); }
	static float DegToRad(float D) { return D * (PI / 180.f); }

	struct FCandidate { int32 X; int32 Y; float Score; float WaterDist; int32 Index; };

	/** Working context shared by the placement helpers. */
	struct FGen
	{
		FWorldLayout& L;
		const FWorldGenConfig& C;
		int32 N;
		FGen(FWorldLayout& InL, const FWorldGenConfig& InC) : L(InL), C(InC), N(InL.GridVertices) {}

		float H(int32 X, int32 Y) const { return L.Heights[X * N + Y]; }

		void WorldToVertex(const FVector2D& P, int32& OutX, int32& OutY) const
		{
			OutX = FMath::Clamp(FMath::FloorToInt((P.X + L.HalfSize) / L.CellSize + 0.5f), 0, N - 1);
			OutY = FMath::Clamp(FMath::FloorToInt((P.Y + L.HalfSize) / L.CellSize + 0.5f), 0, N - 1);
		}

		float Slope(int32 X, int32 Y) const
		{
			const float H0 = H(X, Y);
			float M = 0.f;
			if (X > 0)     M = FMath::Max(M, FMath::Abs(H(X - 1, Y) - H0));
			if (X < N - 1) M = FMath::Max(M, FMath::Abs(H(X + 1, Y) - H0));
			if (Y > 0)     M = FMath::Max(M, FMath::Abs(H(X, Y - 1) - H0));
			if (Y < N - 1) M = FMath::Max(M, FMath::Abs(H(X, Y + 1) - H0));
			return M;
		}

		/** Dry and not too steep: valid for placing things. */
		bool IsWalkableVertex(int32 X, int32 Y) const
		{
			return H(X, Y) > WalkableMinHeight && Slope(X, Y) <= C.MaxWalkableStep;
		}
		/** Reachable on foot, including wading through water shallower than FordableDepth. */
		bool IsTraversableVertex(int32 X, int32 Y) const
		{
			return H(X, Y) > SeaLevel - C.FordableDepth && Slope(X, Y) <= C.MaxWalkableStep;
		}

		/** Walkable, dry, not at the map edge. Used for every placement. */
		bool IsGoodGround(const FVector2D& P) const
		{
			if (!FShamanWorldGenerator::IsInsideMap(L, P, L.CellSize * 4.f)) return false;
			int32 X, Y; WorldToVertex(P, X, Y);
			return IsWalkableVertex(X, Y) && FShamanWorldGenerator::GetHeightAt(L, P) > 40.f;
		}

		/** Flood fill of walkable vertices from P. */
		void Reach(const FVector2D& P, TArray<uint8>& Out) const
		{
			Out.Init(0, N * N);
			int32 SX, SY; WorldToVertex(P, SX, SY);
			if (!IsTraversableVertex(SX, SY)) return;
			TArray<int32> Stack;
			Stack.Add(SX * N + SY);
			Out[SX * N + SY] = 1;
			while (Stack.Num() > 0)
			{
				const int32 I = Stack[Stack.Num() - 1];
				Stack.RemoveAt(Stack.Num() - 1);
				const int32 X = I / N, Y = I % N;
				const int32 DX[4] = { 1, -1, 0, 0 };
				const int32 DY[4] = { 0, 0, 1, -1 };
				for (int32 K = 0; K < 4; ++K)
				{
					const int32 NX = X + DX[K], NY = Y + DY[K];
					if (NX < 0 || NY < 0 || NX >= N || NY >= N) continue;
					const int32 NI = NX * N + NY;
					if (Out[NI] || !IsTraversableVertex(NX, NY)) continue;
					if (FMath::Abs(H(NX, NY) - H(X, Y)) > C.MaxWalkableStep) continue;
					Out[NI] = 1;
					Stack.Add(NI);
				}
			}
		}

		bool IsReached(const TArray<uint8>& R, const FVector2D& P) const
		{
			int32 X, Y; WorldToVertex(P, X, Y);
			return R[X * N + Y] != 0;
		}

		float NearestWaterDist(const FVector2D& P, float MaxDist) const
		{
			const int32 R = FMath::FloorToInt(MaxDist / L.CellSize) + 1;
			int32 CX, CY; WorldToVertex(P, CX, CY);
			float Best = 1e9f;
			for (int32 X = FMath::Max(0, CX - R); X <= FMath::Min(N - 1, CX + R); ++X)
				for (int32 Y = FMath::Max(0, CY - R); Y <= FMath::Min(N - 1, CY + R); ++Y)
				{
					if (H(X, Y) >= SeaLevel) continue;
					const float D = FVector2D::Distance(FShamanWorldGenerator::VertexToWorld(L, X, Y), P);
					if (D < Best) Best = D;
				}
			return Best;
		}

		bool IsFlat(int32 CX, int32 CY) const
		{
			const int32 R = FMath::FloorToInt(C.StartFlatRadius / L.CellSize);
			if (CX - R < 0 || CY - R < 0 || CX + R >= N || CY + R >= N) return false;
			float Lo = 1e9f, Hi = -1e9f;
			for (int32 X = CX - R; X <= CX + R; ++X)
				for (int32 Y = CY - R; Y <= CY + R; ++Y)
				{
					if ((X - CX) * (X - CX) + (Y - CY) * (Y - CY) > R * R) continue;
					const float V = H(X, Y);
					if (V <= WalkableMinHeight) return false;
					Lo = FMath::Min(Lo, V); Hi = FMath::Max(Hi, V);
				}
			return Hi - Lo <= C.StartMaxHeightDelta;
		}

		void AddMarker(EWorldMarkerType T, const FVector2D& P, float Yaw, int32 Tribe)
		{
			FWorldMarker M; M.Type = T; M.Location = P; M.Yaw = Yaw; M.TribeIndex = Tribe;
			L.Markers.Add(M);
		}

		bool IsFree(const FVector2D& P, float Spacing) const
		{
			const float S2 = Spacing * Spacing;
			for (int32 I = 0; I < L.Markers.Num(); ++I)
				if (FVector2D::DistSquared(L.Markers[I].Location, P) < S2) return false;
			return true;
		}

		bool ClearOfSettlements(const FVector2D& P) const
		{
			const float R2 = C.SettlementClearRadius * C.SettlementClearRadius;
			return FVector2D::DistSquared(P, L.PlayerStart) >= R2 && FVector2D::DistSquared(P, L.EnemyStart) >= R2;
		}

		/** Uniform-area scatter in a ring. Returns how many were placed. */
		int32 Scatter(EWorldMarkerType T, int32 Count, const FVector2D& Center, float MinR, float MaxR, FShamanRng& Rng,
			const FVector2D* LimitCenter = nullptr, float LimitRadius = 0.f)
		{
			int32 Placed = 0;
			const int32 MaxTries = FMath::Max(40, Count * 40);
			for (int32 Try = 0; Try < MaxTries && Placed < Count; ++Try)
			{
				const float A = Rng.Range(0.f, 2.f * PI);
				const float D = FMath::Sqrt(Rng.Range(MinR * MinR, MaxR * MaxR));
				const FVector2D Q = Center + Dir(A) * D;
				const float Yaw = Rng.Range(0.f, 360.f);
				if (LimitCenter && FVector2D::Distance(Q, *LimitCenter) > LimitRadius) continue;
				if (!IsGoodGround(Q) || !ClearOfSettlements(Q) || !IsFree(Q, C.MinObjectSpacing)) continue;
				AddMarker(T, Q, Yaw, -1);
				++Placed;
			}
			return Placed;
		}

		/** Circle at the centre, house, campfire, shaman and followers around it. Tries 12 orientations. */
		bool PlaceSettlement(const FVector2D& Center, int32 Tribe, int32 Braves, int32 Warriors, float BaseAngleDeg)
		{
			for (int32 K = 0; K < 12; ++K)
			{
				const float A = BaseAngleDeg + K * 30.f;
				const FVector2D House = Center + Dir(DegToRad(A + 90.f)) * 650.f;
				const FVector2D Fire = Center + Dir(DegToRad(A - 60.f)) * 480.f;
				const FVector2D Shaman = Center + Dir(DegToRad(A + 180.f)) * 260.f;
				if (!IsGoodGround(Center) || !IsGoodGround(House) || !IsGoodGround(Fire) || !IsGoodGround(Shaman)) continue;

				AddMarker(EWorldMarkerType::ReincarnationCircle, Center, A, Tribe);
				AddMarker(EWorldMarkerType::House, House, A + 180.f, Tribe);
				AddMarker(EWorldMarkerType::Campfire, Fire, 0.f, Tribe);
				AddMarker(EWorldMarkerType::Shaman, Shaman, A, Tribe);
				for (int32 B = 0; B < Braves; ++B)
				{
					const float BA = DegToRad(A + B * (360.f / FMath::Max(1, Braves)));
					FVector2D P = Fire + Dir(BA) * 220.f;
					if (!IsGoodGround(P)) P = Center + Dir(BA) * 150.f;
					AddMarker(EWorldMarkerType::Brave, P, A, Tribe);
				}
				for (int32 W = 0; W < Warriors; ++W)
				{
					const float WA = DegToRad(A + 40.f + W * 50.f);
					FVector2D P = House + Dir(WA) * 260.f;
					if (!IsGoodGround(P)) P = Center + Dir(WA) * 180.f;
					AddMarker(EWorldMarkerType::Warrior, P, A, Tribe);
				}
				return true;
			}
			return false;
		}

		void CarvePond(const FVector2D& P, float Radius)
		{
			int32 CX, CY; WorldToVertex(P, CX, CY);
			const int32 R = FMath::FloorToInt(Radius / L.CellSize) + 1;
			for (int32 X = FMath::Max(0, CX - R); X <= FMath::Min(N - 1, CX + R); ++X)
				for (int32 Y = FMath::Max(0, CY - R); Y <= FMath::Min(N - 1, CY + R); ++Y)
				{
					const float D = FVector2D::Distance(FShamanWorldGenerator::VertexToWorld(L, X, Y), P);
					if (D > Radius) continue;
					float& V = L.Heights[X * N + Y];
					const float Target = FMath::Lerp(-220.f, V, SmoothStep01(D / Radius));
					V = FMath::Min(V, Target);
				}
			L.bPondCarved = true;
		}
	};

	static void BuildTerrain(FWorldLayout& L, const FWorldGenConfig& C, uint32 TSeed)
	{
		const int32 N = L.GridVertices;
		L.Heights.Init(0.f, N * N);
		const float F = C.NoiseFrequency;
		float Lo = 1e9f, Hi = -1e9f;
		for (int32 X = 0; X < N; ++X)
		{
			for (int32 Y = 0; Y < N; ++Y)
			{
				const float U = (float)X / (float)(N - 1);
				const float V = (float)Y / (float)(N - 1);
				const float Hill = FShamanWorldGenerator::Fbm(U * F, V * F, TSeed, 5);
				const float RidgeBase = 1.f - FMath::Abs(FShamanWorldGenerator::Fbm(U * F * 0.8f + 17.3f, V * F * 0.8f + 4.1f, TSeed ^ 0xA5A5A5A5u, 4));
				const float Ridge = RidgeBase * RidgeBase * RidgeBase;
				const float MountainMask = SmoothStepRange(C.MountainMaskStart, C.MountainMaskEnd,
					FShamanWorldGenerator::Fbm(U * 1.6f + 3.7f, V * 1.6f + 9.2f, TSeed ^ 0x5A5A5A5Au, 2));
				float Land = C.BaseLandHeight + Hill * C.HillHeight + Ridge * MountainMask * C.MountainHeight;

				// Lakes: deep basins away from mountains.
				const float LakeMask = SmoothStepRange(C.LakeThreshold, C.LakeThreshold + 0.1f,
					FShamanWorldGenerator::Fbm(U * 2.2f + 21.f, V * 2.2f + 7.f, TSeed ^ 0x77777777u, 3)) * (1.f - MountainMask);
				Land = FMath::Lerp(Land, C.LakeDepth, LakeMask);

				// Rivers: shallow, wadeable channels along a noise zero-crossing.
				if (C.RiverWidth > 0.f)
				{
					const float RiverN = FMath::Abs(FShamanWorldGenerator::Fbm(U * 1.7f + 31.f, V * 1.7f + 13.f, TSeed ^ 0x99999999u, 3));
					const float RiverMask = 1.f - SmoothStepRange(0.f, C.RiverWidth, RiverN);
					Land = FMath::Lerp(Land, FMath::Min(Land, C.RiverDepth), RiverMask * (1.f - MountainMask));
				}

				const float DX = U - 0.5f, DY = V - 0.5f;
				const float R = FMath::Sqrt(DX * DX + DY * DY) * 2.f
					+ 0.18f * FShamanWorldGenerator::Fbm(U * 2.5f + 11.f, V * 2.5f + 5.f, TSeed ^ 0x3C3C3C3Cu, 3);
				const float Island = 1.f - SmoothStepRange(C.IslandFalloffStart, C.IslandFalloffEnd, R);
				const float Height = FMath::Lerp(C.SeabedDepth, Land, Island);
				L.Heights[X * N + Y] = Height;
				Lo = FMath::Min(Lo, Height); Hi = FMath::Max(Hi, Height);
			}
		}
		L.MinHeight = Lo; L.MaxHeight = Hi;
	}

	/** Chooses sites and places everything. Returns true when validation passes. */
	static bool PlaceAll(FWorldLayout& L, const FWorldGenConfig& C, const FWorldSeeds& S, int32 Attempt)
	{
		L.Markers.Reset();
		L.bPondCarved = false;
		FGen G(L, C);
		const int32 N = L.GridVertices;
		FShamanRng TribeRng(SplitMix((uint64)(uint32)S.TribeSeed + (uint64)Attempt * 0x1000193ull));

		// 1) Candidate settlement sites: flat, dry, moderate height.
		TArray<FCandidate> Cands;
		const int32 Margin = 6;
		for (int32 X = Margin; X < N - Margin; X += 3)
		{
			for (int32 Y = Margin; Y < N - Margin; Y += 3)
			{
				const float Hh = G.H(X, Y);
				const float Jitter = TribeRng.NextFloat(); // consumed for every vertex so the stream stays aligned
				if (!G.IsWalkableVertex(X, Y) || Hh < C.StartMinHeight || Hh > C.StartMaxHeight || !G.IsFlat(X, Y)) continue;
				const FVector2D P = FShamanWorldGenerator::VertexToWorld(L, X, Y);
				FCandidate Cd;
				Cd.X = X; Cd.Y = Y; Cd.Index = Cands.Num();
				Cd.WaterDist = G.NearestWaterDist(P, C.WaterSearchRadius * 1.5f);
				const float RN = P.Size() / L.HalfSize;
				Cd.Score = Jitter * 0.5f;
				if (Cd.WaterDist >= 900.f && Cd.WaterDist <= C.WaterSearchRadius) Cd.Score += 1.f;
				if (RN >= 0.2f && RN <= 0.65f) Cd.Score += 0.7f;
				Cands.Add(Cd);
			}
		}
		if (Cands.Num() < 2) return false;
		Cands.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score != B.Score ? A.Score > B.Score : A.Index < B.Index; });

		// 2) Player site + enemy site + Wildmen camp, all mutually reachable on foot.
		bool bFound = false;
		TArray<uint8> Reach;
		const int32 MaxPlayerTries = FMath::Min(Cands.Num(), 24);
		for (int32 PI_ = 0; PI_ < MaxPlayerTries && !bFound; ++PI_)
		{
			const FCandidate& PC = Cands[PI_];
			const FVector2D P = FShamanWorldGenerator::VertexToWorld(L, PC.X, PC.Y);
			G.Reach(P, Reach);

			int32 BestEnemy = -1;
			for (int32 E = 0; E < Cands.Num(); ++E)
			{
				if (E == PI_) continue;
				const FVector2D EP = FShamanWorldGenerator::VertexToWorld(L, Cands[E].X, Cands[E].Y);
				const float D = FVector2D::Distance(P, EP);
				if (D < C.EnemyDistMin || D > C.EnemyDistMax || !G.IsReached(Reach, EP)) continue;
				BestEnemy = E; // candidates are score-sorted; first hit is best
				break;
			}
			if (BestEnemy < 0) continue;
			const FVector2D EP = FShamanWorldGenerator::VertexToWorld(L, Cands[BestEnemy].X, Cands[BestEnemy].Y);

			bool bCamp = false;
			FVector2D Camp(0.f, 0.f);
			const float BaseA = TribeRng.Range(0.f, 2.f * PI);
			for (int32 K = 0; K < 24 && !bCamp; ++K)
			{
				const float D = TribeRng.Range(C.WildmenDistMin, C.WildmenDistMax);
				const FVector2D Q = P + Dir(BaseA + K * (2.f * PI / 24.f)) * D;
				if (!G.IsGoodGround(Q) || !G.IsReached(Reach, Q)) continue;
				if (FVector2D::Distance(Q, EP) < 0.45f * FVector2D::Distance(P, EP)) continue;
				Camp = Q; bCamp = true;
			}
			if (!bCamp) continue;

			L.PlayerStart = P; L.EnemyStart = EP; L.WildmenCamp = Camp;
			L.Markers.Reset();
			if (!G.PlaceSettlement(P, 0, C.PlayerBraves, 0, TribeRng.Range(0.f, 360.f))) continue;
			if (!G.PlaceSettlement(EP, 1, C.EnemyBraves, C.EnemyWarriors, TribeRng.Range(0.f, 360.f))) continue;

			// 3) Guarantee water near the start.
			if (PC.WaterDist > C.WaterSearchRadius && C.bCarvePondIfNoWater)
			{
				const FVector2D Away = (P - EP).GetSafeNormal();
				const float AwayA = FMath::Atan2(Away.Y, Away.X);
				for (int32 K = 0; K < 8; ++K)
				{
					const float A = AwayA + (K % 2 == 0 ? 1.f : -1.f) * ((K + 1) / 2) * DegToRad(45.f);
					const FVector2D Q = P + Dir(A) * FMath::Max(1250.f, FMath::Min(1900.f, C.WaterSearchRadius * 0.6f));
					if (!FShamanWorldGenerator::IsInsideMap(L, Q, 1500.f)) continue;
					if (FVector2D::Distance(Q, Camp) < 1100.f) continue;
					G.CarvePond(Q, 500.f); // centre >= 1250 from start keeps the settlement dry
					break;
				}
			}
			bFound = true;
		}
		if (!bFound) return false;

		// 4) Wildmen.
		FShamanRng ResRng(SplitMix((uint64)(uint32)S.ResourceSeed + (uint64)Attempt * 0x9E37ull));
		{
			int32 Placed = 0;
			for (int32 Try = 0; Try < C.Wildmen * 30 && Placed < C.Wildmen; ++Try)
			{
				const FVector2D Q = L.WildmenCamp + Dir(ResRng.Range(0.f, 2.f * PI)) * ResRng.Range(0.f, 380.f);
				if (!G.IsGoodGround(Q) || !G.IsFree(Q, 110.f)) continue;
				G.AddMarker(EWorldMarkerType::Wildman, Q, ResRng.Range(0.f, 360.f), -1);
				++Placed;
			}
		}

		// 5) Start-area resources (wood in two forest clusters, then food/stone/ore rings).
		const FVector2D P = L.PlayerStart;
		const float StartLimit = C.StartAreaRadius * 0.9f;
		int32 Trees = 0;
		for (int32 Cluster = 0; Cluster < 2; ++Cluster)
		{
			for (int32 K = 0; K < 16; ++K)
			{
				const FVector2D CC = P + Dir(ResRng.Range(0.f, 2.f * PI)) * ResRng.Range(1300.f, 2800.f);
				if (!G.IsGoodGround(CC) || !G.ClearOfSettlements(CC)) continue;
				Trees += G.Scatter(EWorldMarkerType::Tree, C.StartTrees / 2, CC, 0.f, 650.f, ResRng, &P, StartLimit);
				break;
			}
		}
		if (Trees < C.StartTrees)
			G.Scatter(EWorldMarkerType::Tree, C.StartTrees - Trees, P, C.SettlementClearRadius, StartLimit, ResRng);
		G.Scatter(EWorldMarkerType::Food, C.StartFood, P, C.SettlementClearRadius, 2600.f, ResRng);
		G.Scatter(EWorldMarkerType::Stone, C.StartStone, P, 1400.f, 3400.f, ResRng);
		G.Scatter(EWorldMarkerType::Ore, C.StartOre, P, 1800.f, 3600.f, ResRng);

		// 6) Enemy-area resources.
		G.Scatter(EWorldMarkerType::Tree, C.EnemyTrees, L.EnemyStart, 1000.f, 2500.f, ResRng);
		G.Scatter(EWorldMarkerType::Food, C.EnemyFood, L.EnemyStart, C.SettlementClearRadius, 2200.f, ResRng);

		// 7) World scatter: forests follow biome noise; minerals and food anywhere on land.
		FShamanRng BiomeRng(SplitMix((uint64)(uint32)S.BiomeSeed + (uint64)Attempt * 0x51EDull));
		{
			int32 Placed = 0;
			for (int32 Try = 0; Try < C.WorldTrees * 8 && Placed < C.WorldTrees; ++Try)
			{
				const FVector2D Q(BiomeRng.Range(-L.HalfSize, L.HalfSize), BiomeRng.Range(-L.HalfSize, L.HalfSize));
				const float Yaw = BiomeRng.Range(0.f, 360.f);
				const float U = (Q.X + L.HalfSize) / (2.f * L.HalfSize), V = (Q.Y + L.HalfSize) / (2.f * L.HalfSize);
				if (FShamanWorldGenerator::Fbm(U * 4.f, V * 4.f, (uint32)S.BiomeSeed, 3) < 0.05f) continue;
				if (!G.IsGoodGround(Q) || !G.ClearOfSettlements(Q) || !G.IsFree(Q, C.MinObjectSpacing)) continue;
				G.AddMarker(EWorldMarkerType::Tree, Q, Yaw, -1);
				++Placed;
			}
		}
		const FVector2D Origin(0.f, 0.f);
		G.Scatter(EWorldMarkerType::Food, C.WorldFood, Origin, 0.f, L.HalfSize, BiomeRng);
		G.Scatter(EWorldMarkerType::Stone, C.WorldStone, Origin, 0.f, L.HalfSize, BiomeRng);
		G.Scatter(EWorldMarkerType::Ore, C.WorldOre, Origin, 0.f, L.HalfSize, BiomeRng);

		return FShamanWorldGenerator::ValidateStartArea(L, C) == 0;
	}
}

namespace SWG = ShamanWorldGenPrivate;

FWorldSeeds FShamanWorldGenerator::DeriveSeeds(const FWorldSeeds& In)
{
	FWorldSeeds S = In;
	const uint64 Base = (uint64)(uint32)In.WorldSeed;
	auto Sub = [Base](uint64 Salt) { int32 V = (int32)(uint32)SWG::SplitMix(Base ^ Salt); return V == 0 ? 1 : V; };
	if (S.BiomeSeed == 0)    S.BiomeSeed = Sub(0xB10Eull);
	if (S.ResourceSeed == 0) S.ResourceSeed = Sub(0x5E50ull);
	if (S.TribeSeed == 0)    S.TribeSeed = Sub(0x781Bull);
	if (S.TerrainSeed == 0)  S.TerrainSeed = Sub(0x7E44ull);
	return S;
}

FWorldLayout FShamanWorldGenerator::Generate(const FWorldGenConfig& Config, const FWorldSeeds& InSeeds)
{
	FWorldLayout L;
	L.Seeds = DeriveSeeds(InSeeds);
	L.GridVertices = FMath::Max(17, Config.GridVertices);
	L.CellSize = FMath::Max(10.f, Config.CellSize);
	L.HalfSize = (L.GridVertices - 1) * L.CellSize * 0.5f;

	const int32 Attempts = FMath::Max(1, Config.MaxTerrainAttempts);
	for (int32 A = 0; A < Attempts; ++A)
	{
		L.TerrainAttempts = A + 1;
		SWG::BuildTerrain(L, Config, (uint32)L.Seeds.TerrainSeed + (uint32)A * 7919u);
		if (SWG::PlaceAll(L, Config, L.Seeds, A)) break;
	}
	L.FailMask = ValidateStartArea(L, Config);
	L.bValid = (L.FailMask == 0);
	return L;
}

int32 FShamanWorldGenerator::ValidateStartArea(const FWorldLayout& L, const FWorldGenConfig& C)
{
	if (L.Heights.Num() != L.GridVertices * L.GridVertices || L.GridVertices < 2) return EStartAreaFail::NoStartSite;
	int32 Mask = 0;
	int32 Circles = 0, Houses = 0, Fires = 0, Shamans = 0, Braves = 0;
	bool bWood = false, bFood = false, bStone = false, bOre = false, bWild = false, bEnemy = false, bEnemyShaman = false;
	const FVector2D P = L.PlayerStart;
	const float SA2 = C.StartAreaRadius * C.StartAreaRadius;
	for (int32 I = 0; I < L.Markers.Num(); ++I)
	{
		const FWorldMarker& M = L.Markers[I];
		const float D2 = FVector2D::DistSquared(M.Location, P);
		switch (M.Type)
		{
		case EWorldMarkerType::Tree:  bWood |= D2 <= SA2; break;
		case EWorldMarkerType::Food:  bFood |= D2 <= SA2; break;
		case EWorldMarkerType::Stone: bStone |= D2 <= SA2; break;
		case EWorldMarkerType::Ore:   bOre |= D2 <= SA2; break;
		case EWorldMarkerType::Wildman: bWild |= D2 <= FMath::Square(C.WildmenDistMax + 500.f); break;
		case EWorldMarkerType::ReincarnationCircle:
			if (M.TribeIndex == 0) ++Circles; else if (M.TribeIndex == 1) bEnemy |= D2 <= FMath::Square(C.EnemyDistMax + 500.f);
			break;
		case EWorldMarkerType::House:    if (M.TribeIndex == 0) ++Houses; break;
		case EWorldMarkerType::Campfire: if (M.TribeIndex == 0) ++Fires; break;
		case EWorldMarkerType::Shaman:   if (M.TribeIndex == 0) ++Shamans; else if (M.TribeIndex == 1) bEnemyShaman = true; break;
		case EWorldMarkerType::Brave:    if (M.TribeIndex == 0) ++Braves; break;
		default: break;
		}
	}
	if (Circles != 1 || Houses < 1 || Fires < 1 || Shamans != 1 || Braves < C.PlayerBraves) Mask |= EStartAreaFail::PlayerSettlementIncomplete;
	if (Circles == 0) Mask |= EStartAreaFail::NoStartSite;
	if (!bWood) Mask |= EStartAreaFail::NoWood;
	if (!bFood) Mask |= EStartAreaFail::NoFood;
	if (!bStone) Mask |= EStartAreaFail::NoStone;
	if (!bOre) Mask |= EStartAreaFail::NoOre;
	if (!bWild) Mask |= EStartAreaFail::NoWildmen;
	if (!bEnemy) Mask |= EStartAreaFail::NoEnemy;
	if (!bEnemyShaman) Mask |= EStartAreaFail::NoEnemyShaman;

	// Water within WaterSearchRadius of the start.
	{
		FWorldLayout& Mutable = const_cast<FWorldLayout&>(L); // FGen needs a non-const ref; queries below do not modify
		SWG::FGen G(Mutable, C);
		if (G.NearestWaterDist(P, C.WaterSearchRadius) > C.WaterSearchRadius) Mask |= EStartAreaFail::NoWater;

		TArray<uint8> Reach;
		G.Reach(P, Reach);
		if (!G.IsReached(Reach, L.EnemyStart)) Mask |= EStartAreaFail::EnemyUnreachable;
		if (!G.IsReached(Reach, L.WildmenCamp)) Mask |= EStartAreaFail::WildmenUnreachable;
	}
	return Mask;
}

float FShamanWorldGenerator::GetHeightAt(const FWorldLayout& L, const FVector2D& P)
{
	const int32 N = L.GridVertices;
	if (N < 2 || L.Heights.Num() != N * N) return 0.f;
	const float FX = FMath::Clamp((P.X + L.HalfSize) / L.CellSize, 0.f, (float)(N - 1) - 0.001f);
	const float FY = FMath::Clamp((P.Y + L.HalfSize) / L.CellSize, 0.f, (float)(N - 1) - 0.001f);
	const int32 X0 = FMath::FloorToInt(FX), Y0 = FMath::FloorToInt(FY);
	const float TX = FX - X0, TY = FY - Y0;
	const float H00 = L.Heights[X0 * N + Y0], H10 = L.Heights[(X0 + 1) * N + Y0];
	const float H01 = L.Heights[X0 * N + Y0 + 1], H11 = L.Heights[(X0 + 1) * N + Y0 + 1];
	return FMath::Lerp(FMath::Lerp(H00, H10, TX), FMath::Lerp(H01, H11, TX), TY);
}

bool FShamanWorldGenerator::IsInsideMap(const FWorldLayout& L, const FVector2D& P, float Margin)
{
	const float Lim = L.HalfSize - Margin;
	return P.X >= -Lim && P.X <= Lim && P.Y >= -Lim && P.Y <= Lim;
}

FVector2D FShamanWorldGenerator::VertexToWorld(const FWorldLayout& L, int32 X, int32 Y)
{
	return FVector2D(-L.HalfSize + X * L.CellSize, -L.HalfSize + Y * L.CellSize);
}

float FShamanWorldGenerator::GradientNoise(float X, float Y, uint32 Seed)
{
	const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
	const float FX = X - X0, FY = Y - Y0;
	auto Grad = [Seed](int32 IX, int32 IY, float DX, float DY)
	{
		const float A = (float)(SWG::HashCell(IX, IY, Seed) & 0xFFFFu) * (2.f * PI / 65536.f);
		return FMath::Cos(A) * DX + FMath::Sin(A) * DY;
	};
	auto Fade = [](float T) { return T * T * T * (T * (T * 6.f - 15.f) + 10.f); };
	const float N00 = Grad(X0, Y0, FX, FY), N10 = Grad(X0 + 1, Y0, FX - 1.f, FY);
	const float N01 = Grad(X0, Y0 + 1, FX, FY - 1.f), N11 = Grad(X0 + 1, Y0 + 1, FX - 1.f, FY - 1.f);
	const float U = Fade(FX), V = Fade(FY);
	return FMath::Lerp(FMath::Lerp(N00, N10, U), FMath::Lerp(N01, N11, U), V) * 1.41421356f;
}

float FShamanWorldGenerator::Fbm(float X, float Y, uint32 Seed, int32 Octaves)
{
	float Sum = 0.f, Amp = 1.f, Freq = 1.f, Norm = 0.f;
	for (int32 O = 0; O < Octaves; ++O)
	{
		Sum += Amp * GradientNoise(X * Freq, Y * Freq, Seed + (uint32)O * 1013u);
		Norm += Amp;
		Amp *= 0.5f;
		Freq *= 2.f;
	}
	return Norm > 0.f ? Sum / Norm : 0.f;
}
