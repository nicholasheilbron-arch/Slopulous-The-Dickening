#include "Player/ShamanControlTypes.h"
#include "Terrain/PlanetFrame.h"
#include "Characters/ShamanUnitBase.h"
#include "TribeMemberComponent.h"

// ---- Camera rig --------------------------------------------------------------------------------------------------------

FVector FShamanRTSCameraRig::UpAt(const FPlanetFrame* Planet, const FVector& Location)
{
	return Planet ? Planet->GetUp(Location) : FVector::UpVector;
}

void FShamanRTSCameraRig::Normalize(const FPlanetFrame* Planet)
{
	const FVector Up = UpAt(Planet, Focus);
	FVector F = Forward - Up * FVector::DotProduct(Forward, Up);
	if (F.SizeSquared() < 1e-6f)
	{
		// Heading parallel to up (degenerate input): pick any horizontal direction.
		const FVector Alt = FMath::Abs(Up.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
		F = Alt - Up * FVector::DotProduct(Alt, Up);
	}
	Forward = F.GetSafeNormal();
}

FVector FShamanRTSCameraRig::GetRight(const FPlanetFrame* Planet) const
{
	return FVector::CrossProduct(UpAt(Planet, Focus), Forward).GetSafeNormal(); // Unreal convention: Right = Up x Forward
}

void FShamanRTSCameraRig::Pan(const FPlanetFrame* Planet, float DForward, float DRight)
{
	Normalize(Planet);
	const FVector Delta = Forward * DForward + GetRight(Planet) * DRight;
	if (!Planet)
	{
		Focus += Delta;
		return;
	}
	// Planet: step along the tangent, then back onto the sphere through the focus (same radius), and carry the
	// heading into the new tangent plane (parallel transport) so it never flips.
	const float R = Planet->GetDistanceFromCenter(Focus);
	Focus = Planet->Center + ((Focus + Delta) - Planet->Center).GetSafeNormal() * R;
	Normalize(Planet);
}

void FShamanRTSCameraRig::Orbit(const FPlanetFrame* Planet, float YawDeg, float PitchDeltaDeg, const FShamanRTSCameraSettings& Settings)
{
	Normalize(Planet);
	if (YawDeg != 0.f) Forward = Forward.RotateAngleAxis(YawDeg, UpAt(Planet, Focus)).GetSafeNormal();
	PitchDeg = FMath::Clamp(PitchDeg + PitchDeltaDeg, Settings.MinPitch, Settings.MaxPitch);
}

void FShamanRTSCameraRig::Zoom(float Steps, const FShamanRTSCameraSettings& Settings)
{
	const float Factor = FMath::Pow(1.f - Settings.ZoomStep, Steps); // Steps > 0: closer
	Distance = FMath::Clamp(Distance * Factor, Settings.MinDistance, Settings.MaxDistance);
}

void FShamanRTSCameraRig::GetView(const FPlanetFrame* Planet, FVector& OutLocation, FRotator& OutRotation) const
{
	FShamanRTSCameraRig R = *this;
	R.Normalize(Planet);
	const FVector Up = UpAt(Planet, R.Focus);
	const float P = FMath::DegreesToRadians(FMath::Clamp(R.PitchDeg, 1.f, 89.f));
	const FVector ToCamera = -R.Forward * FMath::Cos(P) + Up * FMath::Sin(P);
	OutLocation = R.Focus + ToCamera * R.Distance;
	OutRotation = FRotationMatrix::MakeFromXZ(-ToCamera, Up).Rotator();
}

// ---- Selection rules -----------------------------------------------------------------------------------------------------

bool FShamanSelectionRules::IsSelectable(const AShamanUnitBase* Unit, int32 PlayerTribe)
{
	if (!Unit || Unit->IsPendingKillPending() || PlayerTribe < 0) return false;
	if (!Unit->IsAlive() || Unit->GetTribeId() != PlayerTribe) return false;
	const UTribeMemberComponent* M = Unit->TribeMember;
	return Unit->IsShaman() || (M && M->IsFollower());
}

bool FShamanSelectionRules::IsDrag(const FVector2D& Start, const FVector2D& Current, float ThresholdPx)
{
	return FVector2D::DistSquared(Start, Current) > FMath::Square(FMath::Max(1.f, ThresholdPx));
}

float FShamanSelectionRules::DistanceToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const float Len2 = AB.SizeSquared();
	const float T = Len2 > KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / Len2, 0.f, 1.f) : 0.f;
	return FVector2D::Distance(P, A + AB * T);
}

int32 FShamanSelectionRules::PickClosest(const TArray<FShamanScreenCandidate>& Candidates, const FVector2D& Mouse, float RadiusPx)
{
	int32 Best = INDEX_NONE;
	float BestD = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		const FShamanScreenCandidate& C = Candidates[i];
		if (!C.bOnScreen || !C.Unit.IsValid()) continue;
		// Body tolerance grows with the unit's on-screen size so zoomed-in units are easy to click.
		const float Tol = RadiusPx + 0.2f * FVector2D::Distance(C.Feet, C.Head);
		const float D = DistanceToSegment(Mouse, C.Feet, C.Head);
		if (D <= Tol && D < BestD) { BestD = D; Best = i; }
	}
	return Best;
}

void FShamanSelectionRules::BoxSelect(const TArray<FShamanScreenCandidate>& Candidates, const FVector2D& A, const FVector2D& B, TArray<int32>& OutIndices)
{
	OutIndices.Reset();
	const FVector2D Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
	const FVector2D Max(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		const FShamanScreenCandidate& C = Candidates[i];
		if (!C.bOnScreen || !C.Unit.IsValid()) continue;
		const FVector2D Mid = (C.Feet + C.Head) * 0.5f;
		if (Mid.X >= Min.X && Mid.X <= Max.X && Mid.Y >= Min.Y && Mid.Y <= Max.Y) OutIndices.Add(i);
	}
}

// ---- Command rules -------------------------------------------------------------------------------------------------------

void FShamanCommandRules::FormationOffsets(int32 NumUnits, float Spacing, TArray<FVector2D>& OutOffsets)
{
	OutOffsets.Reset();
	if (NumUnits <= 0) return;
	OutOffsets.Add(FVector2D::ZeroVector);
	for (int32 Ring = 1; OutOffsets.Num() < NumUnits; ++Ring)
	{
		const int32 Slots = 6 * Ring; // ring circumference 2*pi*Ring*Spacing / Slots ~= 1.05 * Spacing
		for (int32 i = 0; i < Slots && OutOffsets.Num() < NumUnits; ++i)
		{
			const float A = 2.f * PI * (i + 0.5f * (Ring % 2)) / Slots; // stagger alternate rings
			OutOffsets.Add(FVector2D(FMath::Cos(A), FMath::Sin(A)) * (Ring * Spacing));
		}
	}
}
