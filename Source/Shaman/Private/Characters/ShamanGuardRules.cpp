#include "Characters/ShamanGuardRules.h"

float FShamanGuardRules::RingRadius(float BaseRadius, int32 Count, float Spacing)
{
	const float Crowd = FMath::Max(1, Count) * FMath::Max(1.f, Spacing) / (2.f * PI);
	return FMath::Max(FMath::Max(BaseRadius, 50.f), Crowd);
}

float FShamanGuardRules::SlotAngle(int32 Slot, int32 Count)
{
	return 2.f * PI * (float)FMath::Max(0, Slot) / (float)FMath::Max(1, Count);
}

FVector2D FShamanGuardRules::NextRingPoint(const FVector2D& UnitOffset, int32 Slot, int32 Count, float Radius, float LeadArc, float JoinTolerance)
{
	const float R = FMath::Max(Radius, 1.f);
	const float Dist = UnitOffset.Size();
	if (Dist < KINDA_SMALL_NUMBER || FMath::Abs(Dist - R) > JoinTolerance)
	{
		const float A = SlotAngle(Slot, Count);
		return FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
	}
	const float A = FMath::Atan2(UnitOffset.Y, UnitOffset.X) + FMath::Clamp(LeadArc / R, 0.15f, 1.2f);
	return FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
}

int32 FShamanGuardRules::NextPatrolIndex(int32 Current, int32 NumPoints)
{
	return NumPoints > 0 ? (FMath::Max(0, Current) + 1) % NumPoints : INDEX_NONE;
}

int32 FShamanGuardRules::NearestPoint(const TArray<FVector>& Points, const FVector& Location)
{
	int32 Best = INDEX_NONE;
	float BestD = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		const float D = FVector::DistSquared(Points[i], Location);
		if (D < BestD) { BestD = D; Best = i; }
	}
	return Best;
}

bool FShamanGuardRules::IsDoubleClick(double FirstTime, const FVector2D& FirstPos, double SecondTime, const FVector2D& SecondPos,
	float MaxSeconds, float MaxPixels)
{
	const double Dt = SecondTime - FirstTime;
	return FirstTime >= 0.0 && Dt >= 0.0 && Dt <= MaxSeconds && FVector2D::DistSquared(FirstPos, SecondPos) <= FMath::Square(MaxPixels);
}
