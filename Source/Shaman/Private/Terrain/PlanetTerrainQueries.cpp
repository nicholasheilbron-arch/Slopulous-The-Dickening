#include "Terrain/PlanetTerrainQueries.h"
#include "Terrain/PlanetHeightField.h"

FTerrainSample FPlanetTerrainQueries::SampleDirection(const FPlanetHeightField& HF, const FVector& InDir)
{
	const FPlanetSettings& S = HF.GetSettings();
	const FPlanetFrame& Frame = HF.GetFrame();
	const FVector Dir = InDir.GetSafeNormal();
	FTerrainSample Out;
	if (Dir.SizeSquared() < 0.5f) return Out;

	Out.bValid = true;
	Out.Height = HF.GetHeight(Dir);
	Out.Location = Frame.GetPointAt(Dir, Out.Height);
	Out.Up = Dir;
	Out.Normal = HF.GetSurfaceNormal(Dir);
	Out.WaterDepth = FMath::Max(0.f, Frame.SeaLevelRadius - (S.Radius + Out.Height));
	Out.bUnderwater = Out.WaterDepth > 0.f;
	const float MaxSlopeCos = FMath::Cos(FMath::Clamp(S.MaxWalkableSlopeDeg, 0.f, 89.f) * (PI / 180.f));
	const bool bSteep = FVector::DotProduct(Out.Normal, Out.Up) < MaxSlopeCos;
	Out.bWalkable = !bSteep && Out.WaterDepth <= S.FordableDepth;
	Out.Material = HF.GetMaterialFast(Dir, Out.Height);
	if (Out.Material == ETerrainMaterial::Grass && bSteep) Out.Material = ETerrainMaterial::Rock;
	Out.Flags = (uint8)((Out.bUnderwater ? (int32)ETerrainFlags::Underwater : 0) | (bSteep ? (int32)ETerrainFlags::Steep : 0)
		| (HF.IsEdited(Dir) ? (int32)ETerrainFlags::Edited : 0));
	return Out;
}

FTerrainSample FPlanetTerrainQueries::Sample(const FPlanetHeightField& HF, const FVector& WorldLocation)
{
	return SampleDirection(HF, HF.GetFrame().GetDirection(WorldLocation));
}

bool FPlanetTerrainQueries::Raycast(const FPlanetHeightField& HF, const FVector& Start, const FVector& End, FTerrainRaycastHit& OutHit)
{
	OutHit = FTerrainRaycastHit();
	const FVector C = HF.GetFrame().Center;
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (Length < 1e-3f) return false;
	const FVector D = Delta / Length;

	float T = 0.f, PrevT = 0.f;
	float F = HF.GetSignedDistance(Start - C);
	if (F <= 0.f) // starts inside the ground: report an immediate hit
	{
		OutHit.bHit = true; OutHit.Location = Start; OutHit.Distance = 0.f;
		OutHit.Normal = HF.GetSurfaceNormal(HF.GetFrame().GetDirection(Start));
		return true;
	}
	for (int32 Step = 0; Step < 2048 && T <= Length; ++Step)
	{
		PrevT = T;
		T = FMath::Min(Length, T + FMath::Max(F * 0.6f, 5.f)); // heightfield distance overestimates on slopes: under-step
		F = HF.GetSignedDistance(Start + D * T - C);
		if (F <= 0.f)
		{
			float Lo = PrevT, Hi = T; // bisection between the last outside and first inside sample
			for (int32 I = 0; I < 24; ++I)
			{
				const float Mid = 0.5f * (Lo + Hi);
				if (HF.GetSignedDistance(Start + D * Mid - C) > 0.f) Lo = Mid; else Hi = Mid;
			}
			OutHit.bHit = true;
			OutHit.Distance = Hi;
			OutHit.Location = Start + D * Hi;
			OutHit.Normal = HF.GetSurfaceNormal(HF.GetFrame().GetDirection(OutHit.Location));
			return true;
		}
		if (T >= Length) break;
	}
	return false;
}

void FPlanetTerrainQueries::GetFootprintDirections(const FPlanetFrame& Frame, const FTerrainModification& Mod, TArray<FVector, TInlineAllocator<40>>& OutDirs)
{
	OutDirs.Reset();
	const FVector Dir = (Mod.Center - Frame.Center).GetSafeNormal();
	if (Dir.IsZero()) return;
	OutDirs.Add(Dir);
	FVector T, B, U;
	Frame.GetTangentBasis(Mod.Center, FVector(1.f, 0.f, 0.f), T, B, U);
	const float OuterAngle = FMath::Min(Mod.Radius / FMath::Max(Frame.Radius, 1.f), 3.1f);
	for (int32 Ring = 1; Ring <= 3; ++Ring)
	{
		const float A = OuterAngle * Ring / 3.f;
		for (int32 k = 0; k < 12; ++k)
		{
			const float Phi = 2.f * PI * k / 12.f;
			const FVector Tangent = T * FMath::Cos(Phi) + B * FMath::Sin(Phi);
			OutDirs.Add((Dir * FMath::Cos(A) + Tangent * FMath::Sin(A)).GetSafeNormal());
		}
	}
}

float FPlanetTerrainQueries::GetAffectedRadius(const FTerrainModification& Mod)
{
	const float Vertical = (Mod.Op == ETerrainOp::Raise || Mod.Op == ETerrainOp::Lower) ? FMath::Abs(Mod.Strength) : 0.f;
	float R = Mod.Radius + Vertical;
	if (Mod.Op == ETerrainOp::RaisePath) R += (Mod.PathEnd - Mod.Center).Size();
	return R;
}

bool FPlanetTerrainQueries::Overlaps(const FPlanetFrame& Frame, const FTerrainProtectedRegion& Region, const FTerrainModification& Mod)
{
	if (Mod.Op == ETerrainOp::RaisePath)
	{
		// Sample along the path every ~100 uu (future op; kept so protection already covers it).
		const float Len = Frame.GetSurfaceDistance(Mod.Center, Mod.PathEnd);
		const int32 Steps = FMath::Max(1, FMath::FloorToInt(Len / 100.f));
		for (int32 I = 0; I <= Steps; ++I)
		{
			const float A = (float)I / (float)Steps;
			const FVector P = Frame.Center + FMath::Lerp(Frame.GetDirection(Mod.Center), Frame.GetDirection(Mod.PathEnd), A).GetSafeNormal() * Frame.Radius;
			if (Frame.GetSurfaceDistance(P, Region.Center) < Mod.Radius + Region.Radius) return true;
		}
		return false;
	}
	return Frame.GetSurfaceDistance(Mod.Center, Region.Center) < Mod.Radius + Region.Radius;
}
