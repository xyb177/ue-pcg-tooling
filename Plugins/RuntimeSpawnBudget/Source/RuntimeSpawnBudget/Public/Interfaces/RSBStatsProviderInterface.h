#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RSBTypes.h"
#include "RSBStatsProviderInterface.generated.h"

UINTERFACE(BlueprintType)
class RUNTIMESPAWNBUDGET_API URSBStatsProviderInterface : public UInterface
{
    GENERATED_BODY()
};

class RUNTIMESPAWNBUDGET_API IRSBStatsProviderInterface
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="RuntimeSpawnBudget")
    FRSBWindowStats GetWindowStats() const;
};
