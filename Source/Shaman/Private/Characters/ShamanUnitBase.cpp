#include "Characters/ShamanUnitBase.h"
#include "Characters/HealthComponent.h"
#include "Characters/ShamanCharacterMovementComponent.h"
#include "Terrain/ShamanSpace.h"
#include "Terrain/ShamanTerrainSubsystem.h"
#include "Characters/ShamanUnitAIController.h"
#include "RagdollReactionComponent.h"
#include "HitReactionConfig.h"
#include "Game/ShamanGameData.h"
#include "Tribes/TribeSubsystem.h"
#include "TribeMemberComponent.h"
#include "TribeComponent.h"
#include "TribeRegistrySubsystem.h"
#include "Core/ShamanTargetRules.h"
#include "Core/ShamanVisuals.h"
#include "Core/ShamanDebug.h"
#include "Core/ShamanLog.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Animation/AnimInstance.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"

#define LOCTEXT_NAMESPACE "ShamanUnits"

AShamanUnitBase::AShamanUnitBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UShamanCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f;

	Health = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));
	HitReaction = CreateDefaultSubobject<URagdollReactionComponent>(TEXT("HitReaction"));
	TribeMember = CreateDefaultSubobject<UTribeMemberComponent>(TEXT("TribeMember"));

	// Standard mannequin offset; URagdollReactionComponent::EndRagdoll restores exactly this.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -88.f), FRotator(0.f, -90.f, 0.f));

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	PlaceholderMesh = Cyl.Object;
	if (PlaceholderMesh) PlaceholderBody->SetStaticMesh(PlaceholderMesh);

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
	GetCharacterMovement()->bUseRVOAvoidance = true;
	GetCharacterMovement()->AvoidanceConsiderationRadius = 300.f;

	AIControllerClass = AShamanUnitAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void AShamanUnitBase::LoadRow()
{
	const UShamanGameData* Data = UShamanGameData::Get(this);
	if (const FUnitRow* R = Data->UnitTable ? Data->UnitTable->FindRow<FUnitRow>(UnitId, TEXT("Unit"), false) : nullptr)
	{
		Row = *R;
		return;
	}
	if (ShamanLog::FirstTime(TEXT("UnitRow:") + UnitId.ToString()))
		UE_LOG(LogShaman, Warning, TEXT("Unit row '%s' not found; using defaults (logged once)."), *UnitId.ToString());
	Row = FUnitRow();
	if (UnitId == Data->ShamanUnitId) { Row.Role = EUnitRole::Shaman; Row.bCountsAsFollower = false; }
	else if (UnitId == Data->WildmanUnitId) { Row.Role = EUnitRole::Wildman; Row.bCountsAsFollower = false; Row.bCanFight = false; }
}

void AShamanUnitBase::ApplyIdentity()
{
	// Runs before the components' BeginPlay, so TribeMember registers with its final identity.
	const bool bAdopted = TribeId < 0 && TribeMember->IsFollower() && TribeMember->TribeId >= 0;
	if (bAdopted) TribeId = TribeMember->TribeId; // spawned by a conversion (pending identity) or a Blueprint default
	TribeMember->TribeId = TribeId;
	if (!bAdopted) TribeMember->PopulationCost = FMath::Max(1, Row.PopulationCost); // keep the cost a conversion was checked against
	switch (Row.Role)
	{
	case EUnitRole::Shaman:  TribeMember->UnitKind = EUnitKind::Shaman; break;
	case EUnitRole::Wildman: TribeMember->UnitKind = EUnitKind::Wildman; TribeMember->TribeId = TribeId = ShamanTribes::NoTribe; break;
	default:                 TribeMember->UnitKind = Row.bCountsAsFollower ? EUnitKind::Follower : EUnitKind::Other; break;
	}
}

void AShamanUnitBase::ApplyRowStats()
{
	GetCapsuleComponent()->SetCapsuleSize(Row.CapsuleRadius, Row.CapsuleHalfHeight);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -Row.CapsuleHalfHeight));
	GetCharacterMovement()->MaxWalkSpeed = Row.WalkSpeed;
}

int32 AShamanUnitBase::GetTribeId() const
{
	return TribeMember ? TribeMember->TribeId : TribeId;
}

void AShamanUnitBase::BeginPlay()
{
	const UShamanGameData* Data = UShamanGameData::Get(this);
	if (UnitId.IsNone() && TribeMember->IsFollower()) UnitId = Data->BraveUnitId; // e.g. spawned as ConvertedFollowerClass
	LoadRow();
	ApplyIdentity();
	TribeMember->OnConverted.AddDynamic(this, &AShamanUnitBase::HandleConverted);
	Super::BeginPlay(); // components (TribeMember registration, Shaman's TribeComponent) begin play here

	ApplyRowStats();
	Health->InitHealth(Row.MaxHealth);
	Health->OnDied.AddDynamic(this, &AShamanUnitBase::HandleDeath);

	if (!HitReaction->Config)
		HitReaction->Config = Data->HitReaction ? Data->HitReaction : NewObject<UHitReactionConfig>(HitReaction);

	HomeLocation = GetActorLocation();
	OrderLocation = HomeLocation;
	const FTribeDefinition* Tribe = Data->GetTribe(TribeId);
	Order = Tribe ? Tribe->DefaultFollowerOrder : EFollowerOrder::Wander;

	ApplyVisuals();
	if (IsShaman())
		if (UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>()) Tribes->RegisterShaman(this);
}

void AShamanUnitBase::EndPlay(const EEndPlayReason::Type Reason)
{
	// Follower roster cleanup happens in UTribeMemberComponent::EndPlay.
	if (IsShaman())
		if (UWorld* W = GetWorld())
			if (UTribeSubsystem* Tribes = W->GetSubsystem<UTribeSubsystem>()) Tribes->UnregisterShaman(this);
	Super::EndPlay(Reason);
}

void AShamanUnitBase::HandleConverted(UTribeMemberComponent* From, UTribeMemberComponent* To, EConversionKind Kind)
{
	if (To != TribeMember) return; // replaced by another actor: this one is about to be destroyed
	const UShamanGameData* Data = UShamanGameData::Get(this);
	TribeId = TribeMember->TribeId;
	UnitId = Data->BraveUnitId;
	const float HealthFraction = Health->GetHealthFraction();
	LoadRow();
	ApplyRowStats();
	Health->InitHealth(Row.MaxHealth);
	Health->Health = Row.MaxHealth * HealthFraction;
	HomeLocation = GetActorLocation();
	const FTribeDefinition* T = Data->GetTribe(TribeId);
	SetOrder(T ? T->DefaultFollowerOrder : EFollowerOrder::Wander, GetActorLocation());
	ApplyVisuals();
}

void AShamanUnitBase::ApplyVisuals()
{
	USkeletalMesh* Skel = Row.SkeletalMesh.IsNull() ? nullptr : Row.SkeletalMesh.LoadSynchronous();
	if (Skel)
	{
		GetMesh()->SetSkeletalMesh(Skel);
		if (UClass* Anim = Row.AnimClass.IsNull() ? nullptr : Row.AnimClass.LoadSynchronous())
			GetMesh()->SetAnimInstanceClass(Anim);
		PlaceholderBody->SetHiddenInGame(true);
	}
	else
	{
		PlaceholderBody->SetHiddenInGame(false);
		PlaceholderBody->SetRelativeScale3D(Row.PlaceholderScale);
		PlaceholderBody->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	}
	PlaceholderRestTransform = PlaceholderBody->GetRelativeTransform();

	const UShamanGameData* Data = UShamanGameData::Get(this);
	FLinearColor Color = Data->WildmenColor;
	if (const FTribeDefinition* T = Data->GetTribe(TribeId)) Color = T->Color;
	if (IsShaman()) Color = FMath::Lerp(Color, FLinearColor::White, 0.35f);
	ShamanVisuals::SetTint(PlaceholderBody, Color);
}

bool AShamanUnitBase::IsAlive() const { return Health && !Health->IsDead(); }
bool AShamanUnitBase::IsRagdolling() const { return HitReaction && HitReaction->IsRagdolling(); }

FVector AShamanUnitBase::GetBodyLocation() const
{
	if (IsRagdolling() && GetMesh() && GetMesh()->IsSimulatingPhysics()) return GetMesh()->GetComponentLocation();
	return GetActorLocation();
}

void AShamanUnitBase::SetOrder(EFollowerOrder NewOrder, FVector Location)
{
	Order = NewOrder;
	OrderLocation = Location;
}

void AShamanUnitBase::ChangeTribe(int32 NewTribeId)
{
	if (NewTribeId == GetTribeId() || IsShaman()) return;
	if (TribeMember->IsWildman() && NewTribeId >= 0)
	{
		// Joining a tribe turns a Wildman into a Brave (prefer UTribeMemberComponent::ConvertToTribe, which also checks capacity).
		TribeMember->UnitKind = EUnitKind::Follower;
		UnitId = UShamanGameData::Get(this)->BraveUnitId;
		LoadRow();
		ApplyRowStats();
	}
	TribeId = NewTribeId;
	TribeMember->SetTribeId(NewTribeId); // tribe rosters update through UTribeRegistrySubsystem
	const FTribeDefinition* T = UShamanGameData::Get(this)->GetTribe(TribeId);
	Order = T ? T->DefaultFollowerOrder : EFollowerOrder::Wander;
	ApplyVisuals();
}

// ---------------------------------------------------------------------------------------------------------
// Combat

float AShamanUnitBase::GetMeleeReach(const AActor* Target) const
{
	float TargetRadius = 34.f;
	if (const ACharacter* C = Cast<ACharacter>(Target)) TargetRadius = C->GetCapsuleComponent()->GetScaledCapsuleRadius();
	return Row.MeleeRange + GetCapsuleComponent()->GetScaledCapsuleRadius() + TargetRadius;
}

bool AShamanUnitBase::IsMeleeReady() const
{
	return IsAlive() && Row.bCanFight && !IsRagdolling() && GetWorld()->GetTimeSeconds() - LastMeleeTime >= Row.MeleeCooldown;
}

AActor* AShamanUnitBase::FindMeleeTarget() const
{
	TArray<FOverlapResult> Hits;
	const float Radius = Row.MeleeRange + 120.f;
	GetWorld()->OverlapMultiByObjectType(Hits, GetActorLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Radius));
	AActor* Best = nullptr;
	float BestD = TNumericLimits<float>::Max();
	for (const FOverlapResult& H : Hits)
	{
		AActor* A = H.GetActor();
		if (!A || A == this || !FShamanTargetRules::CanAffect(this, A, ESpellTargetFilter::EnemiesOnly, false)) continue;
		const FVector To = A->GetActorLocation() - GetActorLocation();
		if (FVector::DotProduct(FShamanSpace::HorizontalDirection(this, GetActorLocation(), A->GetActorLocation()), GetActorForwardVector()) < 0.2f) continue;
		if (FShamanSpace::HorizontalDistance(this, GetActorLocation(), A->GetActorLocation()) > GetMeleeReach(A)) continue;
		if (To.SizeSquared() < BestD) { BestD = To.SizeSquared(); Best = A; }
	}
	return Best;
}

bool AShamanUnitBase::TryMeleeAttack(AActor* Target)
{
	if (!IsMeleeReady()) return false;
	if (!Target) Target = FindMeleeTarget();
	if (!Target || !FShamanTargetRules::CanAffect(this, Target, ESpellTargetFilter::EnemiesOnly, false)) return false;
	const FVector To = Target->GetActorLocation() - GetActorLocation();
	if (FShamanSpace::HorizontalDistance(this, GetActorLocation(), Target->GetActorLocation()) > GetMeleeReach(Target)) return false;

	LastMeleeTime = GetWorld()->GetTimeSeconds();
	SetActorRotation(FShamanSpace::UprightRotation(this, GetActorLocation(), To));
	UGameplayStatics::ApplyDamage(Target, Row.MeleeDamage, GetController(), this, UDamageType::StaticClass());
	if (URagdollReactionComponent* Rag = Target->FindComponentByClass<URagdollReactionComponent>())
		Rag->ApplyHit(Row.MeleeDamage * Row.MeleeKnockback, To);
	OnMeleeAttack.Broadcast(Target);
	if (ShamanDebug::IsEnabled())
		DrawDebugLine(GetWorld(), GetActorLocation(), Target->GetActorLocation(), FColor::Orange, false, 0.5f, 0, 3.f);
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// Death, drowning, rebirth

void AShamanUnitBase::HandleDeath(AActor* Killer, AController*)
{
	GetCharacterMovement()->StopMovementImmediately();
	DrownTimer = 0.f;
	if (AController* C = GetController()) C->StopMovement();

	// Leave the tribe roster now (corpses do not count toward population or mana) and stop being a target.
	TribeMember->bConvertible = false;
	if (TribeMember->IsFollower())
	{
		TribeMember->UnitKind = EUnitKind::Other;
		if (UTribeComponent* Tribe = UTribeRegistrySubsystem::Get(this) ? UTribeRegistrySubsystem::Get(this)->FindTribe(GetTribeId()) : nullptr)
			Tribe->UnregisterFollower(TribeMember);
	}
	UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>();

	FVector Dir = GetActorForwardVector() * -1.f;
	if (Killer) Dir = FShamanSpace::HorizontalDirection(this, Killer->GetActorLocation(), GetActorLocation());
	// Launch first, and keep a movement mode that applies it (MOVE_None would discard a pending launch).
	HitReaction->EnterDeathRagdoll(Dir, FMath::Max(Health->GetLastDamage(), 1.f));
	if (GetMesh()->SkeletalMesh) GetCharacterMovement()->DisableMovement(); // real ragdoll takes over

	if (!GetMesh()->SkeletalMesh) // placeholder: lay the cylinder down
	{
		PlaceholderBody->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
		PlaceholderBody->SetRelativeLocation(FVector(0.f, 0.f, -Row.CapsuleHalfHeight + Row.CapsuleRadius));
	}
	// Corpses stop blocking units and spell projectiles (projectiles are WorldDynamic).
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);

	if (IsShaman())
	{
		if (Tribes) Tribes->NotifyShamanDied(this);
	}
	else if (Row.CorpseLifetime > 0.f)
	{
		SetLifeSpan(Row.CorpseLifetime);
	}
}

void AShamanUnitBase::Reincarnate(const FVector& Location, const FRotator& Rotation)
{
	HitReaction->ForceRecover();
	Health->ResetHealth();
	DrownTimer = 0.f;
	PlaceholderBody->SetRelativeTransform(PlaceholderRestTransform);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	FPlanetFrame Planet;
	const UShamanTerrainSubsystem* Terrain = UShamanTerrainSubsystem::Get(this);
	if (Terrain && FShamanSpace::GetPlanet(this, Planet))
	{
		// Planet: re-project onto the ground under Location (callers may still add world-Z offsets) and stand upright.
		const FVector Where = Terrain->QueryTerrain(Location).Location + Planet.GetUp(Location) * (GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.f);
		SetActorLocationAndRotation(Where, FShamanSpace::UprightRotation(this, Where, Rotation.Vector()), false, nullptr, ETeleportType::TeleportPhysics);
	}
	else
	{
		SetActorLocationAndRotation(Location, FRotator(0.f, Rotation.Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	TribeMember->bConvertible = true;
	if (IsShaman())
		if (UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>()) Tribes->RegisterShaman(this);
}

void AShamanUnitBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsAlive()) return;
	UpdateWater(DeltaSeconds);
}

void AShamanUnitBase::UpdateWater(float DeltaSeconds)
{
	const UShamanGameData* Data = UShamanGameData::Get(this);
	const FVector Body = GetBodyLocation();

	FPlanetFrame Planet;
	if (FShamanSpace::GetPlanet(this, Planet))
	{
		// Planet: sea level is a sphere; depth and "out of the world" are measured radially.
		const UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
		const FVector Up = Planet.GetUp(Body);
		const FVector Feet = Body - Up * (IsRagdolling() ? 30.f : GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const FPlanetSettings& PS = T->GetPlanetSettings();
		if (Planet.GetAltitude(Feet) < -(PS.SeabedDepth + PS.Radius * 0.25f) || Planet.GetAltitude(Feet) > PS.Radius * 2.f)
		{
			UE_LOG(LogShaman, Warning, TEXT("%s left the planet (altitude %.0f)"), *GetName(), Planet.GetAltitude(Feet));
			Health->Kill(nullptr);
			return;
		}
		const bool bDeepPlanet = T->GetWaterDepthAt(Feet) > PS.FordableDepth;
		if (!bDeepPlanet || !Row.bDrowns) { DrownTimer = 0.f; return; }
		DrownTimer += DeltaSeconds;
		if (DrownTimer >= Row.DrownTime)
		{
			DrownTimer = 0.f;
			Health->Kill(nullptr);
		}
		return;
	}

	const float FeetZ = IsRagdolling() ? Body.Z - 30.f : Body.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	// Out-of-world safety net: never leave a unit falling forever.
	if (FeetZ < Data->WorldGen.SeabedDepth - 1500.f)
	{
		UE_LOG(LogShaman, Warning, TEXT("%s fell out of the world at Z=%.0f"), *GetName(), FeetZ);
		Health->Kill(nullptr);
		return;
	}

	// Shared water-hazard rule: deeper than FordableDepth below sea level (Z = 0) drowns after DrownTime.
	const bool bDeep = FeetZ < -Data->WorldGen.FordableDepth;
	if (!bDeep || !Row.bDrowns) { DrownTimer = 0.f; return; }
	DrownTimer += DeltaSeconds;
	if (DrownTimer >= Row.DrownTime)
	{
		DrownTimer = 0.f;
		Health->Kill(nullptr);
	}
}

// ---------------------------------------------------------------------------------------------------------
// Interaction

bool AShamanUnitBase::CanInteract(const AActor* Interactor) const
{
	return IsAlive() && Interactor != this;
}

FText AShamanUnitBase::GetInteractionText(const AActor* Interactor) const
{
	const ETribeRelation Rel = FShamanTargetRules::GetActorRelation(Interactor, this);
	if (Rel == ETribeRelation::Wild)
		return FText::Format(LOCTEXT("Wild", "{0}: unaligned. The Convert spell can recruit them."), Row.DisplayName);
	if (Rel == ETribeRelation::Hostile)
		return FText::Format(LOCTEXT("Hostile", "Enemy {0} ({1} HP)"), Row.DisplayName, FText::AsNumber(FMath::CeilToInt(Health->Health)));
	if (Rel == ETribeRelation::Friendly && !IsShaman())
		return Order == EFollowerOrder::FollowShaman
			? FText::Format(LOCTEXT("Hold", "[E] {0}: following you. Order to hold here."), Row.DisplayName)
			: FText::Format(LOCTEXT("Follow", "[E] {0}: holding. Order to follow you."), Row.DisplayName);
	return Row.DisplayName;
}

void AShamanUnitBase::Interact(AActor* Interactor)
{
	if (FShamanTargetRules::GetActorRelation(Interactor, this) != ETribeRelation::Friendly || IsShaman()) return;
	if (Order == EFollowerOrder::FollowShaman) SetOrder(EFollowerOrder::HoldPosition, GetActorLocation());
	else SetOrder(EFollowerOrder::FollowShaman, GetActorLocation());
}

// ---------------------------------------------------------------------------------------------------------
// Gameplay tags (data-driven filters for later systems; Phase 1 rules use tribe ids via FShamanTargetRules)

void AShamanUnitBase::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	auto AddTag = [&TagContainer](FName Name)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Name, /*ErrorIfNotFound*/ false);
		if (Tag.IsValid()) TagContainer.AddTag(Tag);
	};
	const FTribeDefinition* Tribe = UShamanGameData::Get(this)->GetTribe(TribeId);
	AddTag(Tribe ? Tribe->FactionTag : FName(TEXT("Faction.Wild")));
	static const FName RoleTags[] = { TEXT("Unit.Shaman"), TEXT("Unit.Brave"), TEXT("Unit.Warrior"), TEXT("Unit.Wildman") };
	AddTag(RoleTags[FMath::Clamp((int32)Row.Role, 0, 3)]);
	if (!IsAlive()) AddTag(TEXT("State.Dead"));
	if (IsRagdolling()) AddTag(TEXT("State.Ragdoll"));
	if (DrownTimer > 0.f) AddTag(TEXT("State.InWater"));
}

#undef LOCTEXT_NAMESPACE
