#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Player/ShamanControlTypes.h"
#include "ShamanPlayerController.generated.h"

class AShamanRTSCamera;
class AShamanUnitBase;
class UShamanSelectionComponent;
class UInputComponent;

/**
 * Hybrid controls (RTS milestone, Stage A). The controller always possesses the player Shaman (so reincarnation,
 * spells, HUD and every system that asks "who is the player's pawn" are unchanged); the control mode only decides
 * the view target and which input layer is active:
 *   - RTS (default at start): view through AShamanRTSCamera; an RTS input layer (pushed, bBlockInput) takes all keys,
 *     so the Shaman's own bindings (WASD, mouse look, cast, melee, interact) never fire. Mouse cursor shown.
 *   - Shaman: the RTS layer is popped; the Shaman's Phase 1 bindings work exactly as before.
 * Enter toggles (bound on the RTS layer and on the controller's own layer; only one is ever active, so one press
 * toggles once). Keys held during a switch are flushed so nothing carries over.
 *
 * RTS keys: LMB click select / drag box (Shift adds), RMB command (ground: move there and hold; your Shaman: selected
 * Braves follow and guard him), WASD or arrows pan, Q/E rotate, middle-mouse drag orbit,
 * wheel zoom, F focus selection (or Shaman), Home focus Shaman, Backspace or Esc clear selection (Esc stops Play-in-Editor
 * before the game sees it, so use Backspace there), F3 debug.
 */
UCLASS()
class SHAMAN_API AShamanPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AShamanPlayerController();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Controls") EShamanControlMode StartMode = EShamanControlMode::RTS;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Controls") FShamanRTSCameraSettings CameraSettings;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Controls") FShamanSelectionSettings SelectionSettings;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Controls") TSubclassOf<AShamanRTSCamera> RTSCameraClass;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Controls") UShamanSelectionComponent* Selection;

	UFUNCTION(BlueprintCallable, Category="Controls") EShamanControlMode GetControlMode() const { return Mode; }
	UFUNCTION(BlueprintCallable, Category="Controls") void SetControlMode(EShamanControlMode NewMode);
	UFUNCTION(BlueprintCallable, Category="Controls") void ToggleControlMode();
	/** Console: ShamanControlMode (toggle). */
	UFUNCTION(Exec) void ShamanControlMode();
	/** Console: ShamanSelectionDump - logs the player tribe, roster and screen candidates ([SelectDiag]). */
	UFUNCTION(Exec) void ShamanSelectionDump();

	AShamanRTSCamera* GetRTSCamera() const { return RTSCamera; }
	int32 GetPlayerTribe() const;
	/** Pushes the possessed Shaman's tribe into the selection once it is known (>= 0). */
	void RefreshPlayerTribe();

	// Camera actions (also used by tests).
	void PanCamera(float ForwardAxis, float RightAxis, float DeltaSeconds);
	void OrbitCamera(float YawDeg, float PitchDeltaDeg);
	void ZoomCamera(float Steps);
	void FocusCameraOnShaman();
	void FocusCameraOnSelection();

	// Selection gesture (screen pixels; also used by tests).
	void BeginSelectGesture(const FVector2D& ScreenPos);
	void UpdateSelectGesture(const FVector2D& ScreenPos);
	void EndSelectGesture(const FVector2D& ScreenPos, bool bAdditive);
	void CancelSelectGesture();
	bool IsDragSelecting() const { return bSelectPressed && bDragging; }
	void GetDragRect(FVector2D& OutA, FVector2D& OutB) const { OutA = DragStart; OutB = DragCurrent; }
	/** The player's selectable units projected to the screen (player tribe roster + Shaman; no world scan). */
	void GatherScreenCandidates(TArray<FShamanScreenCandidate>& Out) const;

	// RTS commands (also used by tests). Braves get a Task.MoveTo through their tribe's task book (Phase 2.2 executor)
	// and then hold that spot; the Shaman (player-possessed, no AI controller) walks a path steered here.
	/** Move the given units (the player's selectable units only) to Ground in formation. Returns how many got the order. */
	int32 CommandMove(const TArray<AShamanUnitBase*>& Units, const FVector& Ground);
	/** Selected Braves follow and guard the player's Shaman (the Phase 1 FollowShaman order). Returns how many. */
	int32 CommandGuardShaman(const TArray<AShamanUnitBase*>& Units);
	bool IsShamanMoving() const { return ShamanPath.Num() > 0; }
	void StopShamanMove(const TCHAR* Why);
	/** Last command marker (HUD feedback): false when none. */
	bool GetLastCommand(FVector& OutLocation, float& OutAge, bool& bOutGuard) const;

protected:
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** bFromPossess: re-applied right after (re)possession - instant, no key flush, camera kept where it is. */
	void ApplyControlMode(bool bFirstTime, bool bFromPossess = false);
	/** Returns true when a new camera was spawned (it then needs PlaceCameraAtStart). */
	bool EnsureRTSCamera();
	/** Planets: false for points on the far side of the globe from the camera (not visible, not pickable). */
	bool IsOnVisibleSide(const FVector& WorldLocation) const;
	void PlaceCameraAtStart();
	void UpdateRTSInput(float DeltaTime);

	// Input handlers
	void OnToggleModePressed();
	void OnSelectPressed();
	/** RTS: LMB over the spell panel selects a spell (never casts). Returns true when the panel took the click. */
	bool HandleSpellPanelClick(const FVector2D& ScreenPos);
	void OnSelectReleased();
	void OnCommandPressed();
	/** Ground point under a screen position (terrain / static world; planets fall back to the sphere). */
	bool GetGroundUnderScreen(const FVector2D& ScreenPos, FVector& OutGround) const;
	void StartShamanMove(const FVector& Goal);
	void UpdateShamanMove();
	void OnOrbitPressed();
	void OnOrbitReleased();
	void OnZoomIn();
	void OnZoomOut();
	void OnFocusSelection();
	void OnFocusShaman();
	void OnClearSelection();
	void OnToggleDebug();

	UPROPERTY(Transient) UInputComponent* RTSInput = nullptr;
	UPROPERTY(Transient) AShamanRTSCamera* RTSCamera = nullptr;

	EShamanControlMode Mode = EShamanControlMode::RTS;
	bool bModeInitialized = false;
	bool bRTSInputPushed = false;
	bool bSelectPressed = false;
	bool bDragging = false;
	bool bOrbiting = false;
	FVector2D DragStart = FVector2D::ZeroVector;
	FVector2D DragCurrent = FVector2D::ZeroVector;
	FVector2D LastMouse = FVector2D::ZeroVector;

	// Shaman move command (RTS): waypoints steered in PlayerTick.
	TArray<FVector> ShamanPath;
	int32 ShamanPathIndex = 0;
	float ShamanBestDistance = 0.f;
	double ShamanLastProgressTime = 0.0;
	// HUD marker
	FVector LastCommandLocation = FVector::ZeroVector;
	double LastCommandTime = -1.0;
	bool bLastCommandGuard = false;
};
