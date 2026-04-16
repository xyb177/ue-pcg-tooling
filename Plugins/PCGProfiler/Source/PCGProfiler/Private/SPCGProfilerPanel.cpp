
#include "SPCGProfilerPanel.h"

#include "PCGProfilerSubsystem.h"

#if WITH_EDITOR
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "PCGComponent.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#endif

#if WITH_EDITOR
namespace PCGProfilerPanelInternal
{
    static FString FormatMs(const double Value)
    {
        return FString::Printf(TEXT("%.2f"), Value);
    }

    static FString FormatPercent(const double Value01)
    {
        return FString::Printf(TEXT("%.1f%%"), Value01 * 100.0);
    }

    static FString ExtractComponentObjectPath(const FString& ExecutionPath)
    {
        if (ExecutionPath.IsEmpty())
        {
            return FString();
        }

        const FString Marker = TEXT("COMPONENT:");
        int32 MarkerIndex = INDEX_NONE;
        if (!ExecutionPath.FindChar(TEXT(' '), MarkerIndex))
        {
            return FString();
        }

        if (!ExecutionPath.StartsWith(Marker))
        {
            return FString();
        }

        const int32 PathStart = MarkerIndex + 1;
        const int32 GraphIndex = ExecutionPath.Find(TEXT("/GRAPH:"), ESearchCase::IgnoreCase, ESearchDir::FromStart, PathStart);
        const int32 PathEnd = (GraphIndex != INDEX_NONE) ? GraphIndex : ExecutionPath.Len();
        if (PathEnd <= PathStart)
        {
            return FString();
        }

        return ExecutionPath.Mid(PathStart, PathEnd - PathStart).TrimStartAndEnd();
    }

    static FString ExtractActorObjectPathFromComponentPath(const FString& ComponentPath)
    {
        if (ComponentPath.IsEmpty())
        {
            return FString();
        }

        int32 LastDot = INDEX_NONE;
        if (ComponentPath.FindLastChar(TEXT('.'), LastDot) && LastDot > 0)
        {
            return ComponentPath.Left(LastDot);
        }
        return FString();
    }

    static UPCGComponent* ResolveComponent(const FString& ExecutionPath)
    {
        const FString ComponentPath = ExtractComponentObjectPath(ExecutionPath);
        if (ComponentPath.IsEmpty())
        {
            return nullptr;
        }
        return Cast<UPCGComponent>(StaticFindObject(UPCGComponent::StaticClass(), nullptr, *ComponentPath));
    }

    static AActor* ResolveActor(UWorld* World, const FString& ExecutionPath, UPCGComponent* ExistingComponent)
    {
        if (ExistingComponent && ExistingComponent->GetOwner())
        {
            return ExistingComponent->GetOwner();
        }

        const FString ComponentPath = ExtractComponentObjectPath(ExecutionPath);
        const FString ActorPath = ExtractActorObjectPathFromComponentPath(ComponentPath);
        if (ActorPath.IsEmpty() || !World)
        {
            return nullptr;
        }

        if (AActor* Exact = Cast<AActor>(StaticFindObject(AActor::StaticClass(), nullptr, *ActorPath)))
        {
            return Exact;
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (Actor && Actor->GetPathName() == ActorPath)
            {
                return Actor;
            }
        }
        return nullptr;
    }

    static bool MatchFilter(const FString& Value, const FString& FilterToken)
    {
        if (FilterToken.IsEmpty() || FilterToken.Equals(TEXT("all"), ESearchCase::IgnoreCase))
        {
            return true;
        }
        return Value.Contains(FilterToken, ESearchCase::IgnoreCase);
    }

    static double SafeRatio(const double Numerator, const double Denominator)
    {
        if (Denominator > 0.0)
        {
            return Numerator / Denominator;
        }
        return Numerator > 0.0 ? 1.0 : 0.0;
    }
}

class SPCGProfilerNodeRow : public SMultiColumnTableRow<TSharedPtr<SPCGProfilerPanel::FNodeItem>>
{
public:
    SLATE_BEGIN_ARGS(SPCGProfilerNodeRow) {}
        SLATE_ARGUMENT(TSharedPtr<SPCGProfilerPanel::FNodeItem>, Item)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
    {
        Item = InArgs._Item;
        SMultiColumnTableRow<TSharedPtr<SPCGProfilerPanel::FNodeItem>>::Construct(
            SMultiColumnTableRow<TSharedPtr<SPCGProfilerPanel::FNodeItem>>::FArguments(), OwnerTable);
    }

    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
    {
        const FPCGProfilerNodeAggregate& A = Item->Aggregate;
        FString Value;
        if (ColumnName == TEXT("NodeId")) { Value = A.NodeId; }
        else if (ColumnName == TEXT("Node")) { Value = A.NodeName; }
        else if (ColumnName == TEXT("Graph")) { Value = A.GraphName; }
        else if (ColumnName == TEXT("TotalMs")) { Value = PCGProfilerPanelInternal::FormatMs(A.TotalMs); }
        else if (ColumnName == TEXT("P95Ms")) { Value = PCGProfilerPanelInternal::FormatMs(A.P95Ms); }
        else if (ColumnName == TEXT("Calls")) { Value = FString::FromInt(A.CallCount); }
        else if (ColumnName == TEXT("GTRatio")) { Value = PCGProfilerPanelInternal::FormatPercent(A.GameThreadRatio); }
        else if (ColumnName == TEXT("HitRate")) { Value = PCGProfilerPanelInternal::FormatPercent(A.CacheHitRate); }
        return SNew(STextBlock).Text(FText::FromString(Value)).ToolTipText(FText::FromString(Value));
    }

private:
    TSharedPtr<SPCGProfilerPanel::FNodeItem> Item;
};

class SPCGProfilerEventRow : public SMultiColumnTableRow<TSharedPtr<SPCGProfilerPanel::FEventItem>>
{
public:
    SLATE_BEGIN_ARGS(SPCGProfilerEventRow) {}
        SLATE_ARGUMENT(TSharedPtr<SPCGProfilerPanel::FEventItem>, Item)
        SLATE_ARGUMENT(double, StartOffsetMs)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
    {
        Item = InArgs._Item;
        StartOffsetMs = InArgs._StartOffsetMs;
        SMultiColumnTableRow<TSharedPtr<SPCGProfilerPanel::FEventItem>>::Construct(
            SMultiColumnTableRow<TSharedPtr<SPCGProfilerPanel::FEventItem>>::FArguments(), OwnerTable);
    }

    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
    {
        const FPCGProfilerNodeEvent& E = Item->Event;
        FString Value;
        if (ColumnName == TEXT("Time")) { Value = FString::Printf(TEXT("%.1f"), FMath::Max(0.0, E.FirstSeenTimeMs - StartOffsetMs)); }
        else if (ColumnName == TEXT("Thread")) { Value = E.ThreadGroup; }
        else if (ColumnName == TEXT("Node")) { Value = E.NodeTitle; }
        else if (ColumnName == TEXT("DurMs")) { Value = FString::Printf(TEXT("%.2f"), E.InclusiveMs); }
        else if (ColumnName == TEXT("Cache")) { Value = E.bCacheHit ? TEXT("hit") : FString::Printf(TEXT("miss(%s)"), E.CacheMissReason.IsEmpty() ? TEXT("unknown") : *E.CacheMissReason); }
        return SNew(STextBlock).Text(FText::FromString(Value)).ToolTipText(FText::FromString(Value));
    }

private:
    TSharedPtr<SPCGProfilerPanel::FEventItem> Item;
    double StartOffsetMs = 0.0;
};

void SPCGProfilerPanel::Construct(const FArguments& InArgs)
{
    StatusText = TEXT("No run data loaded.");
    DiagnosticsText = TEXT("Diagnostics pending.");
    SelectionDetails = TEXT("Select a node to view details.");
    TimelineText = TEXT("Timeline pending.");
    RebuildCompareText = TEXT("No local rebuild comparison yet.");

    ChildSlot
    [
        SNew(SBorder)
        .Padding(8.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0) [ SNew(SButton).Text(FText::FromString(TEXT("StartRun"))).OnClicked(this, &SPCGProfilerPanel::OnStartRunClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0) [ SNew(SButton).Text(FText::FromString(TEXT("EndRun"))).OnClicked(this, &SPCGProfilerPanel::OnEndRunClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0) [ SNew(SButton).Text(FText::FromString(TEXT("ExportJson"))).OnClicked(this, &SPCGProfilerPanel::OnExportClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0) [ SNew(SButton).Text(FText::FromString(TEXT("One-Click"))).OnClicked(this, &SPCGProfilerPanel::OnOneClickClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
                [
                    SNew(SEditableTextBox)
                    .MinDesiredWidth(60)
                    .Text_Lambda([this]() { return FText::FromString(BatchIterationsText); })
                    .OnTextChanged_Lambda([this](const FText& InText) { BatchIterationsText = InText.ToString(); })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0) [ SNew(SButton).Text(FText::FromString(TEXT("RunBatch"))).OnClicked(this, &SPCGProfilerPanel::OnRunBatchClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0) [ SNew(SButton).Text(FText::FromString(TEXT("Refresh"))).OnClicked(this, &SPCGProfilerPanel::OnRefreshClicked) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [
                    SNew(SButton)
                    .Text_Lambda([this]() { return FText::FromString(bAutoGenerateHtml ? TEXT("Auto HTML: ON") : TEXT("Auto HTML: OFF")); })
                    .OnClicked(this, &SPCGProfilerPanel::OnToggleAutoHtmlClicked)
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0) [ SNew(SButton).Text(FText::FromString(TEXT("Open Dashboard"))).OnClicked(this, &SPCGProfilerPanel::OnOpenDashboardClicked) ]
                + SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(SSearchBox).HintText(FText::FromString(TEXT("Filter nodes by name/id/graph"))).OnTextChanged(this, &SPCGProfilerPanel::OnFilterTextChanged) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6) [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(StatusText); }) ]
            + SVerticalBox::Slot().FillHeight(1.0f)
            [
                SNew(SSplitter)
                + SSplitter::Slot().Value(0.67f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4) [ SNew(STextBlock).Text(FText::FromString(TEXT("Nodes"))) ]
                    + SVerticalBox::Slot().FillHeight(0.54f)
                    [
                        SNew(SScrollBox)
                        .Orientation(Orient_Horizontal)
                        + SScrollBox::Slot()
                        [
                            SNew(SBox)
                            .MinDesiredWidth(1400.0f)
                            [
                                SAssignNew(NodeListView, SListView<TSharedPtr<FNodeItem>>)
                                .ListItemsSource(&FilteredItems)
                                .OnGenerateRow(this, &SPCGProfilerPanel::OnGenerateRow)
                                .OnSelectionChanged(this, &SPCGProfilerPanel::OnSelectionChanged)
                                .HeaderRow
                                (
                                    SNew(SHeaderRow)
                                    + SHeaderRow::Column(TEXT("NodeId")).DefaultLabel(FText::FromString(TEXT("NodeId"))).FillWidth(0.18f)
                                    + SHeaderRow::Column(TEXT("Node")).DefaultLabel(FText::FromString(TEXT("Node"))).FillWidth(0.28f)
                                    + SHeaderRow::Column(TEXT("Graph")).DefaultLabel(FText::FromString(TEXT("Graph"))).FillWidth(0.16f)
                                    + SHeaderRow::Column(TEXT("Calls")).DefaultLabel(FText::FromString(TEXT("Calls"))).FillWidth(0.08f)
                                    + SHeaderRow::Column(TEXT("TotalMs")).DefaultLabel(FText::FromString(TEXT("Total(ms)"))).FillWidth(0.10f)
                                    + SHeaderRow::Column(TEXT("P95Ms")).DefaultLabel(FText::FromString(TEXT("P95"))).FillWidth(0.08f)
                                    + SHeaderRow::Column(TEXT("GTRatio")).DefaultLabel(FText::FromString(TEXT("GT%"))).FillWidth(0.06f)
                                    + SHeaderRow::Column(TEXT("HitRate")).DefaultLabel(FText::FromString(TEXT("HitRate"))).FillWidth(0.06f)
                                )
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 4) [ SNew(STextBlock).Text(FText::FromString(TEXT("Execution Timeline"))) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0) [ SNew(SSearchBox).HintText(FText::FromString(TEXT("thread: all/gamethread/worker"))).OnTextChanged(this, &SPCGProfilerPanel::OnEventThreadFilterChanged) ]
                        + SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(SSearchBox).HintText(FText::FromString(TEXT("cache: all/hit/miss"))).OnTextChanged(this, &SPCGProfilerPanel::OnEventCacheFilterChanged) ]
                    ]
                    + SVerticalBox::Slot().FillHeight(0.46f)
                    [
                        SAssignNew(EventListView, SListView<TSharedPtr<FEventItem>>)
                        .ListItemsSource(&TimelineItems)
                        .OnGenerateRow(this, &SPCGProfilerPanel::OnGenerateEventRow)
                        .HeaderRow
                        (
                            SNew(SHeaderRow)
                            + SHeaderRow::Column(TEXT("Time")).DefaultLabel(FText::FromString(TEXT("Start(ms)"))).FillWidth(0.13f)
                            + SHeaderRow::Column(TEXT("Thread")).DefaultLabel(FText::FromString(TEXT("Track"))).FillWidth(0.15f)
                            + SHeaderRow::Column(TEXT("Node")).DefaultLabel(FText::FromString(TEXT("Node"))).FillWidth(0.45f)
                            + SHeaderRow::Column(TEXT("DurMs")).DefaultLabel(FText::FromString(TEXT("Inclusive(ms)"))).FillWidth(0.17f)
                            + SHeaderRow::Column(TEXT("Cache")).DefaultLabel(FText::FromString(TEXT("Cache"))).FillWidth(0.15f)
                        )
                    ]
                ]
                + SSplitter::Slot().Value(0.33f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4) [ SNew(STextBlock).Text(FText::FromString(TEXT("Diagnostics + Thread Timeline"))) ]
                    + SVerticalBox::Slot().FillHeight(0.30f)
                    [ SNew(SScrollBox) + SScrollBox::Slot() [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return FText::FromString(DiagnosticsText + TEXT("\n\n") + TimelineText); }) ] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 4) [ SNew(STextBlock).Text(FText::FromString(TEXT("Selected Node Drill-down"))) ]
                    + SVerticalBox::Slot().FillHeight(0.45f)
                    [ SNew(SScrollBox) + SScrollBox::Slot() [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return FText::FromString(SelectionDetails); }) ] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 4) [ SNew(STextBlock).Text(FText::FromString(TEXT("Local Rebuild Compare"))) ]
                    + SVerticalBox::Slot().FillHeight(0.17f)
                    [ SNew(SScrollBox) + SScrollBox::Slot() [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return FText::FromString(RebuildCompareText); }) ] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0) [ SNew(SButton).Text(FText::FromString(TEXT("Locate Actor"))).OnClicked(this, &SPCGProfilerPanel::OnLocateClicked) ]
                        + SHorizontalBox::Slot().AutoWidth() [ SNew(SButton).Text(FText::FromString(TEXT("Rebuild Selected"))).OnClicked(this, &SPCGProfilerPanel::OnRebuildSelectedClicked) ]
                    ]
                ]
            ]
        ]
    ];

    RefreshData(true);
}

void SPCGProfilerPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
    if ((InCurrentTime - LastRefreshAt) >= 1.0)
    {
        RefreshData(false);
        LastRefreshAt = InCurrentTime;
    }

    if (bAutoArtifactPipelineActive)
    {
        DiscoverAndQueueNewJsonRuns();
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            TryKickoffAutoArtifacts(Subsystem);
        }
    }

    TickDashboardGenerationState();
}

void SPCGProfilerPanel::RefreshData(bool bForce)
{
    bRefreshingData = true;
    UPCGProfilerSubsystem* Subsystem = GetSubsystem();
    if (!Subsystem)
    {
        StatusText = TEXT("PCGProfiler subsystem unavailable.");
        bRefreshingData = false;
        return;
    }

    const int32 ActiveComponents = Subsystem->GetActivePCGComponentCount();
    const FString RunName = Subsystem->GetCurrentRunName();
    const bool bIdle = Subsystem->IsRunIdle();

    AllItems.Reset();
    const TArray<FPCGProfilerNodeAggregate> Aggregates = Subsystem->GetNodeAggregates();
    AllItems.Reserve(Aggregates.Num());
    for (const FPCGProfilerNodeAggregate& Aggregate : Aggregates)
    {
        TSharedPtr<FNodeItem> Item = MakeShared<FNodeItem>();
        Item->Aggregate = Aggregate;
        Item->OutputInputRatio = PCGProfilerPanelInternal::SafeRatio(static_cast<double>(Aggregate.OutputPointsSum), static_cast<double>(Aggregate.InputPointsSum));
        Item->OutputInputRatioPeak = PCGProfilerPanelInternal::SafeRatio(static_cast<double>(Aggregate.OutputPointsMax), static_cast<double>(Aggregate.InputPointsMax));
        AllItems.Add(Item);
    }

    AllEvents = Subsystem->GetNodeEvents();

    StatusText = FString::Printf(TEXT("Run=%s | Idle=%s | ActivePCGComponents=%d | Nodes=%d | Events=%d"),
        RunName.IsEmpty() ? TEXT("<none>") : *RunName,
        bIdle ? TEXT("true") : TEXT("false"),
        ActiveComponents,
        AllItems.Num(),
        AllEvents.Num());

    RebuildFilteredItems();

    SelectedItem.Reset();
    if (!SelectedNodeIdentity.IsEmpty())
    {
        for (const TSharedPtr<FNodeItem>& Item : FilteredItems)
        {
            if (Item.IsValid() && BuildNodeIdentity(Item->Aggregate) == SelectedNodeIdentity)
            {
                SelectedItem = Item;
                break;
            }
        }
    }
    RebuildDiagnostics();
    RebuildTimelineAndDrilldown();
    RebuildRecompareTextIfNeeded();

    if (NodeListView.IsValid())
    {
        NodeListView->RequestListRefresh();
        if (SelectedItem.IsValid())
        {
            bRestoringSelection = true;
            NodeListView->SetSelection(SelectedItem, ESelectInfo::Direct);
            bRestoringSelection = false;
        }
    }
    if (EventListView.IsValid()) { EventListView->RequestListRefresh(); }
    bRefreshingData = false;
}

void SPCGProfilerPanel::RebuildFilteredItems()
{
    FilteredItems.Reset();
    const FString LowerFilter = FilterText.ToLower();
    for (const TSharedPtr<FNodeItem>& Item : AllItems)
    {
        if (!Item.IsValid()) { continue; }
        const FPCGProfilerNodeAggregate& A = Item->Aggregate;
        const FString Haystack = (A.NodeName + TEXT(" ") + A.NodeId + TEXT(" ") + A.GraphName).ToLower();
        if (!LowerFilter.IsEmpty() && !Haystack.Contains(LowerFilter)) { continue; }
        FilteredItems.Add(Item);
    }

    FilteredItems.Sort([](const TSharedPtr<FNodeItem>& Lhs, const TSharedPtr<FNodeItem>& Rhs)
    {
        return Lhs->Aggregate.TotalMs > Rhs->Aggregate.TotalMs;
    });
}

void SPCGProfilerPanel::RebuildDiagnostics()
{
    if (AllItems.IsEmpty())
    {
        DiagnosticsText = TEXT("No node samples yet.");
        return;
    }

    double TotalMs = 0.0;
    double GameThreadMs = 0.0;
    double WorkerMs = 0.0;
    int32 CacheHit = 0;
    int32 CacheMiss = 0;
    int32 NoExecNodeCount = 0;

    for (const TSharedPtr<FNodeItem>& Item : AllItems)
    {
        const FPCGProfilerNodeAggregate& A = Item->Aggregate;
        TotalMs += A.TotalMs;
        GameThreadMs += A.GameThreadMs;
        WorkerMs += A.WorkerThreadMs;
        if (A.CallCount <= 0 || A.TotalMs <= KINDA_SMALL_NUMBER)
        {
            ++NoExecNodeCount;
        }
        CacheHit += A.CacheHitCount;
        CacheMiss += A.CacheMissCount;
    }

    const double ThreadTotal = GameThreadMs + WorkerMs;
    const double GTRatio = ThreadTotal > 0.0 ? (GameThreadMs / ThreadTotal) : 0.0;
    const double HitRate = (CacheHit + CacheMiss) > 0 ? static_cast<double>(CacheHit) / static_cast<double>(CacheHit + CacheMiss) : 0.0;

    DiagnosticsText = FString::Printf(
        TEXT("Aggregate Node Work: %.2f ms\nGT/Worker: %.2f / %.2f ms (GT %.1f%%)\nCache Hit Rate: %.1f%% (hit=%d miss=%d)\nNoExec Nodes: %d"),
        TotalMs, GameThreadMs, WorkerMs, GTRatio * 100.0, HitRate * 100.0, CacheHit, CacheMiss, NoExecNodeCount);
}

void SPCGProfilerPanel::RebuildTimelineAndDrilldown()
{
    TimelineItems.Reset();
    TimelineText = TEXT("No event timeline data.");

    if (AllEvents.IsEmpty())
    {
        if (!SelectedItem.IsValid())
        {
            SelectionDetails = TEXT("Select a node to view details.");
        }
        return;
    }

    TArray<FPCGProfilerNodeEvent> SortedEvents = AllEvents;
    SortedEvents.Sort([](const FPCGProfilerNodeEvent& L, const FPCGProfilerNodeEvent& R)
    {
        if (!FMath::IsNearlyEqual(L.FirstSeenTimeMs, R.FirstSeenTimeMs, KINDA_SMALL_NUMBER))
        {
            return L.FirstSeenTimeMs < R.FirstSeenTimeMs;
        }
        if (L.NodeId != R.NodeId)
        {
            return L.NodeId < R.NodeId;
        }
        return L.ExecutionPath < R.ExecutionPath;
    });
    EventStartOffsetMs = SortedEvents.IsEmpty() ? 0.0 : SortedEvents[0].FirstSeenTimeMs;

    double MaxEndMs = 0.0;
    int32 GTCount = 0;
    int32 WorkerCount = 0;
    for (const FPCGProfilerNodeEvent& E : SortedEvents)
    {
        MaxEndMs = FMath::Max(MaxEndMs, E.FirstSeenTimeMs + FMath::Max(E.InclusiveMs, 0.0));
        if (E.ThreadGroup.Equals(TEXT("GameThread"), ESearchCase::IgnoreCase)) { ++GTCount; }
        else if (E.ThreadGroup.Equals(TEXT("Worker"), ESearchCase::IgnoreCase)) { ++WorkerCount; }
    }

    constexpr int32 WindowCount = 12;
    const double WindowSizeMs = MaxEndMs > 0.0 ? (MaxEndMs / static_cast<double>(WindowCount)) : 1.0;
    TArray<double> GTWindows;
    TArray<double> WorkerWindows;
    GTWindows.Init(0.0, WindowCount);
    WorkerWindows.Init(0.0, WindowCount);

    for (const FPCGProfilerNodeEvent& E : SortedEvents)
    {
        const int32 Index = FMath::Clamp(FMath::FloorToInt(E.FirstSeenTimeMs / FMath::Max(0.001, WindowSizeMs)), 0, WindowCount - 1);
        if (E.ThreadGroup.Equals(TEXT("GameThread"), ESearchCase::IgnoreCase)) { GTWindows[Index] += E.InclusiveMs; }
        else if (E.ThreadGroup.Equals(TEXT("Worker"), ESearchCase::IgnoreCase)) { WorkerWindows[Index] += E.InclusiveMs; }
    }

    FString WindowLines;
    for (int32 Index = 0; Index < WindowCount; ++Index)
    {
        const double BeginMs = WindowSizeMs * static_cast<double>(Index);
        const double EndMs = WindowSizeMs * static_cast<double>(Index + 1);
        WindowLines += FString::Printf(TEXT("[%5.1f-%5.1f] GT %.2f | Worker %.2f\n"), BeginMs, EndMs, GTWindows[Index], WorkerWindows[Index]);
    }

    int32 PreciseStartCount = 0;
    for (const FPCGProfilerNodeEvent& E : SortedEvents)
    {
        if (E.FirstSeenTimeSource.Contains(TEXT("timer"), ESearchCase::IgnoreCase))
        {
            ++PreciseStartCount;
        }
    }
    TimelineText = FString::Printf(TEXT("Execution Timeline Window Summary (GT/Worker tracks)\nTotal events=%d | GT=%d | Worker=%d | timer_start=%d\nOrder: by event start time. Phase is informational only.\n\n%s"),
        SortedEvents.Num(), GTCount, WorkerCount, PreciseStartCount, *WindowLines);

    const FPCGProfilerNodeAggregate* SelectedAggregate = SelectedItem.IsValid() ? &SelectedItem->Aggregate : nullptr;
    TMap<FString, int32> ThreadCount;
    int32 HitCount = 0;
    int32 MissCount = 0;

    for (const FPCGProfilerNodeEvent& E : SortedEvents)
    {
        bool bMatchNode = true;
        if (SelectedAggregate)
        {
            bMatchNode =
                (!SelectedAggregate->ExecutionPath.IsEmpty() && E.ExecutionPath == SelectedAggregate->ExecutionPath) ||
                (!SelectedAggregate->NodeId.IsEmpty() && E.NodeId == SelectedAggregate->NodeId) ||
                (E.NodeTitle == SelectedAggregate->NodeName && E.GraphName == SelectedAggregate->GraphName);
        }

        if (!bMatchNode) { continue; }
        if (!PCGProfilerPanelInternal::MatchFilter(E.ThreadGroup, EventThreadFilter)) { continue; }
        const FString CacheToken = E.bCacheHit ? TEXT("hit") : TEXT("miss");
        if (!PCGProfilerPanelInternal::MatchFilter(CacheToken, EventCacheFilter)) { continue; }

        TSharedPtr<FEventItem> EventItem = MakeShared<FEventItem>();
        EventItem->Event = E;
        TimelineItems.Add(EventItem);
        ThreadCount.FindOrAdd(E.ThreadGroup.IsEmpty() ? TEXT("Unknown") : E.ThreadGroup) += 1;
        if (E.bCacheHit) { ++HitCount; } else { ++MissCount; }
    }

    TimelineItems.Sort([](const TSharedPtr<FEventItem>& L, const TSharedPtr<FEventItem>& R)
    {
        if (!L.IsValid() || !R.IsValid())
        {
            return L.IsValid();
        }
        if (!FMath::IsNearlyEqual(L->Event.FirstSeenTimeMs, R->Event.FirstSeenTimeMs, KINDA_SMALL_NUMBER))
        {
            return L->Event.FirstSeenTimeMs < R->Event.FirstSeenTimeMs;
        }
        if (L->Event.NodeId != R->Event.NodeId)
        {
            return L->Event.NodeId < R->Event.NodeId;
        }
        return L->Event.ExecutionPath < R->Event.ExecutionPath;
    });
    constexpr int32 MaxRows = 800;
    const int32 TimelineTotalMatched = TimelineItems.Num();
    if (TimelineItems.Num() > MaxRows) { TimelineItems.SetNum(MaxRows); }

    if (!SelectedAggregate)
    {
        SelectionDetails = TEXT("Select a node to view node-level event drill-down.\nCurrent event table shows global timeline with your thread/cache filters.");
        return;
    }

    FString ThreadLines;
    for (const TPair<FString, int32>& KV : ThreadCount) { ThreadLines += FString::Printf(TEXT("  %s: %d\n"), *KV.Key, KV.Value); }
    const FString DataType = SelectedAggregate->DataType.IsEmpty() ? TEXT("Unknown") : SelectedAggregate->DataType;

    SelectionDetails = FString::Printf(
        TEXT("Node: %s\nNodeId: %s\nGraph: %s\nDataType: %s\nState: %s\nTotal/P95: %.2f / %.2f ms\nCalls: %d\nGT%%: %.1f%%\nHitRate: %.1f%%\nExecutionPath:\n%s\n\nDrill-down filters: thread='%s' cache='%s'\nMatched events: %d/%d (hit=%d miss=%d)\nNote: event table is truncated to %d rows for responsiveness.\n\nBy thread:\n%s"),
        *SelectedAggregate->NodeName,
        *SelectedAggregate->NodeId,
        *SelectedAggregate->GraphName,
        *DataType,
        (SelectedAggregate->CallCount <= 0 || SelectedAggregate->TotalMs <= KINDA_SMALL_NUMBER) ? TEXT("NoExec") : TEXT("Executed"),
        SelectedAggregate->TotalMs,
        SelectedAggregate->P95Ms,
        SelectedAggregate->CallCount,
        SelectedAggregate->GameThreadRatio * 100.0,
        SelectedAggregate->CacheHitRate * 100.0,
        *SelectedAggregate->ExecutionPath,
        *EventThreadFilter,
        *EventCacheFilter,
        TimelineItems.Num(),
        TimelineTotalMatched,
        HitCount,
        MissCount,
        MaxRows,
        ThreadLines.IsEmpty() ? TEXT("  <none>\n") : *ThreadLines);
}

void SPCGProfilerPanel::RebuildRecompareTextIfNeeded()
{
    if (!bPendingRebuildCompare)
    {
        return;
    }

    for (const TSharedPtr<FNodeItem>& Item : AllItems)
    {
        if (!Item.IsValid() || BuildNodeIdentity(Item->Aggregate) != PendingRebuildNodeIdentity)
        {
            continue;
        }

        const FPCGProfilerNodeAggregate& Before = PendingBeforeAggregate;
        const FPCGProfilerNodeAggregate& After = Item->Aggregate;
        RebuildCompareText = FString::Printf(
            TEXT("Node: %s\nTotalMs: %.2f -> %.2f (delta %.2f)\nP95: %.2f -> %.2f (delta %.2f)\nCacheHitRate: %.1f%% -> %.1f%% (delta %.1f%%)\nMemDeltaMax(MB): %.2f -> %.2f (delta %.2f)"),
            *After.NodeName,
            Before.TotalMs, After.TotalMs, After.TotalMs - Before.TotalMs,
            Before.P95Ms, After.P95Ms, After.P95Ms - Before.P95Ms,
            Before.CacheHitRate * 100.0, After.CacheHitRate * 100.0, (After.CacheHitRate - Before.CacheHitRate) * 100.0,
            static_cast<double>(Before.MemoryDeltaMaxBytes) / (1024.0 * 1024.0),
            static_cast<double>(After.MemoryDeltaMaxBytes) / (1024.0 * 1024.0),
            static_cast<double>(After.MemoryDeltaMaxBytes - Before.MemoryDeltaMaxBytes) / (1024.0 * 1024.0));

        bPendingRebuildCompare = false;
        PendingRebuildNodeIdentity.Reset();
        return;
    }
}

FString SPCGProfilerPanel::BuildNodeIdentity(const FPCGProfilerNodeAggregate& Aggregate)
{
    if (!Aggregate.NodeId.IsEmpty())
    {
        return Aggregate.GraphName + TEXT("|") + Aggregate.NodeId;
    }
    return Aggregate.GraphName + TEXT("|") + Aggregate.NodeName + TEXT("|") + Aggregate.ExecutionPath;
}

TSharedRef<ITableRow> SPCGProfilerPanel::OnGenerateRow(TSharedPtr<FNodeItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SPCGProfilerNodeRow, OwnerTable).Item(Item);
}

TSharedRef<ITableRow> SPCGProfilerPanel::OnGenerateEventRow(TSharedPtr<FEventItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SPCGProfilerEventRow, OwnerTable).Item(Item).StartOffsetMs(EventStartOffsetMs);
}

void SPCGProfilerPanel::OnSelectionChanged(TSharedPtr<FNodeItem> Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid() && (bRefreshingData || bRestoringSelection) && !SelectedNodeIdentity.IsEmpty())
    {
        return;
    }

    SelectedItem = Item;
    SelectedNodeIdentity = SelectedItem.IsValid() ? BuildNodeIdentity(SelectedItem->Aggregate) : FString();
    RebuildTimelineAndDrilldown();
    if (EventListView.IsValid())
    {
        EventListView->RequestListRefresh();
    }
}

void SPCGProfilerPanel::OnFilterTextChanged(const FText& InText)
{
    FilterText = InText.ToString();
    RebuildFilteredItems();
    if (NodeListView.IsValid())
    {
        NodeListView->RequestListRefresh();
        if (SelectedItem.IsValid())
        {
            bRestoringSelection = true;
            NodeListView->SetSelection(SelectedItem, ESelectInfo::Direct);
            bRestoringSelection = false;
        }
    }
}

void SPCGProfilerPanel::OnEventThreadFilterChanged(const FText& InText)
{
    EventThreadFilter = InText.ToString().TrimStartAndEnd();
    RebuildTimelineAndDrilldown();
    if (EventListView.IsValid())
    {
        EventListView->RequestListRefresh();
    }
}

void SPCGProfilerPanel::OnEventCacheFilterChanged(const FText& InText)
{
    EventCacheFilter = InText.ToString().TrimStartAndEnd();
    RebuildTimelineAndDrilldown();
    if (EventListView.IsValid())
    {
        EventListView->RequestListRefresh();
    }
}

FReply SPCGProfilerPanel::OnStartRunClicked()
{
    if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
    {
        const FString RunName = FString::Printf(TEXT("Run_%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
        Subsystem->StartRun(RunName);
    }
    RefreshData(true);
    return FReply::Handled();
}

FReply SPCGProfilerPanel::OnEndRunClicked()
{
    if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->EndRun();
    }
    RefreshData(true);
    return FReply::Handled();
}

FReply SPCGProfilerPanel::OnExportClicked()
{
    if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
    {
        FString SavedPath;
        Subsystem->ExportJsonReport(FString(), SavedPath);
        if (!SavedPath.IsEmpty())
        {
            StatusText = FString::Printf(TEXT("%s | Exported: %s"), *StatusText, *SavedPath);
        }
    }
    return FReply::Handled();
}

FReply SPCGProfilerPanel::OnOneClickClicked()
{
    if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->RunOneClickProfile(600.0, 0.25, FString());
        bAutoArtifactPipelineActive = bAutoGenerateHtml;
        bBatchSummaryPending = false;
        bBatchSummaryLaunched = false;
        AutoArtifactStartUtc = FDateTime::UtcNow();
        QueuedHtmlJsons.Reset();
        PendingHtmlJsonQueue.Reset();
        BatchJsonPaths.Reset();
        StatusText = FString::Printf(TEXT("%s | One-Click started. Auto HTML=%s"),
            *StatusText,
            bAutoGenerateHtml ? TEXT("ON") : TEXT("OFF"));
    }
    return FReply::Handled();
}

FReply SPCGProfilerPanel::OnRunBatchClicked()
{
    if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
    {
        const int32 Iterations = FMath::Max(1, FCString::Atoi(*BatchIterationsText));
        Subsystem->RunBatchProfile(Iterations, 600.0, 0.25);
        bAutoArtifactPipelineActive = bAutoGenerateHtml;
        bBatchSummaryPending = true;
        bBatchSummaryLaunched = false;
        AutoArtifactStartUtc = FDateTime::UtcNow();
        QueuedHtmlJsons.Reset();
        PendingHtmlJsonQueue.Reset();
        BatchJsonPaths.Reset();
        StatusText = FString::Printf(TEXT("%s | RunBatch started (%d). Auto HTML=%s"),
            *StatusText,
            Iterations,
            bAutoGenerateHtml ? TEXT("ON") : TEXT("OFF"));
    }
    return FReply::Handled();
}

FReply SPCGProfilerPanel::OnRefreshClicked()
{
    RefreshData(true);
    return FReply::Handled();
}

FReply SPCGProfilerPanel::OnToggleAutoHtmlClicked()
{
    bAutoGenerateHtml = !bAutoGenerateHtml;
    StatusText = FString::Printf(TEXT("%s | Auto HTML=%s"), *StatusText, bAutoGenerateHtml ? TEXT("ON") : TEXT("OFF"));
    return FReply::Handled();
}

FReply SPCGProfilerPanel::OnOpenDashboardClicked()
{
    FString HtmlPath;
    if (!LastGeneratedDashboardHtml.IsEmpty() && FPaths::FileExists(LastGeneratedDashboardHtml))
    {
        HtmlPath = LastGeneratedDashboardHtml;
    }
    else if (!FindLatestDashboardHtml(HtmlPath))
    {
        StatusText = FString::Printf(TEXT("%s | Dashboard not found."), *StatusText);
        return FReply::Handled();
    }

    FPlatformProcess::LaunchFileInDefaultExternalApplication(*HtmlPath);
    StatusText = FString::Printf(TEXT("%s | Opened dashboard: %s"), *StatusText, *HtmlPath);
    return FReply::Handled();
}

FReply SPCGProfilerPanel::OnLocateClicked()
{
    UPCGComponent* Component = nullptr;
    AActor* Actor = nullptr;
    if (!GetSelectedComponentAndActor(Component, Actor) || !GEditor || !Actor)
    {
        return FReply::Handled();
    }

    GEditor->SelectNone(false, true, false);
    GEditor->SelectActor(Actor, true, true, true);
    GEditor->MoveViewportCamerasToActor(*Actor, false);
    return FReply::Handled();
}

FReply SPCGProfilerPanel::OnRebuildSelectedClicked()
{
    UPCGComponent* Component = nullptr;
    AActor* Actor = nullptr;
    if (!GetSelectedComponentAndActor(Component, Actor) || !Component || !SelectedItem.IsValid())
    {
        return FReply::Handled();
    }

    PendingBeforeAggregate = SelectedItem->Aggregate;
    PendingRebuildNodeIdentity = BuildNodeIdentity(SelectedItem->Aggregate);
    bPendingRebuildCompare = true;
    RebuildCompareText = FString::Printf(TEXT("Rebuild triggered for %s, waiting for fresh samples..."), *Component->GetPathName());

    Component->CleanupLocal(true);
    Component->GenerateLocal(true);
    StatusText = FString::Printf(TEXT("%s | Rebuild triggered: %s"), *StatusText, *Component->GetPathName());
    return FReply::Handled();
}

UPCGProfilerSubsystem* SPCGProfilerPanel::GetSubsystem() const
{
    return GEngine ? GEngine->GetEngineSubsystem<UPCGProfilerSubsystem>() : nullptr;
}

bool SPCGProfilerPanel::GetSelectedComponentAndActor(UPCGComponent*& OutComponent, AActor*& OutActor) const
{
    OutComponent = nullptr;
    OutActor = nullptr;

    if (!SelectedItem.IsValid())
    {
        return false;
    }

    UWorld* World = nullptr;
    if (GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }

    OutComponent = PCGProfilerPanelInternal::ResolveComponent(SelectedItem->Aggregate.ExecutionPath);
    OutActor = PCGProfilerPanelInternal::ResolveActor(World, SelectedItem->Aggregate.ExecutionPath, OutComponent);
    return OutComponent != nullptr || OutActor != nullptr;
}

bool SPCGProfilerPanel::TryFindLatestJsonSince(const FDateTime& SinceUtc, FString& OutJsonPath) const
{
    OutJsonPath.Reset();
    const FString ProfilingDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling/PCG")));
    TArray<FString> JsonFiles;
    IFileManager::Get().FindFilesRecursive(JsonFiles, *ProfilingDir, TEXT("PCGProfiler_*.json"), true, false, false);
    if (JsonFiles.Num() == 0)
    {
        return false;
    }

    FDateTime BestTs = FDateTime::MinValue();
    FString BestPath;
    for (const FString& File : JsonFiles)
    {
        const FDateTime Ts = IFileManager::Get().GetTimeStamp(*File);
        if (Ts < SinceUtc)
        {
            continue;
        }
        if (Ts > BestTs)
        {
            BestTs = Ts;
            BestPath = File;
        }
    }

    if (BestPath.IsEmpty())
    {
        return false;
    }

    OutJsonPath = FPaths::ConvertRelativePathToFull(BestPath);
    return true;
}

bool SPCGProfilerPanel::TriggerDashboardGeneration(const FString& InputJsonPathAbs)
{
    FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("PCGProfiler/Scripts/Generate-PCGProfilerDashboard.ps1")));
    if (!FPaths::FileExists(ScriptPath))
    {
        ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("PCGProfiler/Scripts/Generate-PCGProfilerDashboard.ps1")));
    }
    if (!FPaths::FileExists(ScriptPath))
    {
        StatusText = FString::Printf(TEXT("%s | Auto HTML failed: dashboard script not found."), *StatusText);
        return false;
    }

    const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    const FString EngineRoot = FPaths::ConvertRelativePathToFull(FPaths::RootDir());
    const FString JsonStem = FPaths::GetBaseFilename(InputJsonPathAbs);
    const FString OutputDir = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling/PCG/Visualization"), JsonStem));
    IFileManager::Get().MakeDirectory(*OutputDir, true);
    FString Params = FString::Printf(
        TEXT("-NoProfile -ExecutionPolicy Bypass -File \"%s\" -InputJson \"%s\" -OutputDir \"%s\" -ProjectRoot \"%s\" -EngineRoot \"%s\" -SkipScreenshot"),
        *ScriptPath,
        *InputJsonPathAbs,
        *OutputDir,
        *ProjectRoot,
        *EngineRoot);

    uint32 ProcId = 0;
    FProcHandle Handle = FPlatformProcess::CreateProc(
        TEXT("powershell.exe"),
        *Params,
        true,
        false,
        false,
        &ProcId,
        0,
        nullptr,
        nullptr);

    if (!Handle.IsValid())
    {
        StatusText = FString::Printf(TEXT("%s | Auto HTML failed: cannot launch powershell."), *StatusText);
        return false;
    }

    FPlatformProcess::CloseProc(Handle);
    bHtmlGenerationInProgress = true;
    HtmlGenerationStartUtc = FDateTime::UtcNow();
    HtmlSourceJsonPath = InputJsonPathAbs;
    StatusText = FString::Printf(TEXT("%s | Auto HTML launched for %s, waiting for dashboard..."), *StatusText, *FPaths::GetCleanFilename(InputJsonPathAbs));
    return true;
}

bool SPCGProfilerPanel::FindLatestDashboardHtml(FString& OutHtmlPath) const
{
    OutHtmlPath.Reset();
    const FString VisualDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling/PCG/Visualization")));
    TArray<FString> HtmlFiles;
    IFileManager::Get().FindFilesRecursive(HtmlFiles, *VisualDir, TEXT("dashboard.html"), true, false, false);
    if (HtmlFiles.Num() == 0)
    {
        return false;
    }

    FDateTime BestTs = FDateTime::MinValue();
    FString BestPath;
    for (const FString& File : HtmlFiles)
    {
        const FDateTime Ts = IFileManager::Get().GetTimeStamp(*File);
        if (Ts > BestTs)
        {
            BestTs = Ts;
            BestPath = File;
        }
    }
    if (BestPath.IsEmpty())
    {
        return false;
    }

    OutHtmlPath = FPaths::ConvertRelativePathToFull(BestPath);
    return true;
}

void SPCGProfilerPanel::TickDashboardGenerationState()
{
    if (!bHtmlGenerationInProgress)
    {
        return;
    }

    FString HtmlPath;
    if (FindLatestDashboardHtml(HtmlPath))
    {
        const FDateTime HtmlTs = IFileManager::Get().GetTimeStamp(*HtmlPath);
        if (HtmlTs >= HtmlGenerationStartUtc - FTimespan::FromSeconds(2.0))
        {
            bHtmlGenerationInProgress = false;
            LastGeneratedDashboardHtml = HtmlPath;
            StatusText = FString::Printf(TEXT("%s | Auto HTML ready: %s"), *StatusText, *HtmlPath);
            return;
        }
    }

    const FTimespan Elapsed = FDateTime::UtcNow() - HtmlGenerationStartUtc;
    if (Elapsed.GetTotalSeconds() > 90.0)
    {
        bHtmlGenerationInProgress = false;
        StatusText = FString::Printf(TEXT("%s | Auto HTML timeout for %s"), *StatusText, *FPaths::GetCleanFilename(HtmlSourceJsonPath));
    }
}

void SPCGProfilerPanel::DiscoverAndQueueNewJsonRuns()
{
    const FDateTime SinceUtc = AutoArtifactStartUtc - FTimespan::FromSeconds(5.0);
    const FString ProfilingDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling/PCG")));
    TArray<FString> JsonFiles;
    IFileManager::Get().FindFilesRecursive(JsonFiles, *ProfilingDir, TEXT("PCGProfiler_*.json"), true, false, false);
    if (JsonFiles.IsEmpty())
    {
        return;
    }

    JsonFiles.Sort([](const FString& A, const FString& B)
    {
        const FDateTime Ta = IFileManager::Get().GetTimeStamp(*A);
        const FDateTime Tb = IFileManager::Get().GetTimeStamp(*B);
        return Ta < Tb;
    });

    for (const FString& JsonPath : JsonFiles)
    {
        const FDateTime Ts = IFileManager::Get().GetTimeStamp(*JsonPath);
        if (Ts < SinceUtc)
        {
            continue;
        }

        const FString AbsPath = FPaths::ConvertRelativePathToFull(JsonPath);
        if (bAutoGenerateHtml && !QueuedHtmlJsons.Contains(AbsPath))
        {
            PendingHtmlJsonQueue.Add(AbsPath);
            QueuedHtmlJsons.Add(AbsPath);
            BatchJsonPaths.Add(AbsPath);
        }
    }
}

void SPCGProfilerPanel::TryKickoffAutoArtifacts(UPCGProfilerSubsystem* Subsystem)
{
    if (!Subsystem)
    {
        return;
    }

    const bool bIdle = Subsystem->IsRunIdle();
    const int32 ActiveComponents = Subsystem->GetActivePCGComponentCount();
    const bool bCanLaunch = bIdle && ActiveComponents == 0;
    if (!bCanLaunch)
    {
        return;
    }

    if (bAutoGenerateHtml && !bHtmlGenerationInProgress && PendingHtmlJsonQueue.Num() > 0)
    {
        const FString NextJson = PendingHtmlJsonQueue[0];
        PendingHtmlJsonQueue.RemoveAt(0);
        TriggerDashboardGeneration(NextJson);
    }

    if (!bHtmlGenerationInProgress
        && PendingHtmlJsonQueue.IsEmpty())
    {
        if (bBatchSummaryPending && !bBatchSummaryLaunched && BatchJsonPaths.Num() > 1)
        {
            bBatchSummaryLaunched = TriggerBatchSummaryGeneration(BatchJsonPaths);
        }
        bAutoArtifactPipelineActive = false;
        bBatchSummaryPending = false;
    }
}

bool SPCGProfilerPanel::TriggerBatchSummaryGeneration(const TArray<FString>& InputJsonPathsAbs)
{
    if (InputJsonPathsAbs.Num() < 2)
    {
        return false;
    }

    FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("PCGProfiler/Scripts/Generate-PCGProfilerBatchSummary.ps1")));
    if (!FPaths::FileExists(ScriptPath))
    {
        ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("PCGProfiler/Scripts/Generate-PCGProfilerBatchSummary.ps1")));
    }
    if (!FPaths::FileExists(ScriptPath))
    {
        StatusText = FString::Printf(TEXT("%s | Batch summary skipped: script not found."), *StatusText);
        return false;
    }

    const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    const FString EngineRoot = FPaths::ConvertRelativePathToFull(FPaths::RootDir());
    const FString Stamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString OutputDir = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling/PCG/Visualization"), FString::Printf(TEXT("batch_%s"), *Stamp)));
    IFileManager::Get().MakeDirectory(*OutputDir, true);

    FString JsonList;
    for (const FString& JsonPath : InputJsonPathsAbs)
    {
        if (!JsonList.IsEmpty())
        {
            JsonList += TEXT("|");
        }
        JsonList += JsonPath;
    }

    const FString Params = FString::Printf(
        TEXT("-NoProfile -ExecutionPolicy Bypass -File \"%s\" -InputJsonList \"%s\" -OutputDir \"%s\" -ProjectRoot \"%s\" -EngineRoot \"%s\""),
        *ScriptPath,
        *JsonList,
        *OutputDir,
        *ProjectRoot,
        *EngineRoot);

    uint32 ProcId = 0;
    FProcHandle Handle = FPlatformProcess::CreateProc(
        TEXT("powershell.exe"),
        *Params,
        true,
        false,
        false,
        &ProcId,
        0,
        *EngineRoot,
        nullptr);

    if (!Handle.IsValid())
    {
        StatusText = FString::Printf(TEXT("%s | Batch summary failed: cannot launch powershell."), *StatusText);
        return false;
    }

    FPlatformProcess::CloseProc(Handle);
    StatusText = FString::Printf(TEXT("%s | Batch summary launched (%d runs)."), *StatusText, InputJsonPathsAbs.Num());
    return true;
}

#endif
