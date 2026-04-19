#pragma once

#include "CoreMinimal.h"
#include "RSBTypes.h"
#include "RSBDebugPanel.generated.h"

class URSBSpawnBudgetSubsystem;

UCLASS(BlueprintType)
class RUNTIMESPAWNBUDGET_API URSBDebugPanel : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    void BindSubsystem(URSBSpawnBudgetSubsystem* InSubsystem);

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    bool IsBound() const;

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    FRSBFrameStats GetLastFrameStats() const;

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    FRSBWindowStats GetWindowStats() const;

private:
    UPROPERTY()
    TObjectPtr<URSBSpawnBudgetSubsystem> Subsystem;
};
