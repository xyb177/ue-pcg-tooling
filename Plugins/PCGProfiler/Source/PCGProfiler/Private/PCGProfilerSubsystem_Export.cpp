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

void UPCGProfilerSubsystem::SetStrictCriticalPathMode(bool bInEnabled)
{
    FScopeLock Lock(&DataMutex);
    bStrictCriticalPathMode = bInEnabled;
}

bool UPCGProfilerSubsystem::IsStrictCriticalPathModeEnabled() const
{
    FScopeLock Lock(&DataMutex);
    return bStrictCriticalPathMode;
}

bool UPCGProfilerSubsystem::ExportJsonReport(const FString& OptionalAbsoluteOrRelativePath, FString& OutSavedPath) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PCGProfiler_ExportJsonReport);

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    bool bNeedHarvest = false;
    {
        FScopeLock Lock(&DataMutex);
        bNeedHarvest = NodeEvents.IsEmpty();
    }

    if (bNeedHarvest)
    {
        UPCGProfilerSubsystem* MutableThis = const_cast<UPCGProfilerSubsystem*>(this);
        MutableThis->HarvestFromPCGExecutionInspection_NoLock();
        UE_LOG(LogTemp, Display, TEXT("PCGProfiler Harvest triggered because NodeEvents was empty."));
    }

    FDateTime LocalRunStart;
    FDateTime LocalRunEnd;
    FString LocalRunName;
    TArray<FPCGProfilerNodeAggregate> Aggregates;
    TArray<FPCGProfilerNodeEvent> LocalEvents;
    TMap<FString, FCrossRunNodeStats> LocalCrossRunStats;
    TArray<FRunThreadSummary> LocalRunThreadHistory;
    bool bLocalStrictCriticalPathMode = false;
    bool bLocalRunThreadSummaryFinalized = false;
    TArray<FString> LocalFlushedChunkPaths;
    int64 LocalTotalEventsSeen = 0;
    int64 LocalTotalEventsFlushed = 0;
    int64 LocalTotalEventsDropped = 0;
    int32 LocalFlushCount = 0;
    int32 LocalMemoryWarningCount = 0;
    int64 LocalPeakProcessMemoryBytes = 0;
    int32 LocalEventsInMemory = 0;
    int32 LocalRuntimeLifecycleEventCount = 0;
    int32 LocalRuntimeStreamingEventCount = 0;
    int64 LocalRuntimeSpawnCountAccum = 0;
    int64 LocalRuntimeDestroyCountAccum = 0;
    double LocalRuntimeSpawnMsAccum = 0.0;
    double LocalRuntimeDestroyMsAccum = 0.0;
    FString LocalRuntimeLastCellId;
    FString LocalRuntimeLastStreamingEvent;
    FString LocalRuntimeLastGenerateReason;

    {
        FScopeLock Lock(&DataMutex);
        LocalRunStart = RunStartUtc;
        LocalRunEnd = (RunEndUtc.GetTicks() > 0) ? RunEndUtc : FDateTime::UtcNow();
        LocalRunName = CurrentRunName;
        LocalEvents = NodeEvents;
        LocalCrossRunStats = CrossRunStats;
        LocalRunThreadHistory = RunThreadHistory;
        bLocalStrictCriticalPathMode = bStrictCriticalPathMode;
        bLocalRunThreadSummaryFinalized = bRunThreadSummaryFinalized;
        LocalEventsInMemory = NodeEvents.Num();
        LocalFlushedChunkPaths = FlushedChunkPaths;
        LocalTotalEventsSeen = TotalEventsSeen;
        LocalTotalEventsFlushed = TotalEventsFlushed;
        LocalTotalEventsDropped = TotalEventsDropped;
        LocalFlushCount = FlushCount;
        LocalMemoryWarningCount = MemoryWarningCount;
        LocalPeakProcessMemoryBytes = PeakProcessMemoryBytes;
        LocalRuntimeLifecycleEventCount = RuntimeLifecycleEventCount;
        LocalRuntimeStreamingEventCount = RuntimeStreamingEventCount;
        LocalRuntimeSpawnCountAccum = RuntimeSpawnCountAccum;
        LocalRuntimeDestroyCountAccum = RuntimeDestroyCountAccum;
        LocalRuntimeSpawnMsAccum = RuntimeSpawnMsAccum;
        LocalRuntimeDestroyMsAccum = RuntimeDestroyMsAccum;
        LocalRuntimeLastCellId = RuntimeLastCellId;
        LocalRuntimeLastStreamingEvent = RuntimeLastStreamingEvent;
        LocalRuntimeLastGenerateReason = RuntimeLastGenerateReason;
    }

    if (!LocalFlushedChunkPaths.IsEmpty())
    {
        for (const FString& ChunkPath : LocalFlushedChunkPaths)
        {
            TArray<FString> Lines;
            if (!FFileHelper::LoadFileToStringArray(Lines, *ChunkPath))
            {
                continue;
            }

            for (const FString& Line : Lines)
            {
                if (Line.IsEmpty())
                {
                    continue;
                }

                FPCGProfilerNodeEvent ParsedEvent;
                if (PCGProfilerSerialization::ParseEventJsonLine(Line, ParsedEvent))
                {
                    LocalEvents.Add(MoveTemp(ParsedEvent));
                }
            }
        }
    }

    Aggregates = GetNodeAggregates();

    struct FPhaseStats
    {
        int32 CallCount = 0;
        double TotalMs = 0.0;
        double MaxMs = 0.0;
    };

    struct FThreadLoadStats
    {
        int32 CallCount = 0;
        double TotalMs = 0.0;
    };

    TMap<FString, FPhaseStats> PhaseBuckets;
    TMap<FString, FPCGProfilerNodeEvent> PathToEvent;
    TMap<FString, FEdgeStats> EdgeStatsByKey;
    TMap<FString, double> NodeChildInclusiveMs;
    TSet<FString> ChildNodePaths;
    TMap<FString, FThreadLoadStats> ThreadLoadBuckets;

    for (const FPCGProfilerNodeEvent& Event : LocalEvents)
    {
        FPhaseStats& Phase = PhaseBuckets.FindOrAdd(Event.Phase.IsEmpty() ? TEXT("Execute") : Event.Phase);
        ++Phase.CallCount;
        Phase.TotalMs += Event.InclusiveMs;
        Phase.MaxMs = FMath::Max(Phase.MaxMs, Event.InclusiveMs);

        if (!Event.ExecutionPath.IsEmpty())
        {
            PathToEvent.Add(Event.ExecutionPath, Event);
        }

        const FString ThreadGroup = PCGProfilerThreading::NormalizeThreadGroup(Event.ThreadGroup);
        FThreadLoadStats& ThreadStats = ThreadLoadBuckets.FindOrAdd(ThreadGroup);
        ++ThreadStats.CallCount;
        ThreadStats.TotalMs += Event.InclusiveMs;
    }

    for (const FPCGProfilerNodeEvent& Event : LocalEvents)
    {
        if (Event.ParentExecutionPath.IsEmpty())
        {
            continue;
        }

        const FPCGProfilerNodeEvent* ParentEvent = PathToEvent.Find(Event.ParentExecutionPath);
        if (!ParentEvent)
        {
            continue;
        }

        const FString EdgeKey = BuildEdgeKey(Event.ParentExecutionPath, Event.ExecutionPath, Event.GraphName);
        FEdgeStats& EdgeStats = EdgeStatsByKey.FindOrAdd(EdgeKey);
        EdgeStats.AddSample(Event, *ParentEvent);

        const FString ParentNodeKey = BuildNodeKey(ParentEvent->NodeTitle, ParentEvent->NodeId, ParentEvent->GraphName);
        NodeChildInclusiveMs.FindOrAdd(ParentNodeKey) += Event.InclusiveMs;
        ChildNodePaths.Add(Event.ExecutionPath);
    }

    Root->SetStringField(TEXT("run_name"), LocalRunName);
    Root->SetStringField(TEXT("schema_version"), TEXT("2.5.0"));
    Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
    Root->SetStringField(TEXT("platform"), FPlatformProperties::IniPlatformName());
    Root->SetStringField(TEXT("build_config"), LexToString(FApp::GetBuildConfiguration()));
    Root->SetStringField(TEXT("graph_name"), LastGraphName);
    Root->SetStringField(TEXT("component_name"), LastComponentName);
    Root->SetStringField(TEXT("percentile_method"), TEXT("linear_interpolation"));
    Root->SetStringField(TEXT("memory_scope"), TEXT("process_estimate"));
    Root->SetBoolField(TEXT("sampling_enabled"), IsSamplingEnabled());
    const PCGProfilerRuntimeSemantic::FRunContext RuntimeContext = PCGProfilerRuntimeSemantic::ResolveRunContext();
    UWorld* ContextWorld = RuntimeContext.ContextWorld;

    FString MapName = FApp::GetProjectName();
    FString MapPath;
    if (ContextWorld)
    {
        const UPackage* WorldPackage = ContextWorld->GetOutermost();
        if (WorldPackage)
        {
            MapPath = WorldPackage->GetName();
        }
        MapName = ContextWorld->GetMapName();
        const FString StreamingPrefix = ContextWorld->StreamingLevelsPrefix;
        if (!StreamingPrefix.IsEmpty())
        {
            MapName.RemoveFromStart(StreamingPrefix);
        }
    }
    Root->SetStringField(TEXT("map_name"), MapName);
    Root->SetStringField(TEXT("map_path"), MapPath);
    Root->SetStringField(TEXT("run_mode"), RuntimeContext.RunMode);
    Root->SetStringField(TEXT("world_type"), RuntimeContext.WorldType);
    Root->SetBoolField(TEXT("is_pie"), RuntimeContext.bIsPIE);
    Root->SetBoolField(TEXT("is_cooked"), RuntimeContext.bIsCooked);
    Root->SetStringField(TEXT("cell_id"), LocalRuntimeLastCellId);
    Root->SetStringField(TEXT("streaming_event"), LocalRuntimeLastStreamingEvent.IsEmpty() ? TEXT("none") : LocalRuntimeLastStreamingEvent);
    Root->SetStringField(TEXT("generate_reason"), LocalRuntimeLastGenerateReason.IsEmpty() ? TEXT("unknown") : LocalRuntimeLastGenerateReason);
    Root->SetStringField(TEXT("run_start_utc"), LocalRunStart.ToIso8601());
    Root->SetStringField(TEXT("run_end_utc"), LocalRunEnd.ToIso8601());
    Root->SetNumberField(TEXT("run_duration_ms"), static_cast<double>((LocalRunEnd - LocalRunStart).GetTotalMilliseconds()));
    Root->SetNumberField(TEXT("node_count"), Aggregates.Num());

    int64 TotalInputPointCount = 0;
    int64 TotalOutputPointCount = 0;
    int64 TotalEstimatedMemoryBytes = 0;
    int64 PeakEstimatedMemoryBytes = 0;
    FString PeakEstimatedMemoryNodeId;
    FString PeakEstimatedMemoryNodeName;
    TMap<FString, int64> PhaseInputPoints;
    TMap<FString, int64> PhaseOutputPoints;
    TMap<FString, int64> ThreadInputPoints;
    TMap<FString, int64> ThreadOutputPoints;
    TMap<FString, int64> TypeInputPoints;
    TMap<FString, int64> TypeOutputPoints;
    TMap<FString, int64> TypeEstimatedMemoryBytes;
    TMap<FString, int32> TypeNodeCount;
    struct FNodeNonZeroStats
    {
        int32 Samples = 0;
        int32 InputCountNonZero = 0;
        int32 OutputCountNonZero = 0;
        int32 InputPointsNonZero = 0;
        int32 OutputPointsNonZero = 0;
        int32 DurationNonZero = 0;
    };
    TMap<FString, FNodeNonZeroStats> NodeNonZeroStatsByKey;
    int64 PeakNodeOutputPoints = 0;
    FString PeakNodeOutputPointsId;
    FString PeakNodeOutputPointsName;
    int64 PeakNodeInputPoints = 0;
    FString PeakNodeInputPointsId;
    FString PeakNodeInputPointsName;
    constexpr int64 LargeObjectPointThreshold = 100000;
    constexpr int64 LargeObjectEstimatedMemoryThresholdBytes = 64ll * 1024ll * 1024ll;
    TArray<TSharedPtr<FJsonValue>> LargeObjectAlertsArray;

    int64 GlobalPeakOutputPoints = 0;
    for (const FPCGProfilerNodeAggregate& Node : Aggregates)
    {
        GlobalPeakOutputPoints = FMath::Max(GlobalPeakOutputPoints, Node.OutputPointsMax);
    }

    for (const FPCGProfilerNodeEvent& Event : LocalEvents)
    {
        TotalInputPointCount += FMath::Max<int64>(0, Event.InputPoints);
        TotalOutputPointCount += FMath::Max<int64>(0, Event.OutputPoints);
        const FString PhaseName = Event.Phase.IsEmpty() ? TEXT("Execute") : Event.Phase;
        PhaseInputPoints.FindOrAdd(PhaseName) += FMath::Max<int64>(0, Event.InputPoints);
        PhaseOutputPoints.FindOrAdd(PhaseName) += FMath::Max<int64>(0, Event.OutputPoints);

        const FString ThreadGroup = PCGProfilerThreading::NormalizeThreadGroup(Event.ThreadGroup);
        ThreadInputPoints.FindOrAdd(ThreadGroup) += FMath::Max<int64>(0, Event.InputPoints);
        ThreadOutputPoints.FindOrAdd(ThreadGroup) += FMath::Max<int64>(0, Event.OutputPoints);

        const FString EventNodeKey = BuildNodeKey(Event.NodeTitle, Event.NodeId, Event.GraphName);
        FNodeNonZeroStats& SparseStats = NodeNonZeroStatsByKey.FindOrAdd(EventNodeKey);
        ++SparseStats.Samples;
        if (Event.InputCount > 0) { ++SparseStats.InputCountNonZero; }
        if (Event.OutputCount > 0) { ++SparseStats.OutputCountNonZero; }
        if (Event.InputPoints > 0) { ++SparseStats.InputPointsNonZero; }
        if (Event.OutputPoints > 0) { ++SparseStats.OutputPointsNonZero; }
        if (Event.InclusiveMs > 0.0) { ++SparseStats.DurationNonZero; }
    }
    TotalEstimatedMemoryBytes = PCGProfilerScaleStats::EstimateMemoryBytes(TotalInputPointCount, TotalOutputPointCount);

    int64 TotalPeakEstimatedMemoryBytes = 0;
    for (const FPCGProfilerNodeAggregate& Node : Aggregates)
    {
        TotalPeakEstimatedMemoryBytes += PCGProfilerScaleStats::EstimateMemoryBytes(Node.InputPointsMax, Node.OutputPointsMax);
    }

    TArray<TSharedPtr<FJsonValue>> NodeArray;
    NodeArray.Reserve(Aggregates.Num());
    for (const FPCGProfilerNodeAggregate& Node : Aggregates)
    {
        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        const FString NodeKey = BuildNodeKey(Node.NodeName, Node.NodeId, Node.GraphName);
        const FNodeNonZeroStats* SparseStats = NodeNonZeroStatsByKey.Find(NodeKey);
        const double SparseSampleCount = SparseStats ? static_cast<double>(SparseStats->Samples) : 0.0;
        const double ChildInclusiveMs = NodeChildInclusiveMs.FindRef(NodeKey);
        const double ComputedSelfMs = FMath::Max(0.0, Node.TotalMs - ChildInclusiveMs);
        NodeObj->SetStringField(TEXT("node_name"), Node.NodeName);
        NodeObj->SetStringField(TEXT("node_title"), Node.NodeName);
        NodeObj->SetStringField(TEXT("node_id"), Node.NodeId);
        NodeObj->SetStringField(TEXT("graph_name"), Node.GraphName);
        NodeObj->SetStringField(TEXT("node_class"), Node.NodeClass);
        NodeObj->SetStringField(TEXT("settings_class"), Node.SettingsClass);
        NodeObj->SetStringField(TEXT("execution_path"), Node.ExecutionPath);
        NodeObj->SetNumberField(TEXT("call_count"), Node.CallCount);
        NodeObj->SetNumberField(TEXT("sample_count"), Node.SampleCount);
        NodeObj->SetNumberField(TEXT("total_ms"), Node.TotalMs);
        NodeObj->SetNumberField(TEXT("self_ms"), ComputedSelfMs);
        NodeObj->SetNumberField(TEXT("inclusive_ms"), Node.TotalMs);
        NodeObj->SetNumberField(TEXT("min_ms"), Node.MinMs);
        NodeObj->SetNumberField(TEXT("max_ms"), Node.MaxMs);
        NodeObj->SetNumberField(TEXT("avg_ms"), Node.AvgMs);
        NodeObj->SetNumberField(TEXT("stddev_ms"), Node.StdDevMs);
        NodeObj->SetNumberField(TEXT("duration_cv"), Node.DurationCv);
        NodeObj->SetNumberField(TEXT("p50_ms"), Node.P50Ms);
        NodeObj->SetNumberField(TEXT("p95_ms"), Node.P95Ms);
        NodeObj->SetNumberField(TEXT("sparse_sample_count"), SparseSampleCount);
        NodeObj->SetNumberField(TEXT("duration_nonzero_samples"), SparseStats ? SparseStats->DurationNonZero : 0.0);
        NodeObj->SetNumberField(TEXT("duration_nonzero_rate"), (SparseStats && SparseStats->Samples > 0) ? (static_cast<double>(SparseStats->DurationNonZero) / static_cast<double>(SparseStats->Samples)) : 0.0);
        NodeObj->SetNumberField(TEXT("first_seen_time_ms"), Node.FirstSeenTimeMs);
        NodeObj->SetStringField(TEXT("first_seen_time_source"), Node.FirstSeenTimeSource);
        NodeObj->SetNumberField(TEXT("input_count_max"), Node.InputCountMax);
        NodeObj->SetNumberField(TEXT("input_count_sum"), static_cast<double>(Node.InputCountSum));
        NodeObj->SetStringField(TEXT("input_count_sum_i64"), LexToString(Node.InputCountSum));
        NodeObj->SetNumberField(TEXT("input_count_last"), Node.InputCountLast);
        NodeObj->SetNumberField(TEXT("input_count_avg"), Node.InputCountAvg);
        NodeObj->SetNumberField(TEXT("input_count_p95"), Node.InputCountP95);
        NodeObj->SetNumberField(TEXT("input_count_cv"), Node.InputCountCv);
        NodeObj->SetNumberField(TEXT("input_count_nonzero_samples"), SparseStats ? SparseStats->InputCountNonZero : 0.0);
        NodeObj->SetNumberField(TEXT("input_count_nonzero_rate"), (SparseStats && SparseStats->Samples > 0) ? (static_cast<double>(SparseStats->InputCountNonZero) / static_cast<double>(SparseStats->Samples)) : 0.0);
        NodeObj->SetNumberField(TEXT("output_count_max"), Node.OutputCountMax);
        NodeObj->SetNumberField(TEXT("output_count_sum"), static_cast<double>(Node.OutputCountSum));
        NodeObj->SetStringField(TEXT("output_count_sum_i64"), LexToString(Node.OutputCountSum));
        NodeObj->SetNumberField(TEXT("output_count_last"), Node.OutputCountLast);
        NodeObj->SetNumberField(TEXT("output_count_avg"), Node.OutputCountAvg);
        NodeObj->SetNumberField(TEXT("output_count_p95"), Node.OutputCountP95);
        NodeObj->SetNumberField(TEXT("output_count_cv"), Node.OutputCountCv);
        NodeObj->SetNumberField(TEXT("output_count_nonzero_samples"), SparseStats ? SparseStats->OutputCountNonZero : 0.0);
        NodeObj->SetNumberField(TEXT("output_count_nonzero_rate"), (SparseStats && SparseStats->Samples > 0) ? (static_cast<double>(SparseStats->OutputCountNonZero) / static_cast<double>(SparseStats->Samples)) : 0.0);
        NodeObj->SetNumberField(TEXT("input_points_max"), static_cast<double>(Node.InputPointsMax));
        NodeObj->SetStringField(TEXT("input_points_max_i64"), LexToString(Node.InputPointsMax));
        NodeObj->SetNumberField(TEXT("input_points_sum"), static_cast<double>(Node.InputPointsSum));
        NodeObj->SetStringField(TEXT("input_points_sum_i64"), LexToString(Node.InputPointsSum));
        NodeObj->SetNumberField(TEXT("input_points_last"), static_cast<double>(Node.InputPointsLast));
        NodeObj->SetStringField(TEXT("input_points_last_i64"), LexToString(Node.InputPointsLast));
        NodeObj->SetNumberField(TEXT("input_points_avg"), Node.InputPointsAvg);
        NodeObj->SetNumberField(TEXT("input_points_p95"), Node.InputPointsP95);
        NodeObj->SetNumberField(TEXT("input_points_cv"), Node.InputPointsCv);
        NodeObj->SetNumberField(TEXT("input_points_nonzero_samples"), SparseStats ? SparseStats->InputPointsNonZero : 0.0);
        NodeObj->SetNumberField(TEXT("input_points_nonzero_rate"), (SparseStats && SparseStats->Samples > 0) ? (static_cast<double>(SparseStats->InputPointsNonZero) / static_cast<double>(SparseStats->Samples)) : 0.0);
        NodeObj->SetNumberField(TEXT("output_points_max"), static_cast<double>(Node.OutputPointsMax));
        NodeObj->SetStringField(TEXT("output_points_max_i64"), LexToString(Node.OutputPointsMax));
        NodeObj->SetNumberField(TEXT("output_points_sum"), static_cast<double>(Node.OutputPointsSum));
        NodeObj->SetStringField(TEXT("output_points_sum_i64"), LexToString(Node.OutputPointsSum));
        NodeObj->SetNumberField(TEXT("output_points_last"), static_cast<double>(Node.OutputPointsLast));
        NodeObj->SetStringField(TEXT("output_points_last_i64"), LexToString(Node.OutputPointsLast));
        NodeObj->SetNumberField(TEXT("output_points_avg"), Node.OutputPointsAvg);
        NodeObj->SetNumberField(TEXT("output_points_p95"), Node.OutputPointsP95);
        NodeObj->SetNumberField(TEXT("output_points_cv"), Node.OutputPointsCv);
        NodeObj->SetNumberField(TEXT("output_points_nonzero_samples"), SparseStats ? SparseStats->OutputPointsNonZero : 0.0);
        NodeObj->SetNumberField(TEXT("output_points_nonzero_rate"), (SparseStats && SparseStats->Samples > 0) ? (static_cast<double>(SparseStats->OutputPointsNonZero) / static_cast<double>(SparseStats->Samples)) : 0.0);
        const double OutputInputRatioMax = PCGProfilerScaleStats::SafeRatio(static_cast<double>(Node.OutputPointsMax), static_cast<double>(Node.InputPointsMax), Node.OutputPointsMax > 0 ? 1.0 : 0.0);
        const double OutputInputRatioSum = PCGProfilerScaleStats::SafeRatio(static_cast<double>(Node.OutputPointsSum), static_cast<double>(Node.InputPointsSum), Node.OutputPointsSum > 0 ? 1.0 : 0.0);
        NodeObj->SetNumberField(TEXT("output_input_ratio_max"), OutputInputRatioMax);
        NodeObj->SetNumberField(TEXT("output_input_ratio_sum"), OutputInputRatioSum);
        const int64 NodeEstimatedMemoryBytes = PCGProfilerScaleStats::EstimateMemoryBytes(Node.InputPointsMax, Node.OutputPointsMax);
        if (NodeEstimatedMemoryBytes > PeakEstimatedMemoryBytes)
        {
            PeakEstimatedMemoryBytes = NodeEstimatedMemoryBytes;
            PeakEstimatedMemoryNodeId = Node.NodeId;
            PeakEstimatedMemoryNodeName = Node.NodeName;
        }
        NodeObj->SetNumberField(
            TEXT("estimated_memory_bytes"),
            static_cast<double>(NodeEstimatedMemoryBytes));
        NodeObj->SetNumberField(TEXT("memory_share_sum"), TotalPeakEstimatedMemoryBytes > 0 ? (static_cast<double>(NodeEstimatedMemoryBytes) / static_cast<double>(TotalPeakEstimatedMemoryBytes)) : 0.0);
        NodeObj->SetNumberField(TEXT("point_share_sum"), TotalOutputPointCount > 0 ? (static_cast<double>(Node.OutputPointsSum) / static_cast<double>(TotalOutputPointCount)) : 0.0);
        NodeObj->SetNumberField(TEXT("point_share_peak"), GlobalPeakOutputPoints > 0 ? (static_cast<double>(Node.OutputPointsMax) / static_cast<double>(GlobalPeakOutputPoints)) : 0.0);
        NodeObj->SetStringField(TEXT("estimated_memory_method"), TEXT("point_count_x_160_bytes"));
        NodeObj->SetStringField(TEXT("estimated_memory_confidence"), TEXT("medium"));
        const FString DataType = !Node.DataType.IsEmpty() ? Node.DataType : TEXT("Unknown");
        NodeObj->SetStringField(TEXT("data_type"), DataType);
        NodeObj->SetNumberField(TEXT("warning_count"), Node.WarningCount);
        NodeObj->SetNumberField(TEXT("error_count"), Node.ErrorCount);
        NodeObj->SetNumberField(TEXT("cancelled_count"), Node.CancelledCount);
        NodeObj->SetNumberField(TEXT("game_thread_ms"), Node.GameThreadMs);
        NodeObj->SetNumberField(TEXT("worker_thread_ms"), Node.WorkerThreadMs);
        NodeObj->SetNumberField(TEXT("game_thread_call_count"), Node.GameThreadCallCount);
        NodeObj->SetNumberField(TEXT("worker_thread_call_count"), Node.WorkerThreadCallCount);
        NodeObj->SetNumberField(TEXT("queue_wait_total_ms"), Node.QueueWaitTotalMs);
        NodeObj->SetNumberField(TEXT("queue_wait_avg_ms"), Node.QueueWaitAvgMs);
        NodeObj->SetNumberField(TEXT("queue_wait_ratio"), Node.QueueWaitRatio);
        NodeObj->SetNumberField(TEXT("queue_wait_cv"), Node.QueueWaitCv);
        NodeObj->SetNumberField(TEXT("execution_total_ms"), Node.ExecutionTotalMs);
        NodeObj->SetNumberField(TEXT("game_thread_ratio"), Node.GameThreadRatio);
        NodeObj->SetNumberField(TEXT("worker_thread_ratio"), Node.WorkerThreadRatio);
        NodeObj->SetNumberField(TEXT("cache_hit_count"), Node.CacheHitCount);
        NodeObj->SetNumberField(TEXT("cache_miss_count"), Node.CacheMissCount);
        NodeObj->SetNumberField(TEXT("cache_hit_rate"), Node.CacheHitRate);
        NodeObj->SetNumberField(TEXT("miss_reason_parameter_change_count"), Node.MissReasonParameterChangeCount);
        NodeObj->SetNumberField(TEXT("miss_reason_input_change_count"), Node.MissReasonInputChangeCount);
        NodeObj->SetNumberField(TEXT("miss_reason_version_change_count"), Node.MissReasonVersionChangeCount);
        NodeObj->SetNumberField(TEXT("miss_reason_unknown_count"), Node.MissReasonUnknownCount);
        NodeObj->SetNumberField(TEXT("cache_reuse_saved_ms_estimate"), Node.CacheReuseSavedMsEstimate);
        NodeObj->SetNumberField(TEXT("measured_memory_delta_sum_bytes"), static_cast<double>(Node.MemoryDeltaSumBytes));
        NodeObj->SetStringField(TEXT("measured_memory_delta_sum_bytes_i64"), LexToString(Node.MemoryDeltaSumBytes));
        NodeObj->SetNumberField(TEXT("measured_memory_delta_max_bytes"), static_cast<double>(Node.MemoryDeltaMaxBytes));
        NodeObj->SetStringField(TEXT("measured_memory_delta_max_bytes_i64"), LexToString(Node.MemoryDeltaMaxBytes));
        NodeObj->SetNumberField(TEXT("measured_memory_delta_min_bytes"), static_cast<double>(Node.MemoryDeltaMinBytes));
        NodeObj->SetStringField(TEXT("measured_memory_delta_min_bytes_i64"), LexToString(Node.MemoryDeltaMinBytes));
        NodeObj->SetNumberField(TEXT("measured_memory_delta_last_bytes"), static_cast<double>(Node.MemoryDeltaLastBytes));
        NodeObj->SetStringField(TEXT("measured_memory_delta_last_bytes_i64"), LexToString(Node.MemoryDeltaLastBytes));
        NodeObj->SetNumberField(TEXT("measured_memory_delta_avg_bytes"), Node.MemoryDeltaAvgBytes);
        NodeObj->SetNumberField(TEXT("measured_memory_delta_p95_bytes"), Node.MemoryDeltaP95Bytes);
        NodeObj->SetNumberField(TEXT("measured_memory_delta_cv"), Node.MemoryDeltaCv);
        NodeObj->SetStringField(TEXT("memory_scope"), Node.MemoryScope);
        NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));

        TypeInputPoints.FindOrAdd(DataType) += FMath::Max<int64>(0, Node.InputPointsSum);
        TypeOutputPoints.FindOrAdd(DataType) += FMath::Max<int64>(0, Node.OutputPointsSum);
        TypeEstimatedMemoryBytes.FindOrAdd(DataType) += NodeEstimatedMemoryBytes;
        TypeNodeCount.FindOrAdd(DataType) += 1;

        if (Node.OutputPointsMax > PeakNodeOutputPoints)
        {
            PeakNodeOutputPoints = Node.OutputPointsMax;
            PeakNodeOutputPointsId = Node.NodeId;
            PeakNodeOutputPointsName = Node.NodeName;
        }
        if (Node.InputPointsMax > PeakNodeInputPoints)
        {
            PeakNodeInputPoints = Node.InputPointsMax;
            PeakNodeInputPointsId = Node.NodeId;
            PeakNodeInputPointsName = Node.NodeName;
        }

        const bool bLargeByPoints = (Node.InputPointsMax >= LargeObjectPointThreshold) || (Node.OutputPointsMax >= LargeObjectPointThreshold);
        const bool bLargeByMemory = (NodeEstimatedMemoryBytes >= LargeObjectEstimatedMemoryThresholdBytes);
        if (bLargeByPoints || bLargeByMemory)
        {
            TSharedPtr<FJsonObject> AlertObj = MakeShared<FJsonObject>();
            AlertObj->SetStringField(TEXT("node_id"), Node.NodeId);
            AlertObj->SetStringField(TEXT("node_name"), Node.NodeName);
            AlertObj->SetStringField(TEXT("graph_name"), Node.GraphName);
            AlertObj->SetStringField(TEXT("data_type"), DataType);
            AlertObj->SetBoolField(TEXT("triggered_by_points"), bLargeByPoints);
            AlertObj->SetBoolField(TEXT("triggered_by_estimated_memory"), bLargeByMemory);
            AlertObj->SetNumberField(TEXT("input_points_max"), static_cast<double>(Node.InputPointsMax));
            AlertObj->SetNumberField(TEXT("output_points_max"), static_cast<double>(Node.OutputPointsMax));
            AlertObj->SetNumberField(TEXT("estimated_memory_bytes"), static_cast<double>(NodeEstimatedMemoryBytes));
            AlertObj->SetNumberField(TEXT("output_input_ratio_max"), OutputInputRatioMax);
            LargeObjectAlertsArray.Add(MakeShared<FJsonValueObject>(AlertObj));
        }
    }
    Root->SetArrayField(TEXT("nodes"), NodeArray);

    TArray<TSharedPtr<FJsonValue>> EventArray;
    EventArray.Reserve(LocalEvents.Num());
    int64 RuntimeSpawnCount = 0;
    int64 RuntimeDestroyCount = 0;
    double RuntimeSpawnMs = 0.0;
    double RuntimeDestroyMs = 0.0;
    int32 MissingRequiredEventCount = 0;
    int32 MissingRequiredFieldTotal = 0;
    TMap<FString, int32> RuntimeGenerateReasonCounts;
    TMap<FString, int32> RuntimeStreamingEventCounts;
    TMap<FString, int32> RuntimeCellEventCounts;
    for (FPCGProfilerNodeEvent Event : LocalEvents)
    {
        if (Event.RunMode.IsEmpty())
        {
            Event.RunMode = RuntimeContext.RunMode;
        }
        if (Event.WorldType.IsEmpty())
        {
            Event.WorldType = RuntimeContext.WorldType;
        }
        Event.bIsPIE = Event.bIsPIE || RuntimeContext.bIsPIE;
        Event.bIsCooked = Event.bIsCooked || RuntimeContext.bIsCooked;
        if (Event.GenerateReason.IsEmpty())
        {
            Event.GenerateReason = TEXT("unknown");
        }
        if (Event.StreamingEvent.IsEmpty())
        {
            Event.StreamingEvent = TEXT("none");
        }

        RuntimeSpawnCount += static_cast<int64>(Event.SpawnCount);
        RuntimeDestroyCount += static_cast<int64>(Event.DestroyCount);
        RuntimeSpawnMs += Event.SpawnMs;
        RuntimeDestroyMs += Event.DestroyMs;
        RuntimeGenerateReasonCounts.FindOrAdd(Event.GenerateReason) += 1;
        RuntimeStreamingEventCounts.FindOrAdd(Event.StreamingEvent) += 1;
        if (!Event.CellId.IsEmpty())
        {
            RuntimeCellEventCounts.FindOrAdd(Event.CellId) += 1;
        }

        const FString MissingTag = TEXT("missing_required=");
        const int32 MissingIndex = Event.ThreadSource.Find(MissingTag, ESearchCase::IgnoreCase);
        if (MissingIndex != INDEX_NONE)
        {
            const int32 ValueStart = MissingIndex + MissingTag.Len();
            int32 ValueEnd = ValueStart;
            while (ValueEnd < Event.ThreadSource.Len() && FChar::IsDigit(Event.ThreadSource[ValueEnd]))
            {
                ++ValueEnd;
            }
            if (ValueEnd > ValueStart)
            {
                const int32 MissingFields = FCString::Atoi(*Event.ThreadSource.Mid(ValueStart, ValueEnd - ValueStart));
                if (MissingFields > 0)
                {
                    ++MissingRequiredEventCount;
                    MissingRequiredFieldTotal += MissingFields;
                }
            }
        }

        TSharedPtr<FJsonObject> EventObj = PCGProfilerSerialization::BuildEventJsonObject(Event);
        EventArray.Add(MakeShared<FJsonValueObject>(EventObj));
    }
    Root->SetArrayField(TEXT("events"), EventArray);
    RuntimeSpawnCount = FMath::Max(RuntimeSpawnCount, LocalRuntimeSpawnCountAccum);
    RuntimeDestroyCount = FMath::Max(RuntimeDestroyCount, LocalRuntimeDestroyCountAccum);
    RuntimeSpawnMs = FMath::Max(RuntimeSpawnMs, LocalRuntimeSpawnMsAccum);
    RuntimeDestroyMs = FMath::Max(RuntimeDestroyMs, LocalRuntimeDestroyMsAccum);
    Root->SetNumberField(TEXT("component_generate_end_count"), static_cast<double>(RuntimeSpawnCount));
    Root->SetNumberField(TEXT("component_cleanup_end_count"), static_cast<double>(RuntimeDestroyCount));
    Root->SetNumberField(TEXT("component_generate_ms_total"), RuntimeSpawnMs);
    Root->SetNumberField(TEXT("component_cleanup_ms_total"), RuntimeDestroyMs);
    // Backward compatibility for existing dashboards/scripts.
    Root->SetNumberField(TEXT("spawn_count"), static_cast<double>(RuntimeSpawnCount));
    Root->SetNumberField(TEXT("destroy_count"), static_cast<double>(RuntimeDestroyCount));
    Root->SetNumberField(TEXT("spawn_ms"), RuntimeSpawnMs);
    Root->SetNumberField(TEXT("destroy_ms"), RuntimeDestroyMs);
    Root->SetNumberField(TEXT("events_total_seen"), static_cast<double>(LocalTotalEventsSeen));
    Root->SetStringField(TEXT("events_total_seen_i64"), LexToString(LocalTotalEventsSeen));
    Root->SetNumberField(TEXT("events_in_memory"), static_cast<double>(LocalEventsInMemory));
    Root->SetStringField(TEXT("events_in_memory_i64"), LexToString(LocalEventsInMemory));
    Root->SetNumberField(TEXT("events_flushed_to_chunks"), static_cast<double>(LocalTotalEventsFlushed));
    Root->SetStringField(TEXT("events_flushed_to_chunks_i64"), LexToString(LocalTotalEventsFlushed));
    Root->SetNumberField(TEXT("events_dropped"), static_cast<double>(LocalTotalEventsDropped));
    Root->SetStringField(TEXT("events_dropped_i64"), LexToString(LocalTotalEventsDropped));
    Root->SetNumberField(TEXT("missing_required_event_count"), MissingRequiredEventCount);
    Root->SetNumberField(TEXT("missing_required_field_total"), MissingRequiredFieldTotal);

    TSharedPtr<FJsonObject> StreamingObj = MakeShared<FJsonObject>();
    StreamingObj->SetStringField(TEXT("retention_policy"), TEXT("streaming_flush_jsonl"));
    StreamingObj->SetNumberField(TEXT("flush_count"), LocalFlushCount);
    StreamingObj->SetNumberField(TEXT("chunk_count"), LocalFlushedChunkPaths.Num());
    StreamingObj->SetNumberField(TEXT("max_events_in_memory_before_flush"), MaxEventsInMemoryBeforeFlush);
    StreamingObj->SetNumberField(TEXT("flush_chunk_event_count"), FlushChunkEventCount);
    Root->SetObjectField(TEXT("event_streaming"), StreamingObj);

    TSharedPtr<FJsonObject> RuntimeWarningObj = MakeShared<FJsonObject>();
    RuntimeWarningObj->SetNumberField(TEXT("memory_warning_count"), LocalMemoryWarningCount);
    RuntimeWarningObj->SetNumberField(TEXT("peak_process_memory_bytes"), static_cast<double>(LocalPeakProcessMemoryBytes));
    RuntimeWarningObj->SetNumberField(TEXT("memory_warning_threshold_bytes"), static_cast<double>(MemoryWarningThresholdBytes));
    RuntimeWarningObj->SetNumberField(TEXT("lifecycle_event_count"), LocalRuntimeLifecycleEventCount);
    RuntimeWarningObj->SetNumberField(TEXT("streaming_event_count"), LocalRuntimeStreamingEventCount);
    Root->SetObjectField(TEXT("runtime_warnings"), RuntimeWarningObj);

    {
        TSharedPtr<FJsonObject> RuntimeSemanticsObj = MakeShared<FJsonObject>();
        RuntimeSemanticsObj->SetNumberField(TEXT("tracked_cells"), RuntimeCellEventCounts.Num());

        TArray<TSharedPtr<FJsonValue>> GenerateReasonArray;
        for (const TPair<FString, int32>& KV : RuntimeGenerateReasonCounts)
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("generate_reason"), KV.Key);
            Item->SetNumberField(TEXT("count"), KV.Value);
            GenerateReasonArray.Add(MakeShared<FJsonValueObject>(Item));
        }
        RuntimeSemanticsObj->SetArrayField(TEXT("generate_reason_breakdown"), GenerateReasonArray);

        TArray<TSharedPtr<FJsonValue>> StreamingEventArray;
        for (const TPair<FString, int32>& KV : RuntimeStreamingEventCounts)
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("streaming_event"), KV.Key);
            Item->SetNumberField(TEXT("count"), KV.Value);
            StreamingEventArray.Add(MakeShared<FJsonValueObject>(Item));
        }
        RuntimeSemanticsObj->SetArrayField(TEXT("streaming_event_breakdown"), StreamingEventArray);

        TArray<TPair<FString, int32>> SortedCells;
        SortedCells.Reserve(RuntimeCellEventCounts.Num());
        for (const TPair<FString, int32>& KV : RuntimeCellEventCounts)
        {
            SortedCells.Add(KV);
        }
        SortedCells.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
        {
            return A.Value > B.Value;
        });

        TArray<TSharedPtr<FJsonValue>> TopCellsArray;
        const int32 TopCellLimit = FMath::Min(10, SortedCells.Num());
        for (int32 Index = 0; Index < TopCellLimit; ++Index)
        {
            const TPair<FString, int32>& KV = SortedCells[Index];
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("cell_id"), KV.Key);
            Item->SetNumberField(TEXT("event_count"), KV.Value);
            TopCellsArray.Add(MakeShared<FJsonValueObject>(Item));
        }
        RuntimeSemanticsObj->SetArrayField(TEXT("top_cells_by_event_count"), TopCellsArray);

        Root->SetObjectField(TEXT("runtime_semantics"), RuntimeSemanticsObj);
    }

    {
        TSharedPtr<FJsonObject> MemoryProfileObj = MakeShared<FJsonObject>();
        const bool bAccurate = (MemoryProfilingMode == EMemoryProfilingMode::AccurateDelta);
        MemoryProfileObj->SetStringField(TEXT("mode"), bAccurate ? TEXT("accurate_delta") : TEXT("fast_estimate"));
        MemoryProfileObj->SetBoolField(TEXT("measured_delta_enabled"), bAccurate);
        MemoryProfileObj->SetStringField(TEXT("estimated_memory_method"), TEXT("point_count_x_160_bytes"));
        MemoryProfileObj->SetStringField(TEXT("estimated_memory_confidence"), TEXT("medium"));
        Root->SetObjectField(TEXT("memory_profile"), MemoryProfileObj);
    }

    TSharedPtr<FJsonObject> ScaleSummaryObj = MakeShared<FJsonObject>();
    ScaleSummaryObj->SetNumberField(TEXT("total_input_point_count"), static_cast<double>(TotalInputPointCount));
    ScaleSummaryObj->SetNumberField(TEXT("total_output_point_count"), static_cast<double>(TotalOutputPointCount));
    ScaleSummaryObj->SetNumberField(TEXT("total_estimated_memory_bytes"), static_cast<double>(TotalEstimatedMemoryBytes));
    ScaleSummaryObj->SetNumberField(TEXT("peak_estimated_memory_bytes"), static_cast<double>(PeakEstimatedMemoryBytes));
    ScaleSummaryObj->SetStringField(TEXT("peak_estimated_memory_node_id"), PeakEstimatedMemoryNodeId);
    ScaleSummaryObj->SetStringField(TEXT("peak_estimated_memory_node_name"), PeakEstimatedMemoryNodeName);
    ScaleSummaryObj->SetStringField(TEXT("estimated_memory_method"), TEXT("point_count_x_160_bytes"));
    ScaleSummaryObj->SetStringField(TEXT("estimated_memory_confidence"), TEXT("medium"));
    ScaleSummaryObj->SetNumberField(TEXT("peak_input_points_estimate"), static_cast<double>(PeakNodeInputPoints));
    ScaleSummaryObj->SetStringField(TEXT("peak_input_points_node_id"), PeakNodeInputPointsId);
    ScaleSummaryObj->SetStringField(TEXT("peak_input_points_node_name"), PeakNodeInputPointsName);
    ScaleSummaryObj->SetNumberField(TEXT("peak_output_points_estimate"), static_cast<double>(PeakNodeOutputPoints));
    ScaleSummaryObj->SetStringField(TEXT("peak_output_points_node_id"), PeakNodeOutputPointsId);
    ScaleSummaryObj->SetStringField(TEXT("peak_output_points_node_name"), PeakNodeOutputPointsName);
    ScaleSummaryObj->SetNumberField(TEXT("global_output_input_ratio"), PCGProfilerScaleStats::SafeRatio(static_cast<double>(TotalOutputPointCount), static_cast<double>(TotalInputPointCount), TotalOutputPointCount > 0 ? 1.0 : 0.0));

    TArray<FPCGProfilerNodeAggregate> TopByInputPoints = Aggregates;
    TopByInputPoints.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B) { return A.InputPointsMax > B.InputPointsMax; });
    TArray<FPCGProfilerNodeAggregate> TopByOutputPoints = Aggregates;
    TopByOutputPoints.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B) { return A.OutputPointsMax > B.OutputPointsMax; });
    TArray<FPCGProfilerNodeAggregate> TopByEstimatedMemory = Aggregates;
    TopByEstimatedMemory.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B)
    {
        return PCGProfilerScaleStats::EstimateMemoryBytes(A.InputPointsMax, A.OutputPointsMax)
            > PCGProfilerScaleStats::EstimateMemoryBytes(B.InputPointsMax, B.OutputPointsMax);
    });

    constexpr int32 ScaleTopN = 20;
    if (TopByInputPoints.Num() > ScaleTopN) { TopByInputPoints.SetNum(ScaleTopN); }
    if (TopByOutputPoints.Num() > ScaleTopN) { TopByOutputPoints.SetNum(ScaleTopN); }
    if (TopByEstimatedMemory.Num() > ScaleTopN) { TopByEstimatedMemory.SetNum(ScaleTopN); }

    TArray<TSharedPtr<FJsonValue>> TopByInputPointsArray;
    for (const FPCGProfilerNodeAggregate& Node : TopByInputPoints)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("input_point_count"), static_cast<double>(Node.InputPointsMax));
        TopByInputPointsArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    ScaleSummaryObj->SetArrayField(TEXT("top_nodes_by_input_points"), TopByInputPointsArray);

    TArray<TSharedPtr<FJsonValue>> TopByOutputPointsArray;
    for (const FPCGProfilerNodeAggregate& Node : TopByOutputPoints)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("output_point_count"), static_cast<double>(Node.OutputPointsMax));
        TopByOutputPointsArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    ScaleSummaryObj->SetArrayField(TEXT("top_nodes_by_output_points"), TopByOutputPointsArray);

    TArray<TSharedPtr<FJsonValue>> TopByEstimatedMemoryArray;
    for (const FPCGProfilerNodeAggregate& Node : TopByEstimatedMemory)
    {
        const int64 NodeEstimatedMemoryBytes = PCGProfilerScaleStats::EstimateMemoryBytes(Node.InputPointsMax, Node.OutputPointsMax);
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("estimated_memory_bytes"), static_cast<double>(NodeEstimatedMemoryBytes));
        TopByEstimatedMemoryArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    ScaleSummaryObj->SetArrayField(TEXT("top_nodes_by_estimated_memory"), TopByEstimatedMemoryArray);

    TArray<TSharedPtr<FJsonValue>> PhaseScaleArray;
    for (const TPair<FString, int64>& KV : PhaseInputPoints)
    {
        const int64 InputPoints = KV.Value;
        const int64 OutputPoints = PhaseOutputPoints.FindRef(KV.Key);
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("phase"), KV.Key);
        Item->SetNumberField(TEXT("input_point_count"), static_cast<double>(InputPoints));
        Item->SetNumberField(TEXT("output_point_count"), static_cast<double>(OutputPoints));
        Item->SetNumberField(TEXT("estimated_memory_bytes"), static_cast<double>(PCGProfilerScaleStats::EstimateMemoryBytes(InputPoints, OutputPoints)));
        PhaseScaleArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    ScaleSummaryObj->SetArrayField(TEXT("by_phase"), PhaseScaleArray);

    TArray<TSharedPtr<FJsonValue>> ThreadScaleArray;
    for (const TPair<FString, int64>& KV : ThreadInputPoints)
    {
        const int64 InputPoints = KV.Value;
        const int64 OutputPoints = ThreadOutputPoints.FindRef(KV.Key);
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("thread_group"), KV.Key);
        Item->SetNumberField(TEXT("input_point_count"), static_cast<double>(InputPoints));
        Item->SetNumberField(TEXT("output_point_count"), static_cast<double>(OutputPoints));
        Item->SetNumberField(TEXT("estimated_memory_bytes"), static_cast<double>(PCGProfilerScaleStats::EstimateMemoryBytes(InputPoints, OutputPoints)));
        ThreadScaleArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    ScaleSummaryObj->SetArrayField(TEXT("by_thread_group"), ThreadScaleArray);

    TArray<TSharedPtr<FJsonValue>> TypeScaleArray;
    for (const TPair<FString, int64>& KV : TypeInputPoints)
    {
        const FString& DataType = KV.Key;
        const int64 InputPoints = KV.Value;
        const int64 OutputPoints = TypeOutputPoints.FindRef(DataType);
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("data_type"), DataType);
        Item->SetNumberField(TEXT("node_count"), TypeNodeCount.FindRef(DataType));
        Item->SetNumberField(TEXT("input_point_count"), static_cast<double>(InputPoints));
        Item->SetNumberField(TEXT("output_point_count"), static_cast<double>(OutputPoints));
        Item->SetNumberField(TEXT("output_input_ratio"), PCGProfilerScaleStats::SafeRatio(static_cast<double>(OutputPoints), static_cast<double>(InputPoints), OutputPoints > 0 ? 1.0 : 0.0));
        Item->SetNumberField(TEXT("estimated_memory_bytes"), static_cast<double>(TypeEstimatedMemoryBytes.FindRef(DataType)));
        TypeScaleArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    ScaleSummaryObj->SetArrayField(TEXT("by_data_type"), TypeScaleArray);
    Root->SetObjectField(TEXT("scale_summary"), ScaleSummaryObj);

    TSharedPtr<FJsonObject> LargeObjectObj = MakeShared<FJsonObject>();
    LargeObjectObj->SetNumberField(TEXT("point_threshold"), static_cast<double>(LargeObjectPointThreshold));
    LargeObjectObj->SetNumberField(TEXT("estimated_memory_threshold_bytes"), static_cast<double>(LargeObjectEstimatedMemoryThresholdBytes));
    LargeObjectObj->SetNumberField(TEXT("alert_count"), LargeObjectAlertsArray.Num());
    LargeObjectObj->SetArrayField(TEXT("alerts"), LargeObjectAlertsArray);
    Root->SetObjectField(TEXT("large_object_alerts"), LargeObjectObj);

    struct FLifecycleItem
    {
        FString NodeId;
        FString NodeName;
        FString GraphName;
        int64 CreatedPoints = 0;
        int64 ReducedPoints = 0;
        int64 ThroughputPoints = 0;
        int64 EstimatedMemoryBytes = 0;
        double OutputInputRatio = 0.0;
        double AvgMs = 0.0;
    };

    TArray<FLifecycleItem> Creators;
    TArray<FLifecycleItem> Reducers;
    TArray<FLifecycleItem> RedundantCandidates;
    TArray<FLifecycleItem> MemoryHotspots;
    int64 LifecycleCreatedPointsTotal = 0;
    int64 LifecycleReducedPointsTotal = 0;

    for (const FPCGProfilerNodeAggregate& Node : Aggregates)
    {
        const int64 InputPoints = FMath::Max<int64>(0, Node.InputPointsSum);
        const int64 OutputPoints = FMath::Max<int64>(0, Node.OutputPointsSum);
        const int64 CreatedPoints = FMath::Max<int64>(0, OutputPoints - InputPoints);
        const int64 ReducedPoints = FMath::Max<int64>(0, InputPoints - OutputPoints);
        const int64 ThroughputPoints = FMath::Min<int64>(InputPoints, OutputPoints);
        const int64 NodeEstimatedMemoryBytes = PCGProfilerScaleStats::EstimateMemoryBytes(Node.InputPointsMax, Node.OutputPointsMax);
        const double OutputInputRatio = PCGProfilerScaleStats::SafeRatio(static_cast<double>(OutputPoints), static_cast<double>(InputPoints), OutputPoints > 0 ? 1.0 : 0.0);
        const double DeltaRatio = PCGProfilerScaleStats::SafeRatio(static_cast<double>(FMath::Abs(OutputPoints - InputPoints)), static_cast<double>(FMath::Max<int64>(1, InputPoints)), 0.0);
        const bool bRedundantCandidate = ThroughputPoints > 50000 && DeltaRatio < 0.05 && Node.AvgMs < 5.0;

        FLifecycleItem Item;
        Item.NodeId = Node.NodeId;
        Item.NodeName = Node.NodeName;
        Item.GraphName = Node.GraphName;
        Item.CreatedPoints = CreatedPoints;
        Item.ReducedPoints = ReducedPoints;
        Item.ThroughputPoints = ThroughputPoints;
        Item.EstimatedMemoryBytes = NodeEstimatedMemoryBytes;
        Item.OutputInputRatio = OutputInputRatio;
        Item.AvgMs = Node.AvgMs;

        if (CreatedPoints > 0) { Creators.Add(Item); }
        if (ReducedPoints > 0) { Reducers.Add(Item); }
        if (bRedundantCandidate) { RedundantCandidates.Add(Item); }
        MemoryHotspots.Add(Item);

        LifecycleCreatedPointsTotal += CreatedPoints;
        LifecycleReducedPointsTotal += ReducedPoints;
    }

    Creators.Sort([](const FLifecycleItem& A, const FLifecycleItem& B) { return A.CreatedPoints > B.CreatedPoints; });
    Reducers.Sort([](const FLifecycleItem& A, const FLifecycleItem& B) { return A.ReducedPoints > B.ReducedPoints; });
    RedundantCandidates.Sort([](const FLifecycleItem& A, const FLifecycleItem& B) { return A.ThroughputPoints > B.ThroughputPoints; });
    MemoryHotspots.Sort([](const FLifecycleItem& A, const FLifecycleItem& B) { return A.EstimatedMemoryBytes > B.EstimatedMemoryBytes; });

    constexpr int32 LifecycleTopN = 20;
    if (Creators.Num() > LifecycleTopN) { Creators.SetNum(LifecycleTopN); }
    if (Reducers.Num() > LifecycleTopN) { Reducers.SetNum(LifecycleTopN); }
    if (RedundantCandidates.Num() > LifecycleTopN) { RedundantCandidates.SetNum(LifecycleTopN); }
    if (MemoryHotspots.Num() > LifecycleTopN) { MemoryHotspots.SetNum(LifecycleTopN); }

    auto BuildLifecycleArray = [](const TArray<FLifecycleItem>& Source) -> TArray<TSharedPtr<FJsonValue>>
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Source.Num());
        for (const FLifecycleItem& Item : Source)
        {
            TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
            Obj->SetStringField(TEXT("node_id"), Item.NodeId);
            Obj->SetStringField(TEXT("node_name"), Item.NodeName);
            Obj->SetStringField(TEXT("graph_name"), Item.GraphName);
            Obj->SetNumberField(TEXT("created_points"), static_cast<double>(Item.CreatedPoints));
            Obj->SetNumberField(TEXT("reduced_points"), static_cast<double>(Item.ReducedPoints));
            Obj->SetNumberField(TEXT("throughput_points"), static_cast<double>(Item.ThroughputPoints));
            Obj->SetNumberField(TEXT("output_input_ratio"), Item.OutputInputRatio);
            Obj->SetNumberField(TEXT("estimated_memory_bytes"), static_cast<double>(Item.EstimatedMemoryBytes));
            Obj->SetNumberField(TEXT("avg_ms"), Item.AvgMs);
            Out.Add(MakeShared<FJsonValueObject>(Obj));
        }
        return Out;
    };

    TSharedPtr<FJsonObject> LifecycleObj = MakeShared<FJsonObject>();
    LifecycleObj->SetNumberField(TEXT("created_points_total"), static_cast<double>(LifecycleCreatedPointsTotal));
    LifecycleObj->SetNumberField(TEXT("reduced_points_total"), static_cast<double>(LifecycleReducedPointsTotal));
    LifecycleObj->SetNumberField(TEXT("net_points_change"), static_cast<double>(LifecycleCreatedPointsTotal - LifecycleReducedPointsTotal));
    LifecycleObj->SetArrayField(TEXT("top_creators"), BuildLifecycleArray(Creators));
    LifecycleObj->SetArrayField(TEXT("top_reducers"), BuildLifecycleArray(Reducers));
    Root->SetObjectField(TEXT("lifecycle_tracking"), LifecycleObj);

    TSharedPtr<FJsonObject> MemoryInsightsObj = MakeShared<FJsonObject>();
    MemoryInsightsObj->SetArrayField(TEXT("memory_hotspots"), BuildLifecycleArray(MemoryHotspots));
    MemoryInsightsObj->SetArrayField(TEXT("redundant_intermediate_candidates"), BuildLifecycleArray(RedundantCandidates));
    MemoryInsightsObj->SetStringField(TEXT("redundant_candidate_rule"), TEXT("throughput_points>50000 && delta_ratio<0.05 && avg_ms<5"));
    Root->SetObjectField(TEXT("memory_insights"), MemoryInsightsObj);

    int32 GraphCacheHitCount = 0;
    int32 GraphCacheMissCount = 0;
    int32 GraphMissReasonParameterChangeCount = 0;
    int32 GraphMissReasonInputChangeCount = 0;
    int32 GraphMissReasonVersionChangeCount = 0;
    double GraphCacheReuseSavedMsEstimate = 0.0;
    TMap<FString, int32> MissByNode;
    TMap<FString, int32> MissPropagationCounts;
    TMap<FString, bool> PathCacheMiss;

    for (const FPCGProfilerNodeEvent& Event : LocalEvents)
    {
        if (Event.bCacheHit)
        {
            ++GraphCacheHitCount;
            GraphCacheReuseSavedMsEstimate += Event.InclusiveMs;
        }
        else
        {
            ++GraphCacheMissCount;
            const FString NodeName = Event.NodeTitle.IsEmpty() ? Event.NodeId : Event.NodeTitle;
            MissByNode.FindOrAdd(NodeName)++;
            if (Event.CacheMissReason == TEXT("input_change"))
            {
                ++GraphMissReasonInputChangeCount;
            }
            else if (Event.CacheMissReason == TEXT("version_change"))
            {
                ++GraphMissReasonVersionChangeCount;
            }
            else
            {
                ++GraphMissReasonParameterChangeCount;
            }
        }

        if (!Event.ExecutionPath.IsEmpty())
        {
            PathCacheMiss.Add(Event.ExecutionPath, !Event.bCacheHit);
        }
    }

    for (const FPCGProfilerNodeEvent& Event : LocalEvents)
    {
        if (Event.bCacheHit || Event.ParentExecutionPath.IsEmpty())
        {
            continue;
        }

        const bool* bParentMiss = PathCacheMiss.Find(Event.ParentExecutionPath);
        if (bParentMiss && *bParentMiss)
        {
            const FString ParentToChild = FString::Printf(
                TEXT("%s -> %s"),
                *Event.ParentExecutionPath,
                *Event.ExecutionPath);
            MissPropagationCounts.FindOrAdd(ParentToChild)++;
        }
    }

    struct FNameCount
    {
        FString Name;
        int32 Count = 0;
    };

    TArray<FNameCount> MissRankItems;
    for (const TPair<FString, int32>& KV : MissByNode)
    {
        FNameCount& Item = MissRankItems.AddDefaulted_GetRef();
        Item.Name = KV.Key;
        Item.Count = KV.Value;
    }
    MissRankItems.Sort([](const FNameCount& A, const FNameCount& B) { return A.Count > B.Count; });

    TArray<FNameCount> PropagationItems;
    for (const TPair<FString, int32>& KV : MissPropagationCounts)
    {
        FNameCount& Item = PropagationItems.AddDefaulted_GetRef();
        Item.Name = KV.Key;
        Item.Count = KV.Value;
    }
    PropagationItems.Sort([](const FNameCount& A, const FNameCount& B) { return A.Count > B.Count; });

    TSharedPtr<FJsonObject> CacheAnalysisObj = MakeShared<FJsonObject>();
    const int32 GraphCacheTotal = GraphCacheHitCount + GraphCacheMissCount;
    CacheAnalysisObj->SetNumberField(TEXT("graph_cache_hit_count"), GraphCacheHitCount);
    CacheAnalysisObj->SetNumberField(TEXT("graph_cache_miss_count"), GraphCacheMissCount);
    CacheAnalysisObj->SetNumberField(TEXT("graph_cache_hit_rate"), GraphCacheTotal > 0 ? (static_cast<double>(GraphCacheHitCount) / static_cast<double>(GraphCacheTotal)) : 0.0);
    CacheAnalysisObj->SetNumberField(TEXT("cache_reuse_saved_ms_estimate"), GraphCacheReuseSavedMsEstimate);

    TArray<TSharedPtr<FJsonValue>> NodeCacheArray;
    NodeCacheArray.Reserve(Aggregates.Num());
    for (const FPCGProfilerNodeAggregate& Node : Aggregates)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("cache_hit_count"), Node.CacheHitCount);
        Item->SetNumberField(TEXT("cache_miss_count"), Node.CacheMissCount);
        Item->SetNumberField(TEXT("cache_hit_rate"), Node.CacheHitRate);
        NodeCacheArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    CacheAnalysisObj->SetArrayField(TEXT("node_cache_stats"), NodeCacheArray);

    TArray<TSharedPtr<FJsonValue>> MissRankArray;
    constexpr int32 MissRankTopN = 20;
    for (int32 i = 0; i < MissRankItems.Num() && i < MissRankTopN; ++i)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_name"), MissRankItems[i].Name);
        Item->SetNumberField(TEXT("miss_count"), MissRankItems[i].Count);
        MissRankArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    CacheAnalysisObj->SetArrayField(TEXT("cache_miss_node_ranking"), MissRankArray);

    TSharedPtr<FJsonObject> MissReasonObj = MakeShared<FJsonObject>();
    MissReasonObj->SetNumberField(TEXT("input_change"), GraphMissReasonInputChangeCount);
    MissReasonObj->SetNumberField(TEXT("version_change"), GraphMissReasonVersionChangeCount);
    MissReasonObj->SetNumberField(TEXT("unknown"), GraphMissReasonParameterChangeCount);
    MissReasonObj->SetNumberField(TEXT("parameter_change"), 0.0);
    CacheAnalysisObj->SetObjectField(TEXT("miss_reason_breakdown"), MissReasonObj);

    TArray<TSharedPtr<FJsonValue>> PerRunCacheArray;
    for (const FRunThreadSummary& Summary : LocalRunThreadHistory)
    {
        const int32 RunTotal = Summary.CacheHitCount + Summary.CacheMissCount;
        TSharedPtr<FJsonObject> RunObj = MakeShared<FJsonObject>();
        RunObj->SetStringField(TEXT("run_name"), Summary.RunName);
        RunObj->SetNumberField(TEXT("cache_hit_count"), Summary.CacheHitCount);
        RunObj->SetNumberField(TEXT("cache_miss_count"), Summary.CacheMissCount);
        RunObj->SetNumberField(TEXT("cache_hit_rate"), RunTotal > 0 ? (static_cast<double>(Summary.CacheHitCount) / static_cast<double>(RunTotal)) : 0.0);
        RunObj->SetNumberField(TEXT("miss_reason_parameter_change"), Summary.MissReasonParameterChangeCount);
        RunObj->SetNumberField(TEXT("miss_reason_input_change"), Summary.MissReasonInputChangeCount);
        RunObj->SetNumberField(TEXT("miss_reason_version_change"), Summary.MissReasonVersionChangeCount);
        RunObj->SetNumberField(TEXT("miss_reason_unknown"), Summary.MissReasonUnknownCount);
        RunObj->SetNumberField(TEXT("cache_reuse_saved_ms_estimate"), Summary.CacheReuseSavedMsEstimate);
        PerRunCacheArray.Add(MakeShared<FJsonValueObject>(RunObj));
    }
    CacheAnalysisObj->SetArrayField(TEXT("per_run_cache_stats"), PerRunCacheArray);

    const double ColdStartHitRate = PerRunCacheArray.Num() > 0 ? PerRunCacheArray[0]->AsObject()->GetNumberField(TEXT("cache_hit_rate")) : 0.0;
    double HotStartHitRate = 0.0;
    if (PerRunCacheArray.Num() > 1)
    {
        double SumHot = 0.0;
        for (int32 i = 1; i < PerRunCacheArray.Num(); ++i)
        {
            SumHot += PerRunCacheArray[i]->AsObject()->GetNumberField(TEXT("cache_hit_rate"));
        }
        HotStartHitRate = SumHot / static_cast<double>(PerRunCacheArray.Num() - 1);
    }

    TSharedPtr<FJsonObject> ColdHotObj = MakeShared<FJsonObject>();
    ColdHotObj->SetNumberField(TEXT("cold_start_hit_rate"), ColdStartHitRate);
    ColdHotObj->SetNumberField(TEXT("hot_start_hit_rate"), HotStartHitRate);
    ColdHotObj->SetNumberField(TEXT("hot_minus_cold"), HotStartHitRate - ColdStartHitRate);
    CacheAnalysisObj->SetObjectField(TEXT("cold_vs_hot_start"), ColdHotObj);

    TArray<TSharedPtr<FJsonValue>> PropagationArray;
    constexpr int32 PropagationTopN = 20;
    for (int32 i = 0; i < PropagationItems.Num() && i < PropagationTopN; ++i)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("edge"), PropagationItems[i].Name);
        Item->SetNumberField(TEXT("propagated_miss_count"), PropagationItems[i].Count);
        PropagationArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    CacheAnalysisObj->SetArrayField(TEXT("cache_invalidation_propagation_chain"), PropagationArray);

    TArray<FPCGProfilerNodeAggregate> SensitivityNodes = Aggregates;
    SensitivityNodes.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B)
    {
        return A.MissReasonParameterChangeCount > B.MissReasonParameterChangeCount;
    });

    TArray<TSharedPtr<FJsonValue>> SensitivityArray;
    constexpr int32 SensitivityTopN = 20;
    for (int32 i = 0; i < SensitivityNodes.Num() && i < SensitivityTopN; ++i)
    {
        const FPCGProfilerNodeAggregate& Node = SensitivityNodes[i];
        if (Node.MissReasonParameterChangeCount <= 0)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("parameter_change_miss_count"), Node.MissReasonParameterChangeCount);
        Item->SetNumberField(TEXT("cache_hit_rate"), Node.CacheHitRate);
        SensitivityArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    CacheAnalysisObj->SetArrayField(TEXT("parameter_sensitivity_report"), SensitivityArray);
    Root->SetObjectField(TEXT("cache_analysis"), CacheAnalysisObj);

    const double GameThreadTotalMs = ThreadLoadBuckets.FindRef(TEXT("GameThread")).TotalMs;
    const double WorkerThreadTotalMs = ThreadLoadBuckets.FindRef(TEXT("Worker")).TotalMs;
    const double UnknownThreadTotalMs = ThreadLoadBuckets.FindRef(TEXT("Unknown")).TotalMs;
    const int32 GameThreadCallCount = ThreadLoadBuckets.FindRef(TEXT("GameThread")).CallCount;
    const int32 WorkerThreadCallCount = ThreadLoadBuckets.FindRef(TEXT("Worker")).CallCount;
    const int32 UnknownThreadCallCount = ThreadLoadBuckets.FindRef(TEXT("Unknown")).CallCount;
    const double ThreadTotalMs = GameThreadTotalMs + WorkerThreadTotalMs + UnknownThreadTotalMs;

    TSharedPtr<FJsonObject> ThreadLoadObj = MakeShared<FJsonObject>();
    ThreadLoadObj->SetNumberField(TEXT("game_thread_total_ms"), GameThreadTotalMs);
    ThreadLoadObj->SetNumberField(TEXT("worker_total_ms"), WorkerThreadTotalMs);
    ThreadLoadObj->SetNumberField(TEXT("unknown_total_ms"), UnknownThreadTotalMs);
    ThreadLoadObj->SetNumberField(TEXT("game_thread_call_count"), GameThreadCallCount);
    ThreadLoadObj->SetNumberField(TEXT("worker_call_count"), WorkerThreadCallCount);
    ThreadLoadObj->SetNumberField(TEXT("unknown_call_count"), UnknownThreadCallCount);
    ThreadLoadObj->SetNumberField(TEXT("total_ms"), ThreadTotalMs);
    ThreadLoadObj->SetNumberField(TEXT("game_thread_ratio"), ThreadTotalMs > 0.0 ? (GameThreadTotalMs / ThreadTotalMs) : 0.0);
    ThreadLoadObj->SetNumberField(TEXT("worker_ratio"), ThreadTotalMs > 0.0 ? (WorkerThreadTotalMs / ThreadTotalMs) : 0.0);
    ThreadLoadObj->SetNumberField(TEXT("unknown_ratio"), ThreadTotalMs > 0.0 ? (UnknownThreadTotalMs / ThreadTotalMs) : 0.0);

    TMap<FString, TMap<FString, FPhaseStats>> ThreadPhaseBuckets;
    for (const FPCGProfilerNodeEvent& Event : LocalEvents)
    {
        const FString ThreadGroup = PCGProfilerThreading::NormalizeThreadGroup(Event.ThreadGroup);
        const FString PhaseName = Event.Phase.IsEmpty() ? TEXT("Execute") : Event.Phase;
        FPhaseStats& Stats = ThreadPhaseBuckets.FindOrAdd(ThreadGroup).FindOrAdd(PhaseName);
        ++Stats.CallCount;
        Stats.TotalMs += Event.InclusiveMs;
        Stats.MaxMs = FMath::Max(Stats.MaxMs, Event.InclusiveMs);
    }

    TArray<TSharedPtr<FJsonValue>> ThreadByPhaseArray;
    for (const TPair<FString, TMap<FString, FPhaseStats>>& ThreadKV : ThreadPhaseBuckets)
    {
        TSharedPtr<FJsonObject> ThreadObj = MakeShared<FJsonObject>();
        ThreadObj->SetStringField(TEXT("thread_group"), ThreadKV.Key);
        TArray<TSharedPtr<FJsonValue>> PhaseStatsArray;
        for (const TPair<FString, FPhaseStats>& PhaseKV : ThreadKV.Value)
        {
            TSharedPtr<FJsonObject> PhaseObj = MakeShared<FJsonObject>();
            PhaseObj->SetStringField(TEXT("phase"), PhaseKV.Key);
            PhaseObj->SetNumberField(TEXT("call_count"), PhaseKV.Value.CallCount);
            PhaseObj->SetNumberField(TEXT("total_ms"), PhaseKV.Value.TotalMs);
            PhaseObj->SetNumberField(TEXT("avg_ms"), PhaseKV.Value.CallCount > 0 ? (PhaseKV.Value.TotalMs / static_cast<double>(PhaseKV.Value.CallCount)) : 0.0);
            PhaseObj->SetNumberField(TEXT("max_ms"), PhaseKV.Value.MaxMs);
            PhaseStatsArray.Add(MakeShared<FJsonValueObject>(PhaseObj));
        }
        ThreadObj->SetArrayField(TEXT("phases"), PhaseStatsArray);
        ThreadByPhaseArray.Add(MakeShared<FJsonValueObject>(ThreadObj));
    }
    ThreadLoadObj->SetArrayField(TEXT("by_phase"), ThreadByPhaseArray);

    constexpr double TimelineWindowMs = 5.0;
    struct FTimelineBucket
    {
        double WindowStartMs = 0.0;
        double WindowEndMs = 0.0;
        double GameThreadMs = 0.0;
        double WorkerThreadMs = 0.0;
        double UnknownThreadMs = 0.0;
    };
    TMap<int32, FTimelineBucket> TimelineBuckets;
    for (const FPCGProfilerNodeEvent& Event : LocalEvents)
    {
        const double StartMs = FMath::Max(0.0, Event.FirstSeenTimeMs);
        const double SpanMs = FMath::Max(Event.DurationMs, Event.InclusiveMs);
        const double EndMs = StartMs + FMath::Max(SpanMs, KINDA_SMALL_NUMBER);
        const int32 StartBucket = FMath::FloorToInt(StartMs / TimelineWindowMs);
        const int32 EndBucket = FMath::FloorToInt((EndMs - KINDA_SMALL_NUMBER) / TimelineWindowMs);
        const FString ThreadGroup = PCGProfilerThreading::NormalizeThreadGroup(Event.ThreadGroup);

        for (int32 Bucket = StartBucket; Bucket <= EndBucket; ++Bucket)
        {
            const double BucketStart = static_cast<double>(Bucket) * TimelineWindowMs;
            const double BucketEnd = BucketStart + TimelineWindowMs;
            const double OverlapStart = FMath::Max(StartMs, BucketStart);
            const double OverlapEnd = FMath::Min(EndMs, BucketEnd);
            const double OverlapMs = FMath::Max(0.0, OverlapEnd - OverlapStart);
            if (OverlapMs <= 0.0)
            {
                continue;
            }

            FTimelineBucket& BucketStats = TimelineBuckets.FindOrAdd(Bucket);
            BucketStats.WindowStartMs = BucketStart;
            BucketStats.WindowEndMs = BucketEnd;
            if (ThreadGroup == TEXT("GameThread"))
            {
                BucketStats.GameThreadMs += OverlapMs;
            }
            else if (ThreadGroup == TEXT("Worker"))
            {
                BucketStats.WorkerThreadMs += OverlapMs;
            }
            else
            {
                BucketStats.UnknownThreadMs += OverlapMs;
            }
        }
    }

    TArray<int32> TimelineKeys;
    TimelineBuckets.GetKeys(TimelineKeys);
    TimelineKeys.Sort();
    TArray<TSharedPtr<FJsonValue>> TimelineArray;
    for (const int32 Key : TimelineKeys)
    {
        const FTimelineBucket& Bucket = TimelineBuckets.FindChecked(Key);
        TSharedPtr<FJsonObject> WindowObj = MakeShared<FJsonObject>();
        WindowObj->SetNumberField(TEXT("window_start_ms"), Bucket.WindowStartMs);
        WindowObj->SetNumberField(TEXT("window_end_ms"), Bucket.WindowEndMs);
        WindowObj->SetNumberField(TEXT("game_thread_ms"), Bucket.GameThreadMs);
        WindowObj->SetNumberField(TEXT("worker_ms"), Bucket.WorkerThreadMs);
        WindowObj->SetNumberField(TEXT("unknown_ms"), Bucket.UnknownThreadMs);
        TimelineArray.Add(MakeShared<FJsonValueObject>(WindowObj));
    }
    ThreadLoadObj->SetNumberField(TEXT("timeline_window_ms"), TimelineWindowMs);
    ThreadLoadObj->SetArrayField(TEXT("timeline_windows"), TimelineArray);

    struct FConcurrencyPoint
    {
        double TimeMs = 0.0;
        int32 Delta = 0;
        int32 GameDelta = 0;
        int32 WorkerDelta = 0;
    };
    TArray<FConcurrencyPoint> ConcurrencyPoints;
    ConcurrencyPoints.Reserve(LocalEvents.Num() * 2);
    for (const FPCGProfilerNodeEvent& Event : LocalEvents)
    {
        const double StartMs = FMath::Max(0.0, Event.FirstSeenTimeMs);
        const double SpanMs = FMath::Max(Event.DurationMs, Event.InclusiveMs);
        const double EndMs = StartMs + FMath::Max(SpanMs, KINDA_SMALL_NUMBER);
        const FString ThreadGroup = PCGProfilerThreading::NormalizeThreadGroup(Event.ThreadGroup);

        FConcurrencyPoint StartPoint;
        StartPoint.TimeMs = StartMs;
        StartPoint.Delta = 1;
        StartPoint.GameDelta = (ThreadGroup == TEXT("GameThread")) ? 1 : 0;
        StartPoint.WorkerDelta = (ThreadGroup == TEXT("Worker")) ? 1 : 0;
        ConcurrencyPoints.Add(StartPoint);

        FConcurrencyPoint EndPoint;
        EndPoint.TimeMs = EndMs;
        EndPoint.Delta = -1;
        EndPoint.GameDelta = (ThreadGroup == TEXT("GameThread")) ? -1 : 0;
        EndPoint.WorkerDelta = (ThreadGroup == TEXT("Worker")) ? -1 : 0;
        ConcurrencyPoints.Add(EndPoint);
    }

    ConcurrencyPoints.Sort([](const FConcurrencyPoint& A, const FConcurrencyPoint& B)
    {
        if (A.TimeMs == B.TimeMs)
        {
            return A.Delta > B.Delta;
        }
        return A.TimeMs < B.TimeMs;
    });

    int32 ActiveTotal = 0;
    int32 ActiveGame = 0;
    int32 ActiveWorker = 0;
    int32 PeakParallel = 0;
    double WeightedParallelSum = 0.0;
    double TotalTimelineMs = 0.0;
    double OverlapMs = 0.0;
    double WorkerActiveMs = 0.0;
    double PrevTimeMs = 0.0;
    bool bHasPrevTime = false;
    for (int32 Index = 0; Index < ConcurrencyPoints.Num(); ++Index)
    {
        const FConcurrencyPoint& Point = ConcurrencyPoints[Index];
        if (bHasPrevTime && Point.TimeMs > PrevTimeMs)
        {
            const double DeltaMs = Point.TimeMs - PrevTimeMs;
            WeightedParallelSum += static_cast<double>(ActiveTotal) * DeltaMs;
            TotalTimelineMs += DeltaMs;
            if (ActiveGame > 0 && ActiveWorker > 0)
            {
                OverlapMs += DeltaMs;
            }
            if (ActiveWorker > 0)
            {
                WorkerActiveMs += DeltaMs;
            }
        }

        ActiveTotal += Point.Delta;
        ActiveGame += Point.GameDelta;
        ActiveWorker += Point.WorkerDelta;
        PeakParallel = FMath::Max(PeakParallel, ActiveTotal);
        PrevTimeMs = Point.TimeMs;
        bHasPrevTime = true;
    }

    TSharedPtr<FJsonObject> ConcurrencyObj = MakeShared<FJsonObject>();
    ConcurrencyObj->SetNumberField(TEXT("peak_parallel_events"), PeakParallel);
    ConcurrencyObj->SetNumberField(TEXT("avg_parallel_events"), TotalTimelineMs > 0.0 ? (WeightedParallelSum / TotalTimelineMs) : 0.0);
    ConcurrencyObj->SetNumberField(TEXT("overlap_ms"), OverlapMs);
    ThreadLoadObj->SetObjectField(TEXT("concurrency"), ConcurrencyObj);

    double WorkerExecutionTotalMs = 0.0;
    double WorkerQueueWaitTotalMs = 0.0;
    for (const FPCGProfilerNodeEvent& Event : LocalEvents)
    {
        if (PCGProfilerThreading::NormalizeThreadGroup(Event.ThreadGroup) == TEXT("Worker"))
        {
            WorkerExecutionTotalMs += Event.ExecutionMs;
            WorkerQueueWaitTotalMs += Event.QueueWaitMs;
        }
    }

    const int32 TheoreticalWorkerConcurrency = FMath::Max(1, FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 1);
    const double EffectiveWorkerConcurrency = WorkerActiveMs > 0.0 ? (WorkerExecutionTotalMs / WorkerActiveMs) : 0.0;
    const double WorkerParallelEfficiency = TheoreticalWorkerConcurrency > 0 ? FMath::Clamp(EffectiveWorkerConcurrency / static_cast<double>(TheoreticalWorkerConcurrency), 0.0, 1.0) : 0.0;

    TSharedPtr<FJsonObject> WorkerParallelismObj = MakeShared<FJsonObject>();
    WorkerParallelismObj->SetNumberField(TEXT("effective_concurrency"), EffectiveWorkerConcurrency);
    WorkerParallelismObj->SetNumberField(TEXT("theoretical_concurrency"), TheoreticalWorkerConcurrency);
    WorkerParallelismObj->SetNumberField(TEXT("efficiency_ratio"), WorkerParallelEfficiency);
    WorkerParallelismObj->SetNumberField(TEXT("worker_active_ms"), WorkerActiveMs);
    WorkerParallelismObj->SetNumberField(TEXT("worker_execution_total_ms"), WorkerExecutionTotalMs);
    ThreadLoadObj->SetObjectField(TEXT("worker_parallelism"), WorkerParallelismObj);

    TSharedPtr<FJsonObject> QueueWaitObj = MakeShared<FJsonObject>();
    QueueWaitObj->SetNumberField(TEXT("worker_queue_wait_total_ms"), WorkerQueueWaitTotalMs);
    QueueWaitObj->SetNumberField(TEXT("worker_execution_total_ms"), WorkerExecutionTotalMs);
    QueueWaitObj->SetNumberField(TEXT("worker_queue_wait_ratio"), (WorkerQueueWaitTotalMs + WorkerExecutionTotalMs) > 0.0 ? (WorkerQueueWaitTotalMs / (WorkerQueueWaitTotalMs + WorkerExecutionTotalMs)) : 0.0);
    ThreadLoadObj->SetObjectField(TEXT("queue_wait_vs_execution"), QueueWaitObj);

    TSharedPtr<FJsonObject> ImbalanceObj = MakeShared<FJsonObject>();
    ImbalanceObj->SetNumberField(TEXT("game_vs_worker_ratio"), WorkerThreadTotalMs > 0.0 ? (GameThreadTotalMs / WorkerThreadTotalMs) : 0.0);
    ImbalanceObj->SetNumberField(TEXT("imbalance_score"), ThreadTotalMs > 0.0 ? (FMath::Abs(GameThreadTotalMs - WorkerThreadTotalMs) / ThreadTotalMs) : 0.0);
    ThreadLoadObj->SetObjectField(TEXT("imbalance"), ImbalanceObj);

    constexpr int32 ThreadTopN = 10;
    TArray<FPCGProfilerNodeAggregate> TopGameNodes = Aggregates;
    TopGameNodes.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B) { return A.GameThreadMs > B.GameThreadMs; });
    TopGameNodes = TopGameNodes.FilterByPredicate([](const FPCGProfilerNodeAggregate& Node) { return Node.GameThreadMs > 0.0; });
    if (TopGameNodes.Num() > ThreadTopN)
    {
        TopGameNodes.SetNum(ThreadTopN);
    }

    TArray<FPCGProfilerNodeAggregate> TopWorkerNodes = Aggregates;
    TopWorkerNodes.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B) { return A.WorkerThreadMs > B.WorkerThreadMs; });
    TopWorkerNodes = TopWorkerNodes.FilterByPredicate([](const FPCGProfilerNodeAggregate& Node) { return Node.WorkerThreadMs > 0.0; });
    if (TopWorkerNodes.Num() > ThreadTopN)
    {
        TopWorkerNodes.SetNum(ThreadTopN);
    }

    TArray<TSharedPtr<FJsonValue>> TopGameArray;
    for (const FPCGProfilerNodeAggregate& Node : TopGameNodes)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("thread_total_ms"), Node.GameThreadMs);
        Item->SetNumberField(TEXT("thread_call_count"), Node.GameThreadCallCount);
        Item->SetNumberField(TEXT("node_peak_ms"), Node.MaxMs);
        TopGameArray.Add(MakeShared<FJsonValueObject>(Item));
    }

    TArray<TSharedPtr<FJsonValue>> TopWorkerArray;
    for (const FPCGProfilerNodeAggregate& Node : TopWorkerNodes)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("thread_total_ms"), Node.WorkerThreadMs);
        Item->SetNumberField(TEXT("thread_call_count"), Node.WorkerThreadCallCount);
        Item->SetNumberField(TEXT("node_peak_ms"), Node.MaxMs);
        TopWorkerArray.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> TopNodesByThreadObj = MakeShared<FJsonObject>();
    TopNodesByThreadObj->SetArrayField(TEXT("top_game_thread_nodes"), TopGameArray);
    TopNodesByThreadObj->SetArrayField(TEXT("top_worker_nodes"), TopWorkerArray);
    ThreadLoadObj->SetObjectField(TEXT("top_nodes_by_thread"), TopNodesByThreadObj);

    TArray<TSharedPtr<FJsonValue>> JitterArray;
    for (const FPCGProfilerNodeAggregate& Node : Aggregates)
    {
        if (Node.GameThreadCallCount < 2 || Node.GameThreadMs <= 0.0 || Node.P95Ms <= 0.0)
        {
            continue;
        }

        const double SpikeRatio = Node.MaxMs / Node.P95Ms;
        if (SpikeRatio < 2.0)
        {
            continue;
        }

        TSharedPtr<FJsonObject> JitterObj = MakeShared<FJsonObject>();
        JitterObj->SetStringField(TEXT("node_id"), Node.NodeId);
        JitterObj->SetStringField(TEXT("node_name"), Node.NodeName);
        JitterObj->SetStringField(TEXT("graph_name"), Node.GraphName);
        JitterObj->SetNumberField(TEXT("gt_max_ms"), Node.MaxMs);
        JitterObj->SetNumberField(TEXT("gt_p95_ms"), Node.P95Ms);
        JitterObj->SetNumberField(TEXT("spike_ratio"), SpikeRatio);
        JitterArray.Add(MakeShared<FJsonValueObject>(JitterObj));
    }
    ThreadLoadObj->SetArrayField(TEXT("thread_jitter"), JitterArray);

    TArray<FPCGProfilerNodeAggregate> TopQueueWaitNodes = Aggregates;
    TopQueueWaitNodes.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B) { return A.QueueWaitTotalMs > B.QueueWaitTotalMs; });
    if (TopQueueWaitNodes.Num() > ThreadTopN)
    {
        TopQueueWaitNodes.SetNum(ThreadTopN);
    }

    TArray<TSharedPtr<FJsonValue>> SyncPointArray;
    for (const FPCGProfilerNodeAggregate& Node : TopQueueWaitNodes)
    {
        if (Node.QueueWaitTotalMs <= 0.0)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("queue_wait_total_ms"), Node.QueueWaitTotalMs);
        Item->SetNumberField(TEXT("execution_total_ms"), Node.ExecutionTotalMs);
        SyncPointArray.Add(MakeShared<FJsonValueObject>(Item));
    }

    TArray<FPCGProfilerNodeAggregate> MainThreadHotspots = Aggregates;
    MainThreadHotspots.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B) { return A.GameThreadMs > B.GameThreadMs; });
    if (MainThreadHotspots.Num() > ThreadTopN)
    {
        MainThreadHotspots.SetNum(ThreadTopN);
    }

    TArray<TSharedPtr<FJsonValue>> MainThreadHotspotArray;
    for (const FPCGProfilerNodeAggregate& Node : MainThreadHotspots)
    {
        if (Node.GameThreadMs <= 0.0)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("game_thread_ms"), Node.GameThreadMs);
        Item->SetNumberField(TEXT("game_thread_call_count"), Node.GameThreadCallCount);
        MainThreadHotspotArray.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> SchedulingObj = MakeShared<FJsonObject>();
    SchedulingObj->SetArrayField(TEXT("sync_points"), SyncPointArray);
    SchedulingObj->SetArrayField(TEXT("main_thread_hotspots"), MainThreadHotspotArray);
    ThreadLoadObj->SetObjectField(TEXT("scheduling_bottlenecks"), SchedulingObj);

    if (!bLocalRunThreadSummaryFinalized && !LocalEvents.IsEmpty())
    {
        FRunThreadSummary CurrentRunSummary;
        CurrentRunSummary.RunName = LocalRunName;
        CurrentRunSummary.GameThreadTotalMs = GameThreadTotalMs;
        CurrentRunSummary.WorkerTotalMs = WorkerThreadTotalMs;
        CurrentRunSummary.UnknownTotalMs = UnknownThreadTotalMs;
        CurrentRunSummary.GameThreadCallCount = GameThreadCallCount;
        CurrentRunSummary.WorkerCallCount = WorkerThreadCallCount;
        CurrentRunSummary.UnknownCallCount = UnknownThreadCallCount;
        for (const FPCGProfilerNodeEvent& Event : LocalEvents)
        {
            if (Event.bCacheHit)
            {
                ++CurrentRunSummary.CacheHitCount;
                CurrentRunSummary.CacheReuseSavedMsEstimate += Event.InclusiveMs;
            }
            else
            {
                ++CurrentRunSummary.CacheMissCount;
                if (Event.CacheMissReason == TEXT("input_change"))
                {
                    ++CurrentRunSummary.MissReasonInputChangeCount;
                }
                else if (Event.CacheMissReason == TEXT("version_change"))
                {
                    ++CurrentRunSummary.MissReasonVersionChangeCount;
                }
                else if (Event.CacheMissReason == TEXT("unknown"))
                {
                    ++CurrentRunSummary.MissReasonUnknownCount;
                }
                else
                {
                    ++CurrentRunSummary.MissReasonUnknownCount;
                }
            }
        }
        LocalRunThreadHistory.Add(CurrentRunSummary);
    }

    TArray<double> RunGameTotals;
    TArray<double> RunWorkerTotals;
    TArray<double> RunDurations;
    TArray<double> RunParallelScores;
    TArray<double> RunTopNodeP95s;
    RunGameTotals.Reserve(LocalRunThreadHistory.Num());
    RunWorkerTotals.Reserve(LocalRunThreadHistory.Num());
    RunDurations.Reserve(LocalRunThreadHistory.Num());
    RunParallelScores.Reserve(LocalRunThreadHistory.Num());
    RunTopNodeP95s.Reserve(LocalRunThreadHistory.Num());

    TArray<TSharedPtr<FJsonValue>> PerRunArray;
    for (const FRunThreadSummary& Summary : LocalRunThreadHistory)
    {
        const double RunTotalMs = Summary.GameThreadTotalMs + Summary.WorkerTotalMs + Summary.UnknownTotalMs;
        TSharedPtr<FJsonObject> RunObj = MakeShared<FJsonObject>();
        RunObj->SetStringField(TEXT("run_name"), Summary.RunName);
        RunObj->SetNumberField(TEXT("run_duration_ms"), Summary.RunDurationMs);
        RunObj->SetNumberField(TEXT("parallel_efficiency_score"), Summary.ParallelEfficiencyScore);
        RunObj->SetNumberField(TEXT("topn_node_p95_ms"), Summary.TopNNodeP95Ms);
        RunObj->SetNumberField(TEXT("game_thread_total_ms"), Summary.GameThreadTotalMs);
        RunObj->SetNumberField(TEXT("worker_total_ms"), Summary.WorkerTotalMs);
        RunObj->SetNumberField(TEXT("unknown_total_ms"), Summary.UnknownTotalMs);
        RunObj->SetNumberField(TEXT("game_thread_call_count"), Summary.GameThreadCallCount);
        RunObj->SetNumberField(TEXT("worker_call_count"), Summary.WorkerCallCount);
        RunObj->SetNumberField(TEXT("unknown_call_count"), Summary.UnknownCallCount);
        RunObj->SetNumberField(TEXT("game_thread_ratio"), RunTotalMs > 0.0 ? (Summary.GameThreadTotalMs / RunTotalMs) : 0.0);
        RunObj->SetNumberField(TEXT("worker_ratio"), RunTotalMs > 0.0 ? (Summary.WorkerTotalMs / RunTotalMs) : 0.0);
        PerRunArray.Add(MakeShared<FJsonValueObject>(RunObj));
        RunGameTotals.Add(Summary.GameThreadTotalMs);
        RunWorkerTotals.Add(Summary.WorkerTotalMs);
        RunDurations.Add(Summary.RunDurationMs);
        RunParallelScores.Add(Summary.ParallelEfficiencyScore);
        RunTopNodeP95s.Add(Summary.TopNNodeP95Ms);
    }

    TSharedPtr<FJsonObject> PerRunSummaryObj = MakeShared<FJsonObject>();
    PerRunSummaryObj->SetArrayField(TEXT("runs"), PerRunArray);
    PerRunSummaryObj->SetNumberField(TEXT("run_count"), LocalRunThreadHistory.Num());
    PerRunSummaryObj->SetNumberField(TEXT("game_thread_p50_ms"), ComputePercentile(RunGameTotals, 0.50));
    PerRunSummaryObj->SetNumberField(TEXT("game_thread_p95_ms"), ComputePercentile(RunGameTotals, 0.95));
    PerRunSummaryObj->SetNumberField(TEXT("worker_p50_ms"), ComputePercentile(RunWorkerTotals, 0.50));
    PerRunSummaryObj->SetNumberField(TEXT("worker_p95_ms"), ComputePercentile(RunWorkerTotals, 0.95));
    ThreadLoadObj->SetObjectField(TEXT("per_run_summary"), PerRunSummaryObj);

    auto ComputeCv = [](const TArray<double>& Values) -> double
    {
        if (Values.Num() < 2)
        {
            return 0.0;
        }
        double Mean = 0.0;
        for (const double V : Values) { Mean += V; }
        Mean /= static_cast<double>(Values.Num());
        if (Mean <= 0.0)
        {
            return 0.0;
        }
        double SumSq = 0.0;
        for (const double V : Values)
        {
            const double D = V - Mean;
            SumSq += D * D;
        }
        const double StdDev = FMath::Sqrt(SumSq / static_cast<double>(Values.Num()));
        return StdDev / Mean;
    };

    TSharedPtr<FJsonObject> StabilityObj = MakeShared<FJsonObject>();
    StabilityObj->SetNumberField(TEXT("run_count"), LocalRunThreadHistory.Num());
    StabilityObj->SetNumberField(TEXT("run_duration_cv"), ComputeCv(RunDurations));
    StabilityObj->SetNumberField(TEXT("parallel_efficiency_cv"), ComputeCv(RunParallelScores));
    StabilityObj->SetNumberField(TEXT("topn_node_p95_cv"), ComputeCv(RunTopNodeP95s));
    Root->SetObjectField(TEXT("stability"), StabilityObj);

    const double QueuePressure = (WorkerQueueWaitTotalMs + WorkerExecutionTotalMs) > 0.0 ? (WorkerQueueWaitTotalMs / (WorkerQueueWaitTotalMs + WorkerExecutionTotalMs)) : 0.0;
    const double MainThreadPressure = ThreadTotalMs > 0.0 ? (GameThreadTotalMs / ThreadTotalMs) : 0.0;
    const double RawScore = 100.0 * (0.55 * WorkerParallelEfficiency + 0.25 * (1.0 - QueuePressure) + 0.20 * (1.0 - MainThreadPressure));
    const double ParallelEfficiencyScore = FMath::Clamp(RawScore, 0.0, 100.0);

    TArray<TSharedPtr<FJsonValue>> Suggestions;
    if (WorkerParallelEfficiency < 0.40)
    {
        Suggestions.Add(MakeShared<FJsonValueString>(TEXT("Worker parallel efficiency is low; split heavy worker nodes into smaller independent tasks.")));
    }
    if (QueuePressure > 0.25)
    {
        Suggestions.Add(MakeShared<FJsonValueString>(TEXT("Queue wait is high; reduce sync barriers and batch task dispatch to lower scheduler contention.")));
    }
    if (MainThreadPressure > 0.65)
    {
        Suggestions.Add(MakeShared<FJsonValueString>(TEXT("Main-thread pressure is high; move non-critical node work off GameThread where possible.")));
    }
    if (Suggestions.IsEmpty())
    {
        Suggestions.Add(MakeShared<FJsonValueString>(TEXT("Parallel utilization looks healthy; prioritize hotspot-specific algorithmic optimization.")));
    }

    TSharedPtr<FJsonObject> ParallelEfficiencyObj = MakeShared<FJsonObject>();
    ParallelEfficiencyObj->SetNumberField(TEXT("score"), ParallelEfficiencyScore);
    ParallelEfficiencyObj->SetNumberField(TEXT("worker_efficiency_ratio"), WorkerParallelEfficiency);
    ParallelEfficiencyObj->SetNumberField(TEXT("queue_wait_ratio"), QueuePressure);
    ParallelEfficiencyObj->SetNumberField(TEXT("main_thread_pressure"), MainThreadPressure);
    ParallelEfficiencyObj->SetArrayField(TEXT("suggestions"), Suggestions);
    ThreadLoadObj->SetObjectField(TEXT("parallel_efficiency"), ParallelEfficiencyObj);

    Root->SetObjectField(TEXT("thread_load"), ThreadLoadObj);

    TArray<TSharedPtr<FJsonValue>> PhaseArray;
    for (const TPair<FString, FPhaseStats>& PhaseKV : PhaseBuckets)
    {
        TSharedPtr<FJsonObject> PhaseObj = MakeShared<FJsonObject>();
        PhaseObj->SetStringField(TEXT("phase"), PhaseKV.Key);
        PhaseObj->SetNumberField(TEXT("call_count"), PhaseKV.Value.CallCount);
        PhaseObj->SetNumberField(TEXT("total_ms"), PhaseKV.Value.TotalMs);
        PhaseObj->SetNumberField(TEXT("avg_ms"), PhaseKV.Value.CallCount > 0 ? (PhaseKV.Value.TotalMs / static_cast<double>(PhaseKV.Value.CallCount)) : 0.0);
        PhaseObj->SetNumberField(TEXT("max_ms"), PhaseKV.Value.MaxMs);
        PhaseArray.Add(MakeShared<FJsonValueObject>(PhaseObj));
    }
    Root->SetArrayField(TEXT("phase_buckets"), PhaseArray);

    constexpr int32 TopN = 20;
    TArray<FPCGProfilerNodeAggregate> TopByTotal = Aggregates;
    TopByTotal.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B) { return A.TotalMs > B.TotalMs; });
    if (TopByTotal.Num() > TopN)
    {
        TopByTotal.SetNum(TopN);
    }

    TArray<FPCGProfilerNodeAggregate> TopByPeak = Aggregates;
    TopByPeak.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B) { return A.MaxMs > B.MaxMs; });
    if (TopByPeak.Num() > TopN)
    {
        TopByPeak.SetNum(TopN);
    }

    TSharedPtr<FJsonObject> TopNObj = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> TopByTotalArray;
    for (const FPCGProfilerNodeAggregate& Node : TopByTotal)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("total_ms"), Node.TotalMs);
        Item->SetNumberField(TEXT("call_count"), Node.CallCount);
        TopByTotalArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    TopNObj->SetArrayField(TEXT("by_total_ms"), TopByTotalArray);

    TArray<TSharedPtr<FJsonValue>> TopByPeakArray;
    for (const FPCGProfilerNodeAggregate& Node : TopByPeak)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("max_ms"), Node.MaxMs);
        Item->SetNumberField(TEXT("call_count"), Node.CallCount);
        TopByPeakArray.Add(MakeShared<FJsonValueObject>(Item));
    }
    TopNObj->SetArrayField(TEXT("by_peak_ms"), TopByPeakArray);
    Root->SetObjectField(TEXT("top_n"), TopNObj);

    TArray<FPCGProfilerNodeAggregate> StabilityNodes = Aggregates;
    StabilityNodes.Sort([](const FPCGProfilerNodeAggregate& A, const FPCGProfilerNodeAggregate& B) { return A.TotalMs > B.TotalMs; });
    TArray<TSharedPtr<FJsonValue>> StabilityNodeArray;
    int32 StabilityCount = 0;
    constexpr int32 StabilityTopN = 20;
    for (const FPCGProfilerNodeAggregate& Node : StabilityNodes)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node.NodeId);
        Item->SetStringField(TEXT("node_name"), Node.NodeName);
        Item->SetStringField(TEXT("graph_name"), Node.GraphName);
        Item->SetNumberField(TEXT("sample_count"), Node.SampleCount);
        Item->SetNumberField(TEXT("duration_cv"), Node.DurationCv);
        Item->SetNumberField(TEXT("queue_wait_cv"), Node.QueueWaitCv);
        Item->SetNumberField(TEXT("input_points_cv"), Node.InputPointsCv);
        Item->SetNumberField(TEXT("output_points_cv"), Node.OutputPointsCv);
        StabilityNodeArray.Add(MakeShared<FJsonValueObject>(Item));
        ++StabilityCount;
        if (StabilityCount >= StabilityTopN)
        {
            break;
        }
    }
    Root->SetArrayField(TEXT("node_stability_topn"), StabilityNodeArray);

    TArray<TSharedPtr<FJsonValue>> CrossRunArray;
    for (const TPair<FString, FCrossRunNodeStats>& CrossKV : LocalCrossRunStats)
    {
        const FCrossRunNodeStats& CrossStats = CrossKV.Value;
        if (CrossStats.TotalCalls <= 0)
        {
            continue;
        }

        TSharedPtr<FJsonObject> CrossObj = MakeShared<FJsonObject>();
        CrossObj->SetStringField(TEXT("node_key"), CrossKV.Key);
        CrossObj->SetNumberField(TEXT("total_calls"), CrossStats.TotalCalls);
        CrossObj->SetNumberField(TEXT("inclusive_p50_ms"), ComputePercentile(CrossStats.InclusiveSamples, 0.50));
        CrossObj->SetNumberField(TEXT("inclusive_p95_ms"), ComputePercentile(CrossStats.InclusiveSamples, 0.95));
        CrossObj->SetNumberField(TEXT("self_p50_ms"), ComputePercentile(CrossStats.SelfSamples, 0.50));
        CrossObj->SetNumberField(TEXT("self_p95_ms"), ComputePercentile(CrossStats.SelfSamples, 0.95));
        CrossRunArray.Add(MakeShared<FJsonValueObject>(CrossObj));
    }
    Root->SetArrayField(TEXT("cross_run_node_stats"), CrossRunArray);

    TArray<TSharedPtr<FJsonValue>> EdgeArray;
    TArray<FEdgeStats> EdgeList;
    EdgeStatsByKey.GenerateValueArray(EdgeList);
    EdgeList.Sort([](const FEdgeStats& A, const FEdgeStats& B) { return A.TotalChildInclusiveMs > B.TotalChildInclusiveMs; });
    for (const FEdgeStats& Edge : EdgeList)
    {
        TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
        EdgeObj->SetStringField(TEXT("graph_name"), Edge.GraphName);
        EdgeObj->SetStringField(TEXT("parent_node_id"), Edge.ParentNodeId);
        EdgeObj->SetStringField(TEXT("parent_node_title"), Edge.ParentNodeTitle);
        EdgeObj->SetStringField(TEXT("child_node_id"), Edge.ChildNodeId);
        EdgeObj->SetStringField(TEXT("child_node_title"), Edge.ChildNodeTitle);
        EdgeObj->SetNumberField(TEXT("call_count"), Edge.CallCount);
        EdgeObj->SetNumberField(TEXT("total_child_inclusive_ms"), Edge.TotalChildInclusiveMs);
        EdgeObj->SetNumberField(TEXT("max_child_inclusive_ms"), Edge.MaxChildInclusiveMs);
        EdgeArray.Add(MakeShared<FJsonValueObject>(EdgeObj));
    }
    Root->SetArrayField(TEXT("link_contribution_graph"), EdgeArray);

    TArray<FPCGProfilerNodeEvent> CriticalPathEvents;
    FString CriticalPathMethod = TEXT("inclusive_topn_approx");
    if (bLocalStrictCriticalPathMode)
    {
        CriticalPathMethod = TEXT("dag_longest_path_self_weighted");
        CriticalPathEvents = PCGProfilerCriticalPath::BuildStrictDagPath(LocalEvents, TopN);
    }
    else
    {
        CriticalPathEvents = LocalEvents;
        CriticalPathEvents.Sort([](const FPCGProfilerNodeEvent& A, const FPCGProfilerNodeEvent& B)
        {
            return A.InclusiveMs > B.InclusiveMs;
        });
        if (CriticalPathEvents.Num() > TopN)
        {
            CriticalPathEvents.SetNum(TopN);
        }
    }

    TArray<TSharedPtr<FJsonValue>> CriticalPathArray;
    for (const FPCGProfilerNodeEvent& Event : CriticalPathEvents)
    {
        TSharedPtr<FJsonObject> CriticalObj = MakeShared<FJsonObject>();
        CriticalObj->SetStringField(TEXT("node_id"), Event.NodeId);
        CriticalObj->SetStringField(TEXT("node_title"), Event.NodeTitle);
        CriticalObj->SetStringField(TEXT("graph_name"), Event.GraphName);
        CriticalObj->SetStringField(TEXT("phase"), Event.Phase);
        CriticalObj->SetStringField(TEXT("execution_path"), Event.ExecutionPath);
        CriticalObj->SetStringField(TEXT("parent_execution_path"), Event.ParentExecutionPath);
        CriticalObj->SetNumberField(TEXT("inclusive_ms"), Event.InclusiveMs);
        CriticalObj->SetNumberField(TEXT("self_ms"), Event.SelfMs);
        CriticalObj->SetNumberField(TEXT("start_ms"), Event.FirstSeenTimeMs);
        CriticalPathArray.Add(MakeShared<FJsonValueObject>(CriticalObj));
    }
    Root->SetStringField(TEXT("critical_path_method"), CriticalPathMethod);
    Root->SetArrayField(TEXT("critical_path"), CriticalPathArray);

    FString JsonText;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
    if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
    {
        return false;
    }

    FString OutputPath = OptionalAbsoluteOrRelativePath;
    if (OutputPath.IsEmpty())
    {
        OutputPath = BuildDefaultOutputPath();
    }
    else if (!FPaths::IsRelative(OutputPath))
    {
        // Keep absolute path unchanged.
    }
    else
    {
        OutputPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir(), OutputPath);
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    const FString Directory = FPaths::GetPath(OutputPath);
    if (!Directory.IsEmpty() && !PlatformFile.DirectoryExists(*Directory))
    {
        PlatformFile.CreateDirectoryTree(*Directory);
    }

    if (!FFileHelper::SaveStringToFile(JsonText, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        return false;
    }

    OutSavedPath = OutputPath;
    return true;
}

FString UPCGProfilerSubsystem::BuildDefaultOutputPath()
{
    const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"), TEXT("PCG"));
    const FString FileName = FString::Printf(TEXT("PCGProfiler_%s.json"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
    return FPaths::Combine(Folder, FileName);
}

