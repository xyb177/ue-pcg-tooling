#include "PCGProfilerSubsystem.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Containers/Ticker.h"
#include "Algo/Sort.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformMemory.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "PCGSubsystem.h"
#include "PCGElement.h"
#include "PCGData.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGSpatialData.h"
#include "Data/PCGSurfaceData.h"

#include "PCGProfilerSubsystem_Internals.h"
#include "PCGProfilerSubsystem_Utils.h"

int64 UPCGProfilerSubsystem::GetProcessMemoryBytes()
{
    const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
    return static_cast<int64>(Stats.UsedPhysical);
}

void UPCGProfilerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (!bRuntimeWorldDelegatesBound)
    {
        FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UPCGProfilerSubsystem::HandleLevelAddedToWorld);
        FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UPCGProfilerSubsystem::HandleLevelRemovedFromWorld);
        bRuntimeWorldDelegatesBound = true;
    }
    StartRun(TEXT("DefaultRun"));
}

void UPCGProfilerSubsystem::Deinitialize()
{
    ClearRuntimeLifecycleBindings();
    if (bRuntimeWorldDelegatesBound)
    {
        FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
        FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
        bRuntimeWorldDelegatesBound = false;
    }
    CancelOneClickProfile(TEXT("deinitialize"));
    EndRun();
    Super::Deinitialize();
}

void UPCGProfilerSubsystem::StartRun(const FString& InRunName)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PCGProfiler_StartRun);

    bool bNeedsCarryoverHarvest = false;
    {
        FScopeLock Lock(&DataMutex);
        bNeedsCarryoverHarvest = RunStartUtc.GetTicks() > 0 && !bRunThreadSummaryFinalized;
    }

    if (bNeedsCarryoverHarvest)
    {
        HarvestFromPCGExecutionInspection_NoLock();
        PCGProfilerHarvest::SetInspectionEnabledOnAllPCGComponents(false);
        FScopeLock Lock(&DataMutex);
        if (RunStartUtc.GetTicks() > 0 && !bRunThreadSummaryFinalized)
        {
            FinalizeRunThreadSummary_NoLock();
        }
    }

    PCGProfilerHarvest::SetInspectionEnabledOnAllPCGComponents(bSamplingEnabled);

    FScopeLock Lock(&DataMutex);

    FinalizeRunThreadSummary_NoLock();

    CurrentRunName = InRunName.IsEmpty() ? TEXT("UnnamedRun") : InRunName;
    RunStartUtc = FDateTime::UtcNow();
    RunStartPlatformSeconds = FPlatformTime::Seconds();
    RunEndUtc = FDateTime();
    NodeStats.Reset();
    NodeEvents.Reset();
    LastGraphName.Reset();
    LastComponentName.Reset();
    bRunThreadSummaryFinalized = false;
    LastProcessMemoryBytes = GetProcessMemoryBytes();
    PeakProcessMemoryBytes = LastProcessMemoryBytes;
    ResetStreamingState_NoLock();
    RuntimeConvergedIdleTicks = 0;
    RuntimeLastComponentCount = -1;
    RuntimeLastComponentChangeAtSeconds = FPlatformTime::Seconds();
    RuntimeLastLifecycleEventAtSeconds = RuntimeLastComponentChangeAtSeconds;
    RuntimeLifecycleEventCount = 0;
    RuntimeStreamingEventCount = 0;
    RuntimeSpawnCountAccum = 0;
    RuntimeDestroyCountAccum = 0;
    RuntimeSpawnMsAccum = 0.0;
    RuntimeDestroyMsAccum = 0.0;
    RuntimeLastCellId.Reset();
    RuntimeLastStreamingEvent = TEXT("none");
    RuntimeLastGenerateReason = TEXT("unknown");
    RuntimeLastCellLoadAtSeconds = 0.0;
    RuntimeLastLoadedCellId.Reset();
    RuntimeGeneratedComponentKeysSeen.Reset();
    RefreshRuntimeLifecycleBindings();
}

void UPCGProfilerSubsystem::EndRun()
{
    // Harvest while inspection cache is still alive; DisableInspection clears pin-level cache data.
    HarvestFromPCGExecutionInspection_NoLock();
    PCGProfilerHarvest::SetInspectionEnabledOnAllPCGComponents(false);

    FScopeLock Lock(&DataMutex);
    if (RunStartUtc.GetTicks() > 0)
    {
        RunEndUtc = FDateTime::UtcNow();
    }
    FinalizeRunThreadSummary_NoLock();
}

FString UPCGProfilerSubsystem::StartRuntimeRun(const FString& OptionalRunName)
{
    FString RunName = OptionalRunName;
    if (RunName.IsEmpty())
    {
        RunName = FString::Printf(TEXT("RuntimeRun_%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
    }

    StartRun(RunName);
    return RunName;
}

bool UPCGProfilerSubsystem::EndRuntimeRunAndExport(const FString& OptionalAbsoluteOrRelativePath, FString& OutSavedPath)
{
    EndRun();
    return ExportJsonReport(OptionalAbsoluteOrRelativePath, OutSavedPath);
}

void UPCGProfilerSubsystem::RecordRuntimeLifecycleEvent(
    const FString& Phase,
    const FString& GenerateReason,
    const FString& CellId,
    const FString& StreamingEvent,
    int32 SpawnCount,
    int32 DestroyCount,
    double SpawnMs,
    double DestroyMs)
{
    const PCGProfilerRuntimeSemantic::FRunContext RuntimeContext = PCGProfilerRuntimeSemantic::ResolveRunContext();

    FPCGProfilerNodeEvent Event;
    Event.NodeId = TEXT("__runtime_lifecycle__");
    Event.NodeTitle = TEXT("__runtime_lifecycle__");
    Event.NodeClass = TEXT("RuntimeLifecycle");
    Event.SettingsClass = TEXT("RuntimeLifecycle");
    Event.Phase = Phase.IsEmpty() ? TEXT("runtime_lifecycle") : Phase;
    Event.InclusiveMs = FMath::Max(0.0, SpawnMs + DestroyMs);
    Event.SelfMs = Event.InclusiveMs;
    Event.DurationMs = Event.InclusiveMs;
    Event.ExecutionMs = Event.InclusiveMs;
    Event.ThreadGroup = TEXT("GameThread");
    Event.ThreadSource = TEXT("runtime_api");
    Event.FirstSeenTimeSource = TEXT("runtime_api");
    Event.RunMode = RuntimeContext.RunMode;
    Event.WorldType = RuntimeContext.WorldType;
    Event.bIsPIE = RuntimeContext.bIsPIE;
    Event.bIsCooked = RuntimeContext.bIsCooked;
    Event.CellId = CellId;
    Event.StreamingEvent = StreamingEvent.IsEmpty() ? TEXT("none") : StreamingEvent;
    Event.GenerateReason = GenerateReason.IsEmpty() ? TEXT("unknown") : GenerateReason;
    Event.SpawnCount = FMath::Max(0, SpawnCount);
    Event.DestroyCount = FMath::Max(0, DestroyCount);
    Event.SpawnMs = FMath::Max(0.0, SpawnMs);
    Event.DestroyMs = FMath::Max(0.0, DestroyMs);

    {
        FScopeLock Lock(&DataMutex);
        const double NowSeconds = FPlatformTime::Seconds();
        RuntimeLastLifecycleEventAtSeconds = NowSeconds;
        ++RuntimeLifecycleEventCount;
        if (!Event.StreamingEvent.Equals(TEXT("none"), ESearchCase::IgnoreCase))
        {
            ++RuntimeStreamingEventCount;
        }
        RuntimeSpawnCountAccum += Event.SpawnCount;
        RuntimeDestroyCountAccum += Event.DestroyCount;
        RuntimeSpawnMsAccum += Event.SpawnMs;
        RuntimeDestroyMsAccum += Event.DestroyMs;
        RuntimeLastCellId = Event.CellId;
        RuntimeLastStreamingEvent = Event.StreamingEvent;
        RuntimeLastGenerateReason = Event.GenerateReason;
    }

    RecordNodeEvent(Event);
}

void UPCGProfilerSubsystem::RefreshRuntimeLifecycleBindings()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PCGProfiler_RefreshRuntimeLifecycleBindings);

    TArray<TWeakObjectPtr<UPCGComponent>> Components;
    PCGProfilerRuntime::CollectAllPCGComponents(Components);

    for (const TWeakObjectPtr<UPCGComponent>& WeakComponent : Components)
    {
        UPCGComponent* Component = WeakComponent.Get();
        if (!Component || RuntimeBoundLifecycleComponents.Contains(Component))
        {
            continue;
        }

        Component->OnPCGGraphStartGeneratingDelegate.AddUObject(this, &UPCGProfilerSubsystem::HandlePCGGraphStartGenerating);
        Component->OnPCGGraphGeneratedDelegate.AddUObject(this, &UPCGProfilerSubsystem::HandlePCGGraphGenerated);
        Component->OnPCGGraphCleanedDelegate.AddUObject(this, &UPCGProfilerSubsystem::HandlePCGGraphCleaned);
        Component->OnPCGGraphCancelledDelegate.AddUObject(this, &UPCGProfilerSubsystem::HandlePCGGraphCancelled);
        RuntimeBoundLifecycleComponents.Add(Component);
    }
}

void UPCGProfilerSubsystem::ClearRuntimeLifecycleBindings()
{
    for (const TWeakObjectPtr<UPCGComponent>& WeakComponent : RuntimeBoundLifecycleComponents)
    {
        UPCGComponent* Component = WeakComponent.Get();
        if (!Component)
        {
            continue;
        }

        Component->OnPCGGraphStartGeneratingDelegate.RemoveAll(this);
        Component->OnPCGGraphGeneratedDelegate.RemoveAll(this);
        Component->OnPCGGraphCleanedDelegate.RemoveAll(this);
        Component->OnPCGGraphCancelledDelegate.RemoveAll(this);
    }

    RuntimeBoundLifecycleComponents.Reset();
}

void UPCGProfilerSubsystem::HandlePCGGraphStartGenerating(UPCGComponent* InComponent)
{
    const FString CellId = PCGProfilerRuntimeSemantic::BuildCellIdFromComponent(InComponent);
    const FString ComponentKey = InComponent ? InComponent->GetPathName() : FString();
    const double NowSeconds = FPlatformTime::Seconds();
    FString Reason = TEXT("player_triggered");
    {
        FScopeLock Lock(&DataMutex);
        if (!ComponentKey.IsEmpty())
        {
            RuntimeGeneratedComponentKeysSeen.Remove(ComponentKey);
        }
        if ((NowSeconds - RunStartPlatformSeconds) <= 5.0)
        {
            Reason = TEXT("initial_generation");
        }
        else if (RuntimeLastCellLoadAtSeconds > 0.0
            && (NowSeconds - RuntimeLastCellLoadAtSeconds) <= 5.0
            && !RuntimeLastLoadedCellId.IsEmpty()
            && (CellId.IsEmpty() || RuntimeLastLoadedCellId == CellId))
        {
            Reason = TEXT("streaming_return");
        }
    }
    RecordRuntimeLifecycleEvent(TEXT("GenerateStart"), Reason, CellId, TEXT("none"), 0, 0, 0.0, 0.0);
}

void UPCGProfilerSubsystem::HandlePCGGraphGenerated(UPCGComponent* InComponent)
{
    const FString ComponentKey = InComponent ? InComponent->GetPathName() : FString();
    {
        FScopeLock Lock(&DataMutex);
        if (!ComponentKey.IsEmpty())
        {
            if (RuntimeGeneratedComponentKeysSeen.Contains(ComponentKey))
            {
                return;
            }
            RuntimeGeneratedComponentKeysSeen.Add(ComponentKey);
        }
    }

    const FString CellId = PCGProfilerRuntimeSemantic::BuildCellIdFromComponent(InComponent);
    RecordRuntimeLifecycleEvent(TEXT("GenerateEnd"), TEXT("component_delegate"), CellId, TEXT("none"), 1, 0, 0.0, 0.0);
}

void UPCGProfilerSubsystem::HandlePCGGraphCleaned(UPCGComponent* InComponent)
{
    const FString CellId = PCGProfilerRuntimeSemantic::BuildCellIdFromComponent(InComponent);
    RecordRuntimeLifecycleEvent(TEXT("CleanupEnd"), TEXT("component_delegate"), CellId, TEXT("none"), 0, 1, 0.0, 0.0);
}

void UPCGProfilerSubsystem::HandlePCGGraphCancelled(UPCGComponent* InComponent)
{
    const FString CellId = PCGProfilerRuntimeSemantic::BuildCellIdFromComponent(InComponent);
    RecordRuntimeLifecycleEvent(TEXT("GenerateCancelled"), TEXT("component_delegate"), CellId, TEXT("none"), 0, 0, 0.0, 0.0);
}

void UPCGProfilerSubsystem::HandleLevelAddedToWorld(ULevel* InLevel, UWorld* InWorld)
{
    const FString CellId = PCGProfilerRuntimeSemantic::BuildCellIdFromLevel(InLevel);
    {
        FScopeLock Lock(&DataMutex);
        RuntimeLastCellLoadAtSeconds = FPlatformTime::Seconds();
        RuntimeLastLoadedCellId = CellId;
    }
    RecordRuntimeLifecycleEvent(TEXT("CellLoad"), TEXT("streaming"), CellId, TEXT("load"), 0, 0, 0.0, 0.0);
    RefreshRuntimeLifecycleBindings();
}

void UPCGProfilerSubsystem::HandleLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
    const FString CellId = PCGProfilerRuntimeSemantic::BuildCellIdFromLevel(InLevel);
    RecordRuntimeLifecycleEvent(TEXT("CellUnload"), TEXT("streaming"), CellId, TEXT("unload"), 0, 0, 0.0, 0.0);
}

void UPCGProfilerSubsystem::RecordNodeTiming(const FString& InNodeName, double DurationMs)
{
    if (InNodeName.IsEmpty() || DurationMs < 0.0)
    {
        return;
    }

    FPCGProfilerNodeEvent Event;
    Event.NodeId = InNodeName;
    Event.NodeTitle = InNodeName;
    Event.Phase = TEXT("Execute");
    Event.InclusiveMs = DurationMs;
    Event.SelfMs = DurationMs;
    Event.DurationMs = DurationMs;
    Event.ExecutionMs = DurationMs;
    Event.PrepareDataMs = 0.0;
    Event.PostExecuteMs = 0.0;
    Event.QueueWaitMs = 0.0;
    Event.ThreadGroup = IsInGameThread() ? TEXT("GameThread") : TEXT("Worker");
    Event.ThreadSource = TEXT("callsite_thread");
    Event.FirstSeenTimeSource = TEXT("estimated");
    RecordNodeEvent(Event);
}

void UPCGProfilerSubsystem::RecordNodeEvent(const FPCGProfilerNodeEvent& InEvent)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PCGProfiler_RecordNodeEvent);

    if (!bSamplingEnabled)
    {
        return;
    }

    if (InEvent.NodeTitle.IsEmpty() || InEvent.InclusiveMs < 0.0)
    {
        return;
    }

    FScopeLock Lock(&DataMutex);
    FPCGProfilerNodeEvent Event = InEvent;
    const int64 MemoryBeforeBytes = LastProcessMemoryBytes > 0 ? LastProcessMemoryBytes : GetProcessMemoryBytes();
    const int64 MemoryAfterBytes = GetProcessMemoryBytes();
    LastProcessMemoryBytes = MemoryAfterBytes;
    Event.MemoryBeforeBytes = MemoryBeforeBytes;
    Event.MemoryAfterBytes = MemoryAfterBytes;
    Event.MemoryDeltaBytes = MemoryAfterBytes - MemoryBeforeBytes;
    Event.MemoryMeasurementMode = (MemoryProfilingMode == EMemoryProfilingMode::AccurateDelta) ? TEXT("accurate_delta") : TEXT("fast_estimate");
    Event.MemoryScope = TEXT("process_estimate");
    if (MemoryProfilingMode == EMemoryProfilingMode::FastEstimate)
    {
        // Fast mode keeps overhead minimal and leaves measured delta as 0.
        Event.MemoryDeltaBytes = 0;
    }
    else
    {
        // Process memory sampling can be noisy and often near-zero at node granularity.
        // Blend with point-scale estimate when measured signal is much smaller than expected footprint.
        const int64 AbsMeasuredDelta = FMath::Abs(Event.MemoryDeltaBytes);
        const int64 EstimatedBytes = PCGProfilerScaleStats::EstimateMemoryBytes(Event.InputPoints, Event.OutputPoints);
        const int64 FallbackTriggerBytes = FMath::Max<int64>(4096, EstimatedBytes / 8);
        if (EstimatedBytes > 0 && AbsMeasuredDelta < FallbackTriggerBytes)
        {
            const bool bLikelyGrowth = Event.OutputPoints >= Event.InputPoints;
            Event.MemoryDeltaBytes = bLikelyGrowth ? EstimatedBytes : -EstimatedBytes;
            Event.MemoryMeasurementMode = TEXT("accurate_delta_with_scale_fallback");
        }
    }

    if (!Event.FirstSeenTimeSource.Equals(TEXT("timer_start"), ESearchCase::IgnoreCase) && RunStartUtc.GetTicks() > 0)
    {
        Event.FirstSeenTimeMs = static_cast<double>((FDateTime::UtcNow() - RunStartUtc).GetTotalMilliseconds());
        Event.FirstSeenTimeSource = TEXT("estimated_runtime");
    }

    if (Event.ParentExecutionPath.IsEmpty() && !Event.ExecutionPath.IsEmpty())
    {
        Event.ParentExecutionPath = ResolveParentExecutionPath(Event.ExecutionPath);
    }

    if (Event.ThreadGroup.IsEmpty())
    {
        Event.ThreadGroup = TEXT("Unknown");
        Event.ThreadSource = TEXT("missing_thread_info");
    }
    else
    {
        Event.ThreadGroup = PCGProfilerThreading::NormalizeThreadGroup(Event.ThreadGroup);
        if (Event.ThreadSource.IsEmpty())
        {
            Event.ThreadSource = TEXT("provided");
        }
    }

    Event.QueueWaitMs = FMath::Max(0.0, Event.QueueWaitMs);
    if (Event.ExecutionMs <= 0.0)
    {
        Event.ExecutionMs = FMath::Max(0.0, Event.DurationMs);
        if (Event.ExecutionMs <= 0.0)
        {
            Event.ExecutionMs = FMath::Max(0.0, Event.InclusiveMs);
        }
    }

    const FString NodeKey = BuildNodeKey(Event.NodeTitle, Event.NodeId, Event.GraphName);
    FNodeStats& Stats = NodeStats.FindOrAdd(NodeKey);
    if (!Event.bCacheHit)
    {
        const uint64 CurrentInputSignature = HashCombine(
            GetTypeHash(Event.InputCount),
            GetTypeHash(Event.InputPoints));
        if (Stats.bHasPreviousSample)
        {
            if (!Stats.PreviousSettingsClass.IsEmpty() && !Event.SettingsClass.IsEmpty() && Stats.PreviousSettingsClass != Event.SettingsClass)
            {
                Event.CacheMissReason = TEXT("version_change");
            }
            else if (Stats.PreviousInputSignature != CurrentInputSignature)
            {
                Event.CacheMissReason = TEXT("input_change");
            }
            else
            {
                Event.CacheMissReason = TEXT("unknown");
            }
        }
        else if (Event.CacheMissReason.IsEmpty())
        {
            Event.CacheMissReason = TEXT("unknown");
        }
    }
    else
    {
        Event.CacheMissReason.Empty();
    }

    Stats.AddSample(Event);
    NodeEvents.Add(Event);
    ++TotalEventsSeen;
    MaybeFlushEventsAndWarnings_NoLock();

    FCrossRunNodeStats& Historical = CrossRunStats.FindOrAdd(NodeKey);
    ++Historical.TotalCalls;
    Historical.InclusiveSamples.Add(Event.InclusiveMs);
    Historical.SelfSamples.Add(Event.SelfMs);

    if (!Event.GraphName.IsEmpty())
    {
        LastGraphName = Event.GraphName;
    }

    if (!Event.ComponentName.IsEmpty())
    {
        LastComponentName = Event.ComponentName;
    }
}

TArray<FPCGProfilerNodeAggregate> UPCGProfilerSubsystem::GetNodeAggregates() const
{
    FScopeLock Lock(&DataMutex);

    TArray<FPCGProfilerNodeAggregate> Result;
    Result.Reserve(NodeStats.Num());

    for (const TPair<FString, FNodeStats>& KV : NodeStats)
    {
        const FNodeStats& Stats = KV.Value;
        if (Stats.CallCount <= 0)
        {
            continue;
        }

        FPCGProfilerNodeAggregate& Aggregate = Result.AddDefaulted_GetRef();
        Aggregate.NodeName = Stats.NodeName;
        Aggregate.NodeId = Stats.NodeId;
        Aggregate.NodeClass = Stats.NodeClass;
        Aggregate.SettingsClass = Stats.SettingsClass;
        Aggregate.ExecutionPath = Stats.ExecutionPath;
        Aggregate.GraphName = Stats.GraphName;
        Aggregate.DataType = Stats.DataType;
        Aggregate.CallCount = Stats.CallCount;
        Aggregate.TotalMs = Stats.TotalMs;
        Aggregate.MinMs = (Stats.MinMs == TNumericLimits<double>::Max()) ? 0.0 : Stats.MinMs;
        Aggregate.MaxMs = Stats.MaxMs;
        Aggregate.AvgMs = Stats.TotalMs / static_cast<double>(Stats.CallCount);
        Aggregate.InclusiveMs = Aggregate.TotalMs;
        Aggregate.SelfMs = Stats.SelfTotalMs;
        Aggregate.FirstSeenTimeMs = Stats.bHasFirstSeen ? Stats.FirstSeenTimeMs : 0.0;
        Aggregate.FirstSeenTimeSource = Stats.bFirstSeenEstimated ? (Stats.bFirstSeenPrecise ? TEXT("mixed") : TEXT("estimated")) : TEXT("precise");
        Aggregate.SampleCount = Stats.Samples.Num();
        Aggregate.CancelledCount = Stats.CancelledCount;
        Aggregate.WarningCount = Stats.WarningCount;
        Aggregate.ErrorCount = Stats.ErrorCount;
        Aggregate.InputCountMax = Stats.InputCount;
        Aggregate.OutputCountMax = Stats.OutputCount;
        Aggregate.InputPointsMax = Stats.InputPoints;
        Aggregate.OutputPointsMax = Stats.OutputPoints;
        Aggregate.InputCountSum = Stats.InputCountSum;
        Aggregate.OutputCountSum = Stats.OutputCountSum;
        Aggregate.InputPointsSum = Stats.InputPointsSum;
        Aggregate.OutputPointsSum = Stats.OutputPointsSum;
        Aggregate.InputCountLast = Stats.InputCountLast;
        Aggregate.OutputCountLast = Stats.OutputCountLast;
        Aggregate.InputPointsLast = Stats.InputPointsLast;
        Aggregate.OutputPointsLast = Stats.OutputPointsLast;
        Aggregate.GameThreadMs = Stats.GameThreadMs;
        Aggregate.WorkerThreadMs = Stats.WorkerThreadMs;
        Aggregate.GameThreadCallCount = Stats.GameThreadCallCount;
        Aggregate.WorkerThreadCallCount = Stats.WorkerThreadCallCount;
        Aggregate.QueueWaitTotalMs = Stats.QueueWaitTotalMs;
        Aggregate.QueueWaitAvgMs = Stats.CallCount > 0 ? (Stats.QueueWaitTotalMs / static_cast<double>(Stats.CallCount)) : 0.0;
        Aggregate.ExecutionTotalMs = Stats.ExecutionTotalMs;
        Aggregate.CacheHitCount = Stats.CacheHitCount;
        Aggregate.CacheMissCount = Stats.CacheMissCount;
        const int32 CacheTotal = Stats.CacheHitCount + Stats.CacheMissCount;
        Aggregate.CacheHitRate = CacheTotal > 0 ? (static_cast<double>(Stats.CacheHitCount) / static_cast<double>(CacheTotal)) : 0.0;
        Aggregate.MissReasonParameterChangeCount = Stats.MissReasonParameterChangeCount;
        Aggregate.MissReasonInputChangeCount = Stats.MissReasonInputChangeCount;
        Aggregate.MissReasonVersionChangeCount = Stats.MissReasonVersionChangeCount;
        Aggregate.MissReasonUnknownCount = Stats.MissReasonUnknownCount;
        Aggregate.CacheReuseSavedMsEstimate = Stats.CacheReuseSavedMsEstimate;
        Aggregate.MemoryDeltaSumBytes = Stats.MemoryDeltaSumBytes;
        Aggregate.MemoryDeltaMaxBytes = (Stats.MemoryDeltaMaxBytes == TNumericLimits<int64>::Lowest()) ? 0 : Stats.MemoryDeltaMaxBytes;
        Aggregate.MemoryDeltaMinBytes = (Stats.MemoryDeltaMinBytes == TNumericLimits<int64>::Max()) ? 0 : Stats.MemoryDeltaMinBytes;
        Aggregate.MemoryDeltaLastBytes = Stats.MemoryDeltaLastBytes;
        Aggregate.MemoryDeltaAvgBytes = Stats.CallCount > 0 ? (static_cast<double>(Stats.MemoryDeltaSumBytes) / static_cast<double>(Stats.CallCount)) : 0.0;
        Aggregate.MemoryDeltaP95Bytes = ComputePercentile(Stats.MemoryDeltaSamples, 0.95);
        if (!Stats.MemoryDeltaSamples.IsEmpty())
        {
            double Mean = 0.0;
            for (const double Sample : Stats.MemoryDeltaSamples) { Mean += Sample; }
            Mean /= static_cast<double>(Stats.MemoryDeltaSamples.Num());
            double SumSq = 0.0;
            for (const double Sample : Stats.MemoryDeltaSamples)
            {
                const double D = Sample - Mean;
                SumSq += D * D;
            }
            const double StdDev = FMath::Sqrt(SumSq / static_cast<double>(Stats.MemoryDeltaSamples.Num()));
            Aggregate.MemoryDeltaCv = Mean > 0.0 ? (StdDev / Mean) : 0.0;
        }
        Aggregate.MemoryScope = TEXT("process_estimate");
        const double ThreadTotalMs = Stats.GameThreadMs + Stats.WorkerThreadMs + Stats.UnknownThreadMs;
        Aggregate.GameThreadRatio = ThreadTotalMs > 0.0 ? (Stats.GameThreadMs / ThreadTotalMs) : 0.0;
        Aggregate.WorkerThreadRatio = ThreadTotalMs > 0.0 ? (Stats.WorkerThreadMs / ThreadTotalMs) : 0.0;
        const double NodeSpanMs = Stats.QueueWaitTotalMs + Stats.ExecutionTotalMs;
        Aggregate.QueueWaitRatio = NodeSpanMs > 0.0 ? (Stats.QueueWaitTotalMs / NodeSpanMs) : 0.0;

        if (!Stats.Samples.IsEmpty())
        {
            double SumSq = 0.0;
            for (const double Sample : Stats.Samples)
            {
                const double Delta = Sample - Aggregate.AvgMs;
                SumSq += Delta * Delta;
            }

            Aggregate.StdDevMs = FMath::Sqrt(SumSq / static_cast<double>(Stats.Samples.Num()));
            Aggregate.DurationCv = Aggregate.AvgMs > 0.0 ? (Aggregate.StdDevMs / Aggregate.AvgMs) : 0.0;

            Aggregate.P50Ms = ComputePercentile(Stats.Samples, 0.50);
            Aggregate.P95Ms = ComputePercentile(Stats.Samples, 0.95);
        }

        if (!Stats.QueueWaitSamples.IsEmpty())
        {
            double Mean = 0.0;
            for (const double Sample : Stats.QueueWaitSamples) { Mean += Sample; }
            Mean /= static_cast<double>(Stats.QueueWaitSamples.Num());
            double SumSq = 0.0;
            for (const double Sample : Stats.QueueWaitSamples)
            {
                const double D = Sample - Mean;
                SumSq += D * D;
            }
            const double StdDev = FMath::Sqrt(SumSq / static_cast<double>(Stats.QueueWaitSamples.Num()));
            Aggregate.QueueWaitCv = Mean > 0.0 ? (StdDev / Mean) : 0.0;
        }

        if (!Stats.InputCountSamples.IsEmpty())
        {
            double Sum = 0.0;
            double SumSq = 0.0;
            for (const double Sample : Stats.InputCountSamples) { Sum += Sample; }
            Aggregate.InputCountAvg = Sum / static_cast<double>(Stats.InputCountSamples.Num());
            Aggregate.InputCountP95 = ComputePercentile(Stats.InputCountSamples, 0.95);
            for (const double Sample : Stats.InputCountSamples)
            {
                const double D = Sample - Aggregate.InputCountAvg;
                SumSq += D * D;
            }
            const double StdDev = FMath::Sqrt(SumSq / static_cast<double>(Stats.InputCountSamples.Num()));
            Aggregate.InputCountCv = Aggregate.InputCountAvg > 0.0 ? (StdDev / Aggregate.InputCountAvg) : 0.0;
        }

        if (!Stats.OutputCountSamples.IsEmpty())
        {
            double Sum = 0.0;
            double SumSq = 0.0;
            for (const double Sample : Stats.OutputCountSamples) { Sum += Sample; }
            Aggregate.OutputCountAvg = Sum / static_cast<double>(Stats.OutputCountSamples.Num());
            Aggregate.OutputCountP95 = ComputePercentile(Stats.OutputCountSamples, 0.95);
            for (const double Sample : Stats.OutputCountSamples)
            {
                const double D = Sample - Aggregate.OutputCountAvg;
                SumSq += D * D;
            }
            const double StdDev = FMath::Sqrt(SumSq / static_cast<double>(Stats.OutputCountSamples.Num()));
            Aggregate.OutputCountCv = Aggregate.OutputCountAvg > 0.0 ? (StdDev / Aggregate.OutputCountAvg) : 0.0;
        }

        if (!Stats.InputPointsSamples.IsEmpty())
        {
            double Sum = 0.0;
            double SumSq = 0.0;
            for (const double Sample : Stats.InputPointsSamples) { Sum += Sample; }
            Aggregate.InputPointsAvg = Sum / static_cast<double>(Stats.InputPointsSamples.Num());
            Aggregate.InputPointsP95 = ComputePercentile(Stats.InputPointsSamples, 0.95);
            for (const double Sample : Stats.InputPointsSamples)
            {
                const double D = Sample - Aggregate.InputPointsAvg;
                SumSq += D * D;
            }
            const double StdDev = FMath::Sqrt(SumSq / static_cast<double>(Stats.InputPointsSamples.Num()));
            Aggregate.InputPointsCv = Aggregate.InputPointsAvg > 0.0 ? (StdDev / Aggregate.InputPointsAvg) : 0.0;
        }

        if (!Stats.OutputPointsSamples.IsEmpty())
        {
            double Sum = 0.0;
            double SumSq = 0.0;
            for (const double Sample : Stats.OutputPointsSamples) { Sum += Sample; }
            Aggregate.OutputPointsAvg = Sum / static_cast<double>(Stats.OutputPointsSamples.Num());
            Aggregate.OutputPointsP95 = ComputePercentile(Stats.OutputPointsSamples, 0.95);
            for (const double Sample : Stats.OutputPointsSamples)
            {
                const double D = Sample - Aggregate.OutputPointsAvg;
                SumSq += D * D;
            }
            const double StdDev = FMath::Sqrt(SumSq / static_cast<double>(Stats.OutputPointsSamples.Num()));
            Aggregate.OutputPointsCv = Aggregate.OutputPointsAvg > 0.0 ? (StdDev / Aggregate.OutputPointsAvg) : 0.0;
        }
    }

    Result.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B)
    {
        return A.TotalMs > B.TotalMs;
    });

    return Result;
}

TArray<FPCGProfilerNodeEvent> UPCGProfilerSubsystem::GetNodeEvents() const
{
    FScopeLock Lock(&DataMutex);
    return NodeEvents;
}

TArray<FPCGProfilerNodeEvent> UPCGProfilerSubsystem::GetCriticalPathEvents(int32 MaxItems) const
{
    FScopeLock Lock(&DataMutex);

    TArray<FPCGProfilerNodeEvent> CriticalPathEvents;
    if (bStrictCriticalPathMode)
    {
        CriticalPathEvents = PCGProfilerCriticalPath::BuildStrictDagPath(NodeEvents, MaxItems);
    }
    else
    {
        CriticalPathEvents = NodeEvents;
        CriticalPathEvents.Sort([](const FPCGProfilerNodeEvent& A, const FPCGProfilerNodeEvent& B)
        {
            return A.InclusiveMs > B.InclusiveMs;
        });
        if (MaxItems > 0 && CriticalPathEvents.Num() > MaxItems)
        {
            CriticalPathEvents.SetNum(MaxItems);
        }
    }

    return CriticalPathEvents;
}

FString UPCGProfilerSubsystem::GetCurrentRunName() const
{
    FScopeLock Lock(&DataMutex);
    return CurrentRunName;
}

int32 UPCGProfilerSubsystem::GetActivePCGComponentCount() const
{
    return PCGProfilerRuntime::CountActivePCGComponents();
}

bool UPCGProfilerSubsystem::IsRunIdle() const
{
    return GetActivePCGComponentCount() == 0;
}

bool UPCGProfilerSubsystem::IsRunConverged(double StableWindowSeconds, int32 RequiredIdleTicks)
{
    const double SafeStableWindowSeconds = FMath::Max(0.2, StableWindowSeconds);
    const int32 SafeRequiredIdleTicks = FMath::Max(1, RequiredIdleTicks);
    const double NowSeconds = FPlatformTime::Seconds();

    int32 CurrentComponentCount = 0;
    PCGProfilerRuntime::ResolvePrimaryPCGWorld(&CurrentComponentCount);
    const bool bIdleNow = (GetActivePCGComponentCount() == 0);

    FScopeLock Lock(&DataMutex);
    if (RuntimeLastComponentCount != CurrentComponentCount)
    {
        RuntimeLastComponentCount = CurrentComponentCount;
        RuntimeLastComponentChangeAtSeconds = NowSeconds;
    }

    RuntimeConvergedIdleTicks = bIdleNow ? (RuntimeConvergedIdleTicks + 1) : 0;

    const bool bIdleStable = RuntimeConvergedIdleTicks >= SafeRequiredIdleTicks;
    const bool bComponentStable = (NowSeconds - RuntimeLastComponentChangeAtSeconds) >= SafeStableWindowSeconds;
    const bool bLifecycleQuiet = (NowSeconds - RuntimeLastLifecycleEventAtSeconds) >= SafeStableWindowSeconds;
    return bIdleStable && bComponentStable && bLifecycleQuiet;
}

bool UPCGProfilerSubsystem::WaitForRunComplete(double TimeoutSeconds, double PollIntervalSeconds)
{
    const double SafeTimeoutSeconds = FMath::Max(0.0, TimeoutSeconds);
    const double SafePollIntervalSeconds = FMath::Max(0.001, PollIntervalSeconds);
    const double StartSeconds = FPlatformTime::Seconds();

    while (true)
    {
        if (IsRunConverged(2.0, 3))
        {
            return true;
        }

        const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
        if (Elapsed >= SafeTimeoutSeconds)
        {
            return false;
        }

        FPlatformProcess::Sleep(static_cast<float>(SafePollIntervalSeconds));
    }
}

bool UPCGProfilerSubsystem::RunOneClickProfile(double TimeoutSeconds, double PollIntervalSeconds, const FString& OptionalOutputPath)
{
    if (bOneClickInProgress)
    {
        UE_LOG(LogTemp, Warning, TEXT("PCGProfiler OneClick already in progress: %s"), *OneClickRunName);
        return false;
    }

    const double SafeTimeoutSeconds = FMath::Max(1.0, TimeoutSeconds);
    const double SafePollIntervalSeconds = FMath::Max(0.01, PollIntervalSeconds);

    OneClickRunName = FString::Printf(TEXT("Run_%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
    OneClickOutputPath = OptionalOutputPath;
    OneClickTimeoutSeconds = SafeTimeoutSeconds;
    OneClickPollIntervalSeconds = SafePollIntervalSeconds;
    OneClickStartedAtSeconds = FPlatformTime::Seconds();
    OneClickLastPollAtSeconds = OneClickStartedAtSeconds;
    OneClickIdleStableTicks = 0;
    bOneClickObservedActiveWork = false;
    bOneClickInProgress = true;

    StartRun(OneClickRunName);

    int32 TargetWorldComponentCount = 0;
    UWorld* TargetWorld = nullptr;
    if (bBatchInProgress && !BatchLockedWorldPath.IsEmpty())
    {
        TargetWorld = PCGProfilerRuntime::ResolveWorldByPath(BatchLockedWorldPath);
        if (TargetWorld)
        {
            TargetWorldComponentCount = PCGProfilerRuntime::CountPCGComponentsInWorld(TargetWorld);
        }
    }
    if (!TargetWorld)
    {
        TargetWorld = PCGProfilerRuntime::ResolvePrimaryPCGWorld(&TargetWorldComponentCount);
    }

    UPCGSubsystem* PCGSubsystem = TargetWorld ? UPCGSubsystem::GetInstance(TargetWorld) : nullptr;
    if (!PCGSubsystem)
    {
        bOneClickInProgress = false;
        UE_LOG(LogTemp, Warning, TEXT("PCGProfiler OneClick failed: could not resolve UPCGSubsystem."));
        return false;
    }

    OneClickWorldPath = TargetWorld ? TargetWorld->GetPathName() : FString();
    OneClickComponentScopeKeys.Reset();
    for (TActorIterator<AActor> It(TargetWorld); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor)
        {
            continue;
        }

        TInlineComponentArray<UPCGComponent*> PCGComponents;
        Actor->GetComponents(PCGComponents);
        for (UPCGComponent* Component : PCGComponents)
        {
            const FString ScopeKey = PCGProfilerRuntime::BuildComponentScopeKey(TargetWorld, Component);
            if (!ScopeKey.IsEmpty())
            {
                OneClickComponentScopeKeys.Add(ScopeKey);
            }
        }
    }

    PCGSubsystem->CleanupAllPCGComponents(/*bPurge=*/bOneClickUsePurge);
    RecordRuntimeLifecycleEvent(TEXT("CleanupStart"), TEXT("explicit_one_click"), TEXT(""), TEXT("none"), 0, 0, 0.0, 0.0);
    PCGSubsystem->GenerateAllPCGComponents(/*bForce=*/true);
    if (TargetWorldComponentCount > 0)
    {
        bOneClickObservedActiveWork = true;
    }
    RecordRuntimeLifecycleEvent(TEXT("GenerateStart"), TEXT("explicit_one_click"), TEXT(""), TEXT("none"), 0, 0, 0.0, 0.0);

    OneClickTickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UPCGProfilerSubsystem::TickOneClickProfile),
        static_cast<float>(OneClickPollIntervalSeconds));

    UE_LOG(LogTemp, Display, TEXT("PCGProfiler OneClick started: run=%s world=%s components=%d scoped_components=%d mode=UPCGSubsystem.CleanupAll(purge=%s)+GenerateAll timeout=%.3fs poll=%.3fs"),
        *OneClickRunName,
        *GetNameSafe(TargetWorld),
        TargetWorldComponentCount,
        OneClickComponentScopeKeys.Num(),
        bOneClickUsePurge ? TEXT("true") : TEXT("false"),
        OneClickTimeoutSeconds,
        OneClickPollIntervalSeconds);
    return true;
}

bool UPCGProfilerSubsystem::RunBatchProfile(int32 Iterations, double TimeoutSeconds, double PollIntervalSeconds)
{
    if (bOneClickInProgress || bBatchInProgress)
    {
        UE_LOG(LogTemp, Warning, TEXT("PCGProfiler Batch cannot start: run/batch already in progress."));
        return false;
    }

    const int32 SafeIterations = FMath::Max(1, Iterations);
    BatchTotalIterations = SafeIterations;
    BatchRemainingIterations = SafeIterations;
    BatchTimeoutSeconds = FMath::Max(1.0, TimeoutSeconds);
    BatchPollIntervalSeconds = FMath::Max(0.01, PollIntervalSeconds);
    BatchCooldownSeconds = 3.0;
    bBatchUsePurge = true;
    BatchPreStartMaxWaitSeconds = 120.0;
    BatchPreStartRequiredIdleTicks = 3;
    BatchPreStartIdleTicks = 0;
    BatchPreStartLastComponentCount = -1;
    BatchPreStartComponentStableWindowSeconds = 3.0;
    BatchPreStartLastComponentChangeAtSeconds = 0.0;
    BatchPreStartWaitStartedAtSeconds = FPlatformTime::Seconds();
    int32 InitialComponentCount = 0;
    UWorld* LockedWorld = PCGProfilerRuntime::ResolvePrimaryPCGWorld(&InitialComponentCount);
    BatchLockedWorldPath = LockedWorld ? LockedWorld->GetPathName() : FString();
    BatchLockedWorldInitialComponentCount = InitialComponentCount;
    bBatchInProgress = true;

    UE_LOG(LogTemp, Display, TEXT("PCGProfiler Batch start: iterations=%d timeout=%.3fs poll=%.3fs cooldown=%.1fs purge=%s locked_world=%s initial_components=%d"),
        BatchTotalIterations, BatchTimeoutSeconds, BatchPollIntervalSeconds, BatchCooldownSeconds,
        bBatchUsePurge ? TEXT("true") : TEXT("false"),
        BatchLockedWorldPath.IsEmpty() ? TEXT("<none>") : *BatchLockedWorldPath,
        BatchLockedWorldInitialComponentCount);

    bOneClickUsePurge = bBatchUsePurge;
    const bool bStarted = RunOneClickProfile(BatchTimeoutSeconds, BatchPollIntervalSeconds, FString());
    if (!bStarted)
    {
        bBatchInProgress = false;
        BatchTotalIterations = 0;
        BatchRemainingIterations = 0;
        BatchLockedWorldPath.Reset();
        BatchLockedWorldInitialComponentCount = -1;
    }

    return bStarted;
}

void UPCGProfilerSubsystem::CancelOneClickProfile(const FString& Reason)
{
    if (!bOneClickInProgress && !bBatchInProgress)
    {
        return;
    }

    if (bBatchInProgress)
    {
        UE_LOG(LogTemp, Warning, TEXT("PCGProfiler Batch cancelled: reason=%s"), *Reason);
    }

    bBatchInProgress = false;
    BatchTotalIterations = 0;
    BatchRemainingIterations = 0;
    BatchLockedWorldPath.Reset();
    BatchLockedWorldInitialComponentCount = -1;
    BatchPreStartIdleTicks = 0;
    BatchPreStartLastComponentCount = -1;
    BatchPreStartLastComponentChangeAtSeconds = 0.0;
    BatchPreStartWaitStartedAtSeconds = 0.0;
    if (BatchDelayTickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(BatchDelayTickHandle);
        BatchDelayTickHandle.Reset();
    }

    FinishOneClickProfile(Reason.IsEmpty() ? TEXT("cancelled") : Reason, false);
}

void UPCGProfilerSubsystem::SetMemoryProfilingModeAccurate(bool bAccurate)
{
    FScopeLock Lock(&DataMutex);
    MemoryProfilingMode = bAccurate ? EMemoryProfilingMode::AccurateDelta : EMemoryProfilingMode::FastEstimate;
}

void UPCGProfilerSubsystem::SetSamplingEnabled(bool bEnabled)
{
    {
        FScopeLock Lock(&DataMutex);
        bSamplingEnabled = bEnabled;
    }

    PCGProfilerHarvest::SetInspectionEnabledOnAllPCGComponents(bEnabled);
}

bool UPCGProfilerSubsystem::IsSamplingEnabled() const
{
    FScopeLock Lock(&DataMutex);
    return bSamplingEnabled;
}

bool UPCGProfilerSubsystem::IsMemoryProfilingModeAccurate() const
{
    FScopeLock Lock(&DataMutex);
    return MemoryProfilingMode == EMemoryProfilingMode::AccurateDelta;
}

bool UPCGProfilerSubsystem::TickOneClickProfile(float DeltaTime)
{
    if (!bOneClickInProgress)
    {
        return false;
    }

    const double NowSeconds = FPlatformTime::Seconds();
    const double ElapsedSeconds = NowSeconds - OneClickStartedAtSeconds;

    if (ElapsedSeconds >= OneClickTimeoutSeconds)
    {
        FinishOneClickProfile(TEXT("timeout"), true);
        return false;
    }

    const bool bIdle = IsRunConverged(2.0, 3);
    const int32 ActiveComponentsNow = GetActivePCGComponentCount();
    if (ActiveComponentsNow > 0)
    {
        bOneClickObservedActiveWork = true;
    }

    if (bIdle)
    {
        ++OneClickIdleStableTicks;
    }
    else
    {
        OneClickIdleStableTicks = 0;
    }

    if (bOneClickObservedActiveWork && OneClickIdleStableTicks >= 3)
    {
        RecordRuntimeLifecycleEvent(TEXT("GenerateEnd"), TEXT("settled"), TEXT(""), TEXT("none"), 0, 0, 0.0, 0.0);
        FinishOneClickProfile(TEXT("stable_by_cpp_idle"), false);
        return false;
    }

    if ((NowSeconds - OneClickLastPollAtSeconds) >= 5.0)
    {
        OneClickLastPollAtSeconds = NowSeconds;
        UE_LOG(LogTemp, Display, TEXT("PCGProfiler OneClick waiting: run=%s elapsed=%.1fs active_components=%d idle_ticks=%d lifecycle_events=%d"),
            *OneClickRunName,
            ElapsedSeconds,
            ActiveComponentsNow,
            OneClickIdleStableTicks,
            RuntimeLifecycleEventCount);
    }

    return true;
}

void UPCGProfilerSubsystem::FinishOneClickProfile(const FString& Reason, bool bDidTimeout)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PCGProfiler_FinishOneClickProfile);

    if (!bOneClickInProgress)
    {
        return;
    }

    const FString RunName = OneClickRunName;
    const FString OutputPath = OneClickOutputPath;
    const double ElapsedSeconds = FPlatformTime::Seconds() - OneClickStartedAtSeconds;

    if (OneClickTickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(OneClickTickHandle);
        OneClickTickHandle.Reset();
    }

    EndRun();

    FString SavedPath;
    const bool bExported = ExportJsonReport(OutputPath, SavedPath);
    UE_LOG(LogTemp, Display, TEXT("PCGProfiler OneClick finished: run=%s reason=%s timeout=%s elapsed=%.1fs exported=%s path=%s"),
        *RunName,
        *Reason,
        bDidTimeout ? TEXT("true") : TEXT("false"),
        ElapsedSeconds,
        bExported ? TEXT("true") : TEXT("false"),
        bExported ? *SavedPath : TEXT("<none>"));

    bOneClickInProgress = false;
    OneClickRunName.Reset();
    OneClickOutputPath.Reset();
    OneClickWorldPath.Reset();
    OneClickComponentScopeKeys.Reset();
    OneClickTimeoutSeconds = 600.0;
    OneClickPollIntervalSeconds = 0.25;
    bOneClickUsePurge = false;
    OneClickStartedAtSeconds = 0.0;
    OneClickLastPollAtSeconds = 0.0;
    OneClickIdleStableTicks = 0;
    bOneClickObservedActiveWork = false;

    if (bBatchInProgress)
    {
        --BatchRemainingIterations;

        const bool bRunFailed = bDidTimeout || !bExported;
        if (bRunFailed)
        {
            UE_LOG(LogTemp, Warning, TEXT("PCGProfiler Batch stopped early: reason=%s remaining=%d"),
                *Reason, BatchRemainingIterations);
            bBatchInProgress = false;
            BatchTotalIterations = 0;
            BatchRemainingIterations = 0;
            BatchLockedWorldPath.Reset();
            BatchLockedWorldInitialComponentCount = -1;
            if (BatchDelayTickHandle.IsValid())
            {
                FTSTicker::GetCoreTicker().RemoveTicker(BatchDelayTickHandle);
                BatchDelayTickHandle.Reset();
            }
            return;
        }

        if (BatchRemainingIterations > 0)
        {
            const int32 Completed = BatchTotalIterations - BatchRemainingIterations;
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler Batch progress: completed=%d/%d, next iteration in %.1fs..."),
                Completed, BatchTotalIterations, BatchCooldownSeconds);

            if (BatchDelayTickHandle.IsValid())
            {
                FTSTicker::GetCoreTicker().RemoveTicker(BatchDelayTickHandle);
                BatchDelayTickHandle.Reset();
            }

            BatchPreStartWaitStartedAtSeconds = FPlatformTime::Seconds();
            BatchPreStartIdleTicks = 0;
            BatchPreStartLastComponentCount = -1;
            BatchPreStartLastComponentChangeAtSeconds = BatchPreStartWaitStartedAtSeconds;

            BatchDelayTickHandle = FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateLambda([this](float)
                {
                    const double NowSeconds = FPlatformTime::Seconds();
                    const double PreStartElapsed = NowSeconds - BatchPreStartWaitStartedAtSeconds;
                    if (PreStartElapsed >= BatchPreStartMaxWaitSeconds)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("PCGProfiler Batch pre-start gate timeout after %.1fs."), PreStartElapsed);
                        bBatchInProgress = false;
                        BatchTotalIterations = 0;
                        BatchRemainingIterations = 0;
                        BatchLockedWorldPath.Reset();
                        BatchLockedWorldInitialComponentCount = -1;
                        BatchDelayTickHandle.Reset();
                        return false;
                    }

                    int32 CurrentComponentCount = 0;
                    UWorld* CurrentWorld = PCGProfilerRuntime::ResolvePrimaryPCGWorld(&CurrentComponentCount);
                    const FString CurrentWorldPath = CurrentWorld ? CurrentWorld->GetPathName() : FString();
                    const int32 ActiveComponents = GetActivePCGComponentCount();
                    const bool bIdle = IsRunConverged(2.0, BatchPreStartRequiredIdleTicks);
                    BatchPreStartIdleTicks = bIdle ? (BatchPreStartIdleTicks + 1) : 0;

                    if (BatchPreStartLastComponentCount != CurrentComponentCount)
                    {
                        BatchPreStartLastComponentCount = CurrentComponentCount;
                        BatchPreStartLastComponentChangeAtSeconds = NowSeconds;
                    }

                    const bool bComponentStable = (NowSeconds - BatchPreStartLastComponentChangeAtSeconds) >= BatchPreStartComponentStableWindowSeconds;
                    const bool bIdleStable = BatchPreStartIdleTicks >= BatchPreStartRequiredIdleTicks;
                    const bool bWorldLocked = BatchLockedWorldPath.IsEmpty() || CurrentWorldPath == BatchLockedWorldPath;

                    if (!bWorldLocked)
                    {
                        UE_LOG(LogTemp, Display, TEXT("PCGProfiler Batch pre-start waiting: world changed (%s -> %s)"),
                            *BatchLockedWorldPath, *CurrentWorldPath);
                        return true;
                    }

                    if (!bIdleStable || !bComponentStable)
                    {
                        return true;
                    }

                    BatchDelayTickHandle.Reset();
                    bOneClickUsePurge = bBatchUsePurge;
                    const bool bNextStarted = RunOneClickProfile(BatchTimeoutSeconds, BatchPollIntervalSeconds, FString());
                    if (!bNextStarted)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("PCGProfiler Batch failed to start next iteration after pre-start gate."));
                        bBatchInProgress = false;
                        BatchTotalIterations = 0;
                        BatchRemainingIterations = 0;
                        BatchLockedWorldPath.Reset();
                        BatchLockedWorldInitialComponentCount = -1;
                    }
                    return false;
                }),
                static_cast<float>(BatchCooldownSeconds));
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("PCGProfiler Batch finished: completed=%d/%d"),
            BatchTotalIterations, BatchTotalIterations);
        bBatchInProgress = false;
        BatchTotalIterations = 0;
        BatchRemainingIterations = 0;
        BatchLockedWorldPath.Reset();
        BatchLockedWorldInitialComponentCount = -1;
        BatchPreStartIdleTicks = 0;
        BatchPreStartLastComponentCount = -1;
        BatchPreStartLastComponentChangeAtSeconds = 0.0;
        BatchPreStartWaitStartedAtSeconds = 0.0;
        if (BatchDelayTickHandle.IsValid())
        {
            FTSTicker::GetCoreTicker().RemoveTicker(BatchDelayTickHandle);
            BatchDelayTickHandle.Reset();
        }
    }
}


