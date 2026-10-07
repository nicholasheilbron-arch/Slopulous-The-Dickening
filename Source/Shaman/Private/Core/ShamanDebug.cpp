#include "Core/ShamanDebug.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarShamanDebug(
	TEXT("shaman.Debug"), 0,
	TEXT("1 = draw Shaman gameplay debug (spawn points, resources, spell traces, damage impulses, reincarnation)."),
	ECVF_Cheat);

bool ShamanDebug::IsEnabled() { return CVarShamanDebug.GetValueOnGameThread() != 0; }
void ShamanDebug::Toggle() { CVarShamanDebug.AsVariable()->Set(IsEnabled() ? 0 : 1, ECVF_SetByConsole); }

#include "Core/ShamanLog.h"
DEFINE_LOG_CATEGORY(LogShaman);

bool ShamanLog::FirstTime(const FString& Key)
{
	static TSet<FString> Seen;
	bool bAlready = false;
	Seen.Add(Key, &bAlready);
	return !bAlready;
}
