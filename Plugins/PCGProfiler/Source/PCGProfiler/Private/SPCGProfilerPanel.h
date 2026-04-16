#pragma once

#include "CoreMinimal.h"
#include "PCGProfilerTypes.h"
#include "Widgets/SCompoundWidget.h"

class SListViewBase;
template <typename ItemType> class SListView;

class SPCGProfilerPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPCGProfilerPanel) {}
    SLATE_END_ARGS()

    struct FNodeItem
    {
        FPCGProfilerNodeAggregate Aggregate;
        double OutputInputRatio = 0.0;
        double OutputInputRatioPeak = 0.0;
    };

    struct FEventItem
    {
        FPCGProfilerNodeEvent Event;
    };

    void Construct(const FArguments& InArgs);
    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
    void RefreshData(bool bForce = false);
    void RebuildFilteredItems();
    void RebuildDiagnostics();
    void RebuildTimelineAndDrilldown();
    void RebuildRecompareTextIfNeeded();
    static FString BuildNodeIdentity(const FPCGProfilerNodeAggregate& Aggregate);

    TSharedRef<class ITableRow> OnGenerateRow(TSharedPtr<FNodeItem> Item, const TSharedRef<class STableViewBase>& OwnerTable);
    TSharedRef<class ITableRow> OnGenerateEventRow(TSharedPtr<FEventItem> Item, const TSharedRef<class STableViewBase>& OwnerTable);
    void OnSelectionChanged(TSharedPtr<FNodeItem> Item, ESelectInfo::Type SelectInfo);
    void OnFilterTextChanged(const FText& InText);
    void OnEventThreadFilterChanged(const FText& InText);
    void OnEventCacheFilterChanged(const FText& InText);

    FReply OnStartRunClicked();
    FReply OnEndRunClicked();
    FReply OnExportClicked();
    FReply OnOneClickClicked();
    FReply OnRunBatchClicked();
    FReply OnRefreshClicked();
    FReply OnToggleAutoHtmlClicked();
    FReply OnOpenDashboardClicked();
    FReply OnLocateClicked();
    FReply OnRebuildSelectedClicked();

    class UPCGProfilerSubsystem* GetSubsystem() const;
    bool GetSelectedComponentAndActor(class UPCGComponent*& OutComponent, class AActor*& OutActor) const;
    bool TryFindLatestJsonSince(const FDateTime& SinceUtc, FString& OutJsonPath) const;
    bool TriggerDashboardGeneration(const FString& InputJsonPathAbs);
    bool FindLatestDashboardHtml(FString& OutHtmlPath) const;
    void TickDashboardGenerationState();
    bool TriggerBatchSummaryGeneration(const TArray<FString>& InputJsonPathsAbs);
    void DiscoverAndQueueNewJsonRuns();
    void TryKickoffAutoArtifacts(class UPCGProfilerSubsystem* Subsystem);

private:
    TSharedPtr<SListView<TSharedPtr<FNodeItem>>> NodeListView;
    TSharedPtr<SListView<TSharedPtr<FEventItem>>> EventListView;
    TArray<TSharedPtr<FNodeItem>> AllItems;
    TArray<TSharedPtr<FNodeItem>> FilteredItems;
    TArray<TSharedPtr<FEventItem>> TimelineItems;
    TSharedPtr<FNodeItem> SelectedItem;
    FString SelectedNodeIdentity;
    TArray<FPCGProfilerNodeEvent> AllEvents;

    FString FilterText;
    FString EventThreadFilter = TEXT("all");
    FString EventCacheFilter = TEXT("all");
    FString StatusText;
    FString DiagnosticsText;
    FString SelectionDetails;
    FString TimelineText;
    FString RebuildCompareText;
    FString BatchIterationsText = TEXT("10");

    bool bPendingRebuildCompare = false;
    bool bRefreshingData = false;
    bool bRestoringSelection = false;
    FString PendingRebuildNodeIdentity;
    FPCGProfilerNodeAggregate PendingBeforeAggregate;
    bool bAutoGenerateHtml = true;
    bool bHtmlGenerationInProgress = false;
    bool bAutoArtifactPipelineActive = false;
    bool bBatchSummaryPending = false;
    bool bBatchSummaryLaunched = false;
    FDateTime AutoArtifactStartUtc;
    TSet<FString> QueuedHtmlJsons;
    TArray<FString> PendingHtmlJsonQueue;
    TArray<FString> BatchJsonPaths;
    FString LastGeneratedDashboardHtml;
    FDateTime HtmlGenerationStartUtc;
    FString HtmlSourceJsonPath;

    double LastRefreshAt = 0.0;
    double EventStartOffsetMs = 0.0;
};
