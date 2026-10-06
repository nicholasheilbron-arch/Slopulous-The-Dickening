#include "Buildings/BuildingActor.h"
#include "Game/ShamanGameData.h"
#include "Tribes/TribeSubsystem.h"
#include "Core/ShamanVisuals.h"
#include "Core/ShamanLog.h"
#include "Core/ShamanTargetRules.h"
#include "Characters/ShamanUnitBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ShamanBuildings"

ABuildingActor::ABuildingActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCanEverAffectNavigation(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	FallbackMesh = Cube.Object;
}

void ABuildingActor::BeginPlay()
{
	Super::BeginPlay();
	const UShamanGameData* Data = UShamanGameData::Get(this);
	if (const FBuildingRow* R = Data->BuildingTable ? Data->BuildingTable->FindRow<FBuildingRow>(BuildingId, TEXT("Building"), false) : nullptr)
		Row = *R;
	else
		UE_LOG(LogShaman, Warning, TEXT("Building row '%s' not found; using defaults."), *BuildingId.ToString());

	FLinearColor Tint = Row.Tint;
	if (const FTribeDefinition* T = Data->GetTribe(TribeId))
		Tint = FMath::Lerp(Row.Tint, T->Color, Row.TribeColorBlend);
	ShamanVisuals::ApplyMesh(Mesh, Row.Mesh, FallbackMesh, Row.MeshScale, Tint);
	if (!Row.bBlocksMovement)
	{
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Mesh->SetCanEverAffectNavigation(false);
	}

	if (UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>())
		Tribes->RegisterBuilding(this);
}

void ABuildingActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWorld* W = GetWorld())
		if (UTribeSubsystem* Tribes = W->GetSubsystem<UTribeSubsystem>())
			Tribes->UnregisterBuilding(this);
	Super::EndPlay(Reason);
}

bool ABuildingActor::IsInvulnerable() const
{
	// Spec: Reincarnation Circles are permanent; enemy circles only become vulnerable once their Shaman is gone (Phase 4).
	return Row.bInvulnerable || Row.bIsReincarnationSite;
}

FVector ABuildingActor::GetRebirthLocation() const
{
	const float Offset = FMath::Max(Row.MeshScale.X, Row.MeshScale.Y) * 50.f + 120.f;
	return GetActorLocation() + GetActorForwardVector() * Offset + FVector(0.f, 0.f, 100.f);
}

FText ABuildingActor::GetInteractionText(const AActor* Interactor) const
{
	const UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>();
	const bool bOwn = FShamanTargetRules::GetTribeOf(Interactor) == TribeId;
	if (Row.bIsReincarnationSite && Tribes)
	{
		const float T = UTribeSubsystem::ComputeRebirthTime(UShamanGameData::Get(this)->Reincarnation, Tribes->GetFollowerCount(TribeId));
		return FText::Format(LOCTEXT("Circle", "{0}: rebirth in {1}s with {2} followers"), Row.DisplayName,
			FText::AsNumber(FMath::RoundToInt(T)), FText::AsNumber(Tribes->GetFollowerCount(TribeId)));
	}
	if (Row.bIsGatheringPoint && bOwn)
		return FText::Format(LOCTEXT("Gather", "[E] {0}: call your followers here"), Row.DisplayName);
	if (Row.PopulationCapacity > 0 && Tribes)
		return FText::Format(LOCTEXT("House", "{0}: tribe population {1} / {2}"), Row.DisplayName,
			FText::AsNumber(Tribes->GetFollowerCount(TribeId)), FText::AsNumber(Tribes->GetPopulationCapacity(TribeId)));
	return Row.DisplayName;
}

void ABuildingActor::Interact(AActor* Interactor)
{
	if (!Row.bIsGatheringPoint || FShamanTargetRules::GetTribeOf(Interactor) != TribeId) return;
	if (UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>())
		for (AShamanUnitBase* F : Tribes->GetFollowers(TribeId))
			F->SetOrder(EFollowerOrder::HoldPosition, GetActorLocation());
}

#undef LOCTEXT_NAMESPACE
