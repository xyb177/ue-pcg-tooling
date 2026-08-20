#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RSBTypes.generated.h"

UENUM(BlueprintType)
enum class ERSBRequestPriority : uint8
{
    Critical = 0,
    High = 1,
    Normal = 2,
    Low = 3
};

USTRUCT(BlueprintType)
struct FRSBSpawnRequest
{
    GENERATED_BODY()

    FRSBSpawnRequest() = default;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    TSubclassOf<AActor> ActorClass;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    FTransform Transform = FTransform::Identity;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    ERSBRequestPriority Priority = ERSBRequestPriority::Normal;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    FName PoolKey = NAME_None;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    FName Tag = NAME_None;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    TObjectPtr<UObject> RequestSourceObject = nullptr;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    bool bAllowPooling = true;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    float DeadlineSeconds = -1.0f;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    int32 RequestId = 0;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    double EnqueuedAtSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct FRSBDestroyRequest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    TWeakObjectPtr<AActor> Actor;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    ERSBRequestPriority Priority = ERSBRequestPriority::Normal;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    FName PoolKey = NAME_None;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    bool bForceDestroy = false;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    int32 RequestId = 0;

    UPROPERTY(BlueprintReadWrite, Category="RuntimeSpawnBudget")
    double EnqueuedAtSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct FRSBFrameStats
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int64 FrameNumber = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 QueueLength = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 SpawnProcessed = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 DestroyProcessed = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 DroppedRequests = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float SpawnTimeMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float DestroyTimeMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float MaxQueueDelayMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float FrameTotalTimeMs = 0.0f;
};

USTRUCT(BlueprintType)
struct FRSBActorClassStats
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    FName ActorClassName = NAME_None;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 SampleCount = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 PoolHitCount = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float AvgSpawnCostMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float P95SpawnCostMs = 0.0f;
};

USTRUCT(BlueprintType)
struct FRSBWindowStats
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float AvgQueueDelayMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float P95QueueDelayMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float P99QueueDelayMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float AvgSpawnTimeMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float P95SpawnTimeMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float P99SpawnTimeMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float PeakFrameTimeMs = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    float PoolHitRate = 0.0f;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 PendingQueueLength = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    TArray<FRSBActorClassStats> ActorClassStats;
};

USTRUCT(BlueprintType)
struct FRSBQueueSnapshot
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 SpawnCritical = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 SpawnHigh = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 SpawnNormal = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 SpawnLow = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 DestroyCritical = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 DestroyHigh = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 DestroyNormal = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 DestroyLow = 0;

    UPROPERTY(VisibleAnywhere, Category="RuntimeSpawnBudget")
    int32 PendingAsyncCount = 0;
};

USTRUCT()
struct FRSBPoolBucket
{
    GENERATED_BODY()

    UPROPERTY()
    FName PoolKey = NAME_None;

    UPROPERTY()
    TSubclassOf<AActor> ActorClass;

    UPROPERTY()
    TArray<TWeakObjectPtr<AActor>> InactiveActors;

    UPROPERTY()
    int32 Capacity = 0;

    UPROPERTY()
    int32 HitCount = 0;

    UPROPERTY()
    int32 MissCount = 0;

    UPROPERTY()
    double LastTouchedSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct FRSBWorldActorClassEntry
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="RuntimeSpawnBudget")
    FName ActorClassName = NAME_None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="RuntimeSpawnBudget")
    int32 Count = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="RuntimeSpawnBudget")
    bool bIsBlueprintClass = false;
};
