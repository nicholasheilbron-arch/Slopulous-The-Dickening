#pragma once
// Shared fixture for tribe tests (ShamanTribeTests.cpp, ShamanTribeSimTests.cpp). Test-only; included under
// WITH_DEV_AUTOMATION_TESTS.
#include "CoreMinimal.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"
#include "Game/ShamanGameData.h"
#include "Tribes/TribeSubsystem.h"
#include "Buildings/BuildingActor.h"

/**
 * Tribe rules on real production data (units/buildings resolve their rows like the game does): follower roster
 * updates on death (mana regen and rebirth time follow it) and enemy-tribe elimination / level win.
 * Flat test world, no movement simulated.
 */
namespace ShamanTribeTestsPrivate
{
	struct FTribeTestWorld
	{
		UWorld* World = nullptr;
		UTribeSubsystem* Tribes = nullptr;
		const UShamanGameData* Data = nullptr;

		FTribeTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ShamanTribeTestWorld"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
			Data = UShamanGameData::Get(World);
			Tribes = World->GetSubsystem<UTribeSubsystem>();
			Tribes->InitTribes(Data->Tribes, Data->Reincarnation); // tribe 0 = player, 1 = enemy (game defaults)
		}
		~FTribeTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			World->RemoveFromRoot();
		}

		/** Runs the world's timers (rebirth, end-of-frame death checks). The test runs inside one engine frame, so
		 *  the frame counter is advanced for FTimerManager's once-per-frame guard. */
		void AdvanceTimers(float Seconds)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(Seconds);
		}

		template<class T>
		T* SpawnUnit(FName UnitId, int32 Tribe, const FVector& Where)
		{
			T* U = World->SpawnActorDeferred<T>(T::StaticClass(), FTransform(Where), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			U->UnitId = UnitId;
			U->TribeId = Tribe;
			U->AutoPossessAI = EAutoPossessAI::Disabled;
			U->FinishSpawning(FTransform(Where));
			if (!U->HasActorBegunPlay()) U->DispatchBeginPlay();
			return U;
		}
		ABuildingActor* SpawnCircle(int32 Tribe, const FVector& Where) { return SpawnBuilding(Data->CircleBuildingId, Tribe, Where); }
		ABuildingActor* SpawnBuilding(FName BuildingId, int32 Tribe, const FVector& Where)
		{
			ABuildingActor* B = World->SpawnActorDeferred<ABuildingActor>(ABuildingActor::StaticClass(), FTransform(Where), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			B->BuildingId = BuildingId;
			B->TribeId = Tribe;
			B->FinishSpawning(FTransform(Where));
			if (!B->HasActorBegunPlay()) B->DispatchBeginPlay();
			return B;
		}
	};
}
