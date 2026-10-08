#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Terrain/PlanetFrame.h"
#include "ShamanCharacterMovementComponent.generated.h"

class UShamanTerrainSubsystem;

/** Custom movement sub-modes used on planets (MOVE_Custom + this). */
UENUM(BlueprintType)
enum class EShamanCustomMove : uint8
{
	None = 0,
	PlanetWalk = 1,
	PlanetFall = 2
};

/**
 * Character movement for every SHAMAN unit.
 *
 * Flat worlds (no planet in UShamanTerrainSubsystem): every override defers to UCharacterMovementComponent, so
 * behaviour is identical to before.
 * Planets: radial gravity. Walking/Falling requests are redirected to MOVE_Custom PlanetWalk/PlanetFall, which
 * move in the local tangent plane, sweep for the floor along -Up, keep the capsule aligned to the radial up,
 * and use the analytic terrain as a safety net when collision is missing (not streamed yet / being rebuilt).
 * Engine code elsewhere that assumes -Z gravity (navmesh, RVO avoidance, ragdoll physics) is not used on planets.
 */
UCLASS()
class SHAMAN_API UShamanCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	UShamanCharacterMovementComponent();

	/** Acceleration toward the planet centre (uu/s^2) while falling on a planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float PlanetGravity = 980.f;
	/** Extra distance below MaxStepHeight in which walking units stick to the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float GroundSnapDistance = 40.f;
	/** How far below the analytic ground a unit may be before it is put back on top (missing/late collision). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float FallThroughTolerance = 80.f;

	UFUNCTION(BlueprintPure, Category="Planet") bool IsOnPlanet() const { return bPlanetActive; }
	const FPlanetFrame& GetPlanetFrame() const { return Frame; }
	/** Radial up at the character (world +Z on flat worlds). */
	FVector GetCharacterUp() const;

	/** Times the analytic safety net had to put this unit back on the ground (acceptance/perf diagnostics). */
	int32 GetGroundRescueCount() const { return GroundRescues; }

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode = 0) override;
	virtual bool IsMovingOnGround() const override;
	virtual bool IsFalling() const override;
	virtual bool DoJump(bool bReplayingMoves) override;
	virtual void PhysicsRotation(float DeltaTime) override;
	virtual bool IsWalkable(const FHitResult& Hit) const override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxBrakingDeceleration() const override;

protected:
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual FVector ConstrainInputAcceleration(const FVector& InputAcceleration) const override;
	virtual FVector ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const override;
	virtual void TwoWallAdjust(FVector& Delta, const FHitResult& Hit, const FVector& OldHitNormal) const override;

private:
	void RefreshPlanet();
	bool IsPlanetMode(EShamanCustomMove Mode) const { return MovementMode == MOVE_Custom && CustomMovementMode == (uint8)Mode; }
	void PhysPlanetWalk(float DeltaTime, int32 Iterations);
	void PhysPlanetFall(float DeltaTime, int32 Iterations);
	/** Capsule sweep along -Up. True if a blocking floor was found within Distance. */
	bool SweepFloor(const FVector& Location, const FQuat& Rotation, const FVector& Up, float Distance, FHitResult& OutHit) const;
	/** Analytic ground radius under Location (from the terrain subsystem), or -1 when unknown. */
	float GetAnalyticGroundRadius(const FVector& Location) const;
	/** Keeps the capsule above the analytic ground. Returns true if it had to move the unit. */
	bool RescueFromBelowGround();
	FQuat GetUprightQuat(const FVector& Up) const;
	void SlideTangent(const FVector& Delta, float RemainingTime, const FHitResult& Hit, const FVector& Up);

	bool bPlanetActive = false;
	FPlanetFrame Frame;
	TWeakObjectPtr<UShamanTerrainSubsystem> Terrain;
	int32 GroundRescues = 0;
	/** Walkable angle in use before the planet took over (restored when the planet goes away). */
	float FlatWalkableFloorAngle = -1.f;
};
