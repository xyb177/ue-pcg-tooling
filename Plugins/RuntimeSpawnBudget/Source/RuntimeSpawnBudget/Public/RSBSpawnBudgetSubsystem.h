#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
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
class URSBSpawnAsyncAction;
class ARSBPressureSpawnerActor;

UCLASS()
class RUNTIMESPAWNBUDGET_API URSBSpawnBudgetSubsystem : public UWorldSubsystem, public IRSBStatsProviderInterface
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    bool EnqueueSpawn(FRSBSpawnRequest Request);

    TFuture<AActor*> EnqueueSpawnFuture(FRSBSpawnRequest Request);

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    bool EnqueueDestroy(FRSBDestroyRequest Request);

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    void PrewarmPool(TSubclassOf<AActor> ActorClass, FName PoolKey, int32 Count);

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    FRSBFrameStats GetLastFrameStats() const;

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    FRSBWindowStats GetWindowStatsBlueprint() const;

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    FRSBQueueSnapshot GetQueueSnapshot() const;

    UFUNCTION(BlueprintCallable, Category="RuntimeSpawnBudget")
    ARSBPressureSpawnerActor* SpawnPressureActor(TSubclassOf<ARSBPressureSpawnerActor> PressureActorClass, const FTransform& SpawnTransform);

    virtual FRSBWindowStats GetWindowStats_Implementation() const override;

    void RegisterAsyncAction(int32 RequestId, URSBSpawnAsyncAction* Action);
    void UnregisterAsyncAction(int32 RequestId, URSBSpawnAsyncAction* Action);
    int32 PrepareSpawnRequest(FRSBSpawnRequest& Request);

private:
    bool Tick(float DeltaTime);
    int32 PriorityToIndex(ERSBRequestPriority Priority) const;
    int32 GetPendingQueueLength() const;
    bool IsSystemEnabled() const;
    void ResolveAsyncSpawnResult(int32 RequestId, AActor* Actor);
    void ResolveAsyncSpawnFailure(int32 RequestId);

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
    TMap<int32, TSharedPtr<TPromise<AActor*>>> PendingSpawnPromises;
    TMap<int32, TArray<TWeakObjectPtr<URSBSpawnAsyncAction>>> PendingSpawnActions;

    FTSTicker::FDelegateHandle TickHandle;
    int32 NextRequestId = 1;
};
