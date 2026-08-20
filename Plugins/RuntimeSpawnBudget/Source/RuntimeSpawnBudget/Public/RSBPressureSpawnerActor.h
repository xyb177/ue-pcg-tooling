#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/RSBSpawnRequestSourceInterface.h"
#include "RSBTypes.h"
#include "RSBPressureSpawnerActor.generated.h"

class URSBSpawnBudgetSubsystem;
class URSBSpawnAsyncAction;

UCLASS(BlueprintType, Blueprintable)
class RUNTIMESPAWNBUDGET_API ARSBPressureSpawnerActor : public AActor, public IRSBSpawnRequestSourceInterface
{
    GENERATED_BODY()

public:
    ARSBPressureSpawnerActor();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void HandleSpawnRequestCompleted_Implementation(int32 RequestId, AActor* SpawnedActor) override;
    virtual void HandleSpawnRequestFailed_Implementation(int32 RequestId) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget")
    TSubclassOf<AActor> SpawnActorClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget")
    bool bAutoResolveSpawnActorClassFromWorld = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RuntimeSpawnBudget")
    bool bPreferPlacedActorClass = true;

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
    TSubclassOf<AActor> ResolveSpawnActorClassFromWorld() const;
    void ExecuteBurst();
    void QueueDestroyForTrackedActors();
    UFUNCTION()
    void HandleDestroyTimer();
    URSBSpawnBudgetSubsystem* ResolveSubsystem() const;

private:
    FTimerHandle BurstTimerHandle;
    FTimerHandle DestroyTimerHandle;
    int32 RemainingBursts = 0;
    TArray<TWeakObjectPtr<AActor>> TrackedSpawnedActors;
};
