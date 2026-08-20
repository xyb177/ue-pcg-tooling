#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RSBTypes.h"
#include "RSBSpawnRequestSourceInterface.generated.h"

UINTERFACE(BlueprintType)
class RUNTIMESPAWNBUDGET_API URSBSpawnRequestSourceInterface : public UInterface
{
    GENERATED_BODY()
};

class RUNTIMESPAWNBUDGET_API IRSBSpawnRequestSourceInterface
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="RuntimeSpawnBudget")
    bool BuildSpawnRequest(FRSBSpawnRequest& OutRequest) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="RuntimeSpawnBudget")
    void HandleSpawnRequestCompleted(int32 RequestId, AActor* SpawnedActor);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="RuntimeSpawnBudget")
    void HandleSpawnRequestFailed(int32 RequestId);
};
