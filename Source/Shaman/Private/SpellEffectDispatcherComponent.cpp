#include "SpellEffectDispatcherComponent.h"
#include "SpellEffect_ConvertUnits.h"
#include "SpellComponent.h"
#include "SpellRow.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"

USpellEffectDispatcherComponent::USpellEffectDispatcherComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	Effects.Add(CreateDefaultSubobject<USpellEffect_ConvertUnits>(TEXT("ConvertWildmen")));
}

void USpellEffectDispatcherComponent::BeginPlay()
{
	Super::BeginPlay();
	BoundSpells = GetOwner() ? GetOwner()->FindComponentByClass<USpellComponent>() : nullptr;
	if (BoundSpells) BoundSpells->OnSpellCast.AddDynamic(this, &USpellEffectDispatcherComponent::HandleSpellCast);
}

void USpellEffectDispatcherComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (BoundSpells) BoundSpells->OnSpellCast.RemoveDynamic(this, &USpellEffectDispatcherComponent::HandleSpellCast);
	BoundSpells = nullptr;
	Super::EndPlay(Reason);
}

USpellEffect* USpellEffectDispatcherComponent::FindEffect(FName EffectId) const
{
	for (USpellEffect* E : Effects) if (E && E->EffectId == EffectId) return E;
	return nullptr;
}

void USpellEffectDispatcherComponent::HandleSpellCast(FName SpellId, FName EffectId, FVector Target, float Radius, float Duration)
{
	FSpellEffectContext Ctx;
	Ctx.Caster = GetOwner();
	Ctx.SpellComponent = BoundSpells;
	Ctx.SpellId = SpellId;
	Ctx.EffectId = EffectId;
	Ctx.Target = Target;
	Ctx.Radius = Radius;
	Ctx.Duration = Duration;
	Dispatch(Ctx);
}

FSpellEffectResult USpellEffectDispatcherComponent::Dispatch(const FSpellEffectContext& Ctx)
{
	FSpellEffectResult Result;
	if (USpellEffect* Effect = FindEffect(Ctx.EffectId))
	{
		Result = Effect->Execute(Ctx);
		if (bPlayRowImpactFeedback && Ctx.SpellComponent && Ctx.SpellComponent->SpellTable)
			if (const FSpellRow* Row = Ctx.SpellComponent->SpellTable->FindRow<FSpellRow>(Ctx.SpellId, TEXT("SpellEffectFeedback"), false))
			{
				if (UParticleSystem* V = Row->ImpactVFX.IsNull() ? nullptr : Row->ImpactVFX.LoadSynchronous())
					UGameplayStatics::SpawnEmitterAtLocation(this, V, Ctx.Target);
				if (USoundBase* S = Row->ImpactSound.IsNull() ? nullptr : Row->ImpactSound.LoadSynchronous())
					UGameplayStatics::PlaySoundAtLocation(this, S, Ctx.Target);
			}
	}
	// Unhandled EffectIds are expected until their milestone: the spell still spent mana and fired OnSpellCast as before.
	LastResult = Result;
	OnSpellEffectResolved.Broadcast(Ctx, Result);
	return Result;
}
