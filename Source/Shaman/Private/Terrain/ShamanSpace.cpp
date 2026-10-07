#include "Terrain/ShamanSpace.h"
#include "Terrain/ShamanTerrainSubsystem.h"

bool FShamanSpace::GetPlanet(const UObject* Ctx, FPlanetFrame& OutFrame)
{
	return UShamanTerrainSubsystem::TryGetPlanetFrame(Ctx, OutFrame);
}

FVector FShamanSpace::GetUp(const UObject* Ctx, const FVector& Location)
{
	FPlanetFrame F;
	return GetPlanet(Ctx, F) ? F.GetUp(Location) : FVector::UpVector;
}

float FShamanSpace::HorizontalDistance(const UObject* Ctx, const FVector& A, const FVector& B)
{
	FPlanetFrame F;
	return GetPlanet(Ctx, F) ? F.GetSurfaceDistance(A, B) : FVector::Dist2D(A, B);
}

FVector FShamanSpace::HorizontalDirection(const UObject* Ctx, const FVector& From, const FVector& To)
{
	FPlanetFrame F;
	return GetPlanet(Ctx, F) ? F.GetTangentDirectionTo(From, To) : (To - From).GetSafeNormal2D();
}

FVector FShamanSpace::HorizontalNormal(const UObject* Ctx, const FVector& Location, const FVector& V)
{
	FPlanetFrame F;
	return GetPlanet(Ctx, F) ? F.ProjectOntoTangent(V, Location).GetSafeNormal() : V.GetSafeNormal2D();
}

FRotator FShamanSpace::UprightRotation(const UObject* Ctx, const FVector& Location, const FVector& Forward)
{
	FPlanetFrame F;
	if (!GetPlanet(Ctx, F)) return FRotator(0.f, Forward.Rotation().Yaw, 0.f);
	FVector Fwd, Right, Up;
	F.GetTangentBasis(Location, Forward, Fwd, Right, Up);
	return FRotationMatrix::MakeFromXZ(Fwd, Up).Rotator();
}

FVector FShamanSpace::OffsetAlongGround(const UObject* Ctx, const FVector& Origin, const FVector2D& Offset)
{
	FPlanetFrame F;
	if (!GetPlanet(Ctx, F)) return Origin + FVector(Offset.X, Offset.Y, 0.f);
	FVector Fwd, Right, Up;
	F.GetTangentBasis(Origin, FVector(1.f, 0.f, 0.f), Fwd, Right, Up);
	const float R = F.GetDistanceFromCenter(Origin);
	return F.Center + ((Origin + Fwd * Offset.X + Right * Offset.Y) - F.Center).GetSafeNormal() * R;
}
