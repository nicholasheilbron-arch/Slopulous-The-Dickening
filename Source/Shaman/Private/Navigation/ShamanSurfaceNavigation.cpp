#include "Navigation/ShamanSurfaceNavigation.h"
#include "Terrain/ShamanTerrainSubsystem.h"

namespace ShamanSurfaceNavPrivate
{
	static TSharedPtr<IShamanSurfacePathfinder>& Active()
	{
		static TSharedPtr<IShamanSurfacePathfinder> P = MakeShared<FGreatCirclePathfinder>();
		return P;
	}
}

const IShamanSurfacePathfinder& FShamanSurfaceNavigation::Get()
{
	return *ShamanSurfaceNavPrivate::Active();
}

void FShamanSurfaceNavigation::Set(TSharedPtr<IShamanSurfacePathfinder> Pathfinder)
{
	if (Pathfinder.IsValid()) ShamanSurfaceNavPrivate::Active() = Pathfinder;
	else ShamanSurfaceNavPrivate::Active() = MakeShared<FGreatCirclePathfinder>();
}

FShamanSurfacePath FGreatCirclePathfinder::FindPath(const UShamanTerrainSubsystem& Terrain, const FVector& Start, const FVector& Goal) const
{
	FShamanSurfacePath Path;
	if (!Terrain.IsPlanetActive()) return Path;
	const FPlanetFrame F = Terrain.GetPlanetFrame();
	const FVector A = F.GetDirection(Start), B = F.GetDirection(Goal);
	const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.f, 1.f));
	const float MaxAngle = MaxPathLength / FMath::Max(F.Radius, 1.f);
	const float WalkAngle = FMath::Min(Angle, MaxAngle);
	// Great-circle direction from A toward B (any tangent when B is antipodal).
	FVector Tangent = (B - A * FVector::DotProduct(A, B)).GetSafeNormal();
	if (Tangent.IsNearlyZero()) { FVector R, U; F.GetTangentBasis(Start, FVector(1.f, 0.f, 0.f), Tangent, R, U); }
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(WalkAngle * F.Radius / FMath::Max(SampleSpacing, 10.f)));

	FVector LastGood = Terrain.QueryTerrainDirection(A).Location;
	for (int32 i = 1; i <= Steps; ++i)
	{
		const float Theta = WalkAngle * i / Steps;
		const FVector D = (A * FMath::Cos(Theta) + Tangent * FMath::Sin(Theta)).GetSafeNormal();
		const FTerrainSample S = Terrain.QueryTerrainDirection(D);
		if (!S.bValid || !S.bWalkable)
		{
			Path.bPartial = true;
			break;
		}
		LastGood = S.Location;
	}
	if (Angle > MaxAngle) Path.bPartial = true;
	Path.Points.Add(LastGood);
	Path.bValid = true;
	return Path;
}
