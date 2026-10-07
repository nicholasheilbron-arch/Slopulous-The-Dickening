#include "World/ResourceNode.h"
#include "Game/ShamanGameData.h"
#include "Core/ShamanVisuals.h"
#include "Core/ShamanLog.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ShamanResources"

AResourceNode::AResourceNode()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	FallbackMesh = Cone.Object;
}

void AResourceNode::BeginPlay()
{
	Super::BeginPlay();
	const UShamanGameData* Data = UShamanGameData::Get(this);
	if (const FResourceRow* R = Data->ResourceTable ? Data->ResourceTable->FindRow<FResourceRow>(ResourceId, TEXT("Resource"), false) : nullptr)
		Row = *R;
	else
		if (ShamanLog::FirstTime(TEXT("ResourceRow:") + ResourceId.ToString()))
			UE_LOG(LogShaman, Warning, TEXT("Resource row '%s' not found; using defaults (logged once)."), *ResourceId.ToString());
	Remaining = Row.Amount;
	ShamanVisuals::ApplyMesh(Mesh, Row.Mesh, FallbackMesh, Row.MeshScale, Row.Tint);
	if (!Row.bBlocksMovement)
	{
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Mesh->SetCanEverAffectNavigation(false);
	}
}

FText AResourceNode::GetInteractionText(const AActor*) const
{
	static const FText TypeNames[] = { LOCTEXT("Wood", "wood"), LOCTEXT("Food", "food"), LOCTEXT("Stone", "stone"), LOCTEXT("Ore", "ore") };
	const int32 TypeIdx = FMath::Clamp((int32)Row.Type, 0, 3);
	return FText::Format(LOCTEXT("Res", "{0}: {1} {2} (gathering arrives in Phase 2)"), Row.DisplayName, FText::AsNumber(Remaining), TypeNames[TypeIdx]);
}

#undef LOCTEXT_NAMESPACE
