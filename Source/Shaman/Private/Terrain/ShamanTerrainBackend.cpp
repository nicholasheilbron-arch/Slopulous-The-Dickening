#include "Terrain/ShamanTerrainBackend.h"
#include "Terrain/PlanetHeightField.h"
#include "Terrain/PlanetTerrainQueries.h"
#include "Core/ShamanLog.h"
#include "HAL/PlatformTime.h"

FTerrainModificationResult UShamanTerrainBackend::ApplyModification(const FTerrainModification& Request)
{
	FTerrainModificationResult R;
	R.Status = ETerrainModifyStatus::RejectedUnsupported;
	return R;
}

// ---------------------------------------------------------------------------------------------------------------

bool UAnalyticPlanetTerrainBackend::InitializeBackend(UWorld* World, const FPlanetSettings& InSettings)
{
	Super::InitializeBackend(World, InSettings);
	const double T0 = FPlatformTime::Seconds();
	HeightField = MakeShared<FPlanetHeightField, ESPMode::ThreadSafe>(InSettings);
	GenerationMs = (float)((FPlatformTime::Seconds() - T0) * 1000.0);
	return true;
}

void UAnalyticPlanetTerrainBackend::ShutdownBackend()
{
	HeightField.Reset();
}

FTerrainSample UAnalyticPlanetTerrainBackend::SampleAt(const FVector& WorldLocation) const
{
	return HeightField.IsValid() ? FPlanetTerrainQueries::Sample(*HeightField, WorldLocation) : FTerrainSample();
}

FTerrainSample UAnalyticPlanetTerrainBackend::SampleDirection(const FVector& Direction) const
{
	return HeightField.IsValid() ? FPlanetTerrainQueries::SampleDirection(*HeightField, Direction) : FTerrainSample();
}

bool UAnalyticPlanetTerrainBackend::Raycast(const FVector& Start, const FVector& End, FTerrainRaycastHit& OutHit) const
{
	OutHit = FTerrainRaycastHit();
	return HeightField.IsValid() && FPlanetTerrainQueries::Raycast(*HeightField, Start, End, OutHit);
}

float UAnalyticPlanetTerrainBackend::GetHeightAt(const FVector& WorldLocation) const
{
	return HeightField.IsValid() ? HeightField->GetHeight(Frame.GetDirection(WorldLocation)) : 0.f;
}

bool UAnalyticPlanetTerrainBackend::SupportsOperation(ETerrainOp Op) const
{
	switch (Op)
	{
	case ETerrainOp::Raise: case ETerrainOp::Lower: case ETerrainOp::Flatten: case ETerrainOp::Paint: return true;
	default: return false; // Smooth, RaisePath: future milestones
	}
}

FTerrainModificationResult UAnalyticPlanetTerrainBackend::ApplyModification(const FTerrainModification& Request)
{
	FTerrainModificationResult R;
	if (!HeightField.IsValid()) { R.Status = ETerrainModifyStatus::RejectedNoBackend; return R; }
	if (!SupportsOperation(Request.Op)) { R.Status = ETerrainModifyStatus::RejectedUnsupported; return R; }
	if (HeightField->GetRemainingCapacity() <= 0) { R.Status = ETerrainModifyStatus::RejectedCapacity; return R; }

	// Footprint samples (centre + 3 rings) before and after, so the reported bounds include the vertical change of
	// every op (Flatten can move the ground by any amount).
	TArray<FVector, TInlineAllocator<40>> Dirs;
	FPlanetTerrainQueries::GetFootprintDirections(Frame, Request, Dirs);
	TArray<float, TInlineAllocator<40>> Before;
	for (const FVector& D : Dirs) Before.Add(HeightField->GetHeight(D));

	const int32 Index = HeightField->AddEdit(Request);
	if (Index < 0) { R.Status = ETerrainModifyStatus::RejectedInvalid; return R; }

	FBox Box(ForceInit);
	for (int32 i = 0; i < Dirs.Num(); ++i)
	{
		const float After = HeightField->GetHeight(Dirs[i]);
		Box += Frame.GetPointAt(Dirs[i], FMath::Min(Before[i], After));
		Box += Frame.GetPointAt(Dirs[i], FMath::Max(Before[i], After));
	}
	// The cap bulges between samples: pad by the ring spacing.
	Box = Box.ExpandBy(Request.Radius * 0.35f + 2.f * Settings.VoxelSize);
	LastEditBounds = Box;

	R.Status = ETerrainModifyStatus::Applied;
	R.EditIndex = Index;
	R.BoundsCenter = Box.GetCenter();
	R.BoundsRadius = Box.GetExtent().Size();
	OnHeightFieldEdited(R);
	return R;
}

void UAnalyticPlanetTerrainBackend::GetEditLog(TArray<FTerrainModification>& OutEdits) const
{
	OutEdits.Reset();
	if (!HeightField.IsValid()) return;
	const int32 Num = HeightField->GetNumEdits();
	OutEdits.Reserve(Num);
	for (int32 i = 0; i < Num; ++i) OutEdits.Add(HeightField->GetEdit(i).Request);
}

bool UAnalyticPlanetTerrainBackend::ResetEdits()
{
	if (!HeightField.IsValid()) return false;
	HeightField->ResetEdits(CanReclaimEditStorage());
	OnHeightFieldReset();
	return true;
}

FString UAnalyticPlanetTerrainBackend::GetStatsString() const
{
	return FString::Printf(TEXT("backend=%s edits=%d gen=%.2fms heightBounds=[%.0f,%.0f]"),
		*GetBackendName().ToString(), HeightField.IsValid() ? HeightField->GetNumEdits() : 0, GenerationMs,
		HeightField.IsValid() ? HeightField->GetMinHeightBound() : 0.f, HeightField.IsValid() ? HeightField->GetMaxHeightBound() : 0.f);
}
