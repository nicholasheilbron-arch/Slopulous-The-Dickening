#include "Tribes/TribeTaskTypes.h"

const TCHAR* FTribeTaskRules::StateName(ETribeTaskState S)
{
	switch (S)
	{
	case ETribeTaskState::Unassigned: return TEXT("Unassigned");
	case ETribeTaskState::Assigned:   return TEXT("Assigned");
	case ETribeTaskState::Active:     return TEXT("Active");
	case ETribeTaskState::Completed:  return TEXT("Completed");
	case ETribeTaskState::Failed:     return TEXT("Failed");
	case ETribeTaskState::Cancelled:  return TEXT("Cancelled");
	default:                          return TEXT("?");
	}
}

const TCHAR* FTribeTaskRules::PriorityName(ETribeTaskPriority P)
{
	switch (P)
	{
	case ETribeTaskPriority::Low:       return TEXT("Low");
	case ETribeTaskPriority::Normal:    return TEXT("Normal");
	case ETribeTaskPriority::High:      return TEXT("High");
	case ETribeTaskPriority::Emergency: return TEXT("Emergency");
	default:                            return TEXT("?");
	}
}

const TCHAR* FTribeTaskRules::SimStateName(EUnitSimState S)
{
	switch (S)
	{
	case EUnitSimState::Idle:        return TEXT("Idle");
	case EUnitSimState::Moving:      return TEXT("Moving");
	case EUnitSimState::Working:     return TEXT("Working");
	case EUnitSimState::Building:    return TEXT("Building");
	case EUnitSimState::Gathering:   return TEXT("Gathering");
	case EUnitSimState::Eating:      return TEXT("Eating");
	case EUnitSimState::Drinking:    return TEXT("Drinking");
	case EUnitSimState::Sleeping:    return TEXT("Sleeping");
	case EUnitSimState::Following:   return TEXT("Following");
	case EUnitSimState::Guarding:    return TEXT("Guarding");
	case EUnitSimState::Combat:      return TEXT("Combat");
	case EUnitSimState::Dead:        return TEXT("Dead");
	case EUnitSimState::Unavailable: return TEXT("Unavailable");
	default:                         return TEXT("?");
	}
}
