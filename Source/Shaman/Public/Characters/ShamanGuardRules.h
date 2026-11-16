#pragma once
#include "CoreMinimal.h"

/**
 * RTS guard / patrol geometry (pure, deterministic, unit-tested). Used by AShamanUnitAIController for the GuardShaman,
 * GuardTarget, GuardSpot and Patrol orders and by AShamanPlayerController when it hands out guard slots.
 * Angles are radians in the ground plane around the guard centre (FShamanSpace::OffsetAlongGround offsets).
 */
struct SHAMAN_API FShamanGuardRules
{
	/** Ring radius for guards: the target's own ring (Shaman, flag, building edge...) widened so Count units walking it
	 *  stay about Spacing apart. */
	static float RingRadius(float BaseRadius, int32 Count, float Spacing = 110.f);
	/** Evenly spaced entry angle of slot Slot of Count (slot 0 at angle 0). */
	static float SlotAngle(int32 Slot, int32 Count);
	/**
	 * Where a guard walks next (offset from the centre). Away from the ring (more than JoinTolerance off its radius) it
	 * heads for its own slot so a group spreads out evenly; on the ring it aims LeadArc uu further round
	 * (counter-clockwise), so it keeps walking circles at its own pace instead of stopping.
	 */
	static FVector2D NextRingPoint(const FVector2D& UnitOffset, int32 Slot, int32 Count, float Radius,
		float LeadArc = 160.f, float JoinTolerance = 120.f);
	static int32 NextPatrolIndex(int32 Current, int32 NumPoints);
	/** Index of the point nearest to Location (patrols start there). INDEX_NONE when empty. */
	static int32 NearestPoint(const TArray<FVector>& Points, const FVector& Location);
	/** Two clicks form a double-click when close in time and on screen. */
	static bool IsDoubleClick(double FirstTime, const FVector2D& FirstPos, double SecondTime, const FVector2D& SecondPos,
		float MaxSeconds = 0.35f, float MaxPixels = 12.f);
};
