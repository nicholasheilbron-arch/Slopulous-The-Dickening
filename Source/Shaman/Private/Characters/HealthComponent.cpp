#include "Characters/HealthComponent.h"
#include "Core/ShamanInterfaces.h"
#include "Core/ShamanDebug.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"

UHealthComponent::UHealthComponent() { PrimaryComponentTick.bCanEverTick = false; }

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner()) Owner->OnTakeAnyDamage.AddDynamic(this, &UHealthComponent::HandleAnyDamage);
}

void UHealthComponent::HandleAnyDamage(AActor* DamagedActor, float Damage, const UDamageType*, AController* InstigatedBy, AActor* DamageCauser)
{
	if (bDead || Damage <= 0.f) return;
	if (const IShamanDamageReceiver* R = Cast<IShamanDamageReceiver>(GetOwner()))
		if (R->IsInvulnerable()) return;

	LastDamage = Damage;
	LastCauser = DamageCauser;
	Health = FMath::Max(0.f, Health - Damage);
	OnHealthChanged.Broadcast(Health, MaxHealth, -Damage);

	if (ShamanDebug::IsEnabled() && DamageCauser)
	{
		const FVector To = GetOwner()->GetActorLocation();
		DrawDebugDirectionalArrow(GetWorld(), DamageCauser->GetActorLocation(), To, 40.f, FColor::Red, false, 2.f, 0, 2.f);
		DrawDebugString(GetWorld(), To + FVector(0, 0, 120), FString::Printf(TEXT("-%.0f"), Damage), nullptr, FColor::Red, 1.5f);
	}
	if (Health <= 0.f) Die(DamageCauser, InstigatedBy);
}

void UHealthComponent::Kill(AActor* Killer)
{
	if (bDead) return;
	LastDamage = Health;
	LastCauser = Killer;
	Health = 0.f;
	OnHealthChanged.Broadcast(0.f, MaxHealth, -LastDamage);
	Die(Killer, nullptr);
}

void UHealthComponent::Die(AActor* Killer, AController* KillerController)
{
	bDead = true;
	OnDied.Broadcast(Killer, KillerController);
}
