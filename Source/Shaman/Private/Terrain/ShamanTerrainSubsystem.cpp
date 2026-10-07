#include "Terrain/ShamanTerrainSubsystem.h"
#include "Terrain/ShamanTerrainBackend.h"
#include "Terrain/PlanetTerrainQueries.h"
#include "Core/ShamanLog.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"

UShamanTerrainSubsystem* UShamanTerrainSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine && WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UShamanTerrainSubsystem>() : nullptr;
}

bool UShamanTerrainSubsystem::TryGetPlanetFrame(const UObject* WorldContext, FPlanetFrame& OutFrame)
{
	const UShamanTerrainSubsystem* T = Get(WorldContext);
	if (!T || !T->IsPlanetActive()) return false;
	OutFrame = T->Frame;
	return true;
}

bool UShamanTerrainSubsystem::InitializeTerrain(TSubclassOf<UShamanTerrainBackend> BackendClass, const FPlanetSettings& InSettings)
{
	if (!BackendClass || BackendClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogShaman, Error, TEXT("Terrain: invalid backend class %s"), *GetNameSafe(BackendClass.Get()));
		return false;
	}
	if (!(InSettings.Radius > 100.f))
	{
		UE_LOG(LogShaman, Error, TEXT("Terrain: planet radius %.1f is too small"), InSettings.Radius);
		return false;
	}
	ShutdownTerrain(); // only after the request was validated, so a bad call keeps the current planet
	UShamanTerrainBackend* NewBackend = NewObject<UShamanTerrainBackend>(this, BackendClass);
	if (!NewBackend->InitializeBackend(GetWorld(), InSettings))
	{
		UE_LOG(LogShaman, Error, TEXT("Terrain: backend %s failed to initialize"), *BackendClass->GetName());
		NewBackend->ShutdownBackend();
		return false;
	}
	Backend = NewBackend;
	Settings = InSettings;
	Frame = FPlanetFrame(InSettings.Center, InSettings.Radius, InSettings.SeaLevelOffset);
	Stats = FStats();
	Stats.GenerationMs = Backend->GetGenerationMs();
	UE_LOG(LogShaman, Log, TEXT("Terrain: planet ready (seed %d, radius %.0f, sea %.0f) %s"),
		Settings.Seed, Settings.Radius, Frame.SeaLevelRadius, *Backend->GetStatsString());
	return true;
}

void UShamanTerrainSubsystem::ShutdownTerrain()
{
	if (Backend) Backend->ShutdownBackend();
	Backend = nullptr;
	PendingUpdates.Reset();
	ProtectedRegions.Reset();
	NextRegionId = 1;
}

void UShamanTerrainSubsystem::Deinitialize()
{
	ShutdownTerrain();
	Super::Deinitialize();
}

// ---- Queries ----------------------------------------------------------------------------------------------------

FTerrainSample UShamanTerrainSubsystem::QueryTerrain(const FVector& WorldLocation) const
{
	if (!Backend) return FTerrainSample();
	FTerrainSample S = Backend->SampleAt(WorldLocation);
	for (const FTerrainProtectedRegion& R : ProtectedRegions)
		if (Frame.GetSurfaceDistance(S.Location, R.Center) <= R.Radius) { S.Flags = (uint8)(S.Flags | ETerrainFlags::Protected); break; }
	return S;
}

FTerrainSample UShamanTerrainSubsystem::QueryTerrainDirection(const FVector& Direction) const
{
	return Backend ? QueryTerrain(Frame.Center + Direction.GetSafeNormal() * Frame.Radius) : FTerrainSample();
}

FVector UShamanTerrainSubsystem::GetSurfaceLocation(const FVector& Direction) const
{
	return Backend ? Backend->SampleDirection(Direction).Location : FVector::ZeroVector;
}

FVector UShamanTerrainSubsystem::GetSurfaceNormal(const FVector& WorldLocation) const
{
	return Backend ? Backend->SampleAt(WorldLocation).Normal : FVector::UpVector;
}

float UShamanTerrainSubsystem::GetTerrainHeight(const FVector& WorldLocation) const
{
	return Backend ? Backend->GetHeightAt(WorldLocation) : 0.f;
}

ETerrainMaterial UShamanTerrainSubsystem::GetTerrainMaterial(const FVector& WorldLocation) const
{
	return Backend ? Backend->SampleAt(WorldLocation).Material : ETerrainMaterial::Grass;
}

bool UShamanTerrainSubsystem::IsWalkable(const FVector& WorldLocation) const
{
	return Backend && Backend->SampleAt(WorldLocation).bWalkable;
}

bool UShamanTerrainSubsystem::IsUnderwater(const FVector& WorldLocation) const
{
	return Backend && Frame.GetDepthBelowSea(WorldLocation) > 0.f;
}

float UShamanTerrainSubsystem::GetWaterDepthAt(const FVector& WorldLocation) const
{
	return Backend ? Frame.GetDepthBelowSea(WorldLocation) : 0.f;
}

bool UShamanTerrainSubsystem::Raycast(const FVector& Start, const FVector& End, FTerrainRaycastHit& OutHit) const
{
	OutHit = FTerrainRaycastHit();
	return Backend && Backend->Raycast(Start, End, OutHit);
}

// ---- Modification -----------------------------------------------------------------------------------------------

bool UShamanTerrainSubsystem::SupportsOperation(ETerrainOp Op) const
{
	return Backend && Backend->SupportsOperation(Op);
}

int32 UShamanTerrainSubsystem::FindBlockingRegion(const FTerrainModification& Request) const
{
	for (const FTerrainProtectedRegion& R : ProtectedRegions)
		if (FPlanetTerrainQueries::Overlaps(Frame, R, Request)) return R.Id;
	return -1;
}

FTerrainModificationResult UShamanTerrainSubsystem::ModifyTerrain(const FTerrainModification& Request)
{
	FTerrainModificationResult R;
	const double T0 = FPlatformTime::Seconds();
	auto Reject = [&](ETerrainModifyStatus Status)
	{
		R.Status = Status;
		++Stats.EditsRejected;
		UE_LOG(LogShaman, Log, TEXT("Terrain: %s from '%s' rejected (%s%s)"), *UEnum::GetValueAsString(Request.Op), *Request.Source.ToString(),
			*UEnum::GetValueAsString(Status), R.BlockingRegionId >= 0 ? *FString::Printf(TEXT(", region %d"), R.BlockingRegionId) : TEXT(""));
		return R;
	};

	if (!Backend) return Reject(ETerrainModifyStatus::RejectedNoBackend);
	if (!Backend->SupportsOperation(Request.Op)) return Reject(ETerrainModifyStatus::RejectedUnsupported);
	if (Request.Center.ContainsNaN() || !(Request.Radius > 0.f) || !FMath::IsFinite(Request.Strength)
		|| (Request.Center - Frame.Center).SizeSquared() < 1.f)
		return Reject(ETerrainModifyStatus::RejectedInvalid);

	R.BlockingRegionId = FindBlockingRegion(Request);
	if (R.BlockingRegionId >= 0) return Reject(ETerrainModifyStatus::RejectedProtected);

	const int32 Blocking = R.BlockingRegionId;
	R = Backend->ApplyModification(Request);
	R.BlockingRegionId = Blocking;
	R.ApplyMs = (float)((FPlatformTime::Seconds() - T0) * 1000.0);
	if (!R.WasApplied())
	{
		const ETerrainModifyStatus Failed = R.Status;
		return Reject(Failed);
	}

	++Stats.EditsApplied;
	Stats.LastApplyMs = R.ApplyMs;
	Stats.MaxApplyMs = FMath::Max(Stats.MaxApplyMs, R.ApplyMs);

	FTerrainChangeEvent Ev;
	Ev.Center = R.BoundsCenter;
	Ev.Radius = R.BoundsRadius;
	Ev.EditIndex = R.EditIndex;
	Ev.Op = Request.Op;
	Ev.Source = Request.Source;
	UE_LOG(LogShaman, Log, TEXT("Terrain: %s r=%.0f s=%.0f from '%s' applied as edit %d in %.3f ms"),
		*UEnum::GetValueAsString(Request.Op), Request.Radius, Request.Strength, *Request.Source.ToString(), R.EditIndex, R.ApplyMs);

	FPendingUpdate P; P.Event = Ev; P.RequestTime = T0;
	PendingUpdates.Add(P);

	TerrainChangedNative.Broadcast(Ev);
	OnTerrainChanged.Broadcast(Ev);
	return R;
}

int32 UShamanTerrainSubsystem::RegisterProtectedRegion(const FVector& Center, float Radius, FName Tag)
{
	FTerrainProtectedRegion Reg;
	Reg.Id = NextRegionId++;
	Reg.Center = Center;
	Reg.Radius = FMath::Max(0.f, Radius);
	Reg.Tag = Tag;
	ProtectedRegions.Add(Reg);
	UE_LOG(LogShaman, Log, TEXT("Terrain: protected region %d '%s' r=%.0f"), Reg.Id, *Tag.ToString(), Reg.Radius);
	return Reg.Id;
}

bool UShamanTerrainSubsystem::UnregisterProtectedRegion(int32 RegionId)
{
	return ProtectedRegions.RemoveAll([RegionId](const FTerrainProtectedRegion& R) { return R.Id == RegionId; }) > 0;
}

void UShamanTerrainSubsystem::GetEditLog(TArray<FTerrainModification>& OutEdits) const
{
	OutEdits.Reset();
	if (Backend) Backend->GetEditLog(OutEdits);
}

bool UShamanTerrainSubsystem::ReplayEditLog(const TArray<FTerrainModification>& Edits)
{
	if (!Backend || !Backend->ResetEdits()) return false;
	PendingUpdates.Reset();
	bool bAll = true;
	for (const FTerrainModification& M : Edits)
	{
		const FTerrainModificationResult R = Backend->ApplyModification(M);
		bAll &= R.WasApplied();
	}
	FTerrainChangeEvent Ev; // whole planet
	Ev.Center = Frame.Center;
	Ev.Radius = Frame.Radius * 2.f;
	Ev.Source = TEXT("ReplayEditLog");
	FPendingUpdate P; P.Event = Ev; P.RequestTime = FPlatformTime::Seconds();
	PendingUpdates.Add(P);
	TerrainChangedNative.Broadcast(Ev);
	OnTerrainChanged.Broadcast(Ev);
	return bAll;
}

bool UShamanTerrainSubsystem::IsUpdatePending() const
{
	return PendingUpdates.Num() > 0 || (Backend && Backend->IsUpdatePending());
}

// ---- Tick -------------------------------------------------------------------------------------------------------

void UShamanTerrainSubsystem::Tick(float DeltaTime)
{
	if (!Backend) return;
	Backend->TickBackend(DeltaTime);
	if (PendingUpdates.Num() == 0) return;

	for (FPendingUpdate& P : PendingUpdates) ++P.FramesWaited;
	// Give the backend a frame to schedule its work before trusting "not pending".
	if (Backend->IsUpdatePending() || PendingUpdates.Last().FramesWaited < 2) return;

	const double Now = FPlatformTime::Seconds();
	TArray<FPendingUpdate> Done = MoveTemp(PendingUpdates);
	PendingUpdates.Reset();
	for (FPendingUpdate& P : Done)
	{
		// Upper bound: includes frame time and any unrelated backend work (e.g. LOD changes) in flight.
		P.Event.UpdateMs = (float)((Now - P.RequestTime) * 1000.0);
		Stats.LastUpdateMs = P.Event.UpdateMs;
		Stats.MaxUpdateMs = FMath::Max(Stats.MaxUpdateMs, P.Event.UpdateMs);
		UE_LOG(LogShaman, Log, TEXT("Terrain: edit %d mesh/collision up to date after %.1f ms"), P.Event.EditIndex, P.Event.UpdateMs);
		TerrainUpdateCompletedNative.Broadcast(P.Event);
		OnTerrainUpdateCompleted.Broadcast(P.Event);
	}
}

TStatId UShamanTerrainSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UShamanTerrainSubsystem, STATGROUP_Tickables);
}

FString UShamanTerrainSubsystem::GetReport() const
{
	if (!Backend) return TEXT("Terrain: no planet");
	return FString::Printf(TEXT("Terrain: %s | applied=%d rejected=%d apply(last/max)=%.3f/%.3f ms update(last/max)=%.1f/%.1f ms protected=%d pending=%d"),
		*Backend->GetStatsString(), Stats.EditsApplied, Stats.EditsRejected, Stats.LastApplyMs, Stats.MaxApplyMs,
		Stats.LastUpdateMs, Stats.MaxUpdateMs, ProtectedRegions.Num(), IsUpdatePending() ? 1 : 0);
}
