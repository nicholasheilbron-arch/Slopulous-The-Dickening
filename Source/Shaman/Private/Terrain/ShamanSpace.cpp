#include "Terrain/ShamanSpace.h"
#include "Terrain/ShamanTerrainSubsystem.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "World/ShamanTerrainActor.h"

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

bool FShamanSpace::ProjectToGround(const UObject* Ctx, const FVector& Location, FVector& OutGround)
{
	OutGround = Location;
	if (const UShamanTerrainSubsystem* Terrain = UShamanTerrainSubsystem::Get(Ctx))
		if (Terrain->IsPlanetActive())
		{
			const FTerrainSample S = Terrain->QueryTerrain(Location);
			if (!S.bValid) return false;
			OutGround = S.Location;
			return true;
		}
	const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
	if (!World) return false;
	// The terrain itself, not roofs, trees or resource nodes standing on it (first hit on an AShamanTerrainActor;
	// any other world-static ground only if the level has no Shaman terrain actor under this point).
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(TEXT("ShamanGround"), false);
	const FVector Top = Location + FVector::UpVector * 50000.f, Bottom = Location - FVector::UpVector * 50000.f;
	World->LineTraceMultiByObjectType(Hits, Top, Bottom, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	const FHitResult* Fallback = nullptr;
	for (const FHitResult& H : Hits)
	{
		if (Cast<AShamanTerrainActor>(H.GetActor())) { OutGround = H.ImpactPoint; return true; }
		if (!Fallback) Fallback = &H;
	}
	if (!Fallback) return false;
	OutGround = Fallback->ImpactPoint;
	return true;
}
