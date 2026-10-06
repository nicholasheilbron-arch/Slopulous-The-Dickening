#include "SpellProjectile.h"
#include "RagdollReactionComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "Core/ShamanTargetRules.h"
#include "Core/ShamanDebug.h"
#include "DrawDebugHelpers.h"

ASpellProjectile::ASpellProjectile()
{
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(20.f);
	Collision->SetCollisionProfileName(TEXT("Projectile"));
	Collision->OnComponentHit.AddDynamic(this, &ASpellProjectile::OnHit);
	RootComponent = Collision;

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->ProjectileGravityScale = 0.f;
	InitialLifeSpan = 5.f;
}

void ASpellProjectile::Init(const FSpellRow& InRow, float RangeUnits)
{
	Row = InRow;
	if (AActor* Caster = GetOwner()) Collision->IgnoreActorWhenMoving(Caster, true); // never collide with the caster
	const float Speed = FMath::Max(1.f, Row.ProjectileSpeed);
	Movement->InitialSpeed = Movement->MaxSpeed = Speed;
	Movement->Velocity = GetActorForwardVector() * Speed;
	SetLifeSpan(RangeUnits / Speed);

	if (Row.bHoming) // acquire nearest pawn (not the caster) inside a forward cone
	{
		TArray<FOverlapResult> Hits;
		GetWorld()->OverlapMultiByObjectType(Hits, GetActorLocation(), FQuat::Identity,
			FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(HomingAcquireRadius));
		float Best = TNumericLimits<float>::Max();
		AActor* Target = nullptr;
		for (const FOverlapResult& R : Hits)
		{
			AActor* A = R.GetActor();
			if (!A || A == GetOwner()) continue;
			const FVector To = (A->GetActorLocation() - GetActorLocation());
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(To.GetSafeNormal(), GetActorForwardVector())));
			if (Angle > HomingConeDegrees) continue;
			if (To.SizeSquared() < Best) { Best = To.SizeSquared(); Target = A; }
		}
		if (Target && Target->GetRootComponent())
		{
			Movement->bIsHomingProjectile = true;
			Movement->HomingAccelerationMagnitude = Speed * 3.f;
			Movement->HomingTargetComponent = Target->GetRootComponent();
		}
	}
}

void ASpellProjectile::OnHit(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, FVector, const FHitResult& Hit)
{
	if (Other == GetOwner()) return;
	const FVector Loc = Hit.ImpactPoint;

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, Loc, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Row.Radius));

	TSet<AActor*> Done;
	for (const FOverlapResult& R : Overlaps)
	{
		AActor* A = R.GetActor();
		if (!A || A == GetOwner() || Done.Contains(A)) continue;
		Done.Add(A);
		// Shared target rules (friendly fire, Wildmen-only, dead units). Blast row: AllUnits + friendly fire = original behaviour.
		if (!FShamanTargetRules::CanAffect(GetOwner(), A, Row.TargetFilter, Row.bFriendlyFire)) continue;
		const float Dist = FVector::Dist(A->GetActorLocation(), Loc);
		const float Falloff = 1.f - FMath::Clamp(Dist / FMath::Max(1.f, Row.Radius), 0.f, 1.f) * 0.6f;
		const float Dmg = Row.Damage * Falloff;
		if (Dmg > 0.f) UGameplayStatics::ApplyDamage(A, Dmg, GetInstigatorController(), this, UDamageType::StaticClass());
		if (auto* Rag = A->FindComponentByClass<URagdollReactionComponent>())
			Rag->ApplyHit(FMath::Max(Dmg, 1.f) * Row.Knockback, A->GetActorLocation() - Loc);
	}
	if (ShamanDebug::IsEnabled())
		DrawDebugSphere(GetWorld(), Loc, Row.Radius, 16, FColor::Orange, false, 1.5f);
	if (UParticleSystem* V = Row.ImpactVFX.LoadSynchronous()) UGameplayStatics::SpawnEmitterAtLocation(this, V, Loc);
	if (USoundBase* S = Row.ImpactSound.LoadSynchronous()) UGameplayStatics::PlaySoundAtLocation(this, S, Loc);
	// TODO (buildings milestone): damage buildings via BuildingDamageMultiplier; ignite via bIgnites.
	Destroy();
}
