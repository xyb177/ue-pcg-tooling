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

void UPCGProfilerSubsystem::FinalizeRunThreadSummary_NoLock()
{
    if (bRunThreadSummaryFinalized || NodeEvents.IsEmpty())
    {
        return;
    }

    FRunThreadSummary Summary;
    Summary.RunName = CurrentRunName;
    if (RunStartUtc.GetTicks() > 0)
    {
        const FDateTime EffectiveEnd = (RunEndUtc.GetTicks() > 0) ? RunEndUtc : FDateTime::UtcNow();
        Summary.RunDurationMs = static_cast<double>((EffectiveEnd - RunStartUtc).GetTotalMilliseconds());
    }

    double WorkerExecutionTotalMs = 0.0;
    double WorkerQueueWaitTotalMs = 0.0;
    for (const FPCGProfilerNodeEvent& Event : NodeEvents)
    {
        if (Event.bCacheHit)
        {
            ++Summary.CacheHitCount;
            Summary.CacheReuseSavedMsEstimate += Event.InclusiveMs;
        }
        else
        {
            ++Summary.CacheMissCount;
            if (Event.CacheMissReason == TEXT("input_change"))
            {
                ++Summary.MissReasonInputChangeCount;
            }
            else if (Event.CacheMissReason == TEXT("version_change"))
            {
                ++Summary.MissReasonVersionChangeCount;
            }
            else
            {
                ++Summary.MissReasonUnknownCount;
            }
        }

        const FString ThreadGroup = PCGProfilerThreading::NormalizeThreadGroup(Event.ThreadGroup);
        if (ThreadGroup == TEXT("GameThread"))
        {
            Summary.GameThreadTotalMs += Event.InclusiveMs;
            ++Summary.GameThreadCallCount;
        }
        else if (ThreadGroup == TEXT("Worker"))
        {
            Summary.WorkerTotalMs += Event.InclusiveMs;
            ++Summary.WorkerCallCount;
            WorkerExecutionTotalMs += Event.ExecutionMs;
            WorkerQueueWaitTotalMs += Event.QueueWaitMs;
        }
        else
        {
            Summary.UnknownTotalMs += Event.InclusiveMs;
            ++Summary.UnknownCallCount;
        }
    }

    const double ThreadTotalMs = Summary.GameThreadTotalMs + Summary.WorkerTotalMs + Summary.UnknownTotalMs;
    const double QueuePressure = (WorkerQueueWaitTotalMs + WorkerExecutionTotalMs) > 0.0 ? (WorkerQueueWaitTotalMs / (WorkerQueueWaitTotalMs + WorkerExecutionTotalMs)) : 0.0;
    const double MainThreadPressure = ThreadTotalMs > 0.0 ? (Summary.GameThreadTotalMs / ThreadTotalMs) : 0.0;
    const double WorkerParallelEfficiency = ThreadTotalMs > 0.0 ? FMath::Clamp(Summary.WorkerTotalMs / ThreadTotalMs, 0.0, 1.0) : 0.0;
    const double RawScore = 100.0 * (0.55 * WorkerParallelEfficiency + 0.25 * (1.0 - QueuePressure) + 0.20 * (1.0 - MainThreadPressure));
    Summary.ParallelEfficiencyScore = FMath::Clamp(RawScore, 0.0, 100.0);

    struct FNodeP95Item { double TotalMs = 0.0; double P95Ms = 0.0; int32 SampleCount = 0; };
    TArray<FNodeP95Item> P95Items;
    for (const TPair<FString, FNodeStats>& KV : NodeStats)
    {
        const FNodeStats& S = KV.Value;
        if (S.CallCount <= 0 || S.Samples.IsEmpty())
        {
            continue;
        }
        FNodeP95Item& Item = P95Items.AddDefaulted_GetRef();
        Item.TotalMs = S.TotalMs;
        Item.P95Ms = ComputePercentile(S.Samples, 0.95);
        Item.SampleCount = S.Samples.Num();
    }
    P95Items.Sort([](const FNodeP95Item& A, const FNodeP95Item& B) { return A.TotalMs > B.TotalMs; });
    constexpr int32 StableTopN = 10;
    int32 Used = 0;
    double P95Sum = 0.0;
    for (const FNodeP95Item& Item : P95Items)
    {
        P95Sum += Item.P95Ms;
        ++Used;
        if (Used >= StableTopN)
        {
            break;
        }
    }
    Summary.TopNNodeP95Ms = Used > 0 ? (P95Sum / static_cast<double>(Used)) : 0.0;

    RunThreadHistory.Add(MoveTemp(Summary));
    bRunThreadSummaryFinalized = true;
}

void UPCGProfilerSubsystem::HarvestFromPCGExecutionInspection_NoLock()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PCGProfiler_HarvestFromInspection);

    if (!bSamplingEnabled)
    {
        return;
    }

    PCGProfilerHarvest::GPinInspectionLookupAttempts = 0;
    PCGProfilerHarvest::GPinInspectionLookupHits = 0;

    if (!GEngine)
    {
        return;
    }

    TSet<FString> SeenExecutionKeys;

    const bool bUseWorldScope = !OneClickWorldPath.IsEmpty();
    const bool bUseComponentScope = !OneClickComponentScopeKeys.IsEmpty();

    int32 WorldCount = 0;
    int32 ComponentCount = 0;
    int32 ExecutedNodeCount = 0;
    int32 EventCount = 0;

    for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
    {
        UWorld* World = WorldContext.World();
        if (!World)
        {
            continue;
        }

        if (World->WorldType != EWorldType::Editor && World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game)
        {
            continue;
        }

        if (bUseWorldScope && World->GetPathName() != OneClickWorldPath)
        {
            continue;
        }
        ++WorldCount;

        for (TActorIterator<AActor> It(World); It; ++It)
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
                if (!Component)
                {
                    continue;
                }

                if (bUseComponentScope)
                {
                    const FString ComponentScopeKey = PCGProfilerRuntime::BuildComponentScopeKey(World, Component);
                    if (!OneClickComponentScopeKeys.Contains(ComponentScopeKey))
                    {
                        continue;
                    }
                }
                ++ComponentCount;

                const TMap<TObjectKey<const UPCGNode>, TSet<FPCGGraphExecutionInspection::FNodeExecutedNotificationData>> ExecutedNodeStacks =
                    Component->GetExecutionState().GetInspection().GetExecutedNodeStacks();
                const FPCGGraphExecutionInspection& Inspection = Component->GetExecutionState().GetInspection();
                ExecutedNodeCount += ExecutedNodeStacks.Num();

                for (const TPair<TObjectKey<const UPCGNode>, TSet<FPCGGraphExecutionInspection::FNodeExecutedNotificationData>>& NodePair : ExecutedNodeStacks)
                {
                    const UPCGNode* Node = NodePair.Key.ResolveObjectPtr();
                    if (!Node)
                    {
                        continue;
                    }

                    for (const FPCGGraphExecutionInspection::FNodeExecutedNotificationData& Notification : NodePair.Value)
                    {
                        FPCGProfilerNodeEvent Event;
                        Event.NodeId = Node->GetName();
                        Event.NodeTitle = Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString();
                        const UPCGSettings* NodeSettings = Node->GetSettings();
                        Event.NodeClass = Node->GetClass()->GetName();
                        Event.SettingsClass = NodeSettings ? NodeSettings->GetClass()->GetName() : FString();

                        Notification.Stack.CreateStackFramePath(Event.ExecutionPath, Node, nullptr);
                        Event.ParentExecutionPath = ResolveParentExecutionPath(Event.ExecutionPath);
                        if (const UPCGGraph* Graph = Notification.Stack.GetGraphForCurrentFrame())
                        {
                            Event.GraphName = Graph->GetName();
                        }

                        Event.ComponentName = Component->GetName();

                        const double PrepareMs = FMath::Max(0.0, Notification.Timer.PrepareDataTime * 1000.0);
                        const double ExecMs = FMath::Max(0.0, Notification.Timer.ExecutionTime * 1000.0);
                        const double PostMs = FMath::Max(0.0, Notification.Timer.PostExecuteTime * 1000.0);
                        const double WallMs = Notification.Timer.TotalWallTime() * 1000.0;
                        const double TotalMs = Notification.Timer.TotalTime() * 1000.0;
                        Event.PrepareDataMs = PrepareMs;
                        Event.ExecutionMs = ExecMs;
                        Event.PostExecuteMs = PostMs;
                        Event.Phase = PCGProfilerPhase::ResolvePhase(PrepareMs, ExecMs, PostMs);
                        const double StageSumMs = PrepareMs + ExecMs + PostMs;
                        Event.DurationMs = WallMs > 0.0 ? WallMs : (TotalMs > 0.0 ? TotalMs : StageSumMs);
                        if (Event.DurationMs <= 0.0)
                        {
                            Event.DurationMs = StageSumMs;
                        }
                        Event.SelfMs = ExecMs > 0.0 ? ExecMs : Event.DurationMs;
                        Event.InclusiveMs = Event.DurationMs > 0.0 ? Event.DurationMs : Event.SelfMs;
                        Event.QueueWaitMs = FMath::Max(0.0, Event.DurationMs - StageSumMs);
                        Event.bCacheHit = (Notification.Timer.PrepareDataTime <= KINDA_SMALL_NUMBER
                            && Notification.Timer.ExecutionTime <= KINDA_SMALL_NUMBER
                            && Notification.Timer.PostExecuteTime <= KINDA_SMALL_NUMBER);
                        Event.CacheMissReason = TEXT("");
                        Event.ThreadGroup = PCGProfilerHarvest::GuessThreadGroup(Node, Notification, Event.DurationMs);
                        Event.ThreadSource = Event.ThreadGroup == TEXT("GameThread") ? TEXT("main_thread_only_element") : TEXT("unknown");
                        Event.FirstSeenTimeMs = 0.0;
                        Event.FirstSeenTimeSource = TEXT("estimated");

                        const FPCGDataCollection InputCollection =
                            PCGProfilerHarvest::BuildPinInspectionCollection(Inspection, Notification.Stack, Node, Node->GetInputPins());
                        const FPCGDataCollection OutputCollection =
                            PCGProfilerHarvest::BuildPinInspectionCollection(Inspection, Notification.Stack, Node, Node->GetOutputPins());

                        Event.InputCount = InputCollection.TaggedData.Num();
                        Event.OutputCount = OutputCollection.TaggedData.Num();
                        Event.InputPoints = PCGProfilerHarvest::CountPointsInCollection(InputCollection);
                        Event.OutputPoints = PCGProfilerHarvest::CountPointsInCollection(OutputCollection);
                        const FString InputType = PCGProfilerDataTyping::InferDataTypeFromCollection(InputCollection);
                        const FString OutputType = PCGProfilerDataTyping::InferDataTypeFromCollection(OutputCollection);
                        Event.DataType = (OutputType != TEXT("Unknown")) ? OutputType : InputType;

                        const double StartTimeSeconds =
                            (Notification.Timer.PrepareDataStartTime != MAX_dbl)
                            ? Notification.Timer.PrepareDataStartTime
                            : ((Notification.Timer.ExecutionStartTime != MAX_dbl) ? Notification.Timer.ExecutionStartTime : -1.0);
                        if (RunStartPlatformSeconds > 0.0 && StartTimeSeconds >= 0.0)
                        {
                            Event.FirstSeenTimeMs = FMath::Max(0.0, (StartTimeSeconds - RunStartPlatformSeconds) * 1000.0);
                            Event.FirstSeenTimeSource = TEXT("timer_start");
                        }

                        const FString DedupKey =
                            Event.NodeId + TEXT("|") +
                            Event.ExecutionPath + TEXT("|") +
                            Event.ParentExecutionPath + TEXT("|") +
                            LexToString(Notification.Timer.PrepareDataTime) + TEXT("|") +
                            LexToString(Notification.Timer.ExecutionTime) + TEXT("|") +
                            LexToString(Notification.Timer.PostExecuteTime) + TEXT("|") +
                            LexToString(Notification.Timer.PrepareDataStartTime) + TEXT("|") +
                            LexToString(Notification.Timer.ExecutionStartTime) + TEXT("|") +
                            LexToString(Notification.Timer.ExecutionEndTime) + TEXT("|") +
                            LexToString(Event.DurationMs) + TEXT("|") +
                            LexToString(Event.InputCount) + TEXT("|") +
                            LexToString(Event.OutputCount) + TEXT("|") +
                            LexToString(static_cast<int64>(Event.FirstSeenTimeMs * 1000.0));
                        if (SeenExecutionKeys.Contains(DedupKey))
                        {
                            continue;
                        }

                        SeenExecutionKeys.Add(DedupKey);
                        RecordNodeEvent(Event);
                        ++EventCount;
                    }
                }
            }
        }
    }

    UE_LOG(LogTemp, Display, TEXT("PCGProfiler Harvest: worlds=%d components=%d executed_nodes=%d harvested_events=%d pin_lookup_hits=%d/%d scoped_world=%s scoped_components=%d"),
        WorldCount, ComponentCount, ExecutedNodeCount, EventCount,
        PCGProfilerHarvest::GPinInspectionLookupHits, PCGProfilerHarvest::GPinInspectionLookupAttempts,
        *(bUseWorldScope ? OneClickWorldPath : FString(TEXT("<all>"))),
        bUseComponentScope ? OneClickComponentScopeKeys.Num() : 0);
}

void UPCGProfilerSubsystem::ResetStreamingState_NoLock()
{
    TotalEventsSeen = 0;
    TotalEventsFlushed = 0;
    TotalEventsDropped = 0;
    FlushChunkIndex = 0;
    FlushCount = 0;
    MemoryWarningCount = 0;
    bMemoryWarningEmitted = false;
    PeakProcessMemoryBytes = 0;
    FlushedChunkPaths.Reset();

    const FString SafeRunName = CurrentRunName.IsEmpty() ? TEXT("unnamed") : CurrentRunName.Replace(TEXT(" "), TEXT("_"));
    EventChunkDir = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("Profiling"),
        TEXT("PCG"),
        TEXT("Chunks"),
        FString::Printf(TEXT("%s_%s"), *SafeRunName, *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))));
}

bool UPCGProfilerSubsystem::FlushEventsToChunk_NoLock(int32 NumEventsToFlush)
{
    if (NumEventsToFlush <= 0 || NodeEvents.IsEmpty())
    {
        return true;
    }

    const int32 SafeFlushCount = FMath::Clamp(NumEventsToFlush, 1, NodeEvents.Num());
    if (EventChunkDir.IsEmpty())
    {
        const FString SafeRunName = CurrentRunName.IsEmpty() ? TEXT("unnamed") : CurrentRunName.Replace(TEXT(" "), TEXT("_"));
        EventChunkDir = FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("Profiling"),
            TEXT("PCG"),
            TEXT("Chunks"),
            FString::Printf(TEXT("%s_%s"), *SafeRunName, *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))));
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*EventChunkDir))
    {
        PlatformFile.CreateDirectoryTree(*EventChunkDir);
    }

    const FString ChunkPath = FPaths::Combine(EventChunkDir, FString::Printf(TEXT("events_%05d.jsonl"), FlushChunkIndex++));
    FString ChunkText;
    ChunkText.Reserve(SafeFlushCount * 256);

    for (int32 Index = 0; Index < SafeFlushCount; ++Index)
    {
        const TSharedPtr<FJsonObject> EventObj = PCGProfilerSerialization::BuildEventJsonObject(NodeEvents[Index]);
        FString EventLine;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&EventLine);
        if (!FJsonSerializer::Serialize(EventObj.ToSharedRef(), Writer))
        {
            continue;
        }

        ChunkText += EventLine;
        ChunkText += TEXT("\n");
    }

    if (!FFileHelper::SaveStringToFile(ChunkText, *ChunkPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        return false;
    }

    NodeEvents.RemoveAt(0, SafeFlushCount, EAllowShrinking::No);
    FlushedChunkPaths.Add(ChunkPath);
    TotalEventsFlushed += SafeFlushCount;
    ++FlushCount;
    return true;
}

void UPCGProfilerSubsystem::MaybeFlushEventsAndWarnings_NoLock()
{
    const int64 CurrentProcessMemoryBytes = LastProcessMemoryBytes > 0 ? LastProcessMemoryBytes : GetProcessMemoryBytes();
    PeakProcessMemoryBytes = FMath::Max(PeakProcessMemoryBytes, CurrentProcessMemoryBytes);
    if (!bMemoryWarningEmitted && CurrentProcessMemoryBytes >= MemoryWarningThresholdBytes)
    {
        bMemoryWarningEmitted = true;
        ++MemoryWarningCount;
        UE_LOG(LogTemp, Warning, TEXT("PCGProfiler memory warning: process memory reached %.2f GB"),
            static_cast<double>(CurrentProcessMemoryBytes) / (1024.0 * 1024.0 * 1024.0));
    }

    if (NodeEvents.Num() < MaxEventsInMemoryBeforeFlush)
    {
        return;
    }

    const int32 FlushCountNow = FMath::Clamp(FlushChunkEventCount, 1, NodeEvents.Num());
    if (!FlushEventsToChunk_NoLock(FlushCountNow))
    {
        // If flush fails, drop oldest data to avoid unbounded growth/OOM.
        NodeEvents.RemoveAt(0, FlushCountNow, EAllowShrinking::No);
        TotalEventsDropped += FlushCountNow;
        UE_LOG(LogTemp, Warning, TEXT("PCGProfiler streaming flush failed; dropped %d events to protect memory."), FlushCountNow);
    }
}


