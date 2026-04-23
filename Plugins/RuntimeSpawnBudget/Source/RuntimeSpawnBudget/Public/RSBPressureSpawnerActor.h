#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RSBTypes.h"
#include "RSBPressureSpawnerActor.generated.h"

class URSBSpawnBudgetSubsystem;
class URSBSpawnAsyncAction;

UCLASS(BlueprintType, Blueprintable)
class RUNTIMESPAWNBUDGET_API ARSBPressureSpawnerActor : public AActor
{
    GENERATED_BODY()

public:
    ARSBPressureSpawnerActor();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget")
    TSubclassOf<AActor> SpawnActorClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget")
    FName PoolKey = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget", meta=(ClampMin="1"))
    int32 BurstSize = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget", meta=(ClampMin="1"))
    int32 BurstCount = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget", meta=(ClampMin="0.01"))
    float BurstIntervalSeconds = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget")
    bool bUseAsyncSpawn = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget")
    bool bDestroySpawnedActorsAfterDelay = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget", meta=(ClampMin="0.01"))
    float DestroyDelaySeconds = 2.0f;

private:
    void StartBurstLoop();
    void ExecuteBurst();
    void QueueDestroyForTrackedActors();
    UFUNCTION()
    void HandleAsyncSpawnCompleted(AActor* SpawnedActor);
    UFUNCTION()
    void HandleAsyncSpawnFailed();
    UFUNCTION()
    void HandleDestroyTimer();
    URSBSpawnBudgetSubsystem* ResolveSubsystem() const;

private:
    FTimerHandle BurstTimerHandle;
    FTimerHandle DestroyTimerHandle;
    int32 RemainingBursts = 0;
    TArray<TWeakObjectPtr<AActor>> TrackedSpawnedActors;
};
