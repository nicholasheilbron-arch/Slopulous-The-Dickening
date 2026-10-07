#include "Characters/ShamanCharacterMovementComponent.h"
#include "Terrain/ShamanTerrainSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/PhysicsVolume.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

UShamanCharacterMovementComponent::UShamanCharacterMovementComponent()
{
}

FVector UShamanCharacterMovementComponent::GetCharacterUp() const
{
	return bPlanetActive && UpdatedComponent ? Frame.GetUp(UpdatedComponent->GetComponentLocation()) : FVector::UpVector;
}

void UShamanCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshPlanet();
}

void UShamanCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	RefreshPlanet();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UShamanCharacterMovementComponent::RefreshPlanet()
{
	UShamanTerrainSubsystem* T = UShamanTerrainSubsystem::Get(this);
	const bool bNow = T && T->IsPlanetActive();
	if (bNow) { Frame = T->GetPlanetFrame(); Terrain = T; }
	if (bNow == bPlanetActive) return;
	bPlanetActive = bNow;
	if (bPlanetActive)
	{
		// RVO avoidance is 2D (world XY) and the navmesh is Z-up: neither is valid on a sphere.
		if (bUseRVOAvoidance) SetAvoidanceEnabled(false);
		if (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking) SetMovementMode(MOVE_Walking);
		else if (MovementMode == MOVE_Falling) SetMovementMode(MOVE_Falling);
	}
	else
	{
		Terrain.Reset();
		if (IsPlanetMode(EShamanCustomMove::PlanetWalk)) SetMovementMode(MOVE_Walking);
		else if (IsPlanetMode(EShamanCustomMove::PlanetFall)) SetMovementMode(MOVE_Falling);
	}
}

// ---- Mode mapping -----------------------------------------------------------------------------------------------

void UShamanCharacterMovementComponent::SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode)
{
	if (bPlanetActive)
	{
		if (NewMovementMode == MOVE_Walking || NewMovementMode == MOVE_NavWalking)
		{
			NewMovementMode = MOVE_Custom; NewCustomMode = (uint8)EShamanCustomMove::PlanetWalk;
		}
		else if (NewMovementMode == MOVE_Falling)
		{
			NewMovementMode = MOVE_Custom; NewCustomMode = (uint8)EShamanCustomMove::PlanetFall;
		}
	}
	Super::SetMovementMode(NewMovementMode, NewCustomMode);
}

bool UShamanCharacterMovementComponent::IsMovingOnGround() const
{
	return IsPlanetMode(EShamanCustomMove::PlanetWalk) || Super::IsMovingOnGround();
}

bool UShamanCharacterMovementComponent::IsFalling() const
{
	return IsPlanetMode(EShamanCustomMove::PlanetFall) || Super::IsFalling();
}

float UShamanCharacterMovementComponent::GetMaxSpeed() const
{
	if (IsPlanetMode(EShamanCustomMove::PlanetWalk) || IsPlanetMode(EShamanCustomMove::PlanetFall))
		return IsCrouching() ? MaxWalkSpeedCrouched : MaxWalkSpeed;
	return Super::GetMaxSpeed();
}

float UShamanCharacterMovementComponent::GetMaxBrakingDeceleration() const
{
	if (IsPlanetMode(EShamanCustomMove::PlanetWalk)) return BrakingDecelerationWalking;
	if (IsPlanetMode(EShamanCustomMove::PlanetFall)) return BrakingDecelerationFalling;
	return Super::GetMaxBrakingDeceleration();
}

bool UShamanCharacterMovementComponent::DoJump(bool bReplayingMoves)
{
	if (!bPlanetActive) return Super::DoJump(bReplayingMoves);
	if (!CharacterOwner || !CharacterOwner->CanJump()) return false;
	const FVector Up = GetCharacterUp();
	Velocity = FVector::VectorPlaneProject(Velocity, Up) + Up * JumpZVelocity;
	SetMovementMode(MOVE_Falling);
	return true;
}

FVector UShamanCharacterMovementComponent::ConstrainInputAcceleration(const FVector& InputAcceleration) const
{
	if (!bPlanetActive) return Super::ConstrainInputAcceleration(InputAcceleration);
	// Walking/falling units ignore "vertical" input; vertical is radial here.
	return FVector::VectorPlaneProject(InputAcceleration, GetCharacterUp());
}

bool UShamanCharacterMovementComponent::IsWalkable(const FHitResult& Hit) const
{
	if (!bPlanetActive) return Super::IsWalkable(Hit);
	if (!Hit.IsValidBlockingHit()) return false;
	return FVector::DotProduct(Hit.ImpactNormal, Frame.GetUp(Hit.ImpactPoint)) >= GetWalkableFloorZ() - KINDA_SMALL_NUMBER;
}

FVector UShamanCharacterMovementComponent::ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const
{
	// The base CMC version limits world-Z "slope boosting"; on planets use the plain plane projection.
	return bPlanetActive ? UMovementComponent::ComputeSlideVector(Delta, Time, Normal, Hit) : Super::ComputeSlideVector(Delta, Time, Normal, Hit);
}

void UShamanCharacterMovementComponent::TwoWallAdjust(FVector& Delta, const FHitResult& Hit, const FVector& OldHitNormal) const
{
	if (bPlanetActive) UMovementComponent::TwoWallAdjust(Delta, Hit, OldHitNormal);
	else Super::TwoWallAdjust(Delta, Hit, OldHitNormal);
}

// ---- Rotation -----------------------------------------------------------------------------------------------------

FQuat UShamanCharacterMovementComponent::GetUprightQuat(const FVector& Up) const
{
	const FQuat Cur = UpdatedComponent->GetComponentQuat();
	FVector Fwd = FVector::VectorPlaneProject(Cur.GetForwardVector(), Up);
	if (Fwd.SizeSquared() < 1e-4f) Fwd = FVector::VectorPlaneProject(-Cur.GetUpVector(), Up); // was looking straight up/down
	if (Fwd.SizeSquared() < 1e-4f) return Cur;
	return FRotationMatrix::MakeFromXZ(Fwd.GetSafeNormal(), Up).ToQuat();
}

void UShamanCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!bPlanetActive) { Super::PhysicsRotation(DeltaTime); return; }
	if (!HasValidData() || (!CharacterOwner->Controller && !bRunPhysicsWithNoController)) return;
	if (MovementMode == MOVE_None) return;

	const FVector Up = GetCharacterUp();
	const FQuat Cur = UpdatedComponent->GetComponentQuat();
	const FQuat Upright = GetUprightQuat(Up);
	const FVector CurFwd = Upright.GetForwardVector();

	FVector DesiredFwd = CurFwd;
	if (bOrientRotationToMovement)
	{
		const FVector A = FVector::VectorPlaneProject(Acceleration, Up);
		if (A.SizeSquared() > KINDA_SMALL_NUMBER) DesiredFwd = A.GetSafeNormal();
	}
	else if (bUseControllerDesiredRotation && CharacterOwner->Controller)
	{
		const FVector C = FVector::VectorPlaneProject(CharacterOwner->Controller->GetDesiredRotation().Vector(), Up);
		if (C.SizeSquared() > KINDA_SMALL_NUMBER) DesiredFwd = C.GetSafeNormal();
	}

	// Turn around Up by at most RotationRate.Yaw * dt (negative rate = instant, like the base CMC).
	const float MaxDeg = GetDeltaRotation(DeltaTime).Yaw;
	const float Cos = FMath::Clamp(FVector::DotProduct(CurFwd, DesiredFwd), -1.f, 1.f);
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Cos));
	FVector NewFwd = DesiredFwd;
	if (AngleDeg > MaxDeg && MaxDeg >= 0.f)
	{
		const float Sign = FVector::DotProduct(FVector::CrossProduct(CurFwd, DesiredFwd), Up) >= 0.f ? 1.f : -1.f;
		NewFwd = CurFwd.RotateAngleAxis(Sign * MaxDeg, Up);
	}
	const FQuat NewQuat = FRotationMatrix::MakeFromXZ(NewFwd, Up).ToQuat();
	if (!NewQuat.Equals(Cur, 1e-5f))
		MoveUpdatedComponent(FVector::ZeroVector, NewQuat, /*bSweep*/ false);
}

// ---- Planet physics -------------------------------------------------------------------------------------------------

void UShamanCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (!bPlanetActive)
	{
		// Planet went away while in a planet mode: fall back to standard physics.
		if (IsPlanetMode(EShamanCustomMove::PlanetWalk)) SetMovementMode(MOVE_Walking);
		else if (IsPlanetMode(EShamanCustomMove::PlanetFall)) SetMovementMode(MOVE_Falling);
		else Super::PhysCustom(DeltaTime, Iterations);
		return;
	}
	if (IsPlanetMode(EShamanCustomMove::PlanetWalk)) PhysPlanetWalk(DeltaTime, Iterations);
	else if (IsPlanetMode(EShamanCustomMove::PlanetFall)) PhysPlanetFall(DeltaTime, Iterations);
	else Super::PhysCustom(DeltaTime, Iterations);
}

bool UShamanCharacterMovementComponent::SweepFloor(const FVector& Location, const FQuat& Rotation, const FVector& Up, float Distance, FHitResult& OutHit) const
{
	const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	if (!Capsule) return false;
	// Slightly thinner capsule (same bottom point) so walls next to the unit are not reported as floor.
	const float Radius = Capsule->GetScaledCapsuleRadius() * 0.9f;
	const float HalfHeight = FMath::Max(Capsule->GetScaledCapsuleHalfHeight(), Radius);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShamanPlanetFloor), false, CharacterOwner);
	FCollisionResponseParams Response;
	UpdatedPrimitive->InitSweepCollisionParams(Params, Response);
	// Start the sweep MaxStepHeight above the capsule so a unit slightly embedded in the ground (mesh above the
	// analytic surface, terrain raised under it) still finds the floor instead of reporting a start-penetration.
	const float Lift = FMath::Max(MaxStepHeight, 10.f);
	const bool bHit = GetWorld()->SweepSingleByChannel(OutHit, Location + Up * Lift, Location - Up * Distance, Rotation,
		UpdatedComponent->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius, HalfHeight), Params, Response);
	if (!bHit || OutHit.bStartPenetrating) return false;
	OutHit.Distance -= Lift; // gap below the capsule's current position (negative = embedded)
	return true;
}

float UShamanCharacterMovementComponent::GetAnalyticGroundRadius(const FVector& Location) const
{
	const UShamanTerrainSubsystem* T = Terrain.Get();
	if (!T || !T->IsPlanetActive()) return -1.f;
	return Frame.Radius + T->GetTerrainHeight(Location); // height-only query (cheap; called every movement step)
}

bool UShamanCharacterMovementComponent::RescueFromBelowGround()
{
	const FVector Loc = UpdatedComponent->GetComponentLocation();
	const float GroundR = GetAnalyticGroundRadius(Loc);
	if (GroundR < 0.f) return false;
	const float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float BottomR = Frame.GetDistanceFromCenter(Loc) - HalfHeight;
	if (BottomR >= GroundR - FallThroughTolerance) return false;
	// Below the terrain (collision not built yet, or the ground was raised under us): put the unit back on top.
	const FVector Up = Frame.GetUp(Loc);
	UpdatedComponent->SetWorldLocationAndRotation(Frame.Center + Up * (GroundR + HalfHeight + 5.f), GetUprightQuat(Up), false, nullptr, ETeleportType::TeleportPhysics);
	Velocity = FVector::VectorPlaneProject(Velocity, Up);
	++GroundRescues;
	return true;
}

void UShamanCharacterMovementComponent::SlideTangent(const FVector& Delta, float RemainingTime, const FHitResult& Hit, const FVector& Up)
{
	FVector Normal = Hit.Normal;
	const float NUp = FVector::DotProduct(Normal, Up);
	// Do not climb unwalkable slopes: treat them as vertical walls (like the base CMC, but with radial up).
	if (NUp > 0.f && NUp < GetWalkableFloorZ())
	{
		const FVector Flat = FVector::VectorPlaneProject(Normal, Up);
		if (Flat.SizeSquared() > KINDA_SMALL_NUMBER) Normal = Flat.GetSafeNormal();
	}
	FHitResult SlideHit = Hit;
	UMovementComponent::SlideAlongSurface(Delta, RemainingTime, Normal, SlideHit, false); // impact already handled by the caller
}

void UShamanCharacterMovementComponent::PhysPlanetWalk(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME || !CharacterOwner || !UpdatedComponent) return;

	float RemainingTime = DeltaTime;
	while (RemainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations && CharacterOwner)
	{
		++Iterations;
		const float Step = GetSimulationTimeStep(RemainingTime, Iterations);
		RemainingTime -= Step;

		const FVector Start = UpdatedComponent->GetComponentLocation();
		const FVector Up = Frame.GetUp(Start);
		Acceleration = FVector::VectorPlaneProject(Acceleration, Up);
		Velocity = FVector::VectorPlaneProject(Velocity, Up);
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
			CalcVelocity(Step, GroundFriction, false, GetMaxBrakingDeceleration());
		Velocity = FVector::VectorPlaneProject(Velocity, Up);

		const FVector Delta = Velocity * Step;
		if (!Delta.IsNearlyZero())
		{
			FHitResult Hit(1.f);
			SafeMoveUpdatedComponent(Delta, GetUprightQuat(Up), true, Hit);
			if (Hit.IsValidBlockingHit())
			{
				HandleImpact(Hit, Step, Delta);
				SlideTangent(Delta, 1.f - Hit.Time, Hit, Up);
				// Like PhysWalking: velocity follows what the move actually achieved (no speed kept into walls).
				if (Step > KINDA_SMALL_NUMBER)
					Velocity = FVector::VectorPlaneProject((UpdatedComponent->GetComponentLocation() - Start) / Step, Up);
			}
		}

		// Stick to the ground under the new position (follows curvature and slopes).
		const FVector Loc = UpdatedComponent->GetComponentLocation();
		const FVector NewUp = Frame.GetUp(Loc);
		const FQuat Rot = GetUprightQuat(NewUp);
		const float ProbeDist = MaxStepHeight + GroundSnapDistance;
		FHitResult Floor;
		if (SweepFloor(Loc, Rot, NewUp, ProbeDist, Floor))
		{
			if (!IsWalkable(Floor))
			{
				SetMovementMode(MOVE_Falling); // too steep: slide off under gravity
				StartNewPhysics(RemainingTime, Iterations);
				return;
			}
			const float Gap = Floor.Distance; // travel until contact (negative = embedded; moves up)
			if (FMath::Abs(Gap - 2.f) > 0.5f)
			{
				FHitResult Adjust;
				SafeMoveUpdatedComponent(-NewUp * (Gap - 2.f), Rot, true, Adjust);
			}
			else if (!UpdatedComponent->GetComponentQuat().Equals(Rot, 1e-4f))
			{
				MoveUpdatedComponent(FVector::ZeroVector, Rot, false);
			}
		}
		else
		{
			// No collision floor in range. Collision may not be built here yet: use the analytic ground.
			const float GroundR = GetAnalyticGroundRadius(Loc);
			const float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float BottomR = Frame.GetDistanceFromCenter(Loc) - HalfHeight;
			if (GroundR >= 0.f && BottomR - GroundR <= ProbeDist)
			{
				UpdatedComponent->SetWorldLocationAndRotation(Frame.Center + NewUp * (GroundR + HalfHeight + 2.f), Rot);
			}
			else
			{
				SetMovementMode(MOVE_Falling);
				StartNewPhysics(RemainingTime, Iterations);
				return;
			}
		}
		RescueFromBelowGround();
	}
}

void UShamanCharacterMovementComponent::PhysPlanetFall(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME || !CharacterOwner || !UpdatedComponent) return;

	float RemainingTime = DeltaTime;
	while (RemainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations && CharacterOwner)
	{
		++Iterations;
		const float Step = GetSimulationTimeStep(RemainingTime, Iterations);
		RemainingTime -= Step;

		const FVector Up = Frame.GetUp(UpdatedComponent->GetComponentLocation());
		// Radial gravity + limited air control in the tangent plane.
		const FVector AirAccel = FVector::VectorPlaneProject(Acceleration, Up) * AirControl;
		Velocity += (AirAccel - Up * PlanetGravity) * Step;
		const float Terminal = GetPhysicsVolume() ? GetPhysicsVolume()->TerminalVelocity : 4000.f;
		Velocity = Velocity.GetClampedToMaxSize(Terminal);

		const FVector Delta = Velocity * Step;
		FHitResult Hit(1.f);
		SafeMoveUpdatedComponent(Delta, GetUprightQuat(Up), true, Hit);

		if (Hit.IsValidBlockingHit())
		{
			const FVector HitUp = Frame.GetUp(Hit.Location);
			if (IsWalkable(Hit) && FVector::DotProduct(Velocity, HitUp) <= 0.f)
			{
				Velocity = FVector::VectorPlaneProject(Velocity, HitUp);
				CharacterOwner->Landed(Hit); // while still falling, like the base ProcessLanded
				SetMovementMode(MOVE_Walking);
				StartNewPhysics(RemainingTime, Iterations);
				return;
			}
			HandleImpact(Hit, Step, Delta);
			SlideTangent(Delta, 1.f - Hit.Time, Hit, Up);
			// Drop the velocity component into the surface (no build-up while sliding on steep ground).
			Velocity = FVector::VectorPlaneProject(Velocity, Hit.Normal);
		}

		// Landed on analytic ground where collision is missing / rescue from below.
		const FVector Loc = UpdatedComponent->GetComponentLocation();
		const float GroundR = GetAnalyticGroundRadius(Loc);
		const float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector NewUp = Frame.GetUp(Loc);
		if (GroundR >= 0.f && FVector::DotProduct(Velocity, NewUp) <= 0.f
			&& Frame.GetDistanceFromCenter(Loc) - HalfHeight <= GroundR + 2.f)
		{
			FHitResult Fake;
			Fake.bBlockingHit = true;
			Fake.Location = Fake.ImpactPoint = Frame.Center + NewUp * GroundR;
			Fake.Normal = Fake.ImpactNormal = NewUp;
			RescueFromBelowGround();
			Velocity = FVector::VectorPlaneProject(Velocity, NewUp);
			CharacterOwner->Landed(Fake);
			SetMovementMode(MOVE_Walking);
			StartNewPhysics(RemainingTime, Iterations);
			return;
		}
	}
}
