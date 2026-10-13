#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/ShamanControlTypes.h"
#include "ShamanRTSCamera.generated.h"

class UCameraComponent;

/**
 * Strategic (RTS) camera: an actor the player controller uses as its view target in RTS mode. It is not a pawn, so
 * moving it never moves the Shaman. State lives in FShamanRTSCameraRig (pure math); this actor applies it each frame,
 * keeps the focus on the ground (FShamanSpace::ProjectToGround) and lifts the camera clear of hills.
 */
UCLASS()
class SHAMAN_API AShamanRTSCamera : public AActor
{
	GENERATED_BODY()
public:
	AShamanRTSCamera();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) UCameraComponent* Camera;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera") FShamanRTSCameraSettings Settings;

	/** Puts the focus on the ground at Anchor, heading along ForwardHint, at the initial pitch and distance. */
	void InitializeView(const FVector& Anchor, const FVector& ForwardHint);
	/** Axis values in [-1, 1]; scaled by PanSpeed * Distance * DeltaSeconds. */
	void Pan(float ForwardAxis, float RightAxis, float DeltaSeconds);
	void Orbit(float YawDeg, float PitchDeltaDeg);
	void Zoom(float Steps);
	/** Moves the focus (keeps heading, pitch and zoom). */
	void FocusOn(const FVector& Location);
	/** Recomputes and applies the camera transform now (also done every tick). */
	void UpdateView();

	const FShamanRTSCameraRig& GetRig() const { return Rig; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	void SnapFocusToGround();
	FShamanRTSCameraRig Rig;
};
