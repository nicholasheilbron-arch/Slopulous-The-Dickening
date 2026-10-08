#include "Game/ShamanPlanetGameMode.h"
#include "Game/ShamanGameData.h"
#include "Terrain/ShamanTerrainSubsystem.h"
#include "Terrain/ShamanTerrainBackend.h"
#include "Terrain/ShamanSpace.h"
#include "Characters/ShamanUnitBase.h"
#include "Characters/ShamanCharacter.h"
#include "Characters/ShamanCharacterMovementComponent.h"
#include "Buildings/BuildingActor.h"
#include "World/SurfaceProbeActor.h"
#include "World/PlanetWaterActor.h"
#include "Tribes/TribeSubsystem.h"
#include "Core/ShamanDebug.h"
#include "Core/ShamanLog.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/Engine.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/Light.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "DrawDebugHelpers.h"

AShamanPlanetGameMode::AShamanPlanetGameMode()
{
	TerrainBackendClass = FSoftClassPath(TEXT("/Script/ShamanVoxel.VoxelPluginTerrainBackend"));
}

void AShamanPlanetGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage); // parses ?Seed= into SeedOverride
	// The southern hemisphere lies far below world Z = 0: disable KillZ / world-bounds destruction on planets.
	if (AWorldSettings* WS = GetWorldSettings()) WS->bEnableWorldBoundsChecks = false;
	if (bHasSeedOverride) Planet.Seed = SeedOverride;
	if (UGameplayStatics::HasOption(Options, TEXT("TerrainBackend")))
		BackendOption = UGameplayStatics::ParseOption(Options, TEXT("TerrainBackend"));
}

// ---- World building -----------------------------------------------------------------------------------------------

bool AShamanPlanetGameMode::BuildPlanet()
{
	UShamanTerrainSubsystem* TerrainSys = UShamanTerrainSubsystem::Get(this);
	if (!TerrainSys) return false;

	UClass* Cls = nullptr;
	if (!BackendOption.Equals(TEXT("Analytic"), ESearchCase::IgnoreCase))
	{
		Cls = TerrainBackendClass.TryLoadClass<UShamanTerrainBackend>();
		if (!Cls) UE_LOG(LogShaman, Warning, TEXT("Terrain backend %s not found; using the analytic backend (no visuals/collision)."), *TerrainBackendClass.ToString());
	}
	if (!Cls) Cls = UAnalyticPlanetTerrainBackend::StaticClass();

	const double T0 = FPlatformTime::Seconds();
	const bool bOk = TerrainSys->InitializeTerrain(Cls, Planet);
	UE_LOG(LogShaman, Log, TEXT("Planet: backend %s init %s in %.1f ms (seed %d)"), *Cls->GetName(), bOk ? TEXT("OK") : TEXT("FAILED"),
		(FPlatformTime::Seconds() - T0) * 1000.0, Planet.Seed);
	return bOk;
}

FVector AShamanPlanetGameMode::FindStartDirection() const
{
	// Deterministic scan (Fibonacci sphere): first dry, walkable, moderate-height site whose surroundings are land too.
	const UShamanTerrainSubsystem* TerrainSys = UShamanTerrainSubsystem::Get(this);
	const FPlanetFrame F = TerrainSys->GetPlanetFrame();
	const int32 N = 4000;
	FVector Best = FVector::UpVector;
	float BestH = -1e9f;
	for (int32 i = 0; i < N; ++i)
	{
		const float Y = 1.f - 2.f * (i + 0.5f) / N;
		const float R = FMath::Sqrt(FMath::Max(0.f, 1.f - Y * Y));
		const float A = 2.39996323f * i;
		const FVector D(FMath::Cos(A) * R, FMath::Sin(A) * R, Y);
		const FTerrainSample S = TerrainSys->QueryTerrainDirection(D);
		if (S.Height > BestH) { BestH = S.Height; Best = D; }
		if (!S.bWalkable || S.bUnderwater || S.Height < 80.f || S.Height > 900.f) continue;
		FVector Fwd, Right, Up;
		F.GetTangentBasis(S.Location, FVector(1.f, 0.f, 0.f), Fwd, Right, Up);
		bool bAllLand = true;
		for (int32 k = 0; k < 8 && bAllLand; ++k)
		{
			const float Phi = k * PI / 4.f;
			const FVector P = S.Location + (Fwd * FMath::Cos(Phi) + Right * FMath::Sin(Phi)) * 1800.f;
			const FTerrainSample Q = TerrainSys->QueryTerrain(P);
			bAllLand = Q.bWalkable && Q.Height > 30.f;
		}
		if (bAllLand) return D;
	}
	UE_LOG(LogShaman, Warning, TEXT("Planet: no ideal start site; using the highest sample."));
	return Best;
}

FVector AShamanPlanetGameMode::GroundAt(const FVector2D& Offset, FVector* OutUp) const
{
	const UShamanTerrainSubsystem* TerrainSys = UShamanTerrainSubsystem::Get(this);
	const FPlanetFrame F = TerrainSys->GetPlanetFrame();
	const FVector Start = F.GetPointAt(StartDir, 0.f);
	FVector Fwd, Right, Up;
	F.GetTangentBasis(Start, FVector(1.f, 0.f, 0.f), Fwd, Right, Up);
	const FVector Dir = F.GetDirection(Start + Fwd * Offset.X + Right * Offset.Y);
	const FTerrainSample S = TerrainSys->QueryTerrainDirection(Dir);
	if (OutUp) *OutUp = S.Up;
	return S.Location;
}

FTransform AShamanPlanetGameMode::UnitSpawnAt(const FVector2D& Offset, float HalfHeight) const
{
	FVector Up;
	const FVector G = GroundAt(Offset, &Up);
	const FVector Loc = G + Up * (HalfHeight + 30.f);
	const FVector Facing = GroundAt(FVector2D::ZeroVector) - G; // face the start site
	return FTransform(FShamanSpace::UprightRotation(this, Loc, Facing.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Facing), Loc);
}

void AShamanPlanetGameMode::EnsureWorldGenerated()
{
	if (bWorldGenerated) return;
	bWorldGenerated = true;
	ResolveGameData();
	if (UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>())
		Tribes->InitTribes(ActiveData->Tribes, ActiveData->Reincarnation);

	if (!BuildPlanet())
	{
		const FString Msg = TEXT("Planet: terrain failed to initialize. See the log (LogShaman).");
		UE_LOG(LogShaman, Error, TEXT("%s"), *Msg);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Red, Msg);
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	StartDir = FindStartDirection();
	SpawnPlanetContent();
	UE_LOG(LogShaman, Log, TEXT("Planet: start site found and content spawned in %.1f ms (%d actors)"), (FPlatformTime::Seconds() - T0) * 1000.0, SpawnedActors.Num());
	if (bSpawnLightsIfMissing) SpawnLightsIfMissing();
}

void AShamanPlanetGameMode::SpawnPlanetContent()
{
	UWorld* W = GetWorld();
	UShamanTerrainSubsystem* TerrainSys = UShamanTerrainSubsystem::Get(this);
	const FPlanetFrame F = TerrainSys->GetPlanetFrame();

	// Sea (debug visual).
	Sea = W->SpawnActor<APlanetWaterActor>(APlanetWaterActor::StaticClass(), FTransform(F.Center));
	if (Sea)
	{
		Sea->Build(F.SeaLevelRadius, ActiveData->WaterMaterial, ActiveData->WaterColor);
		SpawnedActors.Add(Sea);
	}

	// Reincarnation Circle at the start site, upright, inside a protected terrain region.
	{
		FVector Up;
		const FVector G = GroundAt(FVector2D::ZeroVector, &Up);
		const FTransform T(FShamanSpace::UprightRotation(this, G, FVector(1.f, 0.f, 0.f)), G);
		Circle = W->SpawnActorDeferred<ABuildingActor>(ABuildingActor::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Circle)
		{
			Circle->BuildingId = ActiveData->CircleBuildingId;
			Circle->TribeId = 0;
			Circle->FinishSpawning(T);
			SpawnedActors.Add(Circle);
		}
		CircleRegionId = TerrainSys->RegisterProtectedRegion(G, CircleProtectRadius, TEXT("ReincarnationCircle"));
	}

	PlayerSpawn = UnitSpawnAt(FVector2D(900.f, 0.f), 100.f);

	// A Brave of the player's tribe that follows the Shaman (navigation prototype test).
	if (AShamanUnitBase* Brave = SpawnUnit(UnitClass, ActiveData->BraveUnitId, 0, UnitSpawnAt(FVector2D(1100.f, 300.f), 95.f)))
		Brave->SetOrder(EFollowerOrder::FollowShaman, Brave->GetActorLocation());

	// Wildmen for the Convert test.
	for (int32 i = 0; i < NumWildmen; ++i)
	{
		const float A = 2.f * PI * i / FMath::Max(1, NumWildmen);
		SpawnUnit(UnitClass, ActiveData->WildmanUnitId, -1, UnitSpawnAt(FVector2D(-1600.f + 250.f * FMath::Cos(A), 250.f * FMath::Sin(A)), 95.f));
	}

	// Surface object aligned to the terrain normal.
	{
		const FVector G = GroundAt(FVector2D(300.f, -1300.f));
		Probe = W->SpawnActor<ASurfaceProbeActor>(ASurfaceProbeActor::StaticClass(), FTransform(G));
		if (Probe) SpawnedActors.Add(Probe);
	}
}

void AShamanPlanetGameMode::SpawnLightsIfMissing()
{
	UWorld* W = GetWorld();
	if (TActorIterator<ALight>(W)) return; // the level has its own lighting
	FActorSpawnParameters P;
	P.ObjectFlags |= RF_Transient;
	// Sun roughly above the start site so the play area is lit.
	const FRotator SunRot = (-StartDir + FVector(0.3f, 0.2f, 0.f)).GetSafeNormal().Rotation();
	if (ADirectionalLight* Sun = W->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(SunRot), P))
	{
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sun->GetLightComponent()->SetIntensity(6.f);
		SpawnedActors.Add(Sun);
	}
	if (ASkyLight* Sky = W->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity, P))
	{
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sky->GetLightComponent()->SetIntensity(1.f);
		Sky->GetLightComponent()->RecaptureSky();
		SpawnedActors.Add(Sky);
	}
	UE_LOG(LogShaman, Log, TEXT("Planet: level had no lights; spawned a movable sun + sky light."));
}

void AShamanPlanetGameMode::StartPlay()
{
	EnsureWorldGenerated();
	AGameModeBase::StartPlay(); // skip the flat-world navmesh warning in AShamanGameMode::StartPlay
	FpsWindowStart = FPlatformTime::Seconds();
	// This mode is the spherical-terrain test bed (PlanetTest map), not the Phase 1 scenario: say so, so a map whose
	// GameMode Override was switched to it is not mistaken for a broken Phase 1 start.
	const FString Msg = FString::Printf(TEXT("Planet terrain test mode (ShamanPlanetGameMode) on map '%s': circle, 1 Brave, Wildmen only. ")
		TEXT("The Phase 1 start (settlements, resources, enemy tribe) is ShamanGameMode on ShamanPrototype."), *UWorld::RemovePIEPrefix(GetWorld()->GetMapName()));
	UE_LOG(LogShaman, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Yellow, Msg);
}

void AShamanPlanetGameMode::RestartPlayer(AController* NewPlayer)
{
	EnsureWorldGenerated();
	if (!NewPlayer || NewPlayer->IsPendingKillPending()) return;
	RestartPlayerAtTransform(NewPlayer, PlayerSpawn);
}

void AShamanPlanetGameMode::RegenerateWorld(int32 Seed)
{
	DestroyWorld();
	if (UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this)) T->ShutdownTerrain();
	Circle = nullptr; Probe = nullptr; Sea = nullptr; CircleRegionId = -1;
	Planet.Seed = Seed;
	EnsureWorldGenerated();
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (AShamanCharacter* S = Cast<AShamanCharacter>(PC->GetPawn()))
			S->Reincarnate(PlayerSpawn.GetLocation(), PlayerSpawn.Rotator());
}

// ---- Tick / performance -------------------------------------------------------------------------------------------

void AShamanPlanetGameMode::Tick(float DeltaSeconds)
{
	AGameModeBase::Tick(DeltaSeconds); // AShamanGameMode::Tick draws the flat-world debug layout

	++FpsFrames;
	FpsWorstFrameMs = FMath::Max(FpsWorstFrameMs, DeltaSeconds * 1000.f);
	const double Now = FPlatformTime::Seconds();
	if (Now - FpsWindowStart >= 5.0)
	{
		LastAvgFps = (float)(FpsFrames / (Now - FpsWindowStart));
		LastWorstFrameMs = FpsWorstFrameMs;
		int32 Units = 0;
		for (TActorIterator<AShamanUnitBase> It(GetWorld()); It; ++It) ++Units;
		UE_LOG(LogShaman, Log, TEXT("Perf: %.1f fps avg, worst frame %.1f ms, %d units, %.0f MB used"), LastAvgFps, LastWorstFrameMs, Units,
			FPlatformMemory::GetStats().UsedPhysical / (1024.0 * 1024.0));
		FpsWindowStart = Now; FpsFrames = 0; FpsWorstFrameMs = 0.f;
	}

	// Benchmark: next edit once the previous one finished rebuilding.
	if (BenchRemaining > 0 && !bBenchWaiting)
	{
		UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
		if (T && !T->IsUpdatePending())
		{
			FVector Up;
			const float A = 2.f * PI * BenchIndex / 8.f;
			const FVector G = GroundAt(FVector2D(2500.f * FMath::Cos(A), 2500.f * FMath::Sin(A)), &Up);
			FTerrainModification M;
			M.Op = (BenchIndex % 2) ? ETerrainOp::Lower : ETerrainOp::Raise;
			M.Center = G; M.Radius = 500.f; M.Strength = 200.f; M.Source = TEXT("Benchmark");
			const FTerrainModificationResult R = T->ModifyTerrain(M);
			++BenchIndex; --BenchRemaining;
			if (R.WasApplied()) { BenchApplyMs.Add(R.ApplyMs); bBenchWaiting = true; }
			else UE_LOG(LogShaman, Warning, TEXT("Benchmark: edit %d rejected (%s)"), BenchIndex, *UEnum::GetValueAsString(R.Status));
		}
	}

	if (ShamanDebug::IsEnabled()) DrawPlanetDebug();
}

void AShamanPlanetGameMode::DrawPlanetDebug() const
{
	const UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	if (!T || !T->IsPlanetActive()) return;
	const FPlanetFrame F = T->GetPlanetFrame();
	for (const FTerrainProtectedRegion& R : T->GetProtectedRegions())
	{
		FVector Fwd, Right, Up;
		F.GetTangentBasis(R.Center, FVector(1.f, 0.f, 0.f), Fwd, Right, Up);
		DrawDebugCircle(GetWorld(), R.Center + Up * 20.f, R.Radius, 48, FColor::Red, false, -1.f, 0, 6.f, Fwd, Right, false);
	}
}

// ---- Console commands ---------------------------------------------------------------------------------------------

FVector AShamanPlanetGameMode::GetEditTarget() const
{
	// Where the player aims; fallback 800 uu in front of the Shaman.
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (const AShamanCharacter* S = Cast<AShamanCharacter>(PC->GetPawn()))
		{
			const FVector Aim = S->GetAimPoint();
			if (FVector::Dist(Aim, S->GetActorLocation()) < 6000.f) return Aim;
			return S->GetActorLocation() + S->GetActorForwardVector() * 800.f;
		}
	return GroundAt(FVector2D(2500.f, 0.f));
}

void AShamanPlanetGameMode::ApplyDebugEdit(ETerrainOp Op, float Radius, float Strength, float TargetHeight)
{
	UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	if (!T) return;
	FTerrainModification M;
	M.Op = Op;
	M.Center = GetEditTarget();
	M.Radius = Radius > 0.f ? Radius : 500.f;
	M.Strength = Op == ETerrainOp::Flatten ? 1.f : (Strength > 0.f ? Strength : 300.f);
	M.TargetHeight = TargetHeight;
	M.Source = TEXT("Console");
	const FTerrainModificationResult R = T->ModifyTerrain(M);
	const FString Msg = FString::Printf(TEXT("Terrain %s: %s (edit %d, %.3f ms, region %d)"), *UEnum::GetValueAsString(Op),
		*UEnum::GetValueAsString(R.Status), R.EditIndex, R.ApplyMs, R.BlockingRegionId);
	UE_LOG(LogShaman, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, R.WasApplied() ? FColor::Green : FColor::Red, Msg);
}

void AShamanPlanetGameMode::ShamanTerrainRaise(float Radius, float Strength) { ApplyDebugEdit(ETerrainOp::Raise, Radius, Strength, 0.f); }
void AShamanPlanetGameMode::ShamanTerrainLower(float Radius, float Strength) { ApplyDebugEdit(ETerrainOp::Lower, Radius, Strength, 0.f); }

void AShamanPlanetGameMode::ShamanTerrainFlatten(float Radius, float Height)
{
	const UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	const float Target = (Height != 0.f || !T) ? Height : T->GetTerrainHeight(GetEditTarget()); // 0 = flatten to the aimed height
	ApplyDebugEdit(ETerrainOp::Flatten, Radius, 1.f, Target);
}

void AShamanPlanetGameMode::ShamanTerrainProtectedTest()
{
	UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	if (!T || !T->IsPlanetActive() || !Circle) { UE_LOG(LogShaman, Error, TEXT("ProtectedTest: no planet/circle")); return; }
	TArray<FTerrainModification> Before; T->GetEditLog(Before);
	const float H0 = T->GetTerrainHeight(Circle->GetActorLocation());

	FTerrainModification M;
	M.Op = ETerrainOp::Raise; M.Center = Circle->GetActorLocation(); M.Radius = 400.f; M.Strength = 300.f; M.Source = TEXT("ProtectedTest");
	const FTerrainModificationResult R1 = T->ModifyTerrain(M);
	// Overlapping the edge must also be rejected.
	M.Center = GroundAt(FVector2D(CircleProtectRadius + 300.f, 0.f));
	const FTerrainModificationResult R2 = T->ModifyTerrain(M);
	TArray<FTerrainModification> After; T->GetEditLog(After);
	const bool bPass = R1.Status == ETerrainModifyStatus::RejectedProtected && R1.BlockingRegionId == CircleRegionId
		&& R2.Status == ETerrainModifyStatus::RejectedProtected && After.Num() == Before.Num()
		&& T->GetTerrainHeight(Circle->GetActorLocation()) == H0;
	const FString Msg = FString::Printf(TEXT("ProtectedTest: %s (centre: %s, edge: %s, edits %d->%d)"), bPass ? TEXT("PASS") : TEXT("FAIL"),
		*UEnum::GetValueAsString(R1.Status), *UEnum::GetValueAsString(R2.Status), Before.Num(), After.Num());
	UE_LOG(LogShaman, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, bPass ? FColor::Green : FColor::Red, Msg);
}

uint32 AShamanPlanetGameMode::HashHeights(int32 Samples) const
{
	const UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	uint32 Hash = 2166136261u;
	for (int32 i = 0; i < Samples; ++i)
	{
		const float Y = 1.f - 2.f * (i + 0.5f) / Samples;
		const float R = FMath::Sqrt(FMath::Max(0.f, 1.f - Y * Y));
		const FVector D(FMath::Cos(2.39996323f * i) * R, FMath::Sin(2.39996323f * i) * R, Y);
		const float H = T->QueryTerrainDirection(D).Height;
		uint32 Bits; FMemory::Memcpy(&Bits, &H, 4);
		Hash = (Hash ^ Bits) * 16777619u;
	}
	// Plus the area around the start site, where edits usually are.
	for (int32 i = 0; i < 400; ++i)
	{
		const float H = T->GetTerrainHeight(GroundAt(FVector2D((i % 20 - 10) * 300.f, (i / 20 - 10) * 300.f)));
		uint32 Bits; FMemory::Memcpy(&Bits, &H, 4);
		Hash = (Hash ^ Bits) * 16777619u;
	}
	return Hash;
}

void AShamanPlanetGameMode::ShamanTerrainSaveReload()
{
	UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	if (!T || !T->IsPlanetActive()) return;
	const uint32 H0 = HashHeights(4000);
	TArray<FTerrainModification> Log; T->GetEditLog(Log);   // "save": settings (seed) + this list
	const double T0 = FPlatformTime::Seconds();
	const bool bReplay = T->ReplayEditLog(Log);             // "load"
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	const uint32 H1 = HashHeights(4000);
	TArray<FTerrainModification> Log2; T->GetEditLog(Log2);
	const bool bPass = bReplay && H0 == H1 && Log2.Num() == Log.Num();
	const FString Msg = FString::Printf(TEXT("SaveReload: %s (%d edits, hash %08x -> %08x, replay %.2f ms)"), bPass ? TEXT("PASS") : TEXT("FAIL"), Log.Num(), H0, H1, Ms);
	UE_LOG(LogShaman, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, bPass ? FColor::Green : FColor::Red, Msg);
}

void AShamanPlanetGameMode::ShamanTerrainBenchmark(int32 Count)
{
	UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	if (!T || !T->IsPlanetActive()) return;
	BenchRemaining = Count > 0 ? Count : 8;
	BenchIndex = 0;
	bBenchWaiting = false;
	BenchApplyMs.Reset(); BenchUpdateMs.Reset();
	T->OnTerrainUpdateCompletedNative().Remove(BenchHandle);
	BenchHandle = T->OnTerrainUpdateCompletedNative().AddWeakLambda(this, [this](const FTerrainChangeEvent& Ev)
	{
		if (Ev.Source != FName(TEXT("Benchmark"))) return;
		BenchUpdateMs.Add(Ev.UpdateMs);
		bBenchWaiting = false;
		if (BenchRemaining == 0 && BenchUpdateMs.Num() == BenchApplyMs.Num())
		{
			float SumA = 0, MaxA = 0, SumU = 0, MaxU = 0;
			for (float V : BenchApplyMs) { SumA += V; MaxA = FMath::Max(MaxA, V); }
			for (float V : BenchUpdateMs) { SumU += V; MaxU = FMath::Max(MaxU, V); }
			const int32 N = FMath::Max(1, BenchApplyMs.Num());
			const FString Msg = FString::Printf(TEXT("Benchmark: %d edits r=500. Apply avg %.3f / max %.3f ms. Mesh+collision update avg %.1f / max %.1f ms (target < 100 ms) -> %s"),
				BenchApplyMs.Num(), SumA / N, MaxA, SumU / N, MaxU, MaxU < 100.f ? TEXT("PASS") : TEXT("FAIL"));
			UE_LOG(LogShaman, Log, TEXT("%s"), *Msg);
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 15.f, MaxU < 100.f ? FColor::Green : FColor::Red, Msg);
		}
	});
	UE_LOG(LogShaman, Log, TEXT("Benchmark: running %d edits (one at a time)..."), BenchRemaining);
}

void AShamanPlanetGameMode::ShamanSpawnStress(int32 Count)
{
	if (!UShamanTerrainSubsystem::Get(this) || !ActiveData) return;
	Count = Count > 0 ? Count : 300;
	const UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	int32 Spawned = 0;
	for (int32 i = 0; i < Count * 4 && Spawned < Count; ++i)
	{
		// Deterministic spiral of spots around the start site, land only.
		const float A = 2.39996323f * i, R = 1500.f + 6.f * i;
		const FVector2D Off(R * FMath::Cos(A), R * FMath::Sin(A));
		if (!T->QueryTerrain(GroundAt(Off)).bWalkable) continue;
		const bool bBrave = (Spawned % 3) == 0;
		if (AShamanUnitBase* U = SpawnUnit(UnitClass, bBrave ? ActiveData->BraveUnitId : ActiveData->WildmanUnitId, bBrave ? 0 : -1, UnitSpawnAt(Off, 95.f)))
			++Spawned;
	}
	UE_LOG(LogShaman, Log, TEXT("Stress: spawned %d units (requested %d). Watch the 'Perf:' log lines."), Spawned, Count);
}

void AShamanPlanetGameMode::ShamanTerrainProbe()
{
	const UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	if (!T || !T->IsPlanetActive()) return;
	const FVector P = GetEditTarget();
	const FTerrainSample S = T->QueryTerrain(P);
	FTerrainRaycastHit Hit;
	const FVector From = P + S.Up * 3000.f;
	const bool bRay = T->Raycast(From, P - S.Up * 3000.f, Hit);
	const FString Msg = FString::Printf(TEXT("Probe: height %.0f, %s, walkable %d, underwater %d (depth %.0f), flags 0x%x, normal.up %.2f | raycast %d dist %.0f"),
		S.Height, *UEnum::GetValueAsString(S.Material), S.bWalkable, S.bUnderwater, S.WaterDepth, S.Flags, FVector::DotProduct(S.Normal, S.Up),
		bRay, Hit.Distance);
	UE_LOG(LogShaman, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, Msg);
}

void AShamanPlanetGameMode::ShamanTerrainReport()
{
	const UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	if (!T) return;
	int32 Units = 0, Rescues = 0;
	for (TActorIterator<AShamanUnitBase> It(GetWorld()); It; ++It)
	{
		++Units;
		if (const UShamanCharacterMovementComponent* M = Cast<UShamanCharacterMovementComponent>(It->GetCharacterMovement())) Rescues += M->GetGroundRescueCount();
	}
	FString PlayerInfo = TEXT("no player");
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (const APawn* P = PC->GetPawn())
		{
			const FVector L = P->GetActorLocation();
			PlayerInfo = FString::Printf(TEXT("player altitude %.0f, ground %.0f, water depth %.0f, up.z %.2f"),
				T->GetPlanetFrame().GetAltitude(L), T->GetTerrainHeight(L), T->GetWaterDepthAt(L), T->GetPlanetFrame().GetUp(L).Z);
		}
	const FString Msg = FString::Printf(TEXT("%s | %.1f fps (worst %.1f ms) | %d units, %d ground rescues | %s | %.0f MB used"),
		*T->GetReport(), LastAvgFps, LastWorstFrameMs, Units, Rescues, *PlayerInfo, FPlatformMemory::GetStats().UsedPhysical / (1024.0 * 1024.0));
	UE_LOG(LogShaman, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::White, Msg);
}
