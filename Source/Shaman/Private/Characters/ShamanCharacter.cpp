#include "Characters/ShamanCharacter.h"
#include "Characters/HealthComponent.h"
#include "SpellComponent.h"
#include "ProgressionComponent.h"
#include "SpellProjectile.h"
#include "TribeComponent.h"
#include "SpellEffectDispatcherComponent.h"
#include "Game/ShamanGameData.h"
#include "Tribes/TribeSubsystem.h"
#include "Core/ShamanInterfaces.h"
#include "Core/ShamanDebug.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Core/ShamanLog.h"
#include "DrawDebugHelpers.h"

AShamanCharacter::AShamanCharacter()
{
	PrimaryActorTick.TickInterval = 0.f; // player: per-frame focus/aim updates

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 480.f;
	CameraBoom->SocketOffset = FVector(0.f, 70.f, 70.f); // over-the-shoulder
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 12.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	Spells = CreateDefaultSubobject<USpellComponent>(TEXT("Spells"));
	Progression = CreateDefaultSubobject<UProgressionComponent>(TEXT("Progression"));
	Tribe = CreateDefaultSubobject<UTribeComponent>(TEXT("Tribe"));
	EffectDispatcher = CreateDefaultSubobject<USpellEffectDispatcherComponent>(TEXT("EffectDispatcher"));

	UnitId = TEXT("Shaman");
	GetCharacterMovement()->JumpZVelocity = 520.f;
	GetCharacterMovement()->AirControl = 0.3f;
}

void AShamanCharacter::BeginPlay()
{
	Tribe->TribeId = TribeId; // before components begin play, so the tribe registers under the right id
	Super::BeginPlay();
	const UShamanGameData* Data = UShamanGameData::Get(this);
	if (!Spells->SpellTable) Spells->SpellTable = Data->SpellTable;
	for (const TPair<FName, TSubclassOf<ASpellProjectile>>& P : Data->ProjectileClasses)
		if (!Spells->ProjectileClasses.Contains(P.Key)) Spells->ProjectileClasses.Add(P.Key, P.Value);
	for (const FName& Id : Data->StartingSpells) Spells->LearnSpell(Id);
	for (const FName& Id : Progression->GetPermanentSpells()) Spells->LearnSpell(Id); // Vault of Knowledge carry-over
}

// --- Spells ----------------------------------------------------------------------------------------------

FName AShamanCharacter::GetSelectedSpellId() const
{
	return Spells->KnownSpellIds.IsValidIndex(SelectedSpellIndex) ? Spells->KnownSpellIds[SelectedSpellIndex] : NAME_None;
}

FVector AShamanCharacter::GetAimPoint() const
{
	const AController* C = GetController();
	FVector ViewLoc; FRotator ViewRot;
	if (const APlayerController* PC = Cast<APlayerController>(C)) PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	else { ViewLoc = GetActorLocation() + FVector(0, 0, 60); ViewRot = GetActorRotation(); }
	const FVector End = ViewLoc + ViewRot.Vector() * AimTraceDistance;
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("ShamanAim"), true, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, End, ECC_Visibility, Params)) return Hit.ImpactPoint;
	return End;
}

bool AShamanCharacter::TryCastSpell(FName SpellId, FVector AimPoint)
{
	if (!IsAlive() || IsRagdolling())
	{
		Spells->LastCastResult = ESpellCastResult::CasterDisabled;
	}
	else
	{
		const FVector To = AimPoint - GetActorLocation();
		SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f)); // projectiles leave from the Shaman's front
		Spells->CastSpell(SpellId, AimPoint);
		if (ShamanDebug::IsEnabled())
		{
			DrawDebugLine(GetWorld(), GetActorLocation(), AimPoint, FColor::Yellow, false, 1.f, 0, 1.5f);
			DrawDebugCircle(GetWorld(), GetActorLocation(), Spells->GetRangeUnits(SpellId), 48, FColor::Yellow, false, 1.f, 0, 2.f,
				FVector(1, 0, 0), FVector(0, 1, 0), false);
		}
	}
	LastCastFeedback = Spells->LastCastResult;
	LastCastFeedbackTime = GetWorld()->GetTimeSeconds();
	return LastCastFeedback == ESpellCastResult::Success;
}

ESpellCastResult AShamanCharacter::GetLastCastFeedback(float& OutAge) const
{
	OutAge = (float)(GetWorld()->GetTimeSeconds() - LastCastFeedbackTime);
	return LastCastFeedback;
}

// --- Life cycle --------------------------------------------------------------------------------------------

void AShamanCharacter::HandleDeath(AActor* Killer, AController* KillerController)
{
	Super::HandleDeath(Killer, KillerController);
	if (APlayerController* PC = Cast<APlayerController>(GetController())) DisableInput(PC);
}

void AShamanCharacter::Reincarnate(const FVector& Location, const FRotator& Rotation)
{
	Super::Reincarnate(Location, Rotation);
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		EnableInput(PC);
		PC->SetControlRotation(FRotator(-15.f, Rotation.Yaw, 0.f));
	}
}

void AShamanCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsPlayerControlled()) UpdateFocus();
}

// --- Interaction ---------------------------------------------------------------------------------------------

void AShamanCharacter::UpdateFocus()
{
	FocusedInteractable = nullptr;
	if (!IsAlive()) return;
	const float Range = UShamanGameData::Get(this)->InteractRange;
	TArray<FOverlapResult> Hits;
	FCollisionObjectQueryParams Obj;
	Obj.AddObjectTypesToQuery(ECC_Pawn);
	Obj.AddObjectTypesToQuery(ECC_WorldStatic);
	Obj.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(TEXT("ShamanFocus"), false, this);
	GetWorld()->OverlapMultiByObjectType(Hits, GetActorLocation(), FQuat::Identity, Obj, FCollisionShape::MakeSphere(Range), Params);

	// Prefer what the camera looks at, then what is closest.
	FVector ViewLoc; FRotator ViewRot;
	if (const APlayerController* PC = Cast<APlayerController>(GetController())) PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	else ViewRot = GetActorRotation();
	const FVector ViewDir = ViewRot.Vector().GetSafeNormal2D();

	float BestScore = -1.f;
	for (const FOverlapResult& H : Hits)
	{
		AActor* A = H.GetActor();
		IShamanInteractable* I = Cast<IShamanInteractable>(A);
		if (!I || !I->CanInteract(this)) continue;
		const FVector To = A->GetActorLocation() - GetActorLocation();
		const float Facing = FVector::DotProduct(To.GetSafeNormal2D(), ViewDir);
		if (Facing < 0.25f) continue;
		const float Score = Facing * 2.f + (1.f - To.Size2D() / (Range + 400.f));
		if (Score > BestScore) { BestScore = Score; FocusedInteractable = A; }
	}
}

FText AShamanCharacter::GetFocusText() const
{
	const IShamanInteractable* I = Cast<IShamanInteractable>(FocusedInteractable.Get());
	return I ? I->GetInteractionText(this) : FText::GetEmpty();
}

// --- Input -----------------------------------------------------------------------------------------------------

void AShamanCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	Input->BindAxis("MoveForward", this, &AShamanCharacter::MoveForward);
	Input->BindAxis("MoveRight", this, &AShamanCharacter::MoveRight);
	Input->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	Input->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	Input->BindAxis("TurnRate", this, &AShamanCharacter::TurnAtRate);
	Input->BindAxis("LookUpRate", this, &AShamanCharacter::LookUpAtRate);
	Input->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	Input->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);
	Input->BindAction("CastSpell", IE_Pressed, this, &AShamanCharacter::InputCast);
	Input->BindAction("Melee", IE_Pressed, this, &AShamanCharacter::InputMelee);
	Input->BindAction("Interact", IE_Pressed, this, &AShamanCharacter::InputInteract);
	Input->BindAction("NextSpell", IE_Pressed, this, &AShamanCharacter::InputNextSpell);
	Input->BindAction("PrevSpell", IE_Pressed, this, &AShamanCharacter::InputPrevSpell);
	Input->BindAction("RallyFollowers", IE_Pressed, this, &AShamanCharacter::InputRally);
	Input->BindAction("ToggleDebug", IE_Pressed, this, &AShamanCharacter::InputToggleDebug);
}

void AShamanCharacter::MoveForward(float V)
{
	if (!Controller || V == 0.f || !IsAlive()) return;
	const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V);
}

void AShamanCharacter::MoveRight(float V)
{
	if (!Controller || V == 0.f || !IsAlive()) return;
	const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V);
}

void AShamanCharacter::TurnAtRate(float V) { AddControllerYawInput(V * BaseTurnRate * GetWorld()->GetDeltaSeconds()); }
void AShamanCharacter::LookUpAtRate(float V) { AddControllerPitchInput(V * BaseLookUpRate * GetWorld()->GetDeltaSeconds()); }

void AShamanCharacter::InputCast()
{
	const FName Id = GetSelectedSpellId();
	if (!Id.IsNone()) TryCastSpell(Id, GetAimPoint());
}

void AShamanCharacter::InputMelee() { TryMeleeAttack(nullptr); }

void AShamanCharacter::InputInteract()
{
	if (IShamanInteractable* I = Cast<IShamanInteractable>(FocusedInteractable.Get())) I->Interact(this);
}

void AShamanCharacter::InputNextSpell()
{
	const int32 N = Spells->KnownSpellIds.Num();
	if (N > 0) SelectedSpellIndex = (SelectedSpellIndex + 1) % N;
}

void AShamanCharacter::InputPrevSpell()
{
	const int32 N = Spells->KnownSpellIds.Num();
	if (N > 0) SelectedSpellIndex = (SelectedSpellIndex + N - 1) % N;
}

void AShamanCharacter::InputRally()
{
	if (!IsAlive()) return;
	const float R = UShamanGameData::Get(this)->RallyRadius;
	if (UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>())
		for (AShamanUnitBase* F : Tribes->GetFollowers(GetTribeId()))
			if (FVector::Dist(F->GetActorLocation(), GetActorLocation()) <= R)
				F->SetOrder(EFollowerOrder::FollowShaman, GetActorLocation());
}

void AShamanCharacter::InputToggleDebug() { ShamanDebug::Toggle(); }

void AShamanCharacter::ShamanLearnSpell(FName SpellId)
{
	FString Msg;
	if (!Spells->SpellTable) Msg = TEXT("Cannot learn spells: no spell DataTable is loaded (import Content/Data/Spells.csv, row type SpellRow).");
	else if (!Spells->GetSpellRow(SpellId)) Msg = FString::Printf(TEXT("No spell row named '%s' in %s."), *SpellId.ToString(), *Spells->SpellTable->GetName());
	else { Spells->LearnSpell(SpellId); Msg = FString::Printf(TEXT("Learned %s."), *SpellId.ToString()); }
	UE_LOG(LogShaman, Log, TEXT("%s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, Msg);
}
