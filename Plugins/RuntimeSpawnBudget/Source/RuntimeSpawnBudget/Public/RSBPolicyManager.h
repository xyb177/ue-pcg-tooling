#pragma once

#include "CoreMinimal.h"
#include "RSBConfig.h"
#include "RSBPolicyManager.generated.h"

USTRUCT()
struct FRSBBudgetSnapshot
{
    GENERATED_BODY()

    int32 MaxSpawnOpsPerFrame = 32;
    int32 MaxDestroyOpsPerFrame = 32;
    float MaxSpawnTimeMsPerFrame = 1.5f;
    float MaxDestroyTimeMsPerFrame = 1.0f;
};

UCLASS()
class RUNTIMESPAWNBUDGET_API URSBPolicyManager : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(const URSBConfig* InConfig);
    FRSBBudgetSnapshot GetBudget() const;

private:
    FRSBBudgetSnapshot Budget;
};
