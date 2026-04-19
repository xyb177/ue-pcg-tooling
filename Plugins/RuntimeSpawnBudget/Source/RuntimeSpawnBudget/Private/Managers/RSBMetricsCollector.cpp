#include "RSBMetricsCollector.h"

#include "Algo/Sort.h"

void URSBMetricsCollector::Initialize(int32 InWindowSize)
{
    WindowSize = FMath::Max(16, InWindowSize);
    QueueDelaySamples.Reset();
    SpawnTimeSamples.Reset();
    PoolHits = 0;
    PoolMisses = 0;
}

void URSBMetricsCollector::BeginFrame(int64 InFrameNumber, int32 QueueLength)
{
    FrameCounter = InFrameNumber;
    CurrentQueueLength = QueueLength;
    CurrentMaxQueueDelayMs = 0.0f;
}

void URSBMetricsCollector::AddQueueDelayMs(float DelayMs)
{
    const float Clamped = FMath::Max(0.0f, DelayMs);
    CurrentMaxQueueDelayMs = FMath::Max(CurrentMaxQueueDelayMs, Clamped);
    QueueDelaySamples.Add(Clamped);
    if (QueueDelaySamples.Num() > WindowSize)
    {
        QueueDelaySamples.RemoveAt(0, QueueDelaySamples.Num() - WindowSize, EAllowShrinking::No);
    }
}

void URSBMetricsCollector::EndFrame(int32 SpawnProcessed, int32 DestroyProcessed, int32 Dropped, float SpawnTimeMs, float DestroyTimeMs)
{
    LastFrameStats.FrameNumber = FrameCounter;
    LastFrameStats.QueueLength = CurrentQueueLength;
    LastFrameStats.SpawnProcessed = SpawnProcessed;
    LastFrameStats.DestroyProcessed = DestroyProcessed;
    LastFrameStats.DroppedRequests = Dropped;
    LastFrameStats.SpawnTimeMs = SpawnTimeMs;
    LastFrameStats.DestroyTimeMs = DestroyTimeMs;
    LastFrameStats.MaxQueueDelayMs = CurrentMaxQueueDelayMs;

    SpawnTimeSamples.Add(FMath::Max(0.0f, SpawnTimeMs));
    if (SpawnTimeSamples.Num() > WindowSize)
    {
        SpawnTimeSamples.RemoveAt(0, SpawnTimeSamples.Num() - WindowSize, EAllowShrinking::No);
    }
}

void URSBMetricsCollector::AddPoolHit()
{
    ++PoolHits;
}

void URSBMetricsCollector::AddPoolMiss()
{
    ++PoolMisses;
}

FRSBFrameStats URSBMetricsCollector::GetLastFrameStats() const
{
    return LastFrameStats;
}

FRSBWindowStats URSBMetricsCollector::BuildWindowStats(int32 PendingQueueLength) const
{
    FRSBWindowStats Result;
    Result.PendingQueueLength = PendingQueueLength;

    auto BuildStats = [](const TArray<float>& Samples, float& OutAvg, float& OutP95, float& OutP99)
    {
        if (Samples.IsEmpty())
        {
            OutAvg = OutP95 = OutP99 = 0.0f;
            return;
        }

        float Sum = 0.0f;
        TArray<float> Sorted = Samples;
        Sorted.Sort();
        for (float Value : Sorted)
        {
            Sum += Value;
        }

        OutAvg = Sum / static_cast<float>(Sorted.Num());
        OutP95 = URSBMetricsCollector::ComputePercentile(Sorted, 0.95f);
        OutP99 = URSBMetricsCollector::ComputePercentile(Sorted, 0.99f);
    };

    BuildStats(QueueDelaySamples, Result.AvgQueueDelayMs, Result.P95QueueDelayMs, Result.P99QueueDelayMs);
    BuildStats(SpawnTimeSamples, Result.AvgSpawnTimeMs, Result.P95SpawnTimeMs, Result.P99SpawnTimeMs);

    const int32 TotalPoolSamples = PoolHits + PoolMisses;
    Result.PoolHitRate = TotalPoolSamples > 0 ? (static_cast<float>(PoolHits) / static_cast<float>(TotalPoolSamples)) : 0.0f;
    return Result;
}

float URSBMetricsCollector::ComputePercentile(const TArray<float>& SortedValues, float Quantile)
{
    if (SortedValues.IsEmpty())
    {
        return 0.0f;
    }

    const float Q = FMath::Clamp(Quantile, 0.0f, 1.0f);
    const float Position = Q * static_cast<float>(SortedValues.Num() - 1);
    const int32 Lower = FMath::Clamp(FMath::FloorToInt(Position), 0, SortedValues.Num() - 1);
    const int32 Upper = FMath::Clamp(FMath::CeilToInt(Position), 0, SortedValues.Num() - 1);
    if (Lower == Upper)
    {
        return SortedValues[Lower];
    }

    return FMath::Lerp(SortedValues[Lower], SortedValues[Upper], Position - static_cast<float>(Lower));
}
