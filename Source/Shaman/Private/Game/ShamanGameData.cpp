#include "Game/ShamanGameData.h"
#include "Game/ShamanGameMode.h"
#include "Spells/DefaultSpellProjectile.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "SpellRow.h"
#include "Characters/UnitRow.h"
#include "Buildings/BuildingRow.h"
#include "World/ResourceRow.h"
#include "Core/ShamanLog.h"

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
	// No Shaman game mode in this world (automation test worlds, other maps): use the same data production play
	// resolves (tables included), not bare class defaults with no tables. Resolved once per session.
	static TWeakObjectPtr<UShamanGameData> Fallback;
	static bool bResolved = false;
	if (!bResolved || !Fallback.IsValid())
	{
		bResolved = true;
		const AShamanGameMode* CDO = GetDefault<AShamanGameMode>();
		UShamanGameData* D = Resolve(nullptr, CDO->GameData, CDO->DefaultGameDataPath);
		if (!D->IsRooted()) D->AddToRoot(); // transient fallback object must survive GC
		Fallback = D;
	}
	return Fallback.IsValid() ? Fallback.Get() : GetDefault<UShamanGameData>();
}

UDataTable* UShamanGameData::FindTable(const TCHAR* DefaultPath, const UScriptStruct* RowStruct)
{
	if (UDataTable* T = LoadObject<UDataTable>(nullptr, DefaultPath, nullptr, LOAD_NoWarn | LOAD_Quiet)) return T;
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (Registry.Get().IsLoadingAssets()) Registry.Get().SearchAllAssets(true); // e.g. automation right after editor start
	TArray<FAssetData> Assets;
	Registry.Get().GetAssetsByClass(UDataTable::StaticClass()->GetFName(), Assets, true);
	for (const FAssetData& A : Assets)
	{
		UDataTable* T = Cast<UDataTable>(A.GetAsset());
		if (T && T->GetRowStruct() == RowStruct)
		{
			UE_LOG(LogShaman, Log, TEXT("Using %s for %s rows."), *A.ObjectPath.ToString(), *RowStruct->GetName());
			return T;
		}
	}
	return nullptr;
}

UShamanGameData* UShamanGameData::Resolve(UObject* Outer, UShamanGameData* Assigned, const TSoftObjectPtr<UShamanGameData>& DefaultPath)
{
	UShamanGameData* Data = Assigned;
	if (!Data && !DefaultPath.IsNull()) Data = DefaultPath.LoadSynchronous();
	if (!Data)
	{
		// Zero-config fallback: class defaults + tables found below.
		Data = Outer ? NewObject<UShamanGameData>(Outer, TEXT("TransientShamanGameData")) : NewObject<UShamanGameData>(GetTransientPackage());
		UE_LOG(LogShaman, Log, TEXT("No DA_ShamanGameData asset; using built-in defaults."));
	}
	if (!Data->SpellTable)    Data->SpellTable    = FindTable(TEXT("/Game/Data/DT_Spells.DT_Spells"), FSpellRow::StaticStruct());
	if (!Data->UnitTable)     Data->UnitTable     = FindTable(TEXT("/Game/Data/DT_Units.DT_Units"), FUnitRow::StaticStruct());
	if (!Data->BuildingTable) Data->BuildingTable = FindTable(TEXT("/Game/Data/DT_Buildings.DT_Buildings"), FBuildingRow::StaticStruct());
	if (!Data->ResourceTable) Data->ResourceTable = FindTable(TEXT("/Game/Data/DT_Resources.DT_Resources"), FResourceRow::StaticStruct());
	return Data;
}
