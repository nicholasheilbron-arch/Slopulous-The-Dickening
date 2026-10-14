#include "Player/ShamanPlayerController.h"
#include "Player/ShamanRTSCamera.h"
#include "Player/ShamanSelectionComponent.h"
#include "Characters/ShamanUnitBase.h"
#include "Tribes/TribeSubsystem.h"
#include "Terrain/ShamanSpace.h"
#include "Terrain/PlanetFrame.h"
#include "Camera/PlayerCameraManager.h"
#include "Core/ShamanTargetRules.h"
#include "Core/ShamanDebug.h"
#include "Core/ShamanLog.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "HAL/IConsoleManager.h"
#include "UI/ShamanHUD.h"
#include "Characters/ShamanCharacter.h"
#include "SpellComponent.h"
#include "ShamanTypes.h"
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "Tribes/TribeTaskTypes.h"
#include "GameplayTagContainer.h"
#include "Terrain/ShamanTerrainSubsystem.h"
#include "Navigation/ShamanSurfaceNavigation.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "CollisionQueryParams.h"

static TAutoConsoleVariable<int32> CVarShamanDebugSelection(
	TEXT("shaman.DebugSelection"), 0,
	TEXT("1: log every RTS selection step (LMB press/release, mouse position, candidates, projection, pick result)."),
	ECVF_Default);

static bool SelectionDiagnostics() { return CVarShamanDebugSelection.GetValueOnGameThread() != 0; }

static const TCHAR* OrderName(EFollowerOrder O)
{
	switch (O)
	{
	case EFollowerOrder::FollowShaman: return TEXT("FollowShaman");
	case EFollowerOrder::HoldPosition: return TEXT("HoldPosition");
	case EFollowerOrder::GuardHome:    return TEXT("GuardHome");
	default:                           return TEXT("Wander");
	}
}

AShamanPlayerController::AShamanPlayerController()
{
	Selection = CreateDefaultSubobject<UShamanSelectionComponent>(TEXT("Selection"));
	RTSCameraClass = AShamanRTSCamera::StaticClass();
	bShowMouseCursor = false;
}

int32 AShamanPlayerController::GetPlayerTribe() const
{
	// The pawn's tribe is only valid after its BeginPlay (AShamanUnitBase::ApplyIdentity). In PIE the player is
	// possessed before world BeginPlay, so a negative id here means "not known yet", not "no tribe".
	if (const APawn* P = GetPawn())
	{
		const int32 T = FShamanTargetRules::GetTribeOf(P);
		if (T >= 0) return T;
	}
	return Selection ? Selection->GetPlayerTribe() : 0;
}

void AShamanPlayerController::RefreshPlayerTribe()
{
	if (!Selection) return;
	const int32 T = GetPlayerTribe();
	if (T < 0 || T == Selection->GetPlayerTribe()) return;
	UE_LOG(LogShaman, Log, TEXT("[Select] player tribe %d -> %d"), Selection->GetPlayerTribe(), T);
	Selection->SetPlayerTribe(T);
}

// ---- Input layers ------------------------------------------------------------------------------------------------------

void AShamanPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	// Controller layer (active in Shaman mode; blocked by the RTS layer in RTS mode).
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AShamanPlayerController::OnToggleModePressed);
}

static UInputComponent* MakeRTSInput(AShamanPlayerController* PC)
{
	UInputComponent* In = NewObject<UInputComponent>(PC, TEXT("ShamanRTSInput"));
	In->bBlockInput = true; // the Shaman's bindings and the controller layer never see RTS-mode input
	In->RegisterComponent();
	return In;
}

void AShamanPlayerController::ApplyControlMode(bool bFirstTime, bool bFromPossess)
{
	CancelSelectGesture();
	bOrbiting = false;
	if (!bFirstTime && !bFromPossess && PlayerInput) FlushPressedKeys(); // nothing held during a switch carries over
	const float Blend = (bFirstTime || bFromPossess) ? 0.f : CameraSettings.ModeBlendTime;

	if (Mode == EShamanControlMode::RTS)
	{
		const bool bNewCamera = EnsureRTSCamera();
		if (bNewCamera || bFirstTime) PlaceCameraAtStart();
		else if (!bFromPossess) FocusCameraOnShaman();
		RTSCamera->SetActorTickEnabled(true);
		SetViewTargetWithBlend(RTSCamera, Blend);

		if (!RTSInput)
		{
			RTSInput = MakeRTSInput(this);
			RTSInput->BindKey(EKeys::Enter, IE_Pressed, this, &AShamanPlayerController::OnToggleModePressed);
			RTSInput->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AShamanPlayerController::OnSelectPressed);
			RTSInput->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AShamanPlayerController::OnSelectReleased);
			RTSInput->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AShamanPlayerController::OnCommandPressed);
			RTSInput->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &AShamanPlayerController::OnOrbitPressed);
			RTSInput->BindKey(EKeys::MiddleMouseButton, IE_Released, this, &AShamanPlayerController::OnOrbitReleased);
			RTSInput->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AShamanPlayerController::OnZoomIn);
			RTSInput->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AShamanPlayerController::OnZoomOut);
			RTSInput->BindKey(EKeys::F, IE_Pressed, this, &AShamanPlayerController::OnFocusSelection);
			RTSInput->BindKey(EKeys::Home, IE_Pressed, this, &AShamanPlayerController::OnFocusShaman);
			RTSInput->BindKey(EKeys::Escape, IE_Pressed, this, &AShamanPlayerController::OnClearSelection);
			RTSInput->BindKey(EKeys::BackSpace, IE_Pressed, this, &AShamanPlayerController::OnClearSelection);
			RTSInput->BindKey(EKeys::F3, IE_Pressed, this, &AShamanPlayerController::OnToggleDebug);
		}
		if (!bRTSInputPushed) { PushInputComponent(RTSInput); bRTSInputPushed = true; }

		bShowMouseCursor = true;
		if (GetLocalPlayer())
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			SetInputMode(InputMode);
		}
	}
	else
	{
		if (RTSInput && bRTSInputPushed) { PopInputComponent(RTSInput); bRTSInputPushed = false; }
		StopShamanMove(TEXT("player took direct control")); // WASD drives the Shaman now
		if (RTSCamera) RTSCamera->SetActorTickEnabled(false);
		if (APawn* P = GetPawn()) SetViewTargetWithBlend(P, Blend);
		bShowMouseCursor = false;
		if (GetLocalPlayer()) SetInputMode(FInputModeGameOnly());
	}
	UE_LOG(LogShaman, Log, TEXT("[Control] %s MODE%s"), Mode == EShamanControlMode::RTS ? TEXT("RTS") : TEXT("SHAMAN"),
		bFirstTime ? TEXT(" (start)") : TEXT(""));
}

void AShamanPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn); // may reset the view target to the pawn: the mode is re-applied right after
	RefreshPlayerTribe(); // ignored while the pawn has not begun play yet (tribe unknown); PlayerTick retries

	const bool bFirst = !bModeInitialized;
	if (bFirst) { Mode = StartMode; bModeInitialized = true; }
	ApplyControlMode(bFirst, /*bFromPossess*/ true);
}

void AShamanPlayerController::SetControlMode(EShamanControlMode NewMode)
{
	if (bModeInitialized && NewMode == Mode) return;
	Mode = NewMode;
	const bool bFirst = !bModeInitialized;
	bModeInitialized = true;
	ApplyControlMode(bFirst);
}

void AShamanPlayerController::ToggleControlMode()
{
	SetControlMode(Mode == EShamanControlMode::RTS ? EShamanControlMode::Shaman : EShamanControlMode::RTS);
}

void AShamanPlayerController::ShamanControlMode() { ToggleControlMode(); }

void AShamanPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (RTSInput && bRTSInputPushed) { PopInputComponent(RTSInput); bRTSInputPushed = false; }
	if (RTSCamera && Reason == EEndPlayReason::Destroyed) RTSCamera->Destroy(); // world teardown cleans it up otherwise
	RTSCamera = nullptr;
	Super::EndPlay(Reason);
}

// ---- Camera ------------------------------------------------------------------------------------------------------------

bool AShamanPlayerController::EnsureRTSCamera()
{
	if (RTSCamera && !RTSCamera->IsPendingKillPending()) return false;
	FActorSpawnParameters P;
	P.Owner = this;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Where = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
	UClass* Cls = RTSCameraClass ? RTSCameraClass.Get() : AShamanRTSCamera::StaticClass();
	RTSCamera = GetWorld()->SpawnActor<AShamanRTSCamera>(Cls, FTransform(Where), P);
	if (RTSCamera) RTSCamera->Settings = CameraSettings;
	return RTSCamera != nullptr;
}

bool AShamanPlayerController::IsOnVisibleSide(const FVector& WorldLocation) const
{
	FPlanetFrame Planet;
	if (!PlayerCameraManager || !FShamanSpace::GetPlanet(this, Planet)) return true;
	return FVector::DotProduct(Planet.GetUp(WorldLocation), PlayerCameraManager->GetCameraLocation() - WorldLocation) >= 0.f;
}

void AShamanPlayerController::PlaceCameraAtStart()
{
	if (!RTSCamera) return;
	const APawn* S = GetPawn();
	const FVector ShamanAt = S ? S->GetActorLocation() : FVector::ZeroVector;
	FVector Anchor = ShamanAt;
	if (const UTribeSubsystem* Tribes = GetWorld()->GetSubsystem<UTribeSubsystem>()) Tribes->GetSettlementAnchor(GetPlayerTribe(), Anchor);
	// Look from behind the settlement toward the Shaman so both are in view.
	FVector Heading = FShamanSpace::HorizontalDirection(this, Anchor, ShamanAt);
	if (Heading.IsNearlyZero() && S) Heading = FShamanSpace::HorizontalNormal(this, ShamanAt, S->GetActorForwardVector());
	RTSCamera->InitializeView(Anchor, Heading);
}

void AShamanPlayerController::PanCamera(float ForwardAxis, float RightAxis, float DeltaSeconds)
{
	if (RTSCamera) RTSCamera->Pan(ForwardAxis, RightAxis, DeltaSeconds);
}

void AShamanPlayerController::OrbitCamera(float YawDeg, float PitchDeltaDeg)
{
	if (RTSCamera) RTSCamera->Orbit(YawDeg, PitchDeltaDeg);
}

void AShamanPlayerController::ZoomCamera(float Steps)
{
	if (RTSCamera) RTSCamera->Zoom(Steps);
}

void AShamanPlayerController::FocusCameraOnShaman()
{
	if (RTSCamera && GetPawn()) RTSCamera->FocusOn(GetPawn()->GetActorLocation());
}

void AShamanPlayerController::FocusCameraOnSelection()
{
	const TArray<AShamanUnitBase*> Units = Selection ? Selection->GetSelectedUnits() : TArray<AShamanUnitBase*>();
	if (!RTSCamera || Units.Num() == 0) { FocusCameraOnShaman(); return; }
	FVector Sum = FVector::ZeroVector;
	for (const AShamanUnitBase* U : Units) Sum += U->GetActorLocation();
	RTSCamera->FocusOn(Sum / Units.Num()); // projected back onto the ground (planets included)
}

void AShamanPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	RefreshPlayerTribe(); // cheap; also follows a tribe change of the possessed Shaman
	UpdateShamanMove();
	if (Mode == EShamanControlMode::RTS && RTSCamera) UpdateRTSInput(DeltaTime);
}

void AShamanPlayerController::UpdateRTSInput(float DeltaTime)
{
	auto Down = [this](const FKey& K) { return IsInputKeyDown(K) ? 1.f : 0.f; };
	float Fwd = FMath::Clamp(Down(EKeys::W) + Down(EKeys::Up) - Down(EKeys::S) - Down(EKeys::Down), -1.f, 1.f);
	float Right = FMath::Clamp(Down(EKeys::D) + Down(EKeys::Right) - Down(EKeys::A) - Down(EKeys::Left), -1.f, 1.f);

	float MX = 0.f, MY = 0.f;
	const bool bMouse = GetMousePosition(MX, MY);
	const FVector2D Mouse(MX, MY);
	if (bMouse && CameraSettings.bEdgePan && !bSelectPressed)
	{
		int32 VX = 0, VY = 0;
		GetViewportSize(VX, VY);
		const float M = CameraSettings.EdgePanMarginPx;
		if (VX > 0 && VY > 0)
		{
			if (MX <= M) Right -= 1.f; else if (MX >= VX - M) Right += 1.f;
			if (MY <= M) Fwd += 1.f; else if (MY >= VY - M) Fwd -= 1.f;
		}
	}
	PanCamera(FMath::Clamp(Fwd, -1.f, 1.f), FMath::Clamp(Right, -1.f, 1.f), DeltaTime);

	const float Yaw = Down(EKeys::E) - Down(EKeys::Q);
	if (Yaw != 0.f) OrbitCamera(Yaw * CameraSettings.RotateSpeed * DeltaTime, 0.f);
	if (bOrbiting && bMouse)
	{
		const FVector2D D = Mouse - LastMouse;
		if (!D.IsNearlyZero()) OrbitCamera(D.X * CameraSettings.MouseOrbitSpeed, D.Y * CameraSettings.MouseOrbitSpeed);
	}
	if (bSelectPressed && bMouse) UpdateSelectGesture(Mouse);
	if (bMouse) LastMouse = Mouse;
}

// ---- Selection gesture -------------------------------------------------------------------------------------------------

void AShamanPlayerController::BeginSelectGesture(const FVector2D& ScreenPos)
{
	bSelectPressed = true;
	bDragging = false;
	DragStart = DragCurrent = ScreenPos;
}

void AShamanPlayerController::UpdateSelectGesture(const FVector2D& ScreenPos)
{
	if (!bSelectPressed) return;
	DragCurrent = ScreenPos;
	if (!bDragging && FShamanSelectionRules::IsDrag(DragStart, ScreenPos, SelectionSettings.DragThresholdPx)) bDragging = true;
}

void AShamanPlayerController::CancelSelectGesture()
{
	bSelectPressed = false;
	bDragging = false;
}

void AShamanPlayerController::EndSelectGesture(const FVector2D& ScreenPos, bool bAdditive)
{
	if (!bSelectPressed || !Selection)
	{
		if (SelectionDiagnostics()) { UE_LOG(LogShaman, Log, TEXT("[SelectDiag] release ignored: no press recorded (pressed=%d)"), bSelectPressed ? 1 : 0); }
		return;
	}
	RefreshPlayerTribe();
	DragCurrent = ScreenPos;
	const bool bWasDrag = bDragging || FShamanSelectionRules::IsDrag(DragStart, ScreenPos, SelectionSettings.DragThresholdPx);
	CancelSelectGesture();

	TArray<FShamanScreenCandidate> Candidates;
	GatherScreenCandidates(Candidates);
	if (SelectionDiagnostics())
	{
		int32 OnScreen = 0;
		for (const FShamanScreenCandidate& C : Candidates) OnScreen += C.bOnScreen ? 1 : 0;
		UE_LOG(LogShaman, Log, TEXT("[SelectDiag] release at (%.0f,%.0f) start (%.0f,%.0f) %s: controller tribe %d, selection tribe %d, %d candidate(s), %d on screen"),
			ScreenPos.X, ScreenPos.Y, DragStart.X, DragStart.Y, bWasDrag ? TEXT("DRAG") : TEXT("CLICK"), GetPlayerTribe(),
			Selection->GetPlayerTribe(), Candidates.Num(), OnScreen);
	}
	if (bWasDrag)
	{
		// A drag only ever selects: never a command, cast or interaction.
		TArray<int32> Hits;
		FShamanSelectionRules::BoxSelect(Candidates, DragStart, ScreenPos, Hits);
		TArray<AShamanUnitBase*> Units;
		for (int32 I : Hits) if (AShamanUnitBase* U = Candidates[I].Unit.Get()) Units.Add(U);
		Selection->SelectUnits(Units, bAdditive);
		return;
	}
	const int32 Hit = FShamanSelectionRules::PickClosest(Candidates, ScreenPos, SelectionSettings.PickRadiusPx);
	if (SelectionDiagnostics())
	{
		float BestD = -1.f; FString BestName = TEXT("none");
		for (const FShamanScreenCandidate& C : Candidates)
		{
			if (!C.bOnScreen || !C.Unit.IsValid()) continue;
			const float D = FShamanSelectionRules::DistanceToSegment(ScreenPos, C.Feet, C.Head);
			if (BestD < 0.f || D < BestD) { BestD = D; BestName = C.Unit->GetName(); }
		}
		UE_LOG(LogShaman, Log, TEXT("[SelectDiag] click pick: %s (nearest %s at %.0f px)"),
			Hit != INDEX_NONE ? *Candidates[Hit].Unit->GetName() : TEXT("empty ground"), *BestName, BestD);
	}
	if (Hit != INDEX_NONE) Selection->ClickUnit(Candidates[Hit].Unit.Get(), bAdditive);
	else Selection->ClickEmpty(bAdditive);
}

void AShamanPlayerController::GatherScreenCandidates(TArray<FShamanScreenCandidate>& Out) const
{
	Out.Reset();
	const UTribeSubsystem* Tribes = GetWorld() ? GetWorld()->GetSubsystem<UTribeSubsystem>() : nullptr;
	if (!Tribes) return;
	const int32 Tribe = GetPlayerTribe();
	TArray<AShamanUnitBase*> Units = Tribes->GetFollowers(Tribe); // this tribe's roster only
	if (AShamanUnitBase* S = Tribes->GetShaman(Tribe)) Units.AddUnique(S);
	for (AShamanUnitBase* U : Units)
	{
		if (!FShamanSelectionRules::IsSelectable(U, Tribe)) continue;
		const FVector L = U->GetActorLocation();
		if (!IsOnVisibleSide(L)) continue; // over the planet's horizon: not visible, not pickable
		const FVector Up = FShamanSpace::GetUp(this, L);
		const float Half = U->GetCapsuleComponent() ? U->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		FShamanScreenCandidate C;
		C.Unit = U;
		C.bOnScreen = ProjectWorldLocationToScreen(L - Up * Half, C.Feet, false) && ProjectWorldLocationToScreen(L + Up * Half, C.Head, false);
		Out.Add(C);
	}
}

// ---- Handlers ------------------------------------------------------------------------------------------------------------

void AShamanPlayerController::OnToggleModePressed() { ToggleControlMode(); }

void AShamanPlayerController::OnSelectPressed()
{
	float X = 0.f, Y = 0.f;
	const bool bMouse = GetMousePosition(X, Y);
	if (SelectionDiagnostics()) { UE_LOG(LogShaman, Log, TEXT("[SelectDiag] LMB pressed: mouse %s (%.0f,%.0f)"), bMouse ? TEXT("ok") : TEXT("UNAVAILABLE"), X, Y); }
	if (!bMouse) return;
	if (HandleSpellPanelClick(FVector2D(X, Y))) return; // the panel takes the click: no selection gesture
	BeginSelectGesture(FVector2D(X, Y));
}

bool AShamanPlayerController::HandleSpellPanelClick(const FVector2D& ScreenPos)
{
	const AShamanHUD* HUD = Cast<AShamanHUD>(GetHUD());
	if (!HUD || !HUD->IsOverSpellPanel(ScreenPos)) return false;
	FShamanSpellSlot Slot;
	if (!HUD->HitTestSpellPanel(ScreenPos, Slot)) return true; // panel background / footer: swallow, do nothing
	AShamanCharacter* S = Cast<AShamanCharacter>(GetPawn());
	if (Slot.State == EShamanSpellSlotState::Available && S && S->SelectSpell(Slot.SpellId))
	{
		UE_LOG(LogShaman, Log, TEXT("[SpellPanel] selected %s (not cast: casting from RTS mode is not implemented yet; Enter for Shaman mode, then LMB casts)"),
			*Slot.SpellId.ToString());
	}
	else
	{
		// Undiscovered / locked tiles must not reveal which spell they are.
		const FString Why = Slot.State == EShamanSpellSlotState::Discoverable ? FString(TEXT("undiscovered spell (learn it on this map first)"))
			: Slot.State == EShamanSpellSlotState::Locked ? FString(TEXT("locked spell (not available on this map)"))
			: FString::Printf(TEXT("%s has no effect yet"), *Slot.SpellId.ToString());
		UE_LOG(LogShaman, Log, TEXT("[SpellPanel] %s: not selectable"), *Why);
	}
	return true;
}

bool AShamanPlayerController::HandleSpellPanelRightClick(const FVector2D& ScreenPos)
{
	const AShamanHUD* HUD = Cast<AShamanHUD>(GetHUD());
	if (!HUD || !HUD->IsOverSpellPanel(ScreenPos)) return false;
	FShamanSpellSlot Slot;
	if (!HUD->HitTestSpellPanel(ScreenPos, Slot)) return true; // panel background: swallow (no world command)
	AShamanCharacter* S = Cast<AShamanCharacter>(GetPawn());
	if (S && S->Spells && Slot.State == EShamanSpellSlotState::Available && S->Spells->ToggleSpellCharging(Slot.SpellId))
	{
		UE_LOG(LogShaman, Log, TEXT("[SpellPanel] %s charging %s (%d spell(s) now share the mana)"), *Slot.SpellId.ToString(),
			S->Spells->IsCharging(Slot.SpellId) ? TEXT("ON") : TEXT("OFF"), S->Spells->GetActiveChargingCount());
	}
	else
	{
		UE_LOG(LogShaman, Log, TEXT("[SpellPanel] right-click: this tile cannot charge (%s)"),
			Slot.State == EShamanSpellSlotState::Discoverable ? TEXT("undiscovered")
			: Slot.State == EShamanSpellSlotState::Locked ? TEXT("locked")
			: Slot.State == EShamanSpellSlotState::Unimplemented ? TEXT("no effect yet")
			: Slot.IsTemporaryOnly() ? TEXT("temporary charges never recharge")
			: !Slot.bChargeEconomy ? TEXT("spell charges are off: reimport Spells.csv") : TEXT("no charge data"));
	}
	return true;
}

void AShamanPlayerController::ShamanSelectionDump()
{
	RefreshPlayerTribe();
	TArray<FShamanScreenCandidate> Candidates;
	GatherScreenCandidates(Candidates);
	const UTribeSubsystem* Tribes = GetWorld() ? GetWorld()->GetSubsystem<UTribeSubsystem>() : nullptr;
	const int32 Tribe = GetPlayerTribe();
	UE_LOG(LogShaman, Log, TEXT("[SelectDiag] mode %s, pawn %s (tribe %d), selection tribe %d, roster %d follower(s), %d candidate(s), %d selected"),
		Mode == EShamanControlMode::RTS ? TEXT("RTS") : TEXT("SHAMAN"), GetPawn() ? *GetPawn()->GetName() : TEXT("none"), Tribe,
		Selection ? Selection->GetPlayerTribe() : -99, Tribes ? Tribes->GetFollowers(Tribe).Num() : -1, Candidates.Num(), Selection ? Selection->Num() : 0);
	for (const FShamanScreenCandidate& C : Candidates)
	{
		const AShamanUnitBase* U = C.Unit.Get();
		if (!U) continue;
		const UTribeMemberComponent* M = U->TribeMember;
		UE_LOG(LogShaman, Log, TEXT("[SelectDiag]   %s on screen %d feet (%.0f,%.0f) head (%.0f,%.0f) order %s task #%d state %s dist to Shaman %.0f"),
			*U->GetName(), C.bOnScreen ? 1 : 0, C.Feet.X, C.Feet.Y, C.Head.X, C.Head.Y, OrderName(U->GetOrder()),
			M ? M->GetCurrentTaskId() : -1, M ? FTribeTaskRules::SimStateName(M->GetSimState()) : TEXT("-"),
			GetPawn() ? FShamanSpace::HorizontalDistance(this, U->GetActorLocation(), GetPawn()->GetActorLocation()) : -1.f);
	}
}

void AShamanPlayerController::OnSelectReleased()
{
	float X = 0.f, Y = 0.f;
	const FVector2D Pos = GetMousePosition(X, Y) ? FVector2D(X, Y) : DragCurrent; // released outside the viewport: last known
	EndSelectGesture(Pos, IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
}

void AShamanPlayerController::OnOrbitPressed()
{
	float X = 0.f, Y = 0.f;
	if (GetMousePosition(X, Y)) LastMouse = FVector2D(X, Y);
	bOrbiting = true;
}

void AShamanPlayerController::OnOrbitReleased() { bOrbiting = false; }
void AShamanPlayerController::OnZoomIn() { ZoomCamera(1.f); }
void AShamanPlayerController::OnZoomOut() { ZoomCamera(-1.f); }
void AShamanPlayerController::OnFocusSelection() { FocusCameraOnSelection(); }
void AShamanPlayerController::OnFocusShaman() { FocusCameraOnShaman(); }
void AShamanPlayerController::OnClearSelection() { CancelSelectGesture(); if (Selection) Selection->ClearSelection(); }
void AShamanPlayerController::OnToggleDebug() { ShamanDebug::Toggle(); }

// ---- RTS commands ------------------------------------------------------------------------------------------------------

void AShamanPlayerController::OnCommandPressed()
{
	float X = 0.f, Y = 0.f;
	if (!GetMousePosition(X, Y) || !Selection) return;
	const FVector2D Mouse(X, Y);
	if (HandleSpellPanelRightClick(Mouse)) return; // the panel takes the click (charging on/off): no world command
	CancelSelectGesture();
	RefreshPlayerTribe();
	const TArray<AShamanUnitBase*> Units = Selection->GetSelectedUnits();
	if (Units.Num() == 0)
	{
		UE_LOG(LogShaman, Log, TEXT("[Command] nothing selected: select units with LMB first"));
		return;
	}
	// Right-click on the player's Shaman: the selected Braves guard him.
	TArray<FShamanScreenCandidate> Candidates;
	GatherScreenCandidates(Candidates);
	const int32 Hit = FShamanSelectionRules::PickClosest(Candidates, Mouse, SelectionSettings.PickRadiusPx);
	if (Hit != INDEX_NONE && Candidates[Hit].Unit.IsValid() && Candidates[Hit].Unit->IsShaman())
	{
		CommandGuardShaman(Units);
		return;
	}
	FVector Ground;
	if (!GetGroundUnderScreen(Mouse, Ground))
	{
		UE_LOG(LogShaman, Log, TEXT("[Command] no ground under the cursor"));
		return;
	}
	CommandMove(Units, Ground);
}

bool AShamanPlayerController::GetGroundUnderScreen(const FVector2D& ScreenPos, FVector& OutGround) const
{
	FVector Origin, Dir;
	if (!DeprojectScreenPositionToWorld(ScreenPos.X, ScreenPos.Y, Origin, Dir) || Dir.IsNearlyZero()) return false;
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("ShamanRTSCommand"), true, GetPawn());
	const FVector End = Origin + Dir * 500000.f;
	FVector Point;
	bool bFound = GetWorld()->LineTraceSingleByObjectType(Hit, Origin, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	if (bFound) Point = Hit.ImpactPoint;
	FPlanetFrame Planet;
	if (!bFound && FShamanSpace::GetPlanet(this, Planet))
	{
		// No collision hit (terrain collision still cooking, or none): intersect the ray with the base sphere.
		const FVector L = Origin - Planet.Center;
		const float B = FVector::DotProduct(L, Dir), C = L.SizeSquared() - FMath::Square(Planet.Radius);
		const float Disc = B * B - C;
		if (Disc >= 0.f)
		{
			const float T = -B - FMath::Sqrt(Disc);
			if (T > 0.f) { Point = Origin + Dir * T; bFound = true; }
		}
	}
	if (!bFound) return false;
	// Clicked a rooftop or a tree: the order goes to the ground under it.
	if (!FShamanSpace::ProjectToGround(this, Point, OutGround)) OutGround = Point;
	return true;
}

int32 AShamanPlayerController::CommandMove(const TArray<AShamanUnitBase*>& Units, const FVector& Ground)
{
	RefreshPlayerTribe();
	const int32 Tribe = GetPlayerTribe();
	UTribeSubsystem* Tribes = GetWorld() ? GetWorld()->GetSubsystem<UTribeSubsystem>() : nullptr;
	UTribeComponent* TC = Tribes ? Tribes->GetTribeComponent(Tribe) : nullptr;

	TArray<AShamanUnitBase*> Braves;
	bool bShaman = false;
	for (AShamanUnitBase* U : Units)
	{
		if (!FShamanSelectionRules::IsSelectable(U, Tribe)) continue;
		if (U->IsShaman()) { bShaman |= (U == GetPawn()); continue; }
		Braves.AddUnique(U);
	}
	// Closest Braves take the inner slots (shorter, less crossing paths).
	Braves.Sort([&Ground](const AShamanUnitBase& A, const AShamanUnitBase& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), Ground) < FVector::DistSquared(B.GetActorLocation(), Ground);
	});
	TArray<FVector2D> Offsets;
	FShamanCommandRules::FormationOffsets(Braves.Num() + (bShaman ? 1 : 0), SelectionSettings.FormationSpacing, Offsets);

	int32 Slot = 0, Ordered = 0, Refused = 0;
	if (bShaman) { StartShamanMove(Ground); ++Slot; ++Ordered; } // the Shaman takes the centre

	const FGameplayTag MoveTag = FGameplayTag::RequestGameplayTag(TEXT("Task.MoveTo"), /*ErrorIfNotFound*/ false);
	for (AShamanUnitBase* B : Braves)
	{
		FVector Dest = FShamanSpace::OffsetAlongGround(this, Ground, Offsets[Slot++]);
		FVector OnGround;
		if (FShamanSpace::ProjectToGround(this, Dest, OnGround)) Dest = OnGround;
		UTribeMemberComponent* M = B->TribeMember;
		bool bTask = false;
		if (TC && M && MoveTag.IsValid())
		{
			// A player command replaces whatever the unit was doing (its old task ends as Cancelled / PlayerCommand).
			if (M->HasTask()) TC->CancelTask(M->GetCurrentTaskId(), TEXT("PlayerCommand"));
			const int32 Id = TC->CreateTask(MoveTag, ETribeTaskPriority::High, nullptr, Dest, true, TEXT("PlayerCommand"));
			bTask = Id != INDEX_NONE && TC->AssignTask(Id, M) && TC->StartTask(Id);
			FTribeTask T;
			if (!bTask && Id != INDEX_NONE && TC->GetTask(Id, T) && T.IsOpen()) TC->CancelTask(Id, TEXT("PlayerCommandRefused"));
		}
		if (!bTask)
		{
			++Refused;
			UE_LOG(LogShaman, Log, TEXT("[Command] %s could not take a move task (unavailable); it will still walk there and hold"), *B->GetName());
		}
		// After arriving (or if the task could not run / the spot is unreachable) the Brave holds this spot instead of
		// returning to its default order. It defends itself there (Phase 1 leash around the hold point).
		B->SetOrder(EFollowerOrder::HoldPosition, Dest);
		++Ordered;
	}

	LastCommandLocation = Ground;
	LastCommandTime = GetWorld()->GetTimeSeconds();
	bLastCommandGuard = false;
	UE_LOG(LogShaman, Log, TEXT("[Command] move: %d Brave(s)%s to (%.0f, %.0f, %.0f)%s"), Braves.Num(), bShaman ? TEXT(" + Shaman") : TEXT(""),
		Ground.X, Ground.Y, Ground.Z, Refused > 0 ? *FString::Printf(TEXT(", %d without a task"), Refused) : TEXT(""));
	return Ordered;
}

int32 AShamanPlayerController::CommandGuardShaman(const TArray<AShamanUnitBase*>& Units)
{
	RefreshPlayerTribe();
	const int32 Tribe = GetPlayerTribe();
	UTribeSubsystem* Tribes = GetWorld() ? GetWorld()->GetSubsystem<UTribeSubsystem>() : nullptr;
	AShamanUnitBase* S = Tribes ? Tribes->GetShaman(Tribe) : nullptr;
	if (!S || !S->IsAlive())
	{
		UE_LOG(LogShaman, Log, TEXT("[Command] guard: the Shaman is not alive"));
		return 0;
	}
	UTribeComponent* TC = Tribes->GetTribeComponent(Tribe);
	int32 N = 0;
	for (AShamanUnitBase* U : Units)
	{
		if (!FShamanSelectionRules::IsSelectable(U, Tribe) || U->IsShaman()) continue;
		if (TC && U->TribeMember && U->TribeMember->HasTask()) TC->CancelTask(U->TribeMember->GetCurrentTaskId(), TEXT("PlayerCommand"));
		U->SetOrder(EFollowerOrder::FollowShaman, S->GetActorLocation()); // follow within FollowDistance, fight within the leash
		++N;
	}
	LastCommandLocation = S->GetActorLocation();
	LastCommandTime = GetWorld()->GetTimeSeconds();
	bLastCommandGuard = true;
	UE_LOG(LogShaman, Log, TEXT("[Command] guard Shaman: %d Brave(s)"), N);
	return N;
}

bool AShamanPlayerController::GetLastCommand(FVector& OutLocation, float& OutAge, bool& bOutGuard) const
{
	if (LastCommandTime < 0.0 || !GetWorld()) return false;
	OutLocation = LastCommandLocation;
	OutAge = (float)(GetWorld()->GetTimeSeconds() - LastCommandTime);
	bOutGuard = bLastCommandGuard;
	return true;
}

void AShamanPlayerController::StartShamanMove(const FVector& Goal)
{
	const APawn* P = GetPawn();
	if (!P) return;
	ShamanPath.Reset();
	ShamanPathIndex = 0;
	const FVector Start = P->GetActorLocation();
	const UShamanTerrainSubsystem* Terrain = UShamanTerrainSubsystem::Get(this);
	if (Terrain && Terrain->IsPlanetActive())
	{
		const FShamanSurfacePath Path = FShamanSurfaceNavigation::Get().FindPath(*Terrain, Start, Goal);
		if (Path.bValid) ShamanPath = Path.Points;
	}
	else if (const UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(this, Start, Goal, const_cast<APawn*>(P)))
	{
		if (NavPath->IsValid())
			for (int32 i = 1; i < NavPath->PathPoints.Num(); ++i) ShamanPath.Add(NavPath->PathPoints[i]);
	}
	const bool bStraight = ShamanPath.Num() == 0;
	if (bStraight) ShamanPath.Add(Goal); // no navigation data: walk straight; the stuck check ends it if blocked
	ShamanBestDistance = FShamanSpace::HorizontalDistance(this, Start, ShamanPath.Last());
	ShamanLastProgressTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogShaman, Log, TEXT("[Command] Shaman walking to (%.0f, %.0f, %.0f): %d waypoint(s)%s"), Goal.X, Goal.Y, Goal.Z, ShamanPath.Num(),
		bStraight ? TEXT(" (no path found: straight line)") : TEXT(""));
}

void AShamanPlayerController::StopShamanMove(const TCHAR* Why)
{
	if (ShamanPath.Num() == 0) return;
	ShamanPath.Reset();
	ShamanPathIndex = 0;
	UE_LOG(LogShaman, Log, TEXT("[Command] Shaman move ended: %s"), Why);
}

void AShamanPlayerController::UpdateShamanMove()
{
	if (ShamanPath.Num() == 0) return;
	AShamanUnitBase* S = Cast<AShamanUnitBase>(GetPawn());
	if (!S || !S->IsAlive()) { StopShamanMove(TEXT("Shaman down")); return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (S->IsRagdolling()) { ShamanLastProgressTime = Now; return; } // knocked down: wait, do not count as stuck
	const FVector Here = S->GetActorLocation();
	const bool bLast = ShamanPathIndex >= ShamanPath.Num() - 1;
	if (FShamanSpace::HorizontalDistance(this, Here, ShamanPath[ShamanPathIndex]) <= (bLast ? 90.f : 60.f))
	{
		if (bLast) { StopShamanMove(TEXT("arrived")); return; }
		++ShamanPathIndex;
	}
	S->AddMovementInput(FShamanSpace::HorizontalDirection(this, Here, ShamanPath[ShamanPathIndex]), 1.f);
	const float ToGoal = FShamanSpace::HorizontalDistance(this, Here, ShamanPath.Last());
	if (ToGoal < ShamanBestDistance - 50.f) { ShamanBestDistance = ToGoal; ShamanLastProgressTime = Now; }
	else if (Now - ShamanLastProgressTime > 4.0) StopShamanMove(TEXT("stuck (no progress for 4 s)"));
}
