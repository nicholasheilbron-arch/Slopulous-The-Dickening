#include "Player/ShamanRTSCamera.h"
#include "Terrain/ShamanSpace.h"
#include "Terrain/PlanetFrame.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"

AShamanRTSCamera::AShamanRTSCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics; // after units moved, before the camera manager reads the view
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);
	Camera->FieldOfView = 60.f;
	SetCanBeDamaged(false);
}

void AShamanRTSCamera::InitializeView(const FVector& Anchor, const FVector& ForwardHint)
{
	Rig.Focus = Anchor;
	Rig.Forward = ForwardHint.IsNearlyZero() ? FVector::ForwardVector : ForwardHint;
	Rig.PitchDeg = Settings.InitialPitch;
	Rig.Distance = FMath::Clamp(Settings.InitialDistance, Settings.MinDistance, Settings.MaxDistance);
	SnapFocusToGround();
	UpdateView();
}

void AShamanRTSCamera::SnapFocusToGround()
{
	FPlanetFrame Planet;
	const bool bPlanet = FShamanSpace::GetPlanet(this, Planet);
	FVector Ground;
	if (FShamanSpace::ProjectToGround(this, Rig.Focus, Ground)) Rig.Focus = Ground;
	Rig.Normalize(bPlanet ? &Planet : nullptr);
}

void AShamanRTSCamera::Pan(float ForwardAxis, float RightAxis, float DeltaSeconds)
{
	if (ForwardAxis == 0.f && RightAxis == 0.f) return;
	FPlanetFrame Planet;
	const bool bPlanet = FShamanSpace::GetPlanet(this, Planet);
	const float Step = Settings.PanSpeed * Rig.Distance * DeltaSeconds;
	Rig.Pan(bPlanet ? &Planet : nullptr, FMath::Clamp(ForwardAxis, -1.f, 1.f) * Step, FMath::Clamp(RightAxis, -1.f, 1.f) * Step);
	SnapFocusToGround();
}

void AShamanRTSCamera::Orbit(float YawDeg, float PitchDeltaDeg)
{
	FPlanetFrame Planet;
	const bool bPlanet = FShamanSpace::GetPlanet(this, Planet);
	Rig.Orbit(bPlanet ? &Planet : nullptr, YawDeg, PitchDeltaDeg, Settings);
}

void AShamanRTSCamera::Zoom(float Steps)
{
	Rig.Zoom(Steps, Settings);
}

void AShamanRTSCamera::FocusOn(const FVector& Location)
{
	Rig.Focus = Location;
	SnapFocusToGround();
	UpdateView();
}

void AShamanRTSCamera::UpdateView()
{
	FPlanetFrame Planet;
	const bool bPlanet = FShamanSpace::GetPlanet(this, Planet);
	FVector Loc; FRotator Rot;
	Rig.GetView(bPlanet ? &Planet : nullptr, Loc, Rot);

	// Keep the camera above the ground under it (one terrain query per frame).
	FVector Ground;
	if (FShamanSpace::ProjectToGround(this, Loc, Ground))
	{
		const FVector Up = FShamanRTSCameraRig::UpAt(bPlanet ? &Planet : nullptr, Ground);
		const float Height = FVector::DotProduct(Loc - Ground, Up);
		if (Height < Settings.MinHeightAboveGround)
		{
			Loc += Up * (Settings.MinHeightAboveGround - Height);
			Rot = FRotationMatrix::MakeFromXZ(Rig.Focus - Loc, FShamanRTSCameraRig::UpAt(bPlanet ? &Planet : nullptr, Rig.Focus)).Rotator();
		}
	}
	SetActorLocationAndRotation(Loc, Rot);
}

void AShamanRTSCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateView();
}
