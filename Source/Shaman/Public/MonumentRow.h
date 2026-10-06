#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShamanTypes.h"
#include "MonumentRow.generated.h"

/** One row per monument archetype (DT_Monuments, import Content/Data/Monuments.csv).
 *  Which spell/building/event a placed monument grants is set per instance on AMonumentActor. */
USTRUCT(BlueprintType)
struct SHAMAN_API FMonumentRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EMonumentType MonumentType = EMonumentType::StoneHead;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EWorshipRule WorshipRule = EWorshipRule::ShamanOnly;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bIneligibleStillAnimate = false; // Gargoyle: followers play worship anim, add no progress
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float WorshipRequired = 30.f;         // progress points to complete a cycle
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProgressPerWorshipperPerSec = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaxWorshippers = 8;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EMonumentRewardType DefaultRewardType = EMonumentRewardType::UnlockSpell;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DefaultRewardCount = 1;         // Stone Head: total charge pool; world events: e.g. 5 Angels
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 ChargesPerCycle = 1;            // Stone Head only
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bPersistsAcrossMaps = false;     // Vault rewards carry to future maps
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bSinksWhenDepleted = false;      // Stone Head
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bRare = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SegmentRotationDegrees = 0.f;   // Obelisk: 90 on completion (visual, handled in Blueprint)
};
