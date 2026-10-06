#include "RagdollReactionComponent.h"
#include "HitReactionConfig.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h" // needed for GetMesh() member access (was missing; first-build fix)
#include "Engine/World.h"
#include "TimerManager.h"

void URagdollReactionComponent::ApplyHit(float Damage, FVector Direction)
{
	if (!Config || bRagdolling) return;
	const EHitTier Tier = Config->GetTier(Damage);
	if (Tier == EHitTier::None) return;
	OnHitReaction.Broadcast(Tier, Damage);

	ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char) return;

	const FVector Impulse = ComputeImpulse(Direction, Damage);
	Direction = Impulse.GetSafeNormal();

	if (!HasSkeletalMesh()) // placeholder art: no physics asset to ragdoll, so launch the capsule instead
	{
		if (Tier != EHitTier::Stagger) Char->LaunchCharacter(Impulse * FallbackLaunchScale * (Tier == EHitTier::Knockdown ? 0.25f : 1.f), true, true);
		else Char->LaunchCharacter(Direction * 200.f, true, false);
		return;
	}

	switch (Tier)
	{
	case EHitTier::Stagger:
		Char->LaunchCharacter(Direction * 200.f, true, false);
		break;
	case EHitTier::Knockdown:
		StartRagdoll(Impulse * 0.25f);
		break;
	default: // Ragdoll + Launch: distance scales with damage via Impulse
		StartRagdoll(Impulse);
		break;
	}
}

FVector URagdollReactionComponent::ComputeImpulse(FVector Direction, float Damage) const
{
	// Unchanged from the original ApplyHit: Impulse = BaseImpulse * DamageMultiplier * Direction (scaled by damage tier).
	Direction = (Direction.GetSafeNormal() + FVector::UpVector * Config->UpwardBias).GetSafeNormal();
	return Direction * Config->BaseImpulse * Config->DamageMultiplier * (Damage / Config->RagdollMinDamage);
}

bool URagdollReactionComponent::HasSkeletalMesh() const
{
	const ACharacter* Char = Cast<ACharacter>(GetOwner());
	return Char && Char->GetMesh() && Char->GetMesh()->SkeletalMesh != nullptr;
}

void URagdollReactionComponent::EnterDeathRagdoll(FVector Direction, float Damage)
{
	ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char) return;
	GetWorld()->GetTimerManager().ClearTimer(RecoverTimer);
	bDeathRagdoll = true;
	if (bRagdolling) return; // already flying: just never recover
	const FVector Impulse = Config ? ComputeImpulse(Direction, Damage) : FVector::ZeroVector;
	if (HasSkeletalMesh())
	{
		StartRagdoll(Impulse);
		GetWorld()->GetTimerManager().ClearTimer(RecoverTimer);
	}
	else
	{
		bRagdolling = true;
		Char->LaunchCharacter(Impulse * FallbackLaunchScale, true, true);
	}
}

void URagdollReactionComponent::ForceRecover()
{
	GetWorld()->GetTimerManager().ClearTimer(RecoverTimer);
	bDeathRagdoll = false;
	if (!bRagdolling) return;
	if (HasSkeletalMesh()) EndRagdoll();
	bRagdolling = false;
}

void URagdollReactionComponent::StartRagdoll(const FVector& Impulse)
{
	ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char || !Char->GetMesh()) return;
	bRagdolling = true;
	Char->GetCharacterMovement()->DisableMovement();
	Char->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Char->GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	Char->GetMesh()->SetSimulatePhysics(true);
	Char->GetMesh()->AddImpulse(Impulse, NAME_None, true);
	GetWorld()->GetTimerManager().SetTimer(RecoverTimer, this, &URagdollReactionComponent::EndRagdoll, Config->RagdollRecoverTime, false);
}

void URagdollReactionComponent::EndRagdoll()
{
	ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char) return;
	const float HalfHeight = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Landed = Char->GetMesh()->GetComponentLocation() + FVector(0, 0, HalfHeight);
	Char->GetMesh()->SetSimulatePhysics(false);
	Char->GetMesh()->AttachToComponent(Char->GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Char->GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -HalfHeight), FRotator(0, -90, 0));
	Char->SetActorLocation(Landed, false, nullptr, ETeleportType::TeleportPhysics);
	Char->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	bRagdolling = false;
}
