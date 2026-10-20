#include "Characters/ShamanCharacter.h"
#include "Characters/HealthComponent.h"
#include "Terrain/ShamanSpace.h"
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
#include "MonumentActor.h"
#include "EngineUtils.h"

AShamanCharacter::AShamanCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
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

	// Spell charges (spell panel slice): the player tribe's Shaman charges spells from the shared mana economy. Decided
	// before the starting spells are learned so they get their initial charging state. Without the charge columns in
	// the imported table, keep the Phase 1 mana-at-cast rule so TAKA still works (HUD + log warn).
	const FTribeDefinition* TribeDef = Data->GetTribe(TribeId);
	const bool bWantCharges = Data->bPlayerUsesSpellCharges && TribeDef && TribeDef->bPlayerControlled && Spells->SpellTable;
	bChargeDataMissing = bWantCharges && !USpellComponent::TableHasChargeData(Spells->SpellTable);
	Spells->bUseChargeEconomy = bWantCharges && !bChargeDataMissing;
	if (Spells->bUseChargeEconomy)
	{
		// Populous mana: made only by the tribe's living members (per minute, by activity); the level starts with an empty
		// pool, so the first mana goes straight into charging Blast.
		Spells->bTribeManaGeneration = true;
		Spells->ManaConfig = Data->Mana;
		Spells->MaxMana = Data->Mana.PoolCap;
		Spells->Mana = FMath::Min(Data->Mana.StartingMana, Spells->MaxMana);
	}
	if (bChargeDataMissing)
	{
		UE_LOG(LogShaman, Warning, TEXT("[Spells] %s has no MaxCharges data: reimport Content/Data/Spells.csv into it. Using mana-at-cast until then."),
			*Spells->SpellTable->GetName());
	}
	Spells->OnSpellbookChanged.AddUObject(this, &AShamanCharacter::HandleSpellbookChanged);

	for (const FName& Id : Data->StartingSpells) Spells->LearnSpell(Id);
	for (const FName& Id : Progression->GetPermanentSpells()) Spells->LearnSpell(Id); // Vault of Knowledge carry-over
	// Map-specific discovery: spells taught by monuments on this map ("?" until learned). Monuments that begin play
	// after this Shaman register themselves (AMonumentActor::BeginPlay).
	for (TActorIterator<AMonumentActor> It(GetWorld()); It; ++It)
	{
		FName Reward;
		if (It->GetSpellUnlockReward(Reward)) Spells->SetSpellAvailableOnMap(Reward);
	}
	CacheSelectedSpell();
}

void AShamanCharacter::CacheSelectedSpell() { SelectedSpellIdCache = GetSelectedSpellId(); }

void AShamanCharacter::HandleSpellbookChanged(FName /*SpellId*/)
{
	// Keep the same spell selected when the list changes (temporary spells come and go); if it is gone, stay on the
	// same slot index, clamped.
	const TArray<FName>& Known = Spells->KnownSpellIds;
	const int32 I = SelectedSpellIdCache.IsNone() ? INDEX_NONE : Known.IndexOfByKey(SelectedSpellIdCache);
	if (I != INDEX_NONE) SelectedSpellIndex = I;
	else SelectedSpellIndex = Known.Num() > 0 ? FMath::Clamp(SelectedSpellIndex, 0, Known.Num() - 1) : 0;
	CacheSelectedSpell();
}

// --- Spells ----------------------------------------------------------------------------------------------

FName AShamanCharacter::GetSelectedSpellId() const
{
	return Spells->KnownSpellIds.IsValidIndex(SelectedSpellIndex) ? Spells->KnownSpellIds[SelectedSpellIndex] : NAME_None;
}

bool AShamanCharacter::SelectSpell(FName SpellId)
{
	const int32 Index = Spells->KnownSpellIds.IndexOfByKey(SpellId);
	if (Index == INDEX_NONE) return false;
	SelectedSpellIndex = Index;
	CacheSelectedSpell();
	return true;
}

FVector AShamanCharacter::GetAimPoint() const
{
	const AController* C = GetController();
	FVector ViewLoc; FRotator ViewRot;
	if (const APlayerController* PC = Cast<APlayerController>(C)) PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	else { ViewLoc = GetActorLocation() + FShamanSpace::GetUp(this, GetActorLocation()) * 60.f; ViewRot = GetActorRotation(); }
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
		SetActorRotation(FShamanSpace::UprightRotation(this, GetActorLocation(), To)); // projectiles leave from the Shaman's front
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
	ResetPlanetView();
}

void AShamanCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsPlayerControlled())
	{
		UpdatePlanetCamera();
		UpdateFocus();
	}
}

// --- Planet camera ------------------------------------------------------------------------------------------------

void AShamanCharacter::ResetPlanetView()
{
	FPlanetFrame Planet;
	if (!FShamanSpace::GetPlanet(this, Planet)) return;
	PlanetViewForward = Planet.ProjectOntoTangent(GetActorForwardVector(), GetActorLocation()).GetSafeNormal();
	PlanetViewPitch = -15.f;
}

void AShamanCharacter::UpdatePlanetCamera()
{
	FPlanetFrame Planet;
	const bool bPlanet = FShamanSpace::GetPlanet(this, Planet);
	if (bPlanet != bPlanetCamera)
	{
		bPlanetCamera = bPlanet;
		CameraBoom->bUsePawnControlRotation = !bPlanet;
		CameraBoom->SetUsingAbsoluteRotation(bPlanet);
		if (bPlanet) ResetPlanetView();
		else CameraBoom->SetRelativeRotation(FRotator::ZeroRotator);
	}
	if (!bPlanet) return;

	FVector Fwd, Right, Up;
	Planet.GetTangentBasis(GetActorLocation(), PlanetViewForward, Fwd, Right, Up); // parallel transport to the new tangent plane
	PlanetViewForward = Fwd;
	const float P = FMath::DegreesToRadians(PlanetViewPitch);
	const FVector ViewDir = Fwd * FMath::Cos(P) + Up * FMath::Sin(P);
	CameraBoom->SetWorldRotation(FRotationMatrix::MakeFromXZ(ViewDir, Up).ToQuat());
}

void AShamanCharacter::InputTurn(float V)
{
	if (!bPlanetCamera) { AddControllerYawInput(V); return; }
	if (V == 0.f) return;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const float Deg = V * (PC ? PC->InputYawScale : 2.5f);
	PlanetViewForward = PlanetViewForward.RotateAngleAxis(Deg, FShamanSpace::GetUp(this, GetActorLocation()));
}

void AShamanCharacter::InputLookUp(float V)
{
	if (!bPlanetCamera) { AddControllerPitchInput(V); return; }
	if (V == 0.f) return;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	PlanetViewPitch = FMath::Clamp(PlanetViewPitch + V * (PC ? PC->InputPitchScale : -2.5f), PlanetPitchMin, PlanetPitchMax);
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
	const FVector ViewDir = FShamanSpace::HorizontalNormal(this, GetActorLocation(), ViewRot.Vector());

	float BestScore = -1.f;
	for (const FOverlapResult& H : Hits)
	{
		AActor* A = H.GetActor();
		IShamanInteractable* I = Cast<IShamanInteractable>(A);
		if (!I || !I->CanInteract(this)) continue;
		const float Facing = FVector::DotProduct(FShamanSpace::HorizontalDirection(this, GetActorLocation(), A->GetActorLocation()), ViewDir);
		if (Facing < 0.25f) continue;
		const float Score = Facing * 2.f + (1.f - FShamanSpace::HorizontalDistance(this, GetActorLocation(), A->GetActorLocation()) / (Range + 400.f));
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
	Input->BindAxis("Turn", this, &AShamanCharacter::InputTurn);     // = AddControllerYawInput on flat worlds
	Input->BindAxis("LookUp", this, &AShamanCharacter::InputLookUp); // = AddControllerPitchInput on flat worlds
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
	if (bPlanetCamera) { AddMovementInput(PlanetViewForward, V); return; }
	const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V);
}

void AShamanCharacter::MoveRight(float V)
{
	if (!Controller || V == 0.f || !IsAlive()) return;
	if (bPlanetCamera) { AddMovementInput(FVector::CrossProduct(FShamanSpace::GetUp(this, GetActorLocation()), PlanetViewForward), V); return; }
	const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V);
}

void AShamanCharacter::TurnAtRate(float V) { InputTurn(V * BaseTurnRate * GetWorld()->GetDeltaSeconds()); }
void AShamanCharacter::LookUpAtRate(float V) { InputLookUp(V * BaseLookUpRate * GetWorld()->GetDeltaSeconds()); }

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
	CacheSelectedSpell();
}

void AShamanCharacter::InputPrevSpell()
{
	const int32 N = Spells->KnownSpellIds.Num();
	if (N > 0) SelectedSpellIndex = (SelectedSpellIndex + N - 1) % N;
	CacheSelectedSpell();
}

void AShamanCharacter::InputRally()
{
	if (!IsAlive()) return;
	const float R = UShamanGameData::Get(this)->RallyRadius;
	if (UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>())
		for (AShamanUnitBase* F : Tribes->GetFollowers(GetTribeId()))
			if (FVector::Dist(F->GetActorLocation(), GetActorLocation()) <= R) // 3D distance: valid on planets too
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

// --- Spell panel slice: cheats and test fixtures (console) ------------------------------------------------------------

static void ReportSpellConsole(const FString& Msg)
{
	UE_LOG(LogShaman, Log, TEXT("[Spells] %s"), *Msg);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, Msg);
}

void AShamanCharacter::ShamanUnlockAllSpells()
{
	if (!Spells->SpellTable) { ReportSpellConsole(TEXT("Cannot unlock spells: no spell DataTable is loaded.")); return; }
	int32 Learned = 0, Working = 0, Total = 0;
	for (const FName& Id : Spells->SpellTable->GetRowNames())
	{
		const FSpellRow* SpellRow = Spells->GetSpellRow(Id);
		if (!SpellRow || !FSpellChargeRules::IsChargeable(*SpellRow)) continue; // super spells only come as temporary charges
		++Total;
		if (!Spells->IsPermanentSpell(Id)) { Spells->LearnSpell(Id); ++Learned; }
		Spells->SetStoredChargesForTest(Id, FSpellChargeRules::GetMaxCharges(*SpellRow));
		if (Spells->IsSpellImplemented(Id)) ++Working;
	}
	ReportSpellConsole(FString::Printf(TEXT("CHEAT: unlocked %d new spell(s) (%d permanent spells in total, %d with a working effect); all charges full.%s"),
		Learned, Total, Working, Total == 0 ? TEXT(" The spell table has no MaxCharges data: reimport Content/Data/Spells.csv.") : TEXT("")));
}

void AShamanCharacter::ShamanFillSpellCharges()
{
	int32 N = 0;
	for (const FName& Id : TArray<FName>(Spells->KnownSpellIds))
		if (Spells->IsPermanentSpell(Id) && Spells->GetMaxCharges(Id) > 0) { Spells->SetStoredChargesForTest(Id, Spells->GetMaxCharges(Id)); ++N; }
	ReportSpellConsole(FString::Printf(TEXT("Filled the charges of %d spell(s)."), N));
}

void AShamanCharacter::ShamanGrantTempCharges(FName SpellId, int32 Count)
{
	if (!Spells->GetSpellRow(SpellId)) { ReportSpellConsole(FString::Printf(TEXT("No spell row named '%s'."), *SpellId.ToString())); return; }
	if (Count <= 0) Count = 1;
	Spells->AddCharges(SpellId, Count); // the same reward path AMonumentActor (Stone Head) uses
	ReportSpellConsole(FString::Printf(TEXT("Granted %d temporary charge(s) of %s (now %d temporary)."), Count, *SpellId.ToString(), Spells->GetTemporaryCharges(SpellId)));
}

void AShamanCharacter::ShamanSpellOnMap(FName SpellId)
{
	if (!Spells->GetSpellRow(SpellId)) { ReportSpellConsole(FString::Printf(TEXT("No spell row named '%s'."), *SpellId.ToString())); return; }
	Spells->SetSpellAvailableOnMap(SpellId);
	ReportSpellConsole(FString::Printf(TEXT("%s is now discoverable on this map%s."), *SpellId.ToString(),
		Spells->IsPermanentSpell(SpellId) ? TEXT(" (already learned)") : TEXT(" (shown as ?)")));
}

void AShamanCharacter::ShamanToggleCharging(FName SpellId)
{
	if (Spells->ToggleSpellCharging(SpellId))
		ReportSpellConsole(FString::Printf(TEXT("%s charging %s."), *SpellId.ToString(), Spells->IsCharging(SpellId) ? TEXT("ON") : TEXT("OFF")));
	else
		ReportSpellConsole(FString::Printf(TEXT("%s cannot charge (not learned permanently, no charge data, no effect, or charges are off)."), *SpellId.ToString()));
}

void AShamanCharacter::ShamanSpellDump()
{
	const FTribeManaBreakdown Made = Spells->GetManaBreakdown();
	UE_LOG(LogShaman, Log, TEXT("[Spells] %s: charge economy %s, pool %.1f / %.0f, made %.1f/min (Shaman %s, %d working, %d moving, %d idle), to spells %.1f/min (%d charging), training %.1f/min, banked %.1f/min, wasted %.1f/min"),
		*GetName(), Spells->bUseChargeEconomy ? TEXT("ON") : TEXT("OFF"), Spells->Mana, Spells->MaxMana, Spells->GetManaRegen() * 60.f,
		Made.bShamanAlive ? TEXT("alive") : TEXT("dead"), Made.Working, Made.Moving, Made.Idle, Spells->GetChargeDrawPerSecond() * 60.f,
		Spells->GetActiveChargingCount(), Spells->GetTrainingPerSecond() * 60.f, Spells->GetBankedPerSecond() * 60.f, Spells->GetWastedPerSecond() * 60.f);
	for (const FName& Id : Spells->KnownSpellIds)
	{
		UE_LOG(LogShaman, Log, TEXT("[Spells]   %s: %s, charges %d/%d (+%d temporary), progress %.0f%%, charging %s, implemented %s%s"),
			*Id.ToString(), Spells->IsPermanentSpell(Id) ? TEXT("permanent") : TEXT("temporary"), Spells->GetStoredCharges(Id), Spells->GetMaxCharges(Id),
			Spells->GetTemporaryCharges(Id), Spells->GetChargeProgress01(Id) * 100.f, Spells->IsCharging(Id) ? TEXT("on") : TEXT("off"),
			Spells->IsSpellImplemented(Id) ? TEXT("yes") : TEXT("no"), Id == GetSelectedSpellId() ? TEXT("  [selected]") : TEXT(""));
	}
	for (const FName& Id : Spells->GetMapSpellIds())
		UE_LOG(LogShaman, Log, TEXT("[Spells]   on this map: %s%s"), *Id.ToString(), Spells->IsPermanentSpell(Id) ? TEXT(" (learned)") : TEXT(" (undiscovered)"));
}
