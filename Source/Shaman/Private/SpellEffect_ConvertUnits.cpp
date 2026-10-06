#include "SpellEffect_ConvertUnits.h"
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "TribeRegistrySubsystem.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"

USpellEffect_ConvertUnits::USpellEffect_ConvertUnits()
{
	EffectId = TEXT("ConvertWildmen");
}

FSpellEffectResult USpellEffect_ConvertUnits::Execute(const FSpellEffectContext& Ctx)
{
	FSpellEffectResult Result;
	Result.bHandled = true;

	UTribeComponent* Tribe = UTribeComponent::FindTribeFor(Ctx.Caster);
	UTribeRegistrySubsystem* Registry = UTribeRegistrySubsystem::Get(Ctx.Caster);
	if (!Tribe || !Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: caster %s has no tribe; nothing converted."), *EffectId.ToString(), *GetNameSafe(Ctx.Caster));
		return Result;
	}

	// Snapshot candidates first (conversion may destroy actors), closest first so capacity goes to the nearest Wildmen.
	TArray<UTribeMemberComponent*> InArea;
	Registry->GetMembersInRadius(Ctx.Target, Ctx.Radius, InArea);
	TArray<TWeakObjectPtr<UTribeMemberComponent>> Candidates;
	for (UTribeMemberComponent* M : InArea)
	{
		if (M->GetOwner() == Ctx.Caster) continue;                       // never the caster
		if (!M->CanBeConvertedTo(Tribe->TribeId, Kind)) continue;        // Wildmen only for Recruit: skips own/enemy followers, Shamans, buildings
		Candidates.Add(M);
	}
	Result.Candidates = Candidates.Num();

	UParticleSystem* VFX = PerTargetVFX.IsNull() ? nullptr : PerTargetVFX.LoadSynchronous();
	USoundBase* Sound = PerTargetSound.IsNull() ? nullptr : PerTargetSound.LoadSynchronous();
	for (const TWeakObjectPtr<UTribeMemberComponent>& W : Candidates)
	{
		UTribeMemberComponent* M = W.Get();
		if (!M) continue;
		if (!Tribe->CanAddFollowers(M->PopulationCost)) { ++Result.SkippedForCapacity; continue; } // capacity checked BEFORE converting
		const FVector Where = M->GetOwner()->GetActorLocation();
		if (M->ConvertToTribe(Tribe, Kind))
		{
			++Result.Affected;
			if (VFX) UGameplayStatics::SpawnEmitterAtLocation(Tribe, VFX, Where);
			if (Sound) UGameplayStatics::PlaySoundAtLocation(Tribe, Sound, Where);
		}
	}
	return Result;
}
