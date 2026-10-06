#include "Game/ShamanGameMode.h"
#include "Game/ShamanGameData.h"
#include "World/WorldGenerator.h"
#include "World/ShamanTerrainActor.h"
#include "World/ShamanWaterActor.h"
#include "World/ResourceNode.h"
#include "Buildings/BuildingActor.h"
#include "Characters/ShamanUnitBase.h"
#include "Characters/ShamanCharacter.h"
#include "Tribes/TribeSubsystem.h"
#include "UI/ShamanHUD.h"
#include "Core/ShamanDebug.h"
#include "Core/ShamanLog.h"
#include "ProceduralMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "DrawDebugHelpers.h"

AShamanGameMode::AShamanGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AShamanCharacter::StaticClass();
	HUDClass = AShamanHUD::StaticClass();
	UnitClass = AShamanUnitBase::StaticClass();
	ShamanClass = AShamanCharacter::StaticClass();
	DefaultGameDataPath = TSoftObjectPtr<UShamanGameData>(FSoftObjectPath(TEXT("/Game/Data/DA_ShamanGameData.DA_ShamanGameData")));
}

void AShamanGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	if (UGameplayStatics::HasOption(Options, TEXT("Seed")))
	{
		SeedOverride = FCString::Atoi(*UGameplayStatics::ParseOption(Options, TEXT("Seed")));
		bHasSeedOverride = true;
	}
}

void AShamanGameMode::ResolveGameData()
{
	if (ActiveData) return;
	ActiveData = GameData;
	if (!ActiveData && !DefaultGameDataPath.IsNull()) ActiveData = DefaultGameDataPath.LoadSynchronous();
	if (!ActiveData)
	{
		// Zero-config fallback: class defaults + tables imported at their conventional paths.
		ActiveData = NewObject<UShamanGameData>(this, TEXT("TransientShamanGameData"));
		ActiveData->SpellTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Spells.DT_Spells"));
		ActiveData->UnitTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Units.DT_Units"));
		ActiveData->BuildingTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Buildings.DT_Buildings"));
		ActiveData->ResourceTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Resources.DT_Resources"));
		UE_LOG(LogShaman, Warning, TEXT("No DA_ShamanGameData found; using defaults. Tables loaded: Spells=%d Units=%d Buildings=%d Resources=%d"),
			ActiveData->SpellTable != nullptr, ActiveData->UnitTable != nullptr, ActiveData->BuildingTable != nullptr, ActiveData->ResourceTable != nullptr);
	}
	if (!ActiveData->SpellTable) UE_LOG(LogShaman, Error, TEXT("No spell table: the Shaman cannot cast. Import Content/Data/Spells.csv as DT_Spells (row type SpellRow)."));
}

void AShamanGameMode::EnsureWorldGenerated()
{
	if (bWorldGenerated) return;
	bWorldGenerated = true;
	ResolveGameData();

	FWorldSeeds Seeds = ActiveData->Seeds;
	if (bHasSeedOverride)
	{
		Seeds = FWorldSeeds(); // sub-seeds re-derived from the override
		Seeds.WorldSeed = SeedOverride;
	}
	if (Seeds.WorldSeed == 0) Seeds.WorldSeed = (int32)(FDateTime::Now().GetTicks() % 2000000000) | 1;

	const double T0 = FPlatformTime::Seconds();
	Layout = FShamanWorldGenerator::Generate(ActiveData->WorldGen, Seeds);
	const double GenMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogShaman, Log, TEXT("World generated in %.1f ms. Seeds: World=%d Biome=%d Resource=%d Tribe=%d Terrain=%d. Valid=%d FailMask=0x%x Attempts=%d Pond=%d Markers=%d"),
		GenMs, Layout.Seeds.WorldSeed, Layout.Seeds.BiomeSeed, Layout.Seeds.ResourceSeed, Layout.Seeds.TribeSeed, Layout.Seeds.TerrainSeed,
		Layout.bValid, Layout.FailMask, Layout.TerrainAttempts, Layout.bPondCarved, Layout.Markers.Num());
	if (!Layout.bValid)
		UE_LOG(LogShaman, Error, TEXT("Start area failed validation (mask 0x%x). Playable but constraints not met; try another seed."), Layout.FailMask);

	if (UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>())
		Tribes->InitTribes(ActiveData->Tribes, ActiveData->Reincarnation);

	const double T1 = FPlatformTime::Seconds();
	SpawnWorld();
	UE_LOG(LogShaman, Log, TEXT("World spawned in %.1f ms (%d actors)."), (FPlatformTime::Seconds() - T1) * 1000.0, SpawnedActors.Num());
}

// --------------------------------------------------------------------------------------------------------
// Spawning

FName AShamanGameMode::UnitIdFor(EWorldMarkerType Type) const
{
	switch (Type)
	{
	case EWorldMarkerType::Shaman:  return ActiveData->ShamanUnitId;
	case EWorldMarkerType::Brave:   return ActiveData->BraveUnitId;
	case EWorldMarkerType::Warrior: return ActiveData->WarriorUnitId;
	case EWorldMarkerType::Wildman: return ActiveData->WildmanUnitId;
	default: return NAME_None;
	}
}

FName AShamanGameMode::BuildingIdFor(EWorldMarkerType Type) const
{
	switch (Type)
	{
	case EWorldMarkerType::House:               return ActiveData->HouseBuildingId;
	case EWorldMarkerType::Campfire:            return ActiveData->CampfireBuildingId;
	case EWorldMarkerType::ReincarnationCircle: return ActiveData->CircleBuildingId;
	default: return NAME_None;
	}
}

FName AShamanGameMode::ResourceIdFor(EWorldMarkerType Type) const
{
	switch (Type)
	{
	case EWorldMarkerType::Tree:  return ActiveData->TreeResourceId;
	case EWorldMarkerType::Food:  return ActiveData->FoodResourceId;
	case EWorldMarkerType::Stone: return ActiveData->StoneResourceId;
	case EWorldMarkerType::Ore:   return ActiveData->OreResourceId;
	default: return NAME_None;
	}
}

FVector AShamanGameMode::GroundPoint(const FVector2D& P) const
{
	const float Z = FShamanWorldGenerator::GetHeightAt(Layout, P);
	if (Terrain && Terrain->Mesh)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("ShamanGround"), true);
		const FVector Start(P.X, P.Y, Layout.MaxHeight + 1000.f), End(P.X, P.Y, Layout.MinHeight - 1000.f);
		if (Terrain->Mesh->LineTraceComponent(Hit, Start, End, Params)) return Hit.ImpactPoint;
	}
	return FVector(P.X, P.Y, Z);
}

FTransform AShamanGameMode::UnitTransform(const FWorldMarker& M, float HalfHeight) const
{
	return FTransform(FRotator(0.f, M.Yaw, 0.f), GroundPoint(M.Location) + FVector(0.f, 0.f, HalfHeight + 10.f));
}

AShamanUnitBase* AShamanGameMode::SpawnUnit(TSubclassOf<AShamanUnitBase> Class, FName UnitId, int32 Tribe, const FTransform& T)
{
	AShamanUnitBase* U = GetWorld()->SpawnActorDeferred<AShamanUnitBase>(Class ? *Class : AShamanUnitBase::StaticClass(), T,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!U) return nullptr;
	U->UnitId = UnitId;
	U->TribeId = Tribe;
	U->FinishSpawning(T);
	SpawnedActors.Add(U);
	return U;
}

void AShamanGameMode::SpawnWorld()
{
	UWorld* W = GetWorld();
	const FWorldGenConfig& Cfg = ActiveData->WorldGen;

	// Terrain + water (configured before FinishSpawning so collision/nav register the final state).
	{
		const FTransform T = FTransform::Identity;
		Terrain = W->SpawnActorDeferred<AShamanTerrainActor>(AShamanTerrainActor::StaticClass(), T);
		UMaterialInterface* Mat = ActiveData->TerrainMaterial;
		if (!Mat) Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
		Terrain->Build(Layout, Mat);
		Terrain->FinishSpawning(T);
		SpawnedActors.Add(Terrain);

		AShamanWaterActor* Water = W->SpawnActorDeferred<AShamanWaterActor>(AShamanWaterActor::StaticClass(), T);
		Water->Setup(Layout.HalfSize, Layout.MinHeight, Cfg.FordableDepth, ActiveData->WaterMaterial, ActiveData->WaterColor);
		Water->FinishSpawning(T);
		SpawnedActors.Add(Water);
	}

	MarkerGround.Reset();
	MarkerGround.Reserve(Layout.Markers.Num());
	for (const FWorldMarker& M : Layout.Markers) MarkerGround.Add(GroundPoint(M.Location));

	// Buildings and resources first so units spawn around them.
	for (int32 I = 0; I < Layout.Markers.Num(); ++I)
	{
		const FWorldMarker& M = Layout.Markers[I];
		const FTransform T(FRotator(0.f, M.Yaw, 0.f), MarkerGround[I]);
		const FName BId = BuildingIdFor(M.Type);
		if (!BId.IsNone())
		{
			ABuildingActor* B = W->SpawnActorDeferred<ABuildingActor>(ABuildingActor::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			B->BuildingId = BId;
			B->TribeId = M.TribeIndex;
			B->FinishSpawning(T);
			SpawnedActors.Add(B);
			continue;
		}
		const FName RId = ResourceIdFor(M.Type);
		if (!RId.IsNone())
		{
			AResourceNode* R = W->SpawnActorDeferred<AResourceNode>(AResourceNode::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			R->ResourceId = RId;
			R->FinishSpawning(T);
			SpawnedActors.Add(R);
		}
	}

	// Units (the player's own Shaman is spawned by RestartPlayer).
	for (const FWorldMarker& M : Layout.Markers)
	{
		const FName UId = UnitIdFor(M.Type);
		if (UId.IsNone()) continue;
		if (M.Type == EWorldMarkerType::Shaman && M.TribeIndex == 0) continue;
		const int32 Tribe = M.Type == EWorldMarkerType::Wildman ? -1 : M.TribeIndex;
		TSubclassOf<AShamanUnitBase> Cls = M.Type == EWorldMarkerType::Shaman ? TSubclassOf<AShamanUnitBase>(ShamanClass) : UnitClass;
		SpawnUnit(Cls, UId, Tribe, UnitTransform(M, 95.f));
	}
}

void AShamanGameMode::RestartPlayer(AController* NewPlayer)
{
	EnsureWorldGenerated();
	if (!NewPlayer || NewPlayer->IsPendingKillPending()) return;
	FTransform T(FRotator::ZeroRotator, FVector(Layout.PlayerStart.X, Layout.PlayerStart.Y, 500.f));
	for (const FWorldMarker& M : Layout.Markers)
		if (M.Type == EWorldMarkerType::Shaman && M.TribeIndex == 0) { T = UnitTransform(M, 100.f); break; }
	RestartPlayerAtTransform(NewPlayer, T);
	if (APawn* P = NewPlayer->GetPawn()) NewPlayer->SetControlRotation(FRotator(-15.f, P->GetActorRotation().Yaw, 0.f));
}

APawn* AShamanGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	UClass* Cls = GetDefaultPawnClassForController(NewPlayer);
	if (!Cls || !Cls->IsChildOf(AShamanCharacter::StaticClass()))
		return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, SpawnTransform);
	AShamanCharacter* S = GetWorld()->SpawnActorDeferred<AShamanCharacter>(Cls, SpawnTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!S) return nullptr;
	S->UnitId = ActiveData ? ActiveData->ShamanUnitId : FName(TEXT("Shaman"));
	S->TribeId = 0;
	S->AutoPossessAI = EAutoPossessAI::Disabled; // the player controller possesses it; no stray AI controller
	S->FinishSpawning(SpawnTransform);
	return S;
}

void AShamanGameMode::StartPlay()
{
	EnsureWorldGenerated();
	Super::StartPlay();

	bool bHasNavBounds = false;
	for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It) { bHasNavBounds = true; break; }
	if (!bHasNavBounds)
	{
		const FString Msg = FString::Printf(TEXT("No NavMeshBoundsVolume in the level: AI walks in straight lines. Add one covering +/-%.0f uu."), Layout.HalfSize);
		UE_LOG(LogShaman, Warning, TEXT("%s"), *Msg);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Yellow, Msg);
	}
}

// --------------------------------------------------------------------------------------------------------
// Debug / iteration

void AShamanGameMode::DestroyWorld()
{
	for (AActor* A : SpawnedActors) if (IsValid(A)) A->Destroy();
	SpawnedActors.Reset();
	Terrain = nullptr;
	bWorldGenerated = false;
}

void AShamanGameMode::ShamanRegenerate(int32 Seed)
{
	DestroyWorld();
	SeedOverride = Seed;
	bHasSeedOverride = true;
	EnsureWorldGenerated();

	// Move the player's Shaman to the new start and re-register it with the fresh tribe state.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (AShamanCharacter* S = Cast<AShamanCharacter>(PC->GetPawn()))
		{
			FTransform T(FRotator::ZeroRotator, FVector(Layout.PlayerStart.X, Layout.PlayerStart.Y, 500.f));
			for (const FWorldMarker& M : Layout.Markers)
				if (M.Type == EWorldMarkerType::Shaman && M.TribeIndex == 0) { T = UnitTransform(M, 100.f); break; }
			S->Reincarnate(T.GetLocation(), T.Rotator());
		}
}

void AShamanGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (ShamanDebug::IsEnabled()) DrawDebug();
}

void AShamanGameMode::DrawDebug() const
{
	UWorld* W = GetWorld();
	static const FColor Colors[] = { FColor::Green, FColor::Magenta, FColor(128, 128, 128), FColor::Orange, FColor(139, 69, 19),
		FColor::Yellow, FColor::Cyan, FColor::Red, FColor::Black, FColor(255, 69, 0), FColor::White };
	for (int32 I = 0; I < Layout.Markers.Num() && I < MarkerGround.Num(); ++I)
	{
		const int32 Type = FMath::Clamp((int32)Layout.Markers[I].Type, 0, 10);
		DrawDebugLine(W, MarkerGround[I], MarkerGround[I] + FVector(0, 0, 400.f), Colors[Type], false, -1.f, 0, 6.f);
	}
	const FWorldGenConfig& C = ActiveData->WorldGen;
	const FVector P = GroundPoint(Layout.PlayerStart) + FVector(0, 0, 50.f);
	DrawDebugCircle(W, P, C.StartAreaRadius, 96, FColor::Green, false, -1.f, 0, 8.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	DrawDebugCircle(W, P, C.WaterSearchRadius, 96, FColor::Blue, false, -1.f, 0, 8.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	DrawDebugLine(W, P, GroundPoint(Layout.EnemyStart) + FVector(0, 0, 50.f), FColor::Red, false, -1.f, 0, 8.f);
	DrawDebugLine(W, P, GroundPoint(Layout.WildmenCamp) + FVector(0, 0, 50.f), FColor(139, 69, 19), false, -1.f, 0, 8.f);

	if (const UTribeSubsystem* Tribes = W->GetSubsystem<UTribeSubsystem>())
		for (int32 Id = 0; Id < Tribes->GetNumTribes(); ++Id)
			if (const ABuildingActor* Circle = Tribes->GetReincarnationCircle(Id))
			{
				const AShamanUnitBase* S = Tribes->GetShaman(Id);
				const float R = Tribes->GetRebirthRemaining(Id);
				const FString Txt = FString::Printf(TEXT("Tribe %d circle | Shaman %s | followers %d | rebirth time %.1fs"), Id,
					!S ? TEXT("none") : S->IsAlive() ? TEXT("alive") : *FString::Printf(TEXT("reborn in %.1fs"), R),
					Tribes->GetFollowerCount(Id),
					UTribeSubsystem::ComputeRebirthTime(ActiveData->Reincarnation, Tribes->GetFollowerCount(Id)));
				DrawDebugString(W, Circle->GetActorLocation() + FVector(0, 0, 300.f), Txt, nullptr, Tribes->GetTribeColor(Id).ToFColor(true), 0.f, true);
			}
}
