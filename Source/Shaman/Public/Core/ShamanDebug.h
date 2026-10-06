#pragma once
#include "CoreMinimal.h"

/** Console: shaman.Debug 1 (or F3 in game). Draws spawn markers, resources, spell traces, damage impulses,
 *  reincarnation state. Use the built-in "show Navigation" for the navmesh. */
namespace ShamanDebug
{
	SHAMAN_API bool IsEnabled();
	SHAMAN_API void Toggle();
}
