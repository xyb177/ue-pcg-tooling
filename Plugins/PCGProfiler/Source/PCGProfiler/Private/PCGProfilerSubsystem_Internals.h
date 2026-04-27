#pragma once

#include "CoreMinimal.h"

#include "PCGProfilerSubsystem.h"

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
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace PCGProfilerThreading
{
    static FString NormalizeThreadGroup(const FString& InThreadGroup)
    {
        if (InThreadGroup.Equals(TEXT("GameThread"), ESearchCase::IgnoreCase))
        {
            return TEXT("GameThread");
        }

        if (InThreadGroup.Equals(TEXT("Worker"), ESearchCase::IgnoreCase))
        {
            return TEXT("Worker");
        }

        return TEXT("Unknown");
    }
}

namespace PCGProfilerCriticalPath
{
    struct FPathVertex
    {
        FString ExecutionPath;
        FString ParentExecutionPath;
        FPCGProfilerNodeEvent Event;
        TArray<FString> Children;
    };

    static TArray<FPCGProfilerNodeEvent> BuildStrictDagPath(const TArray<FPCGProfilerNodeEvent>& Events, const int32 MaxItems)
    {
        TMap<FString, FPathVertex> Vertices;
        for (const FPCGProfilerNodeEvent& Event : Events)
        {
            if (Event.ExecutionPath.IsEmpty())
            {
                continue;
            }

            FPathVertex& Vertex = Vertices.FindOrAdd(Event.ExecutionPath);
            const double ExistingWeight = FMath::Max(Vertex.Event.SelfMs, Vertex.Event.InclusiveMs);
            const double CandidateWeight = FMath::Max(Event.SelfMs, Event.InclusiveMs);
            if (Vertex.ExecutionPath.IsEmpty() || CandidateWeight >= ExistingWeight)
            {
                Vertex.ExecutionPath = Event.ExecutionPath;
                Vertex.ParentExecutionPath = Event.ParentExecutionPath;
                Vertex.Event = Event;
            }
        }

        for (TPair<FString, FPathVertex>& KV : Vertices)
        {
            const FString& ParentPath = KV.Value.ParentExecutionPath;
            if (!ParentPath.IsEmpty() && Vertices.Contains(ParentPath))
            {
                Vertices.FindChecked(ParentPath).Children.Add(KV.Key);
            }
        }

        TMap<FString, double> MemoScore;
        TMap<FString, FString> NextNode;
        TFunction<double(const FString&)> Solve = [&](const FString& Path) -> double
        {
            if (const double* Cached = MemoScore.Find(Path))
            {
                return *Cached;
            }

            const FPathVertex& Vertex = Vertices.FindChecked(Path);
            const double SelfWeight = Vertex.Event.SelfMs > 0.0 ? Vertex.Event.SelfMs : Vertex.Event.InclusiveMs;
            double Best = SelfWeight;
            FString BestNext;
            for (const FString& ChildPath : Vertex.Children)
            {
                const double Candidate = SelfWeight + Solve(ChildPath);
                if (Candidate > Best)
                {
                    Best = Candidate;
                    BestNext = ChildPath;
                }
            }

            MemoScore.Add(Path, Best);
            if (!BestNext.IsEmpty())
            {
                NextNode.Add(Path, BestNext);
            }
            return Best;
        };

        FString BestRoot;
        double BestRootScore = -1.0;
        for (const TPair<FString, FPathVertex>& KV : Vertices)
        {
            const double Score = Solve(KV.Key);
            if (Score > BestRootScore)
            {
                BestRootScore = Score;
                BestRoot = KV.Key;
            }
        }

        TArray<FPCGProfilerNodeEvent> Result;
        FString Cursor = BestRoot;
        while (!Cursor.IsEmpty() && Vertices.Contains(Cursor))
        {
            Result.Add(Vertices.FindChecked(Cursor).Event);
            const FString* Next = NextNode.Find(Cursor);
            if (!Next)
            {
                break;
            }
            Cursor = *Next;
        }

        if (MaxItems > 0 && Result.Num() > MaxItems)
        {
            Result.SetNum(MaxItems);
        }

        return Result;
    }
}

namespace PCGProfilerHarvest
{
    static int32 GPinInspectionLookupAttempts = 0;
    static int32 GPinInspectionLookupHits = 0;

    static void SetInspectionEnabledOnAllPCGComponents(bool bEnable)
    {
        if (!GEngine)
        {
            return;
        }

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

                    FPCGGraphExecutionInspection& Inspection = Component->GetExecutionState().GetInspection();
                    if (bEnable)
                    {
                        if (!Inspection.IsInspecting())
                        {
                            Inspection.EnableInspection();
                        }
                    }
                    else
                    {
                        if (Inspection.IsInspecting())
                        {
                            Inspection.DisableInspection();
                        }
                    }
                }
            }
        }
    }

    static int64 CountPointsInCollection(const FPCGDataCollection& Collection)
    {
        int64 TotalPoints = 0;
        for (const FPCGTaggedData& TaggedData : Collection.TaggedData)
        {
            const UPCGData* Data = TaggedData.Data.Get();
            const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Data);
            if (PointData)
            {
                TotalPoints += PointData->GetNumPoints();
            }
        }

        return TotalPoints;
    }

    static FPCGDataCollection BuildPinInspectionCollection(
        const FPCGGraphExecutionInspection& Inspection,
        const FPCGStack& Stack,
        const UPCGNode* Node,
        const TArray<TObjectPtr<UPCGPin>>& Pins)
    {
        FPCGDataCollection Result;
        if (!Node)
        {
            return Result;
        }

        for (const UPCGPin* Pin : Pins)
        {
            if (!Pin)
            {
                continue;
            }

            // Different runtime paths can report inspection data under different stack shapes.
            // Try "Stack+Pin" first, then fallback to "Stack+Node+Pin".
            auto TryAppendInspectionData = [&](const FPCGStack& CandidateStack)
            {
                ++GPinInspectionLookupAttempts;
                if (const FPCGDataCollection* PinData = Inspection.GetInspectionData(CandidateStack))
                {
                    ++GPinInspectionLookupHits;
                    Result += *PinData;
                    return true;
                }

                return false;
            };

            FPCGStack PinOnlyStack = Stack;
            PinOnlyStack.GetStackFramesMutable().Emplace(Pin);
            const bool bFoundPinOnly = TryAppendInspectionData(PinOnlyStack);

            if (!bFoundPinOnly)
            {
                FPCGStack NodePinStack = Stack;
                TArray<FPCGStackFrame>& StackFrames = NodePinStack.GetStackFramesMutable();
                StackFrames.Reserve(StackFrames.Num() + 2);
                StackFrames.Emplace(Node);
                StackFrames.Emplace(Pin);
                TryAppendInspectionData(NodePinStack);
            }
        }

        return Result;
    }

    static FString GuessThreadGroup(
        const UPCGNode* Node,
        const FPCGGraphExecutionInspection::FNodeExecutedNotificationData& Notification,
        const double DurationMs)
    {
        if (Node && Node->GetSettings())
        {
            if (const FPCGElementPtr Element = Node->GetSettings()->GetElement())
            {
                if (Element->CanExecuteOnlyOnMainThread(nullptr))
                {
                    return TEXT("GameThread");
                }
            }
        }

        // Inspection notifications may leave ExecutionTime/PrepareDataTime at 0 for completed nodes.
        // If we still have positive wall/total duration, classify as Worker by default to avoid skewing to Unknown.
        if (Notification.Timer.ExecutionTime > 0.0 || Notification.Timer.PrepareDataTime > 0.0 || DurationMs > 0.0)
        {
            return TEXT("Worker");
        }

        return TEXT("Unknown");
    }
}

namespace PCGProfilerRuntime
{
    static FString BuildComponentScopeKey(const UWorld* World, const UPCGComponent* Component)
    {
        if (!World || !Component)
        {
            return FString();
        }

        const AActor* Owner = Component->GetOwner();
        return FString::Printf(TEXT("%s|%s|%s"),
            *World->GetPathName(),
            Owner ? *Owner->GetPathName() : TEXT("<no-owner>"),
            *Component->GetName());
    }

    static int32 CountPCGComponentsInWorld(const UWorld* World)
    {
        if (!World)
        {
            return 0;
        }

        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            const AActor* Actor = *It;
            if (!Actor)
            {
                continue;
            }

            TInlineComponentArray<UPCGComponent*> PCGComponents;
            const_cast<AActor*>(Actor)->GetComponents(PCGComponents);
            Count += PCGComponents.Num();
        }

        return Count;
    }

    static UWorld* ResolvePrimaryPCGWorld(int32* OutComponentCount = nullptr)
    {
        UWorld* BestWorld = nullptr;
        int32 BestComponentCount = -1;
        int32 BestPriority = -1;

        if (!GEngine)
        {
            if (OutComponentCount)
            {
                *OutComponentCount = 0;
            }
            return nullptr;
        }

        for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
        {
            UWorld* World = WorldContext.World();
            if (!World)
            {
                continue;
            }

            int32 Priority = -1;
            if (World->WorldType == EWorldType::Editor)
            {
                Priority = 3;
            }
            else if (World->WorldType == EWorldType::PIE)
            {
                Priority = 2;
            }
            else if (World->WorldType == EWorldType::Game)
            {
                Priority = 1;
            }
            else
            {
                continue;
            }

            const int32 ComponentCount = CountPCGComponentsInWorld(World);
            if (Priority > BestPriority || (Priority == BestPriority && ComponentCount > BestComponentCount))
            {
                BestPriority = Priority;
                BestComponentCount = ComponentCount;
                BestWorld = World;
            }
        }

        if (OutComponentCount)
        {
            *OutComponentCount = FMath::Max(0, BestComponentCount);
        }
        return BestWorld;
    }

    static UWorld* ResolveWorldByPath(const FString& WorldPath)
    {
        if (WorldPath.IsEmpty() || !GEngine)
        {
            return nullptr;
        }

        for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
        {
            UWorld* World = WorldContext.World();
            if (World && World->GetPathName() == WorldPath)
            {
                return World;
            }
        }
        return nullptr;
    }

    static void CollectAllPCGComponents(TArray<TWeakObjectPtr<UPCGComponent>>& OutComponents)
    {
        OutComponents.Reset();

        if (!GEngine)
        {
            return;
        }

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
                    if (Component)
                    {
                        OutComponents.Add(Component);
                    }
                }
            }
        }
    }

    static int32 CountActivePCGComponents()
    {
        if (!GEngine)
        {
            return 0;
        }

        int32 ActiveCount = 0;
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

                    if (Component->IsRefreshInProgress() || Component->IsCleaningUp())
                    {
                        ++ActiveCount;
                    }
                }
            }
        }

        return ActiveCount;
    }
}

namespace PCGProfilerRuntimeSemantic
{
    struct FRunContext
    {
        FString RunMode = GIsEditor ? TEXT("editor") : TEXT("runtime");
        FString WorldType = TEXT("Editor");
        bool bIsPIE = false;
        bool bIsCooked = FPlatformProperties::RequiresCookedData();
        UWorld* ContextWorld = nullptr;
    };

    static FString BuildCellIdFromLevel(const ULevel* Level)
    {
        return Level ? Level->GetPathName() : FString();
    }

    static FString BuildCellIdFromComponent(const UPCGComponent* Component)
    {
        if (!Component)
        {
            return FString();
        }

        const AActor* Owner = Component->GetOwner();
        if (!Owner)
        {
            return FString();
        }

        if (const ULevel* Level = Owner->GetLevel())
        {
            const FString LevelCellId = BuildCellIdFromLevel(Level);
            if (!LevelCellId.IsEmpty())
            {
                return LevelCellId;
            }
        }

        return Owner->GetPathName();
    }

    static FString ToWorldTypeString(const EWorldType::Type WorldType)
    {
        switch (WorldType)
        {
            case EWorldType::Editor: return TEXT("Editor");
            case EWorldType::EditorPreview: return TEXT("EditorPreview");
            case EWorldType::PIE: return TEXT("PIE");
            case EWorldType::Game: return TEXT("Game");
            case EWorldType::GamePreview: return TEXT("GamePreview");
            case EWorldType::GameRPC: return TEXT("GameRPC");
            case EWorldType::Inactive: return TEXT("Inactive");
            default: return TEXT("Unknown");
        }
    }

    static FRunContext ResolveRunContext()
    {
        FRunContext Context;
        Context.ContextWorld = PCGProfilerRuntime::ResolvePrimaryPCGWorld(nullptr);
        if (!Context.ContextWorld && GEngine)
        {
            int32 BestPriority = -1;
            for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
            {
                UWorld* World = Ctx.World();
                if (!World)
                {
                    continue;
                }

                int32 Priority = -1;
                if (World->WorldType == EWorldType::PIE)
                {
                    Priority = 3;
                }
                else if (World->WorldType == EWorldType::Game)
                {
                    Priority = 2;
                }
                else if (World->WorldType == EWorldType::Editor || World->WorldType == EWorldType::EditorPreview)
                {
                    Priority = 1;
                }

                if (Priority > BestPriority)
                {
                    BestPriority = Priority;
                    Context.ContextWorld = World;
                }
            }
        }

        if (!Context.ContextWorld)
        {
            return Context;
        }

        Context.WorldType = ToWorldTypeString(Context.ContextWorld->WorldType);
        Context.bIsPIE = (Context.ContextWorld->WorldType == EWorldType::PIE);
        if (Context.ContextWorld->WorldType == EWorldType::PIE)
        {
            Context.RunMode = TEXT("pie");
        }
        else if (Context.ContextWorld->WorldType == EWorldType::Game)
        {
            Context.RunMode = Context.bIsCooked ? TEXT("packaged") : TEXT("standalone");
        }
        else if (Context.ContextWorld->WorldType == EWorldType::Editor || Context.ContextWorld->WorldType == EWorldType::EditorPreview)
        {
            Context.RunMode = TEXT("editor");
        }
        else
        {
            Context.RunMode = Context.bIsCooked ? TEXT("packaged") : TEXT("runtime");
        }

        return Context;
    }
}

inline FString UPCGProfilerSubsystem::BuildNodeKey(const FString& NodeTitle, const FString& NodeId, const FString& GraphName)
{
    return FString::Printf(TEXT("%s|%s|%s"), *GraphName, *NodeId, *NodeTitle);
}

inline FString UPCGProfilerSubsystem::BuildEdgeKey(const FString& ParentPath, const FString& ChildPath, const FString& GraphName)
{
    return FString::Printf(TEXT("%s|%s|%s"), *GraphName, *ParentPath, *ChildPath);
}

inline double UPCGProfilerSubsystem::ComputePercentile(const TArray<double>& Samples, double Quantile)
{
    if (Samples.IsEmpty())
    {
        return 0.0;
    }

    TArray<double> Sorted = Samples;
    Sorted.Sort();
    const int32 Index = FMath::Clamp(FMath::FloorToInt(Quantile * static_cast<double>(Sorted.Num() - 1)), 0, Sorted.Num() - 1);
    return Sorted[Index];
}

inline FString UPCGProfilerSubsystem::ResolveParentExecutionPath(const FString& ExecutionPath)
{
    int32 LastSeparator = INDEX_NONE;
    int32 Candidate = INDEX_NONE;

    if (ExecutionPath.FindLastChar(TEXT('/'), Candidate))
    {
        LastSeparator = FMath::Max(LastSeparator, Candidate);
    }

    if (ExecutionPath.FindLastChar(TEXT('>'), Candidate))
    {
        LastSeparator = FMath::Max(LastSeparator, Candidate);
    }

    if (ExecutionPath.FindLastChar(TEXT('|'), Candidate))
    {
        LastSeparator = FMath::Max(LastSeparator, Candidate);
    }

    if (LastSeparator == INDEX_NONE)
    {
        return FString();
    }

    return ExecutionPath.Left(LastSeparator);
}

