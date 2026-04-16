#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "PCGProfilerTypes.h"
#include "Subsystems/EngineSubsystem.h"
#include "PCGProfilerSubsystem.generated.h"

UCLASS()
class PCGPROFILER_API UPCGProfilerSubsystem : public UEngineSubsystem
{
    GENERATED_BODY()

public:
    enum class EMemoryProfilingMode : uint8
    {
        FastEstimate = 0,
        AccurateDelta = 1
    };

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    void StartRun(const FString& InRunName);

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    void EndRun();

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    void RecordNodeTiming(const FString& InNodeName, double DurationMs);

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    void RecordNodeEvent(const FPCGProfilerNodeEvent& InEvent);

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    bool ExportJsonReport(const FString& OptionalAbsoluteOrRelativePath, FString& OutSavedPath) const;

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    void SetStrictCriticalPathMode(bool bInEnabled);

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    bool IsStrictCriticalPathModeEnabled() const;

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    TArray<FPCGProfilerNodeAggregate> GetNodeAggregates() const;

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    TArray<FPCGProfilerNodeEvent> GetNodeEvents() const;

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    TArray<FPCGProfilerNodeEvent> GetCriticalPathEvents(int32 MaxItems = 20) const;

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    FString GetCurrentRunName() const;

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    int32 GetActivePCGComponentCount() const;

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    bool IsRunIdle() const;

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    bool WaitForRunComplete(double TimeoutSeconds = 120.0, double PollIntervalSeconds = 0.05);

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    bool RunOneClickProfile(double TimeoutSeconds = 600.0, double PollIntervalSeconds = 0.25, const FString& OptionalOutputPath = TEXT(""));

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    bool RunBatchProfile(int32 Iterations = 10, double TimeoutSeconds = 600.0, double PollIntervalSeconds = 0.25);

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    void CancelOneClickProfile(const FString& Reason = TEXT("cancelled"));

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    void SetMemoryProfilingModeAccurate(bool bAccurate);

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    bool IsMemoryProfilingModeAccurate() const;

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    void SetSamplingEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category="PCG Profiler")
    bool IsSamplingEnabled() const;

private:
    struct FNodeStats
    {
        FString NodeName;
        FString NodeId;
        FString NodeClass;
        FString SettingsClass;
        FString ExecutionPath;
        FString GraphName;
        FString DataType;
        int32 InputCount = 0;
        int32 OutputCount = 0;
        int64 InputPoints = 0;
        int64 OutputPoints = 0;
        int64 InputCountSum = 0;
        int64 OutputCountSum = 0;
        int64 InputPointsSum = 0;
        int64 OutputPointsSum = 0;
        int32 InputCountLast = 0;
        int32 OutputCountLast = 0;
        int64 InputPointsLast = 0;
        int64 OutputPointsLast = 0;
        TArray<double> InputCountSamples;
        TArray<double> OutputCountSamples;
        TArray<double> InputPointsSamples;
        TArray<double> OutputPointsSamples;
        TArray<double> QueueWaitSamples;
        int32 WarningCount = 0;
        int32 ErrorCount = 0;
        int32 CancelledCount = 0;
        int32 GameThreadCallCount = 0;
        int32 WorkerThreadCallCount = 0;
        int32 UnknownThreadCallCount = 0;
        double GameThreadMs = 0.0;
        double WorkerThreadMs = 0.0;
        double UnknownThreadMs = 0.0;
        double QueueWaitTotalMs = 0.0;
        double ExecutionTotalMs = 0.0;
        double SelfTotalMs = 0.0;
        int32 CacheHitCount = 0;
        int32 CacheMissCount = 0;
        int32 MissReasonParameterChangeCount = 0;
        int32 MissReasonInputChangeCount = 0;
        int32 MissReasonVersionChangeCount = 0;
        int32 MissReasonUnknownCount = 0;
        double CacheReuseSavedMsEstimate = 0.0;
        int64 MemoryDeltaSumBytes = 0;
        int64 MemoryDeltaMaxBytes = TNumericLimits<int64>::Lowest();
        int64 MemoryDeltaMinBytes = TNumericLimits<int64>::Max();
        int64 MemoryDeltaLastBytes = 0;
        TArray<double> MemoryDeltaSamples;
        bool bHasPreviousSample = false;
        int32 PreviousInputCount = 0;
        int64 PreviousInputPoints = 0;
        uint64 PreviousInputSignature = 0;
        FString PreviousSettingsClass;
        double FirstSeenTimeMs = 0.0;
        bool bHasFirstSeen = false;
        bool bFirstSeenPrecise = false;
        bool bFirstSeenEstimated = false;

        int32 CallCount = 0;
        double TotalMs = 0.0;
        double MinMs = TNumericLimits<double>::Max();
        double MaxMs = 0.0;
        TArray<double> Samples;

        void AddSample(const FPCGProfilerNodeEvent& InEvent)
        {
            ++CallCount;
            TotalMs += InEvent.InclusiveMs;
            MinMs = FMath::Min(MinMs, InEvent.InclusiveMs);
            MaxMs = FMath::Max(MaxMs, InEvent.InclusiveMs);
            Samples.Add(InEvent.InclusiveMs);
            WarningCount += InEvent.WarningCount;
            ErrorCount += InEvent.ErrorCount;
            CancelledCount += InEvent.bCancelled ? 1 : 0;
            // Keep the most representative non-zero IO footprint across calls
            // instead of overwriting with the last sample (which is often 0 on cache/no-op paths).
            InputCount = FMath::Max(InputCount, InEvent.InputCount);
            OutputCount = FMath::Max(OutputCount, InEvent.OutputCount);
            InputPoints = FMath::Max(InputPoints, InEvent.InputPoints);
            OutputPoints = FMath::Max(OutputPoints, InEvent.OutputPoints);
            InputCountSum += InEvent.InputCount;
            OutputCountSum += InEvent.OutputCount;
            InputPointsSum += InEvent.InputPoints;
            OutputPointsSum += InEvent.OutputPoints;
            InputCountLast = InEvent.InputCount;
            OutputCountLast = InEvent.OutputCount;
            InputPointsLast = InEvent.InputPoints;
            OutputPointsLast = InEvent.OutputPoints;
            InputCountSamples.Add(static_cast<double>(InEvent.InputCount));
            OutputCountSamples.Add(static_cast<double>(InEvent.OutputCount));
            InputPointsSamples.Add(static_cast<double>(InEvent.InputPoints));
            OutputPointsSamples.Add(static_cast<double>(InEvent.OutputPoints));
            NodeId = InEvent.NodeId;
            NodeName = InEvent.NodeTitle;
            NodeClass = InEvent.NodeClass;
            SettingsClass = InEvent.SettingsClass;
            ExecutionPath = InEvent.ExecutionPath;
            GraphName = InEvent.GraphName;
            if (!bHasFirstSeen || InEvent.FirstSeenTimeMs < FirstSeenTimeMs)
            {
                FirstSeenTimeMs = InEvent.FirstSeenTimeMs;
                bHasFirstSeen = true;
            }
            if (InEvent.FirstSeenTimeSource.Contains(TEXT("timer"), ESearchCase::IgnoreCase))
            {
                bFirstSeenPrecise = true;
            }
            else
            {
                bFirstSeenEstimated = true;
            }
            QueueWaitTotalMs += InEvent.QueueWaitMs;
            ExecutionTotalMs += InEvent.ExecutionMs;
            SelfTotalMs += InEvent.SelfMs;
            QueueWaitSamples.Add(InEvent.QueueWaitMs);
            if (!InEvent.DataType.IsEmpty())
            {
                DataType = InEvent.DataType;
            }
            if (InEvent.bCacheHit)
            {
                ++CacheHitCount;
                CacheReuseSavedMsEstimate += InEvent.InclusiveMs;
            }
            else
            {
                ++CacheMissCount;
                if (InEvent.CacheMissReason == TEXT("input_change"))
                {
                    ++MissReasonInputChangeCount;
                }
                else if (InEvent.CacheMissReason == TEXT("version_change"))
                {
                    ++MissReasonVersionChangeCount;
                }
                else if (InEvent.CacheMissReason == TEXT("parameter_change"))
                {
                    ++MissReasonParameterChangeCount;
                }
                else
                {
                    ++MissReasonUnknownCount;
                }
            }

            MemoryDeltaSumBytes += InEvent.MemoryDeltaBytes;
            MemoryDeltaMaxBytes = FMath::Max(MemoryDeltaMaxBytes, InEvent.MemoryDeltaBytes);
            MemoryDeltaMinBytes = FMath::Min(MemoryDeltaMinBytes, InEvent.MemoryDeltaBytes);
            MemoryDeltaLastBytes = InEvent.MemoryDeltaBytes;
            MemoryDeltaSamples.Add(static_cast<double>(InEvent.MemoryDeltaBytes));

            if (InEvent.ThreadGroup == TEXT("GameThread"))
            {
                ++GameThreadCallCount;
                GameThreadMs += InEvent.InclusiveMs;
            }
            else if (InEvent.ThreadGroup == TEXT("Worker"))
            {
                ++WorkerThreadCallCount;
                WorkerThreadMs += InEvent.InclusiveMs;
            }
            else
            {
                ++UnknownThreadCallCount;
                UnknownThreadMs += InEvent.InclusiveMs;
            }

            PreviousInputCount = InEvent.InputCount;
            PreviousInputPoints = InEvent.InputPoints;
            PreviousInputSignature = HashCombine(
                GetTypeHash(InEvent.InputCount),
                GetTypeHash(InEvent.InputPoints));
            PreviousSettingsClass = InEvent.SettingsClass;
            bHasPreviousSample = true;
        }
    };

    struct FCrossRunNodeStats
    {
        int32 TotalCalls = 0;
        TArray<double> InclusiveSamples;
        TArray<double> SelfSamples;
    };

    struct FRunThreadSummary
    {
        FString RunName;
        double RunDurationMs = 0.0;
        double ParallelEfficiencyScore = 0.0;
        double TopNNodeP95Ms = 0.0;
        double GameThreadTotalMs = 0.0;
        double WorkerTotalMs = 0.0;
        double UnknownTotalMs = 0.0;
        int32 GameThreadCallCount = 0;
        int32 WorkerCallCount = 0;
        int32 UnknownCallCount = 0;
        int32 CacheHitCount = 0;
        int32 CacheMissCount = 0;
        int32 MissReasonParameterChangeCount = 0;
        int32 MissReasonInputChangeCount = 0;
        int32 MissReasonVersionChangeCount = 0;
        int32 MissReasonUnknownCount = 0;
        double CacheReuseSavedMsEstimate = 0.0;
    };

    struct FEdgeStats
    {
        FString ParentNodeId;
        FString ParentNodeTitle;
        FString ChildNodeId;
        FString ChildNodeTitle;
        FString GraphName;
        double TotalChildInclusiveMs = 0.0;
        int32 CallCount = 0;
        double MaxChildInclusiveMs = 0.0;

        void AddSample(const FPCGProfilerNodeEvent& InEvent, const FPCGProfilerNodeEvent& InParentEvent)
        {
            ParentNodeId = InParentEvent.NodeId;
            ParentNodeTitle = InParentEvent.NodeTitle;
            ChildNodeId = InEvent.NodeId;
            ChildNodeTitle = InEvent.NodeTitle;
            GraphName = InEvent.GraphName;
            TotalChildInclusiveMs += InEvent.InclusiveMs;
            ++CallCount;
            MaxChildInclusiveMs = FMath::Max(MaxChildInclusiveMs, InEvent.InclusiveMs);
        }
    };

    static FString BuildNodeKey(const FString& NodeTitle, const FString& NodeId, const FString& GraphName);
    static FString BuildEdgeKey(const FString& ParentPath, const FString& ChildPath, const FString& GraphName);
    static double ComputePercentile(const TArray<double>& Samples, double Quantile);
    static FString ResolveParentExecutionPath(const FString& ExecutionPath);
    static int64 GetProcessMemoryBytes();
    void HarvestFromPCGExecutionInspection_NoLock();
    void FinalizeRunThreadSummary_NoLock();
    void ResetStreamingState_NoLock();
    bool FlushEventsToChunk_NoLock(int32 NumEventsToFlush);
    void MaybeFlushEventsAndWarnings_NoLock();

    static FString BuildDefaultOutputPath();
    bool TickOneClickProfile(float DeltaTime);
    void FinishOneClickProfile(const FString& Reason, bool bDidTimeout);

private:
    mutable FCriticalSection DataMutex;
    FString CurrentRunName;
    FDateTime RunStartUtc;
    FDateTime RunEndUtc;
    TMap<FString, FNodeStats> NodeStats;
    TArray<FPCGProfilerNodeEvent> NodeEvents;
    TMap<FString, FCrossRunNodeStats> CrossRunStats;
    TArray<FRunThreadSummary> RunThreadHistory;
    FString LastGraphName;
    FString LastComponentName;
    bool bStrictCriticalPathMode = false;
    bool bSamplingEnabled = true;
    bool bRunThreadSummaryFinalized = false;

    bool bOneClickInProgress = false;
    FString OneClickRunName;
    FString OneClickOutputPath;
    FString OneClickWorldPath;
    TSet<FString> OneClickComponentScopeKeys;
    bool bOneClickUsePurge = false;
    double OneClickTimeoutSeconds = 600.0;
    double OneClickPollIntervalSeconds = 0.25;
    double OneClickStartedAtSeconds = 0.0;
    double OneClickLastPollAtSeconds = 0.0;
    int32 OneClickIdleStableTicks = 0;
    bool bOneClickObservedActiveWork = false;
    FTSTicker::FDelegateHandle OneClickTickHandle;

    bool bBatchInProgress = false;
    int32 BatchTotalIterations = 0;
    int32 BatchRemainingIterations = 0;
    double BatchTimeoutSeconds = 600.0;
    double BatchPollIntervalSeconds = 0.25;
    double BatchCooldownSeconds = 3.0;
    bool bBatchUsePurge = true;
    FString BatchLockedWorldPath;
    int32 BatchLockedWorldInitialComponentCount = -1;
    double BatchPreStartWaitStartedAtSeconds = 0.0;
    double BatchPreStartMaxWaitSeconds = 120.0;
    int32 BatchPreStartRequiredIdleTicks = 3;
    int32 BatchPreStartIdleTicks = 0;
    int32 BatchPreStartLastComponentCount = -1;
    double BatchPreStartComponentStableWindowSeconds = 3.0;
    double BatchPreStartLastComponentChangeAtSeconds = 0.0;
    FTSTicker::FDelegateHandle BatchDelayTickHandle;

    EMemoryProfilingMode MemoryProfilingMode = EMemoryProfilingMode::AccurateDelta;
    int64 LastProcessMemoryBytes = 0;
    int64 PeakProcessMemoryBytes = 0;
    double RunStartPlatformSeconds = 0.0;
    int32 MaxEventsInMemoryBeforeFlush = 50000;
    int32 FlushChunkEventCount = 20000;
    int64 TotalEventsSeen = 0;
    int64 TotalEventsFlushed = 0;
    int64 TotalEventsDropped = 0;
    int32 FlushChunkIndex = 0;
    int32 FlushCount = 0;
    int32 MemoryWarningCount = 0;
    bool bMemoryWarningEmitted = false;
    int64 MemoryWarningThresholdBytes = 3ll * 1024ll * 1024ll * 1024ll;
    FString EventChunkDir;
    TArray<FString> FlushedChunkPaths;
};
