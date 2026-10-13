#pragma once
#include "CoreMinimal.h"
#include "ShamanControlTypes.generated.h"

class AShamanUnitBase;
struct FPlanetFrame;

/**
 * Hybrid RTS controls (RTS milestone, Stage A): shared types and the pure math behind the strategic camera and unit
 * selection. Nothing here touches the world, so camera and selection rules are unit-tested on their own.
 */

/** Who the player is driving: the tribe from the strategic camera, or the Shaman directly (Phase 1 controls). */
UENUM(BlueprintType)
enum class EShamanControlMode : uint8
{
	RTS,
	Shaman
};

/** Strategic camera tuning (AShamanPlayerController::CameraSettings). Distances in uu, angles in degrees. */
USTRUCT(BlueprintType)
struct SHAMAN_API FShamanRTSCameraSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="100")) float InitialDistance = 4200.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="100")) float MinDistance = 700.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="100")) float MaxDistance = 14000.f;
	/** Angle of the view below the local horizon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="5", ClampMax="89")) float InitialPitch = 55.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="5", ClampMax="89")) float MinPitch = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="5", ClampMax="89")) float MaxPitch = 85.f;
	/** Pan speed as a fraction of the current zoom distance per second (zoomed out pans faster). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="0")) float PanSpeed = 1.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="0")) float RotateSpeed = 90.f;
	/** Middle-mouse drag orbit, degrees per pixel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="0")) float MouseOrbitSpeed = 0.25f;
	/** Fraction of the distance removed per mouse-wheel step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="0.01", ClampMax="0.5")) float ZoomStep = 0.12f;
	/** The camera is lifted to stay at least this far above the ground under it (hills, cliffs). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="0")) float MinHeightAboveGround = 250.f;
	/** Pan when the mouse is at the screen edge. Off by default (annoying in a windowed editor viewport). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera") bool bEdgePan = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="1")) float EdgePanMarginPx = 8.f;
	/** Camera blend when switching modes (0 = cut). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(ClampMin="0")) float ModeBlendTime = 0.25f;
};

/** Selection tuning (AShamanPlayerController::SelectionSettings). */
USTRUCT(BlueprintType)
struct SHAMAN_API FShamanSelectionSettings
{
	GENERATED_BODY()
	/** A left press that moves further than this becomes a drag-box selection instead of a click. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Selection", meta=(ClampMin="1")) float DragThresholdPx = 6.f;
	/** Click tolerance around a unit's on-screen body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Selection", meta=(ClampMin="1")) float PickRadiusPx = 16.f;
	/** Selection ring radius as a multiple of the unit's capsule radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Selection", meta=(ClampMin="0.5")) float RingRadiusScale = 1.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Selection") FLinearColor RingColor = FLinearColor(1.f, 0.85f, 0.1f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Selection") FLinearColor BoxFillColor = FLinearColor(0.35f, 0.85f, 1.f, 0.18f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Selection") FLinearColor BoxEdgeColor = FLinearColor(0.55f, 0.95f, 1.f, 1.f);
	/** RTS commands: spacing of the move formation around the clicked point (uu, along the ground). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Commands", meta=(ClampMin="40")) float FormationSpacing = 140.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Commands") FLinearColor MoveMarkerColor = FLinearColor(0.3f, 1.f, 0.4f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Commands") FLinearColor GuardMarkerColor = FLinearColor(1.f, 0.85f, 0.1f, 1.f);
};

/**
 * The strategic camera as a pure value: a ground focus point, a heading tangent to the ground there, a pitch below the
 * local horizon and a distance. "Up" is the planet's radial up when a planet frame is given, world +Z otherwise.
 * The heading is parallel-transported as the focus moves, so the view never flips when crossing curved terrain.
 */
struct SHAMAN_API FShamanRTSCameraRig
{
	FVector Focus = FVector::ZeroVector;
	FVector Forward = FVector(1.f, 0.f, 0.f);
	float PitchDeg = 55.f;
	float Distance = 4200.f;

	static FVector UpAt(const FPlanetFrame* Planet, const FVector& Location);
	/** Re-projects Forward onto the tangent plane at Focus (keeps it unit and horizontal). */
	void Normalize(const FPlanetFrame* Planet);
	/** Moves the focus along the ground by DForward / DRight uu (planet: along the sphere at the same radius). */
	void Pan(const FPlanetFrame* Planet, float DForward, float DRight);
	/** Turns the heading about the local up and tilts the pitch (clamped). */
	void Orbit(const FPlanetFrame* Planet, float YawDeg, float PitchDeltaDeg, const FShamanRTSCameraSettings& Settings);
	/** Steps > 0 zooms in, < 0 zooms out; distance clamped to [MinDistance, MaxDistance]. */
	void Zoom(float Steps, const FShamanRTSCameraSettings& Settings);
	/** World camera location and rotation for the current state. */
	void GetView(const FPlanetFrame* Planet, FVector& OutLocation, FRotator& OutRotation) const;
	FVector GetRight(const FPlanetFrame* Planet) const;
};

/** A unit as seen on screen, for click and box picking. Feet/Head are viewport pixel positions. */
struct SHAMAN_API FShamanScreenCandidate
{
	TWeakObjectPtr<AShamanUnitBase> Unit;
	FVector2D Feet = FVector2D::ZeroVector;
	FVector2D Head = FVector2D::ZeroVector;
	bool bOnScreen = false;
};

/** Selection rules (pure). */
struct SHAMAN_API FShamanSelectionRules
{
	/** A living Brave/follower or the Shaman of the player's tribe. Never enemies, Wildmen, buildings or corpses. */
	static bool IsSelectable(const AShamanUnitBase* Unit, int32 PlayerTribe);
	static bool IsDrag(const FVector2D& Start, const FVector2D& Current, float ThresholdPx);
	/** Index of the candidate whose body (feet-head segment) is nearest the mouse within RadiusPx, or INDEX_NONE. */
	static int32 PickClosest(const TArray<FShamanScreenCandidate>& Candidates, const FVector2D& Mouse, float RadiusPx);
	/** Candidates whose body centre lies in the rectangle spanned by A and B (any corner order). */
	static void BoxSelect(const TArray<FShamanScreenCandidate>& Candidates, const FVector2D& A, const FVector2D& B, TArray<int32>& OutIndices);
	static float DistanceToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B);
};

/** RTS command rules (pure). */
struct SHAMAN_API FShamanCommandRules
{
	/** Ground-plane offsets (tangent space, uu) for N units around a move target: the first at the centre, then rings of
	 *  6, 12, 18... slots Spacing apart. Deterministic; every pair of slots is at least ~Spacing apart. */
	static void FormationOffsets(int32 NumUnits, float Spacing, TArray<FVector2D>& OutOffsets);
};
