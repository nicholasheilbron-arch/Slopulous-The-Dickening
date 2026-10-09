#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TribeTypes.h"
#include "Tribes/TribeTaskTypes.h"
#include "TribeMemberComponent.generated.h"

class UTribeComponent;
class UTribeMemberComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnUnitConverted, UTribeMemberComponent*, From, UTribeMemberComponent*, To, EConversionKind, Kind);
/** Native: the task this unit holds changed state (Assigned, Active, Completed, Failed, Cancelled). Phase 2.2. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnMemberTaskStateChanged, UTribeMemberComponent* /*Member*/, int32 /*TaskId*/, ETribeTaskState /*NewState*/);

/**
 * Gameplay identity of any unit for spell targeting and conversion: which tribe owns it, what kind of unit it is,
 * and whether it can change sides. Add to BP_Wildman (Kind = Wildman, TribeId = -1), BP_Brave (Kind = Follower),
 * the Shaman (Kind = Shaman) and buildings (Kind = Building). Targeting never looks at class names or collision channels.
 */
UCLASS(ClassGroup=(Shaman), meta=(BlueprintSpawnableComponent))
class SHAMAN_API UTribeMemberComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UTribeMemberComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribe") int32 TribeId = ShamanTribes::NoTribe;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribe") EUnitKind UnitKind = EUnitKind::Wildman;
	/** Designer switch, e.g. for scripted Wildmen that must not be recruited. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribe") bool bConvertible = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tribe", meta=(ClampMin="0")) int32 PopulationCost = 1;

	UFUNCTION(BlueprintCallable, Category="Tribe") int32 GetTribeId() const { return TribeId; }
	UFUNCTION(BlueprintCallable, Category="Tribe") EUnitKind GetUnitKind() const { return UnitKind; }
	UFUNCTION(BlueprintCallable, Category="Tribe") bool IsWildman() const { return UnitKind == EUnitKind::Wildman; }
	UFUNCTION(BlueprintCallable, Category="Tribe") bool IsFollower() const { return UnitKind == EUnitKind::Follower; }
	/** Not locked by design, not mid-conversion, not being destroyed. */
	UFUNCTION(BlueprintCallable, Category="Tribe") bool IsConvertible() const;
	/** Full rule check for a specific conversion. */
	UFUNCTION(BlueprintCallable, Category="Tribe") bool CanBeConvertedTo(int32 NewTribeId, EConversionKind Kind) const;

	/**
	 * Converts this unit to NewTribe. Checks rules and capacity first; on any failure nothing changes and null is returned.
	 * If NewTribe->ConvertedFollowerClass is set, a new actor of that class replaces this one (this actor is destroyed);
	 * otherwise this unit is converted in place. Returns the member component of the resulting follower.
	 */
	UFUNCTION(BlueprintCallable, Category="Tribe") UTribeMemberComponent* ConvertToTribe(UTribeComponent* NewTribe, EConversionKind Kind);

	/** Changes tribe and keeps tribe rosters consistent (unregister old, register new). */
	UFUNCTION(BlueprintCallable, Category="Tribe") void SetTribeId(int32 NewTribeId);

	/** Fired on the original unit after a successful conversion (VFX / mesh swap hook). From == To for in-place conversions. */
	UPROPERTY(BlueprintAssignable, Category="Tribe") FOnUnitConverted OnConverted;

	UFUNCTION(BlueprintCallable, Category="Tribe") static UTribeMemberComponent* FindOn(const AActor* Actor);

	// ---- Tribe simulation (Phase 2.1): availability and the unit's one primary task ------------------------------
	// Tasks are owned and ended by the tribe (UTribeComponent::CreateTask/AssignTask/...); the member only records
	// which one it holds and relays its state changes (OnTaskStateChanged) to the unit's executor (Phase 2.2).

	/** Owner is alive (IShamanDamageReceiver), not being destroyed. Members without health count as alive. */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool IsAlive() const;
	/** Worker pool rule: a living follower of a tribe, not marked unavailable, holding no task. Never true for Shamans. */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool IsAvailableWorker() const;
	/** Temporarily remove/return this unit from/to its tribe's worker pool (stunned, scripted, in a cutscene...). */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") void SetUnavailable(bool bNewUnavailable);
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool IsUnavailable() const { return bUnavailable; }
	/** Id of the task this unit holds (Assigned or Active), or INDEX_NONE. */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") int32 GetCurrentTaskId() const { return CurrentTaskId; }
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") bool HasTask() const { return CurrentTaskId != INDEX_NONE; }
	/** Derived: Dead > Unavailable > task activity (Working, or Moving / Guarding while a task executes) > current
	 *  activity (Idle / Following / Guarding / Combat). */
	UFUNCTION(BlueprintCallable, Category="Tribe|Simulation") EUnitSimState GetSimState() const;
	/** What the unit's own AI is doing when it holds no task (set by the Phase 1 AI; Idle by default). */
	void SetActivity(EUnitSimState NewActivity) { Activity = NewActivity; }

	/** What the unit's task executor is doing while it holds a task (Phase 2.2; Working = not executed / waiting). */
	void SetTaskActivity(EUnitSimState NewActivity) { TaskActivity = NewActivity; }

	/** Fired by the owning tribe for the task this unit holds; the unit's FUnitTaskExecutor listens (Phase 2.2). */
	FOnMemberTaskStateChanged OnTaskStateChanged;

	/** Bookkeeping hooks for UTribeComponent only. */
	void SetCurrentTaskId(int32 TaskId) { CurrentTaskId = TaskId; }
	void NotifyTaskState(int32 TaskId, ETribeTaskState NewState) { OnTaskStateChanged.Broadcast(this, TaskId, NewState); }

protected:
	/** Applies a pending conversion identity (see UTribeRegistrySubsystem::BeginPendingConversion) before any BeginPlay runs. */
	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	/** Tells the owning tribe that this unit's worker availability may have changed. */
	void NotifyAvailabilityChanged() const;

	bool bConversionInProgress = false;
	UPROPERTY(VisibleInstanceOnly, Category="Tribe|Simulation") bool bUnavailable = false;
	UPROPERTY(VisibleInstanceOnly, Category="Tribe|Simulation") int32 CurrentTaskId = INDEX_NONE;
	UPROPERTY(VisibleInstanceOnly, Category="Tribe|Simulation") EUnitSimState Activity = EUnitSimState::Idle;
	UPROPERTY(VisibleInstanceOnly, Category="Tribe|Simulation") EUnitSimState TaskActivity = EUnitSimState::Working;
};
