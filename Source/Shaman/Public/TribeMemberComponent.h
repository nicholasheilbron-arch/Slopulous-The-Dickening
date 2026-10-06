#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TribeTypes.h"
#include "TribeMemberComponent.generated.h"

class UTribeComponent;
class UTribeMemberComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnUnitConverted, UTribeMemberComponent*, From, UTribeMemberComponent*, To, EConversionKind, Kind);

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

protected:
	/** Applies a pending conversion identity (see UTribeRegistrySubsystem::BeginPendingConversion) before any BeginPlay runs. */
	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	bool bConversionInProgress = false;
};
