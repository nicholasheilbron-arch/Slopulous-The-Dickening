#include "VoxelPluginTerrainBackend.h"
#include "ShamanPlanetVoxelGenerator.h"
#include "Terrain/PlanetHeightField.h"
#include "Core/ShamanLog.h"
#include "VoxelWorld.h"
#include "VoxelIntBox.h"
#include "VoxelTools/VoxelDataTools.h"
#include "VoxelTools/VoxelBlueprintLibrary.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "HAL/PlatformTime.h"

// VOXEL-SPECIFIC. Everything Voxel Plugin related in SHAMAN lives in this module (ShamanVoxel).
// The header stores the voxel objects as engine base types; these helpers recover the Voxel types here only.

namespace ShamanVoxelPrivate
{
	static AVoxelWorld* AsVoxelWorld(AActor* A) { return Cast<AVoxelWorld>(A); }
}
using namespace ShamanVoxelPrivate;

bool UVoxelPluginTerrainBackend::InitializeBackend(UWorld* World, const FPlanetSettings& InSettings)
{
	if (!World || !Super::InitializeBackend(World, InSettings)) return false; // builds the height field
	const double T0 = FPlatformTime::Seconds();

	// Size the octree to contain the planet plus headroom for raised terrain.
	const float VoxelSize = FMath::Max(InSettings.VoxelSize, 10.f);
	const float OuterRadius = InSettings.Radius + HeightField->GetMaxHeightBound() + EditHeadroom;
	const int32 NeededVoxels = FMath::CeilToInt(2.f * OuterRadius / VoxelSize) + 8;
	RenderOctreeDepth = 1;
	while ((32 << RenderOctreeDepth) < NeededVoxels && RenderOctreeDepth < 20) ++RenderOctreeDepth;

	UShamanPlanetVoxelGenerator* Generator = NewObject<UShamanPlanetVoxelGenerator>(this);
	Generator->HeightField = HeightField;
	GeneratorObject = Generator;

	FActorSpawnParameters SP;
	SP.ObjectFlags |= RF_Transient;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Planet centre == voxel origin, identity rotation/scale (the generator relies on it).
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>(AVoxelWorld::StaticClass(), FTransform(InSettings.Center), SP);
	VoxelWorldActor = VoxelWorld;
	if (!VoxelWorld)
	{
		UE_LOG(LogShaman, Error, TEXT("Terrain(Voxel): could not spawn AVoxelWorld"));
		return false;
	}
	VoxelWorld->VoxelSize = VoxelSize;
	VoxelWorld->SetRenderOctreeDepth(RenderOctreeDepth);
	VoxelWorld->SetGeneratorObject(Generator);
	VoxelWorld->MaterialConfig = EVoxelMaterialConfig::RGB;
	VoxelWorld->VoxelMaterial = Cast<UMaterialInterface>(VoxelMaterialPath.TryLoad());
	if (!VoxelWorld->VoxelMaterial)
		UE_LOG(LogShaman, Warning, TEXT("Terrain(Voxel): material %s not found, using the engine default"), *VoxelMaterialPath.ToString());
	VoxelWorld->bEnableCollisions = true;
	VoxelWorld->bComputeVisibleChunksCollisions = true;
	VoxelWorld->VisibleChunksCollisionsMaxLOD = VisibleChunksCollisionsMaxLOD;
	VoxelWorld->bEnableNavmesh = false;          // navigation is SHAMAN-side (see UShamanSurfaceNavigation)
	VoxelWorld->bUseCameraIfNoInvokersFound = true;
	VoxelWorld->bCreateWorldAutomatically = false;

	CreateStartTime = FPlatformTime::Seconds();
	VoxelWorld->CreateWorld();
	if (!VoxelWorld->IsCreated())
	{
		UE_LOG(LogShaman, Error, TEXT("Terrain(Voxel): CreateWorld failed"));
		VoxelWorld->Destroy();
		VoxelWorldActor = nullptr;
		return false;
	}
	UpdatePlayerInvoker();
	GenerationMs += (float)((FPlatformTime::Seconds() - T0) * 1000.0);
	UE_LOG(LogShaman, Log, TEXT("Terrain(Voxel): world created, voxel %.0f uu, octree depth %d (%d voxels), %.1f ms"),
		VoxelSize, RenderOctreeDepth, 32 << RenderOctreeDepth, GenerationMs);
	return true;
}

void UVoxelPluginTerrainBackend::ShutdownBackend()
{
	// May run during world teardown (subsystem Deinitialize): only touch objects that are still valid.
	AVoxelWorld* VoxelWorld = IsValid(VoxelWorldActor) ? AsVoxelWorld(VoxelWorldActor) : nullptr;
	const UWorld* World = VoxelWorld ? VoxelWorld->GetWorld() : nullptr;
	const bool bTearingDown = !World || World->bIsTearingDown;
	if (IsValid(PlayerInvokerComponent) && !bTearingDown) PlayerInvokerComponent->DestroyComponent();
	PlayerInvokerComponent = nullptr;
	InvokerPawn.Reset();
	if (VoxelWorld)
	{
		if (VoxelWorld->IsCreated()) VoxelWorld->DestroyWorld();
		if (!bTearingDown) VoxelWorld->Destroy();
	}
	VoxelWorldActor = nullptr;
	if (UShamanPlanetVoxelGenerator* Generator = Cast<UShamanPlanetVoxelGenerator>(GeneratorObject)) Generator->HeightField.Reset();
	GeneratorObject = nullptr;
	Super::ShutdownBackend();
}

void UVoxelPluginTerrainBackend::TickBackend(float DeltaSeconds)
{
	UpdatePlayerInvoker();
	const AVoxelWorld* VoxelWorld = AsVoxelWorld(VoxelWorldActor);
	if (InitialMeshMs < 0.f && VoxelWorld && VoxelWorld->IsCreated() && !IsUpdatePending())
	{
		InitialMeshMs = (float)((FPlatformTime::Seconds() - CreateStartTime) * 1000.0);
		UE_LOG(LogShaman, Log, TEXT("Terrain(Voxel): initial meshes + collision ready after %.0f ms"), InitialMeshMs);
	}
}

void UVoxelPluginTerrainBackend::UpdatePlayerInvoker()
{
	// High-res LOD/collision follows the player's pawn (re-attached after death/rebirth respawns).
	UWorld* World = VoxelWorldActor ? VoxelWorldActor->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (Pawn == InvokerPawn.Get() && (PlayerInvokerComponent || !Pawn)) return;
	if (PlayerInvokerComponent) { PlayerInvokerComponent->DestroyComponent(); PlayerInvokerComponent = nullptr; }
	InvokerPawn = Pawn;
	if (!Pawn || !Pawn->GetRootComponent()) return;
	UVoxelSimpleInvokerComponent* PlayerInvoker = NewObject<UVoxelSimpleInvokerComponent>(Pawn, NAME_None, RF_Transient);
	PlayerInvoker->bUseForLOD = true;
	PlayerInvoker->LODToSet = 0;
	PlayerInvoker->LODRange = InvokerLODRange;
	PlayerInvoker->bUseForCollisions = true;
	PlayerInvoker->CollisionsRange = InvokerCollisionRange;
	PlayerInvoker->bUseForNavmesh = false;
	PlayerInvoker->SetupAttachment(Pawn->GetRootComponent());
	PlayerInvoker->RegisterComponent();
	PlayerInvokerComponent = PlayerInvoker;
}

bool UVoxelPluginTerrainBackend::IsUpdatePending() const
{
	AVoxelWorld* VoxelWorld = AsVoxelWorld(VoxelWorldActor);
	if (!VoxelWorld || !VoxelWorld->IsCreated()) return false;
	return UVoxelBlueprintLibrary::GetTaskCount(VoxelWorld) > 0 || UVoxelBlueprintLibrary::IsVoxelWorldMeshLoading(VoxelWorld);
}

void UVoxelPluginTerrainBackend::RemeshBox(const FBox& WorldBox)
{
	AVoxelWorld* VoxelWorld = AsVoxelWorld(VoxelWorldActor);
	if (!VoxelWorld || !VoxelWorld->IsCreated() || !WorldBox.IsValid) return;
	const FVoxelIntBox Box = VoxelWorld->GlobalToLocalBounds(WorldBox).Extend(2);
	// The generator reads the height field directly; drop any cached generator output, then rebuild meshes +
	// collision in the box. The plugin's own voxel edit data is never used (SHAMAN owns the edit log).
	UVoxelDataTools::ClearCachedValues(VoxelWorld, Box);
	UVoxelDataTools::ClearCachedMaterials(VoxelWorld, Box);
	UVoxelBlueprintLibrary::UpdateBounds(VoxelWorld, Box);
	++RemeshCount;
}

void UVoxelPluginTerrainBackend::OnHeightFieldEdited(const FTerrainModificationResult& Result)
{
	RemeshBox(LastEditBounds);
}

void UVoxelPluginTerrainBackend::OnHeightFieldReset()
{
	const AVoxelWorld* VoxelWorld = AsVoxelWorld(VoxelWorldActor);
	if (!VoxelWorld || !VoxelWorld->IsCreated()) return;
	const float R = Settings.Radius + HeightField->GetMaxHeightBound() + EditHeadroom;
	RemeshBox(FBox(Settings.Center - FVector(R), Settings.Center + FVector(R)));
}

FString UVoxelPluginTerrainBackend::GetStatsString() const
{
	AVoxelWorld* VoxelWorld = AsVoxelWorld(VoxelWorldActor);
	return FString::Printf(TEXT("%s | voxel octreeDepth=%d initialMesh=%.0fms remeshes=%d tasks=%d"), *Super::GetStatsString(),
		RenderOctreeDepth, InitialMeshMs, RemeshCount, VoxelWorld && VoxelWorld->IsCreated() ? UVoxelBlueprintLibrary::GetTaskCount(VoxelWorld) : 0);
}
