#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "RSBTypes.h"
#include "RSBSpawnAsyncAction.generated.h"

class URSBSpawnBudgetSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRSBSpawnAsyncCompleted, AActor*, SpawnedActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRSBSpawnAsyncFailed);

UCLASS()
class RUNTIMESPAWNBUDGET_API URSBSpawnAsyncAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FRSBSpawnAsyncCompleted OnCompleted;

    UPROPERTY(BlueprintAssignable)
    FRSBSpawnAsyncFailed OnFailed;

    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject"), Category="RuntimeSpawnBudget")
    static URSBSpawnAsyncAction* SpawnActorAsync(UObject* WorldContextObject, FRSBSpawnRequest Request);

    virtual void Activate() override;
    virtual void BeginDestroy() override;

    void NotifyCompleted(AActor* SpawnedActor);
    void NotifyFailed();

private:
    void CompleteAndRelease();

private:
    UPROPERTY()
    TObjectPtr<UObject> WorldContextObject;

    UPROPERTY()
    TObjectPtr<URSBSpawnBudgetSubsystem> Subsystem;

    FRSBSpawnRequest Request;
    int32 RequestId = 0;
    bool bFinished = false;
};
