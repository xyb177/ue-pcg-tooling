#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "Interfaces/RSBStatsProviderInterface.h"
#include "RSBTypes.h"
#include "RSBSpawnBudgetSubsystem.generated.h"

class URSBConfig;
class URSBMetricsCollector;
class URSBPolicyManager;
class URSBPoolManager;
class URSBSpawnExecutor;

UCLASS()
class RUNTIMESPAWNBUDGET_API URSBSpawnBudgetSubsystem : public UWorldSubsystem, public IRSBStatsProviderInterface
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    bool EnqueueSpawn(FRSBSpawnRequest Request);

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    bool EnqueueDestroy(FRSBDestroyRequest Request);

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    void PrewarmPool(TSubclassOf<AActor> ActorClass, FName PoolKey, int32 Count);

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    FRSBFrameStats GetLastFrameStats() const;

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    FRSBWindowStats GetWindowStatsBlueprint() const;

    virtual FRSBWindowStats GetWindowStats_Implementation() const override;

private:
    bool Tick(float DeltaTime);
    int32 PriorityToIndex(ERSBRequestPriority Priority) const;
    int32 GetPendingQueueLength() const;
    bool IsSystemEnabled() const;

private:
    const URSBConfig* Config = nullptr;

    UPROPERTY()
    URSBMetricsCollector* MetricsCollector = nullptr;

    UPROPERTY()
    URSBPolicyManager* PolicyManager = nullptr;

    UPROPERTY()
    URSBPoolManager* PoolManager = nullptr;

    UPROPERTY()
    URSBSpawnExecutor* SpawnExecutor = nullptr;

    TArray<FRSBSpawnRequest> SpawnQueues[4];
    TArray<FRSBDestroyRequest> DestroyQueues[4];

    FTSTicker::FDelegateHandle TickHandle;
    int32 NextRequestId = 1;
};
