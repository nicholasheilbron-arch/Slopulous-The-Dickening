#pragma once
#include "CoreMinimal.h"

/**
 * All planet geometry in one place. Rule: "up" is away from the planet centre, never world +Z.
 * Pure value type (no UObject, no engine systems) so it can be unit-tested anywhere.
 */
struct SHAMAN_API FPlanetFrame
{
	FVector Center = FVector(0.f, 0.f, 0.f);
	float Radius = 1.f;          // base radius
	float SeaLevelRadius = 1.f;  // absolute radius of the sea surface

	FPlanetFrame() {}
	FPlanetFrame(const FVector& InCenter, float InRadius, float InSeaLevelOffset = 0.f)
		: Center(InCenter), Radius(InRadius), SeaLevelRadius(InRadius + InSeaLevelOffset) {}

	/** Radial up at a world location (unit). Falls back to +Z exactly at the centre. */
	FVector GetUp(const FVector& Location) const
	{
		const FVector D = Location - Center;
		const float Len = D.Size();
		return Len > 1e-4f ? D / Len : FVector(0.f, 0.f, 1.f);
	}
	/** Gravity direction (unit, toward the centre). */
	FVector GetGravityDir(const FVector& Location) const { return -GetUp(Location); }

	float GetDistanceFromCenter(const FVector& Location) const { return (Location - Center).Size(); }
	/** Height above the base radius. */
	float GetAltitude(const FVector& Location) const { return GetDistanceFromCenter(Location) - Radius; }
	/** Positive when Location is below the sea surface. */
	float GetDepthBelowSea(const FVector& Location) const { return SeaLevelRadius - GetDistanceFromCenter(Location); }

	/** Unit direction from the centre (planet-relative "where on the globe"). */
	FVector GetDirection(const FVector& Location) const { return GetUp(Location); }
	/** World point at Height above the base radius along a direction. */
	FVector GetPointAt(const FVector& Direction, float Height) const
	{
		return Center + Direction.GetSafeNormal() * (Radius + Height);
	}

	/** Removes the radial component of V at Location. */
	FVector ProjectOntoTangent(const FVector& V, const FVector& Location) const
	{
		const FVector Up = GetUp(Location);
		return V - Up * FVector::DotProduct(V, Up);
	}
	/** Unit tangent direction at From that points along the surface toward To. */
	FVector GetTangentDirectionTo(const FVector& From, const FVector& To) const
	{
		return ProjectOntoTangent(To - From, From).GetSafeNormal();
	}

	/** Orthonormal local basis at Location (Forward, Right, Up), Unreal convention Right = Up x Forward. */
	void GetTangentBasis(const FVector& Location, const FVector& ForwardHint, FVector& OutForward, FVector& OutRight, FVector& OutUp) const
	{
		OutUp = GetUp(Location);
		OutForward = (ForwardHint - OutUp * FVector::DotProduct(ForwardHint, OutUp)).GetSafeNormal();
		if (OutForward.SizeSquared() < 0.5f)
		{
			// Hint parallel to up: pick any axis that is not.
			const FVector Alt = FMath::Abs(OutUp.Z) < 0.9f ? FVector(0.f, 0.f, 1.f) : FVector(1.f, 0.f, 0.f);
			OutForward = (Alt - OutUp * FVector::DotProduct(Alt, OutUp)).GetSafeNormal();
		}
		OutRight = FVector::CrossProduct(OutUp, OutForward);
	}

	/** Great-circle distance along the base sphere between two world locations. */
	float GetSurfaceDistance(const FVector& A, const FVector& B) const
	{
		const float CosAngle = FMath::Clamp(FVector::DotProduct(GetUp(A), GetUp(B)), -1.f, 1.f);
		return FMath::Acos(CosAngle) * Radius;
	}

	/** Parallel-transports a tangent vector from one location's tangent plane to another's (keeps it tangent). */
	FVector TransportTangent(const FVector& V, const FVector& NewLocation) const
	{
		const float Len = V.Size();
		const FVector P = ProjectOntoTangent(V, NewLocation).GetSafeNormal();
		return P * Len;
	}
};
