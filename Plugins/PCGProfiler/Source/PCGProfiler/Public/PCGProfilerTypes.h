#pragma once

#include "CoreMinimal.h"
#include "PCGProfilerTypes.generated.h"

USTRUCT(BlueprintType)
struct FPCGProfilerNodeAggregate
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString NodeName;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 CallCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double TotalMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double MinMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double MaxMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double AvgMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double StdDevMs = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double DurationCv = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double P50Ms = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double P95Ms = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double SelfMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double InclusiveMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double FirstSeenTimeMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString FirstSeenTimeSource;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 SampleCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 CancelledCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 WarningCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 ErrorCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 InputCountMax = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 InputCountSum = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 InputCountLast = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double InputCountAvg = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double InputCountP95 = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double InputCountCv = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 OutputCountMax = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 OutputCountSum = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 OutputCountLast = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double OutputCountAvg = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double OutputCountP95 = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double OutputCountCv = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 InputPointsMax = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 InputPointsSum = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 InputPointsLast = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double InputPointsAvg = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double InputPointsP95 = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double InputPointsCv = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 OutputPointsMax = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 OutputPointsSum = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 OutputPointsLast = 0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double OutputPointsAvg = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double OutputPointsP95 = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double OutputPointsCv = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString NodeId;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString NodeClass;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString SettingsClass;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString ExecutionPath;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString GraphName;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString DataType;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double GameThreadMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double WorkerThreadMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 GameThreadCallCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 WorkerThreadCallCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double QueueWaitTotalMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double QueueWaitAvgMs = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double QueueWaitRatio = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double QueueWaitCv = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double ExecutionTotalMs = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double GameThreadRatio = 0.0;
    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double WorkerThreadRatio = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 CacheHitCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 CacheMissCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double CacheHitRate = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 MissReasonParameterChangeCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 MissReasonInputChangeCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 MissReasonVersionChangeCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 MissReasonUnknownCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double CacheReuseSavedMsEstimate = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 MemoryDeltaSumBytes = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 MemoryDeltaMaxBytes = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 MemoryDeltaMinBytes = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 MemoryDeltaLastBytes = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double MemoryDeltaAvgBytes = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double MemoryDeltaP95Bytes = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double MemoryDeltaCv = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString MemoryScope;
};

USTRUCT(BlueprintType)
struct FPCGProfilerNodeEvent
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString NodeId;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString NodeTitle;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString NodeClass;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString SettingsClass;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString ExecutionPath;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString ParentExecutionPath;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString Phase;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString GraphName;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString ComponentName;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString DataType;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double DurationMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double SelfMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double InclusiveMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double FirstSeenTimeMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString FirstSeenTimeSource;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 InputCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 OutputCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 InputPoints = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 OutputPoints = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 WarningCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int32 ErrorCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    bool bCancelled = false;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString ThreadGroup;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString ThreadSource;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double QueueWaitMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double ExecutionMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double PrepareDataMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    double PostExecuteMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    bool bCacheHit = false;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString CacheMissReason;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 MemoryBeforeBytes = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 MemoryAfterBytes = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    int64 MemoryDeltaBytes = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString MemoryMeasurementMode;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler")
    FString MemoryScope;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    FString RunMode;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    FString WorldType;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    bool bIsPIE = false;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    bool bIsCooked = false;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    FString CellId;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    FString StreamingEvent;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    FString GenerateReason;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    int32 SpawnCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    int32 DestroyCount = 0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    double SpawnMs = 0.0;

    UPROPERTY(VisibleAnywhere, Category="PCG Profiler|Runtime")
    double DestroyMs = 0.0;
};
