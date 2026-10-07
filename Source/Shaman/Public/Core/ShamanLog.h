#pragma once
#include "CoreMinimal.h"
SHAMAN_API DECLARE_LOG_CATEGORY_EXTERN(LogShaman, Log, All);

namespace ShamanLog
{
	/** True the first time Key is seen this session; use to log a repeated problem only once. */
	SHAMAN_API bool FirstTime(const FString& Key);
}
