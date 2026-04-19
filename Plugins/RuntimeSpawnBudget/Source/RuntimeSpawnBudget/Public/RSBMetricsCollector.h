#pragma once

#include "CoreMinimal.h"
#include "RSBTypes.h"
#include "RSBMetricsCollector.generated.h"

UCLASS()
class RUNTIMESPAWNBUDGET_API URSBMetricsCollector : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(int32 InWindowSize);
    void BeginFrame(int64 InFrameNumber, int32 QueueLength);
    void AddQueueDelayMs(float DelayMs);
    void EndFrame(int32 SpawnProcessed, int32 DestroyProcessed, int32 Dropped, float SpawnTimeMs, float DestroyTimeMs);

    void AddPoolHit();
    void AddPoolMiss();

    FRSBFrameStats GetLastFrameStats() const;
    FRSBWindowStats BuildWindowStats(int32 PendingQueueLength) const;

private:
    static float ComputePercentile(const TArray<float>& SortedValues, float Quantile);

private:
    int32 WindowSize = 256;
    int64 FrameCounter = 0;
    int32 CurrentQueueLength = 0;
    float CurrentMaxQueueDelayMs = 0.0f;

    FRSBFrameStats LastFrameStats;
    TArray<float> QueueDelaySamples;
    TArray<float> SpawnTimeSamples;

    int32 PoolHits = 0;
    int32 PoolMisses = 0;
};
