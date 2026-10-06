#include "Game/ShamanGameData.h"
#include "Game/ShamanGameMode.h"
#include "Spells/DefaultSpellProjectile.h"
#include "Engine/World.h"

UShamanGameData::UShamanGameData()
{
	Seeds.WorldSeed = 1337;
	StartingSpells.Add(TEXT("Blast"));
	ProjectileClasses.Add(TEXT("FireBlast"), ADefaultSpellProjectile::StaticClass());

	FTribeDefinition Player;
	Player.DisplayName = NSLOCTEXT("Shaman", "PlayerTribe", "Your Tribe");
	Player.Color = FLinearColor(0.15f, 0.35f, 0.95f, 1.f);
	Player.FactionTag = TEXT("Faction.Player");
	Player.bPlayerControlled = true;
	Player.DefaultFollowerOrder = EFollowerOrder::FollowShaman;
	Tribes.Add(Player);

	FTribeDefinition Enemy;
	Enemy.DisplayName = NSLOCTEXT("Shaman", "EnemyTribe", "Red Tribe");
	Enemy.Color = FLinearColor(0.9f, 0.12f, 0.1f, 1.f);
	Enemy.FactionTag = TEXT("Faction.Enemy");
	Enemy.DefaultFollowerOrder = EFollowerOrder::GuardHome;
	Tribes.Add(Enemy);
}

const UShamanGameData* UShamanGameData::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (const AShamanGameMode* GM = World ? World->GetAuthGameMode<AShamanGameMode>() : nullptr)
		if (const UShamanGameData* D = GM->GetGameData()) return D;
	return GetDefault<UShamanGameData>();
}
