#include "RSBPolicyManager.h"

void URSBPolicyManager::Initialize(const URSBConfig* InConfig)
{
    if (!InConfig)
    {
        return;
    }

    Budget.MaxSpawnOpsPerFrame = FMath::Max(1, InConfig->MaxSpawnOpsPerFrame);
    Budget.MaxDestroyOpsPerFrame = FMath::Max(1, InConfig->MaxDestroyOpsPerFrame);
    Budget.MaxSpawnTimeMsPerFrame = FMath::Max(0.0f, InConfig->MaxSpawnTimeMsPerFrame);
    Budget.MaxDestroyTimeMsPerFrame = FMath::Max(0.0f, InConfig->MaxDestroyTimeMsPerFrame);
}

FRSBBudgetSnapshot URSBPolicyManager::GetBudget() const
{
    return Budget;
}
