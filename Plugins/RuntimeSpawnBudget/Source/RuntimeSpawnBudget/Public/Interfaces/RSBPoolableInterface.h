#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RSBPoolableInterface.generated.h"

UINTERFACE(BlueprintType)
class RUNTIMESPAWNBUDGET_API URSBPoolableInterface : public UInterface
{
    GENERATED_BODY()
};

class RUNTIMESPAWNBUDGET_API IRSBPoolableInterface
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="RuntimeSpawnBudget")
    void OnPooledAcquire();

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="RuntimeSpawnBudget")
    void OnPooledRelease();

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="RuntimeSpawnBudget")
    void OnPooledReset();
};
