#include "Terrain/PlanetHeightField.h"

// Engine-independent (CoreMinimal math only): verified outside Unreal by Tools/TerrainHarness.

namespace ShamanPlanetHFPrivate
{
	static uint32 Hash32(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352du;
		X ^= X >> 15; X *= 0x846ca68bu;
		X ^= X >> 16;
		return X;
	}
	static uint32 HashCell(int32 X, int32 Y, int32 Z, uint32 Seed)
	{
		return Hash32((uint32)X * 0x8da6b343u ^ Hash32((uint32)Y * 0xd8163841u ^ Hash32((uint32)Z * 0xcb1ab31fu ^ Seed)));
	}
	static uint64 SplitMix(uint64 X)
	{
		X += 0x9E3779B97F4A7C15ull;
		X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
		X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
		return X ^ (X >> 31);
	}
	static float UnitFromBits(uint64 V) { return (float)((V >> 40) & 0xFFFFFF) / 16777216.f; }
	static float SmoothStep01(float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * (3.f - 2.f * T); }
	static float SmoothRange(float A, float B, float X) { return SmoothStep01((X - A) / (B - A)); }
	static float Fade(float T) { return T * T * T * (T * (T * 6.f - 15.f) + 10.f); }
	static float Grad(uint32 H, float X, float Y, float Z)
	{
		switch (H % 12u)
		{
		case 0: return  X + Y;  case 1: return -X + Y;  case 2: return  X - Y;  case 3: return -X - Y;
		case 4: return  X + Z;  case 5: return -X + Z;  case 6: return  X - Z;  case 7: return -X - Z;
		case 8: return  Y + Z;  case 9: return -Y + Z;  case 10: return Y - Z;  default: return -Y - Z;
		}
	}
}
namespace SPHF = ShamanPlanetHFPrivate;

FPlanetHeightField::FPlanetHeightField(const FPlanetSettings& InSettings)
	: Settings(InSettings)
	, Frame(InSettings.Center, InSettings.Radius, InSettings.SeaLevelOffset)
{
	const uint64 Base = (uint64)(uint32)Settings.Seed;
	SeedContinent = (uint32)SPHF::SplitMix(Base ^ 0xC0A1ull);
	SeedDetail = (uint32)SPHF::SplitMix(Base ^ 0xDE7Aull);
	SeedMountain = (uint32)SPHF::SplitMix(Base ^ 0x307Eull);
	auto Offset = [Base](uint64 Salt)
	{
		const uint64 A = SPHF::SplitMix(Base ^ Salt), B = SPHF::SplitMix(A), C = SPHF::SplitMix(B);
		return FVector(SPHF::UnitFromBits(A) * 100.f, SPHF::UnitFromBits(B) * 100.f, SPHF::UnitFromBits(C) * 100.f);
	};
	OffsetContinent = Offset(0x11ull);
	OffsetDetail = Offset(0x22ull);
	OffsetMountain = Offset(0x33ull);
	Edits.SetNum(MaxEdits); // never reallocated: readers on other threads index into it
}

float FPlanetHeightField::GradientNoise3(const FVector& P, uint32 Seed)
{
	const int32 X0 = FMath::FloorToInt(P.X), Y0 = FMath::FloorToInt(P.Y), Z0 = FMath::FloorToInt(P.Z);
	const float FX = P.X - X0, FY = P.Y - Y0, FZ = P.Z - Z0;
	const float U = SPHF::Fade(FX), V = SPHF::Fade(FY), W = SPHF::Fade(FZ);
	auto G = [Seed](int32 X, int32 Y, int32 Z, float DX, float DY, float DZ) { return SPHF::Grad(SPHF::HashCell(X, Y, Z, Seed), DX, DY, DZ); };
	const float X00 = FMath::Lerp(G(X0, Y0, Z0, FX, FY, FZ), G(X0 + 1, Y0, Z0, FX - 1.f, FY, FZ), U);
	const float X10 = FMath::Lerp(G(X0, Y0 + 1, Z0, FX, FY - 1.f, FZ), G(X0 + 1, Y0 + 1, Z0, FX - 1.f, FY - 1.f, FZ), U);
	const float X01 = FMath::Lerp(G(X0, Y0, Z0 + 1, FX, FY, FZ - 1.f), G(X0 + 1, Y0, Z0 + 1, FX - 1.f, FY, FZ - 1.f), U);
	const float X11 = FMath::Lerp(G(X0, Y0 + 1, Z0 + 1, FX, FY - 1.f, FZ - 1.f), G(X0 + 1, Y0 + 1, Z0 + 1, FX - 1.f, FY - 1.f, FZ - 1.f), U);
	return FMath::Lerp(FMath::Lerp(X00, X10, V), FMath::Lerp(X01, X11, V), W) * 0.75f; // ~[-1,1]
}

float FPlanetHeightField::Fbm3(const FVector& P, uint32 Seed, int32 Octaves)
{
	float Sum = 0.f, Amp = 1.f, Norm = 0.f, Freq = 1.f;
	for (int32 O = 0; O < Octaves; ++O)
	{
		Sum += Amp * GradientNoise3(P * Freq, Seed + (uint32)O * 1013u);
		Norm += Amp;
		Amp *= 0.5f;
		Freq *= 2.f;
	}
	return Norm > 0.f ? Sum / Norm : 0.f;
}

float FPlanetHeightField::GetBaseHeight(const FVector& Dir) const
{
	const float C = Fbm3(Dir * Settings.ContinentFrequency + OffsetContinent, SeedContinent, 4) * 1.6f + Settings.LandBias;
	const float LandMask = SPHF::SmoothRange(-0.04f, 0.10f, C);
	const float Hills = Fbm3(Dir * Settings.DetailFrequency + OffsetDetail, SeedDetail, 4);
	const float RidgeN = 1.f - FMath::Abs(Fbm3(Dir * Settings.MountainFrequency + OffsetMountain, SeedMountain, 4));
	const float Ridge = RidgeN * RidgeN * RidgeN;
	const float MountainMask = SPHF::SmoothRange(0.15f, 0.45f, C);

	const float Land = 60.f
		+ Settings.LandHeight * (0.35f * LandMask + 0.65f * SPHF::SmoothRange(0.f, 0.5f, C))
		+ Hills * Settings.LandHeight * 0.35f
		+ Ridge * MountainMask * Settings.MountainHeight;
	const float Sea = -Settings.SeabedDepth * SPHF::SmoothRange(0.f, 0.35f, -C) - 60.f;
	return FMath::Lerp(Sea, Land, LandMask);
}

float FPlanetHeightField::EditWeight(const FPlanetEdit& E, float CosAngle)
{
	if (CosAngle < E.CosOuter) return 0.f;
	const float Angle = FMath::Acos(FMath::Clamp(CosAngle, -1.f, 1.f));
	if (Angle <= E.InnerAngle) return 1.f;
	const float Span = FMath::Max(E.OuterAngle - E.InnerAngle, 1e-6f);
	return 1.f - SPHF::SmoothStep01((Angle - E.InnerAngle) / Span);
}

float FPlanetHeightField::ApplyEdits(const FVector& Dir, float Height, int32 Begin, int32 End) const
{
	for (int32 I = Begin; I < End; ++I)
	{
		const FPlanetEdit& E = Edits[I];
		const float W = EditWeight(E, FVector::DotProduct(Dir, E.Dir));
		if (W <= 0.f) continue;
		switch (E.Op)
		{
		case ETerrainOp::Raise:   Height += E.Strength * W; break;
		case ETerrainOp::Lower:   Height -= E.Strength * W; break;
		case ETerrainOp::Flatten: Height = FMath::Lerp(Height, E.TargetHeight, W * FMath::Clamp(E.Strength, 0.f, 1.f)); break;
		default: break; // Paint: material only; Smooth/RaisePath are rejected before reaching here
		}
	}
	return Height;
}

float FPlanetHeightField::GetHeight(const FVector& Dir) const
{
	const uint64 R = EditRange.load(std::memory_order_acquire);
	const float Base = GetBaseHeight(Dir);
	return RangeEnd(R) > RangeBegin(R) ? ApplyEdits(Dir, Base, RangeBegin(R), RangeEnd(R)) : Base;
}

FVector FPlanetHeightField::GetSurfaceNormal(const FVector& Dir) const
{
	FVector T1, T2, Up;
	Frame.GetTangentBasis(Frame.Center + Dir, FVector(1.f, 0.f, 0.f), T1, T2, Up);
	const float Eps = 60.f / FMath::Max(Settings.Radius, 1.f); // ~60 uu on the surface
	const FVector D1 = (Dir + T1 * Eps).GetSafeNormal();
	const FVector D2 = (Dir + T2 * Eps).GetSafeNormal();
	const FVector P0 = Dir * GetSurfaceRadius(Dir);
	const FVector P1 = D1 * GetSurfaceRadius(D1);
	const FVector P2 = D2 * GetSurfaceRadius(D2);
	FVector N = FVector::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal();
	if (FVector::DotProduct(N, Dir) < 0.f) N = -N;
	return N.SizeSquared() > 0.5f ? N : Dir;
}

ETerrainMaterial FPlanetHeightField::GetMaterialFast(const FVector& Dir, float Height) const
{
	const uint64 R = EditRange.load(std::memory_order_acquire);
	for (int32 I = RangeEnd(R) - 1; I >= RangeBegin(R); --I) // latest paint wins
	{
		const FPlanetEdit& E = Edits[I];
		if (E.Op == ETerrainOp::Paint && EditWeight(E, FVector::DotProduct(Dir, E.Dir)) >= 0.5f) return E.Material;
	}
	const float H = Height - Settings.SeaLevelOffset;
	if (H < -150.f) return ETerrainMaterial::Seabed;
	if (H < 40.f) return ETerrainMaterial::Sand;
	if (H < 1300.f) return ETerrainMaterial::Grass;
	if (H < 2000.f) return ETerrainMaterial::Rock;
	return ETerrainMaterial::Snow;
}

ETerrainMaterial FPlanetHeightField::GetMaterial(const FVector& Dir) const
{
	const ETerrainMaterial M = GetMaterialFast(Dir, GetHeight(Dir));
	if (M == ETerrainMaterial::Grass && FVector::DotProduct(GetSurfaceNormal(Dir), Dir) < 0.766f) return ETerrainMaterial::Rock; // > ~40 deg
	return M;
}

bool FPlanetHeightField::IsEdited(const FVector& Dir) const
{
	const uint64 R = EditRange.load(std::memory_order_acquire);
	for (int32 I = RangeBegin(R); I < RangeEnd(R); ++I)
		if (EditWeight(Edits[I], FVector::DotProduct(Dir, Edits[I].Dir)) > 0.f) return true;
	return false;
}

float FPlanetHeightField::GetMinHeightBound() const
{
	const float BaseMin = FMath::Min(-Settings.SeabedDepth - 60.f, 60.f - 0.35f * Settings.LandHeight) - 50.f;
	return BaseMin - EditLowerBound.load(std::memory_order_acquire);
}

float FPlanetHeightField::GetMaxHeightBound() const
{
	const float BaseMax = 60.f + 1.35f * Settings.LandHeight + Settings.MountainHeight + 50.f;
	return BaseMax + EditRaiseBound.load(std::memory_order_acquire);
}

float FPlanetHeightField::GetSignedDistance(const FVector& LocalPos) const
{
	const float R = LocalPos.Size();
	if (R < 1.f) return -Settings.Radius;
	// Far from the crust: skip the noise entirely (sign stays correct, magnitude is a lower bound).
	const float Outer = Settings.Radius + GetMaxHeightBound();
	const float Inner = Settings.Radius + GetMinHeightBound();
	if (R > Outer + 200.f) return R - Outer;
	if (R < Inner - 200.f) return R - Inner;
	return R - GetSurfaceRadius(LocalPos / R);
}

int32 FPlanetHeightField::AddEdit(const FTerrainModification& Mod)
{
	const uint64 Range = EditRange.load(std::memory_order_relaxed);
	const int32 Begin = RangeBegin(Range), Slot = RangeEnd(Range);
	if (Slot >= MaxEdits) return -1;
	if (!(Mod.Radius > 1.f) || !FMath::IsFinite(Mod.Strength) || !FMath::IsFinite(Mod.TargetHeight)) return -1;
	const FVector Rel = Mod.Center - Frame.Center;
	if (Rel.SizeSquared() < 1.f) return -1;

	FPlanetEdit E;
	E.Op = Mod.Op;
	E.Dir = Rel.GetSafeNormal();
	E.OuterAngle = FMath::Min(Mod.Radius / FMath::Max(Settings.Radius, 1.f), 3.1f);
	E.InnerAngle = E.OuterAngle * FMath::Clamp(Mod.InnerFraction, 0.f, 0.99f);
	E.CosOuter = FMath::Cos(E.OuterAngle);
	E.Strength = Mod.Strength;
	E.TargetHeight = Mod.TargetHeight;
	E.Material = Mod.Material;
	E.Request = Mod;

	// Conservative bounds for range culling (written only on the game thread).
	if (Mod.Op == ETerrainOp::Raise) EditRaiseBound.store(EditRaiseBound.load() + FMath::Abs(Mod.Strength));
	if (Mod.Op == ETerrainOp::Lower) EditLowerBound.store(EditLowerBound.load() + FMath::Abs(Mod.Strength));
	if (Mod.Op == ETerrainOp::Flatten)
	{
		const float BaseMax = 60.f + 1.35f * Settings.LandHeight + Settings.MountainHeight + 50.f;
		const float BaseMin = FMath::Min(-Settings.SeabedDepth - 60.f, 60.f - 0.35f * Settings.LandHeight) - 50.f;
		if (Mod.TargetHeight > BaseMax) EditRaiseBound.store(EditRaiseBound.load() + (Mod.TargetHeight - BaseMax));
		if (Mod.TargetHeight < BaseMin) EditLowerBound.store(EditLowerBound.load() + (BaseMin - Mod.TargetHeight));
	}

	Edits[Slot] = E;
	EditRange.store(MakeRange(Begin, Slot + 1), std::memory_order_release); // publish after the element is fully written
	return Slot - Begin;
}

void FPlanetHeightField::ResetEdits(bool bReclaimStorage)
{
	const int32 End = RangeEnd(EditRange.load(std::memory_order_relaxed));
	EditRange.store(bReclaimStorage ? 0ull : MakeRange(End, End), std::memory_order_release);
	EditRaiseBound.store(0.f);
	EditLowerBound.store(0.f);
}
