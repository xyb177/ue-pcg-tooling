#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "RSBSpawnBudgetSubsystem.h"
#endif

#if WITH_EDITOR
class SRSBDebugPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRSBDebugPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        ChildSlot
        [
            SNew(SBorder)
            .Padding(8.0f)
            [
                SAssignNew(DebugText, STextBlock)
                .Text(FText::FromString(TEXT("Initializing RuntimeSpawnBudget debug panel...")))
            ]
        ];
    }

    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
    {
        SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

        if (!DebugText.IsValid())
        {
            return;
        }

        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        URSBSpawnBudgetSubsystem* Subsystem = World ? World->GetSubsystem<URSBSpawnBudgetSubsystem>() : nullptr;
        if (!Subsystem)
        {
            DebugText->SetText(FText::FromString(TEXT("RuntimeSpawnBudget subsystem is not available in current editor world.")));
            return;
        }

        const FRSBQueueSnapshot Queue = Subsystem->GetQueueSnapshot();
        const FRSBFrameStats Frame = Subsystem->GetLastFrameStats();
        const FRSBWindowStats Window = Subsystem->GetWindowStatsBlueprint();

        FString TopClasses;
        const int32 TopCount = FMath::Min(3, Window.ActorClassStats.Num());
        for (int32 Index = 0; Index < TopCount; ++Index)
        {
            const FRSBActorClassStats& Stats = Window.ActorClassStats[Index];
            TopClasses += FString::Printf(
                TEXT("\n  %d) %s avg=%.3fms p95=%.3fms samples=%d hits=%d"),
                Index + 1,
                *Stats.ActorClassName.ToString(),
                Stats.AvgSpawnCostMs,
                Stats.P95SpawnCostMs,
                Stats.SampleCount,
                Stats.PoolHitCount);
        }

        if (TopClasses.IsEmpty())
        {
            TopClasses = TEXT("\n  (no spawn samples yet)");
        }

        DebugText->SetText(FText::FromString(FString::Printf(
            TEXT("Queue\n")
            TEXT("  Spawn: C=%d H=%d N=%d L=%d\n")
            TEXT("  Destroy: C=%d H=%d N=%d L=%d\n")
            TEXT("  PendingAsync=%d\n\n")
            TEXT("Last Frame\n")
            TEXT("  Frame=%lld Queue=%d SpawnProcessed=%d DestroyProcessed=%d Dropped=%d\n")
            TEXT("  SpawnTime=%.3fms DestroyTime=%.3fms MaxQueueDelay=%.3fms\n\n")
            TEXT("Window\n")
            TEXT("  AvgQueueDelay=%.3fms P95QueueDelay=%.3fms\n")
            TEXT("  AvgSpawnTime=%.3fms P95SpawnTime=%.3fms\n")
            TEXT("  PoolHitRate=%.2f%%\n")
            TEXT("  ActorClass Top:%s"),
            Queue.SpawnCritical, Queue.SpawnHigh, Queue.SpawnNormal, Queue.SpawnLow,
            Queue.DestroyCritical, Queue.DestroyHigh, Queue.DestroyNormal, Queue.DestroyLow,
            Queue.PendingAsyncCount,
            Frame.FrameNumber, Frame.QueueLength, Frame.SpawnProcessed, Frame.DestroyProcessed, Frame.DroppedRequests,
            Frame.SpawnTimeMs, Frame.DestroyTimeMs, Frame.MaxQueueDelayMs,
            Window.AvgQueueDelayMs, Window.P95QueueDelayMs,
            Window.AvgSpawnTimeMs, Window.P95SpawnTimeMs,
            Window.PoolHitRate * 100.0f,
            *TopClasses)));
    }

private:
    TSharedPtr<STextBlock> DebugText;
};
#endif

class FRuntimeSpawnBudgetModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
#if WITH_EDITOR
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            DebugTabName,
            FOnSpawnTab::CreateStatic(&FRuntimeSpawnBudgetModule::SpawnDebugTab))
            .SetDisplayName(FText::FromString(TEXT("Runtime Spawn Budget")))
            .SetTooltipText(FText::FromString(TEXT("Real-time queue and budget statistics for RuntimeSpawnBudget.")));
#endif
    }

    virtual void ShutdownModule() override
    {
#if WITH_EDITOR
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DebugTabName);
#endif
    }

#if WITH_EDITOR
private:
    static constexpr const TCHAR* DebugTabName = TEXT("RuntimeSpawnBudget.DebugTab");

    static TSharedRef<SDockTab> SpawnDebugTab(const FSpawnTabArgs& SpawnArgs)
    {
        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [
                SNew(SRSBDebugPanel)
            ];
    }
#endif
};

IMPLEMENT_MODULE(FRuntimeSpawnBudgetModule, RuntimeSpawnBudget)
