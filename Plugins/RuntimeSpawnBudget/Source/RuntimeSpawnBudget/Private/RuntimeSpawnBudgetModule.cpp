#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "EngineUtils.h"
#include "RSBPressureSpawnerActor.h"
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
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .FillHeight(0.52f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("World Actor Catalog")))
                        ]
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        [
                            SAssignNew(ActorCatalogScrollBox, SScrollBox)
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(12.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Selected Class / Settings")))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(this, &SRSBDebugPanel::HandleGetSelectedClassText)
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            [
                                SNew(SButton)
                                .Text(FText::FromString(TEXT("Apply To Pressure Actor")))
                                .OnClicked(this, &SRSBDebugPanel::HandleApplySelectedClassClicked)
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                            [
                                SNew(SButton)
                                .Text(FText::FromString(TEXT("Reset to Current Pressure Actor")))
                                .OnClicked(this, &SRSBDebugPanel::HandleResetToCurrentPressureActorClicked)
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            [
                                SNew(STextBlock).Text(FText::FromString(TEXT("BurstSize")))
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            [
                                SNew(SSpinBox<int32>)
                                .MinValue(1)
                                .MaxValue(1024)
                                .Value(this, &SRSBDebugPanel::GetBurstSize)
                                .OnValueChanged(this, &SRSBDebugPanel::HandleBurstSizeChanged)
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            [
                                SNew(STextBlock).Text(FText::FromString(TEXT("BurstCount")))
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            [
                                SNew(SSpinBox<int32>)
                                .MinValue(1)
                                .MaxValue(4096)
                                .Value(this, &SRSBDebugPanel::GetBurstCount)
                                .OnValueChanged(this, &SRSBDebugPanel::HandleBurstCountChanged)
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            [
                                SNew(STextBlock).Text(FText::FromString(TEXT("BurstInterval")))
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            [
                                SNew(SSpinBox<float>)
                                .MinValue(0.01f)
                                .MaxValue(10.0f)
                                .Delta(0.01f)
                                .Value(this, &SRSBDebugPanel::GetBurstIntervalSeconds)
                                .OnValueChanged(this, &SRSBDebugPanel::HandleBurstIntervalChanged)
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            [
                                SNew(STextBlock).Text(FText::FromString(TEXT("DestroyDelay")))
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            [
                                SNew(SSpinBox<float>)
                                .MinValue(0.01f)
                                .MaxValue(60.0f)
                                .Delta(0.05f)
                                .Value(this, &SRSBDebugPanel::GetDestroyDelaySeconds)
                                .OnValueChanged(this, &SRSBDebugPanel::HandleDestroyDelayChanged)
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 6.0f, 0.0f, 4.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            [
                                SNew(STextBlock).Text(FText::FromString(TEXT("Async Spawn")))
                            ]
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                            [
                                SNew(SCheckBox)
                                .IsChecked(this, &SRSBDebugPanel::GetUseAsyncSpawnState)
                                .OnCheckStateChanged(this, &SRSBDebugPanel::HandleUseAsyncSpawnChanged)
                            ]
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(12.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Selected Class Stats")))
                        ]
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        [
                            SAssignNew(SelectedStatsText, STextBlock)
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .FillHeight(0.48f)
                .Padding(0.0f, 12.0f, 0.0f, 0.0f)
                [
                    SAssignNew(DebugText, STextBlock)
                    .Text(FText::FromString(TEXT("Initializing RuntimeSpawnBudget debug panel...")))
                ]
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

        UWorld* World = nullptr;
        if (GEditor)
        {
            if (GEditor->PlayWorld)
            {
                World = GEditor->PlayWorld;
            }
            else
            {
                const FWorldContext* PIEContext = GEditor->GetPIEWorldContext();
                World = PIEContext ? PIEContext->World() : GEditor->GetEditorWorldContext().World();
            }
        }

        URSBSpawnBudgetSubsystem* Subsystem = World ? World->GetSubsystem<URSBSpawnBudgetSubsystem>() : nullptr;
        if (!Subsystem)
        {
            DebugText->SetText(FText::FromString(FString::Printf(
                TEXT("RuntimeSpawnBudget subsystem is not available.\n")
                TEXT("World: %s\n")
                TEXT("Tip: press Play and keep the PIE session active."),
                World ? *World->GetName() : TEXT("None"))));
            return;
        }

        const FRSBQueueSnapshot Queue = Subsystem->GetQueueSnapshot();
        const FRSBFrameStats Frame = Subsystem->GetLastFrameStats();
        const FRSBWindowStats Window = Subsystem->GetWindowStatsBlueprint();
        const TArray<FRSBWorldActorClassEntry> ActorCatalog = Subsystem->GetWorldActorClassCatalog();
        SyncSelectionToPressureActor(World);
        UpdateCatalogIfChanged(ActorCatalog);

        int32 PressureActorCount = 0;
        FString PressureActorSummary = TEXT("\n  (no pressure actor placed)");
        if (World)
        {
            for (TActorIterator<ARSBPressureSpawnerActor> It(World); It; ++It)
            {
                ++PressureActorCount;
                if (PressureActorCount == 1 && *It)
                {
                    const ARSBPressureSpawnerActor* PressureActor = *It;
                    PressureActorSummary = FString::Printf(
                        TEXT("\n  SpawnClass=%s BurstSize=%d BurstCount=%d Async=%s"),
                        PressureActor->SpawnActorClass ? *PressureActor->SpawnActorClass->GetName() : TEXT("None"),
                        PressureActor->BurstSize,
                        PressureActor->BurstCount,
                        PressureActor->bUseAsyncSpawn ? TEXT("true") : TEXT("false"));
                }
            }

            if (PressureActorCount <= 0)
            {
                PressureActorSummary = TEXT("\n  (no pressure actor placed)");
            }
            else
            {
                PressureActorSummary = FString::Printf(TEXT("\n  Count=%d%s"), PressureActorCount, *PressureActorSummary);
            }
        }

        FString SelectedStatsTextValue;
        if (SelectedActorClassName.IsNone())
        {
            SelectedStatsTextValue = TEXT("No class selected.\nClick a class in World Actor Catalog to view its stats.");
        }
        else
        {
            const FName NormalizedSelectedClass = NormalizeClassName(SelectedActorClassName);
            bool bFound = false;
            for (const FRSBActorClassStats& Stats : Window.ActorClassStats)
            {
                if (NormalizeClassName(Stats.ActorClassName) == NormalizedSelectedClass)
                {
                    SelectedStatsTextValue = FString::Printf(
                        TEXT("\n  Class=%s\n  AvgSpawnCost=%.3fms\n  P95SpawnCost=%.3fms\n  SampleCount=%d\n  PoolHitCount=%d"),
                        *Stats.ActorClassName.ToString(),
                        Stats.AvgSpawnCostMs,
                        Stats.P95SpawnCostMs,
                        Stats.SampleCount,
                        Stats.PoolHitCount);
                    bFound = true;
                    break;
                }
            }

            if (!bFound)
            {
                SelectedStatsTextValue = FString::Printf(
                    TEXT("No spawn samples recorded yet for:\n%s\n\nTip: make sure a Pressure Actor is spawning this class in PIE."),
                    *SelectedActorClassName.ToString());
            }
        }

        if (SelectedStatsText.IsValid())
        {
            SelectedStatsText->SetText(FText::FromString(SelectedStatsTextValue));
        }

        FString CatalogText;
        const int32 CatalogCount = FMath::Min(12, ActorCatalog.Num());
        for (int32 Index = 0; Index < CatalogCount; ++Index)
        {
            const FRSBWorldActorClassEntry& Entry = ActorCatalog[Index];
            CatalogText += FString::Printf(TEXT("\n  %d) %s x%d"), Index + 1, *Entry.ActorClassName.ToString(), Entry.Count);
        }
        if (CatalogText.IsEmpty())
        {
            CatalogText = TEXT("\n  (no actor classes found in world)");
        }

        DebugText->SetText(FText::FromString(FString::Printf(
            TEXT("World: %s\n")
            TEXT("PressureActors:%s\n")
            TEXT("Queue\n")
            TEXT("  Spawn: C=%d H=%d N=%d L=%d\n")
            TEXT("  Destroy: C=%d H=%d N=%d L=%d\n")
            TEXT("  PendingAsync=%d\n\n")
            TEXT("Last Frame\n")
            TEXT("  Frame=%lld Queue=%d SpawnProcessed=%d DestroyProcessed=%d Dropped=%d\n")
            TEXT("  SpawnTime=%.3fms DestroyTime=%.3fms FrameTotal=%.3fms MaxQueueDelay=%.3fms\n\n")
            TEXT("Window\n")
            TEXT("  AvgQueueDelay=%.3fms P95QueueDelay=%.3fms\n")
            TEXT("  AvgSpawnTime=%.3fms P95SpawnTime=%.3fms\n")
            TEXT("  PeakFrameTime=%.3fms\n")
            TEXT("  PoolHitRate=%.2f%%\n")
            TEXT("World Actor Catalog (top %d):%s"),
            World ? *World->GetName() : TEXT("None"),
            *PressureActorSummary,
            Queue.SpawnCritical, Queue.SpawnHigh, Queue.SpawnNormal, Queue.SpawnLow,
            Queue.DestroyCritical, Queue.DestroyHigh, Queue.DestroyNormal, Queue.DestroyLow,
            Queue.PendingAsyncCount,
            Frame.FrameNumber, Frame.QueueLength, Frame.SpawnProcessed, Frame.DestroyProcessed, Frame.DroppedRequests,
            Frame.SpawnTimeMs, Frame.DestroyTimeMs, Frame.FrameTotalTimeMs, Frame.MaxQueueDelayMs,
            Window.AvgQueueDelayMs, Window.P95QueueDelayMs,
            Window.AvgSpawnTimeMs, Window.P95SpawnTimeMs,
            Window.PeakFrameTimeMs,
            Window.PoolHitRate * 100.0f,
            CatalogCount,
            *CatalogText)));
    }

private:
    uint32 ComputeCatalogHash(const TArray<FRSBWorldActorClassEntry>& InCatalog) const
    {
        uint32 Hash = 0;
        for (const FRSBWorldActorClassEntry& Entry : InCatalog)
        {
            Hash = HashCombine(Hash, GetTypeHash(Entry.ActorClassName));
            Hash = HashCombine(Hash, GetTypeHash(Entry.Count));
            Hash = HashCombine(Hash, GetTypeHash(Entry.bIsBlueprintClass));
        }
        return Hash;
    }

    void SyncSelectionToPressureActor(UWorld* World)
    {
        if (!World)
        {
            return;
        }

        FName PressureClassName = NAME_None;
        for (TActorIterator<ARSBPressureSpawnerActor> It(World); It; ++It)
        {
            const ARSBPressureSpawnerActor* PressureActor = *It;
            if (PressureActor && PressureActor->SpawnActorClass)
            {
                PressureClassName = PressureActor->SpawnActorClass->GetFName();
                break;
            }
        }

        if (!PressureClassName.IsNone() && SelectedActorClassName != PressureClassName)
        {
            SelectedActorClassName = PressureClassName;
            RefreshCatalogWidgets();
        }
    }

    void UpdateCatalogIfChanged(const TArray<FRSBWorldActorClassEntry>& InCatalog)
    {
        const uint32 NewHash = ComputeCatalogHash(InCatalog);
        if (NewHash == LastCatalogHash && ActorCatalogOptions.Num() > 0)
        {
            return;
        }
        LastCatalogHash = NewHash;

        const FName PreviouslySelected = SelectedActorClassName;
        ActorCatalogOptions.Reset();

        for (const FRSBWorldActorClassEntry& Entry : InCatalog)
        {
            ActorCatalogOptions.Add(MakeShared<FRSBWorldActorClassEntry>(Entry));
        }

        if (ActorCatalogOptions.Num() > 0)
        {
            if (PreviouslySelected.IsNone())
            {
                SelectedActorClassName = ActorCatalogOptions[0]->ActorClassName;
            }
            else
            {
                bool bFound = false;
                for (const TSharedPtr<FRSBWorldActorClassEntry>& Entry : ActorCatalogOptions)
                {
                    if (Entry.IsValid() && Entry->ActorClassName == PreviouslySelected)
                    {
                        bFound = true;
                        break;
                    }
                }

                if (!bFound)
                {
                    SelectedActorClassName = ActorCatalogOptions[0]->ActorClassName;
                }
            }
        }

        RefreshCatalogWidgets();
    }

    TSharedRef<SWidget> BuildCatalogRow(TSharedPtr<FRSBWorldActorClassEntry> InItem)
    {
        const FName ClassName = InItem.IsValid() ? InItem->ActorClassName : NAME_None;
        const int32 Count = InItem.IsValid() ? InItem->Count : 0;

        return SNew(SButton)
            .ButtonStyle(FAppStyle::Get(), "FlatButton")
            .ButtonColorAndOpacity(SelectedActorClassName == ClassName ? FLinearColor(0.18f, 0.42f, 0.95f, 0.90f) : FLinearColor(0.17f, 0.17f, 0.17f, 0.65f))
            .HAlign(HAlign_Left)
            .OnClicked_Lambda([this, ClassName]()
            {
                SelectedActorClassName = ClassName;
                RefreshCatalogWidgets();
                return FReply::Handled();
            })
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT(">")))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(
                        TEXT("%s  (x%d)"),
                        *ClassName.ToString(),
                        Count)))
                ]
            ];
    }

    FText HandleGetSelectedClassText() const
    {
        if (SelectedActorClassName.IsNone())
        {
            return FText::FromString(TEXT("Selected: <none>"));
        }

        return FText::FromString(FString::Printf(TEXT("Selected: %s"), *SelectedActorClassName.ToString()));
    }

    FReply HandleApplySelectedClassClicked()
    {
        UWorld* World = GetCurrentWorld();
        if (!World || SelectedActorClassName.IsNone())
        {
            return FReply::Handled();
        }

        UClass* ChosenClass = nullptr;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Candidate = *It;
            if (!Candidate || Candidate->IsA<ARSBPressureSpawnerActor>() || Candidate->IsA<AWorldSettings>())
            {
                continue;
            }

            if (Candidate->GetClass() && Candidate->GetClass()->GetFName() == SelectedActorClassName)
            {
                ChosenClass = Candidate->GetClass();
                break;
            }
        }

        if (!ChosenClass)
        {
            return FReply::Handled();
        }

        int32 AppliedCount = 0;
        for (TActorIterator<ARSBPressureSpawnerActor> It(World); It; ++It)
        {
            if (ARSBPressureSpawnerActor* PressureActor = *It)
            {
                PressureActor->SpawnActorClass = ChosenClass;
                PressureActor->bAutoResolveSpawnActorClassFromWorld = false;
                ++AppliedCount;
            }
        }

        UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] Applied selected class %s to %d pressure actors"), *ChosenClass->GetName(), AppliedCount);
        return FReply::Handled();
    }

    FReply HandleResetToCurrentPressureActorClicked()
    {
        SyncEditableValuesFromPressureActor();
        return FReply::Handled();
    }

    void ApplySettingsToPressureActors()
    {
        UWorld* World = GetCurrentWorld();
        if (!World)
        {
            return;
        }

        int32 AppliedCount = 0;
        for (TActorIterator<ARSBPressureSpawnerActor> It(World); It; ++It)
        {
            if (ARSBPressureSpawnerActor* PressureActor = *It)
            {
                PressureActor->BurstSize = EditableBurstSize;
                PressureActor->BurstCount = EditableBurstCount;
                PressureActor->BurstIntervalSeconds = EditableBurstIntervalSeconds;
                PressureActor->DestroyDelaySeconds = EditableDestroyDelaySeconds;
                PressureActor->bUseAsyncSpawn = bEditableUseAsyncSpawn;
                ++AppliedCount;
            }
        }

        UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] Applied pressure settings to %d pressure actors"), AppliedCount);
    }

    int32 GetBurstSize() const { return EditableBurstSize; }
    int32 GetBurstCount() const { return EditableBurstCount; }
    float GetBurstIntervalSeconds() const { return EditableBurstIntervalSeconds; }
    float GetDestroyDelaySeconds() const { return EditableDestroyDelaySeconds; }
    ECheckBoxState GetUseAsyncSpawnState() const { return bEditableUseAsyncSpawn ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

    void HandleBurstSizeChanged(int32 InValue)
    {
        EditableBurstSize = FMath::Max(1, InValue);
        ApplySettingsToPressureActors();
    }

    void HandleBurstCountChanged(int32 InValue)
    {
        EditableBurstCount = FMath::Max(1, InValue);
        ApplySettingsToPressureActors();
    }

    void HandleBurstIntervalChanged(float InValue)
    {
        EditableBurstIntervalSeconds = FMath::Max(0.01f, InValue);
        ApplySettingsToPressureActors();
    }

    void HandleDestroyDelayChanged(float InValue)
    {
        EditableDestroyDelaySeconds = FMath::Max(0.01f, InValue);
        ApplySettingsToPressureActors();
    }

    void HandleUseAsyncSpawnChanged(ECheckBoxState NewState)
    {
        bEditableUseAsyncSpawn = (NewState == ECheckBoxState::Checked);
        ApplySettingsToPressureActors();
    }

    void SyncEditableValuesFromPressureActor()
    {
        UWorld* World = GetCurrentWorld();
        if (!World)
        {
            return;
        }

        for (TActorIterator<ARSBPressureSpawnerActor> It(World); It; ++It)
        {
            if (const ARSBPressureSpawnerActor* PressureActor = *It)
            {
                EditableBurstSize = FMath::Max(1, PressureActor->BurstSize);
                EditableBurstCount = FMath::Max(1, PressureActor->BurstCount);
                EditableBurstIntervalSeconds = FMath::Max(0.01f, PressureActor->BurstIntervalSeconds);
                EditableDestroyDelaySeconds = FMath::Max(0.01f, PressureActor->DestroyDelaySeconds);
                bEditableUseAsyncSpawn = PressureActor->bUseAsyncSpawn;
                if (PressureActor->SpawnActorClass)
                {
                    SelectedActorClassName = PressureActor->SpawnActorClass->GetFName();
                }
                RefreshCatalogWidgets();
                return;
            }
        }
    }

    UWorld* GetCurrentWorld() const
    {
        if (!GEditor)
        {
            return nullptr;
        }
        if (GEditor->PlayWorld)
        {
            return GEditor->PlayWorld;
        }
        const FWorldContext* PIEContext = GEditor->GetPIEWorldContext();
        return PIEContext ? PIEContext->World() : GEditor->GetEditorWorldContext().World();
    }

    void RefreshCatalogWidgets()
    {
        if (!ActorCatalogScrollBox.IsValid())
        {
            return;
        }

        ActorCatalogScrollBox->ClearChildren();

        TArray<TSharedPtr<FRSBWorldActorClassEntry>> BlueprintEntries;
        TArray<TSharedPtr<FRSBWorldActorClassEntry>> NativeEntries;
        for (const TSharedPtr<FRSBWorldActorClassEntry>& Entry : ActorCatalogOptions)
        {
            if (!Entry.IsValid())
            {
                continue;
            }

            if (Entry->bIsBlueprintClass)
            {
                BlueprintEntries.Add(Entry);
            }
            else
            {
                NativeEntries.Add(Entry);
            }
        }

        auto AddSection = [this](const TCHAR* Header, const TArray<TSharedPtr<FRSBWorldActorClassEntry>>& Entries)
        {
            ActorCatalogScrollBox->AddSlot()
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("%s (%d)"), Header, Entries.Num())))
            ];

            for (const TSharedPtr<FRSBWorldActorClassEntry>& Entry : Entries)
            {
                ActorCatalogScrollBox->AddSlot()
                [
                    SNew(SBorder)
                    .Padding(2.0f)
                    [
                        BuildCatalogRow(Entry)
                    ]
                ];
            }
        };

        AddSection(TEXT("Blueprint Classes"), BlueprintEntries);
        AddSection(TEXT("Native Classes"), NativeEntries);
    }

    static FName NormalizeClassName(FName InName)
    {
        FString ClassName = InName.ToString();
        if (ClassName.EndsWith(TEXT("_C")))
        {
            ClassName.LeftChopInline(2);
        }
        return FName(*ClassName);
    }

    TSharedPtr<STextBlock> DebugText;
    TSharedPtr<STextBlock> SelectedStatsText;
    TSharedPtr<SScrollBox> ActorCatalogScrollBox;
    TArray<TSharedPtr<FRSBWorldActorClassEntry>> ActorCatalogOptions;
    FName SelectedActorClassName = NAME_None;
    uint32 LastCatalogHash = 0;
    int32 EditableBurstSize = 16;
    int32 EditableBurstCount = 8;
    float EditableBurstIntervalSeconds = 0.25f;
    float EditableDestroyDelaySeconds = 2.0f;
    bool bEditableUseAsyncSpawn = true;
};
#endif

class FRuntimeSpawnBudgetModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
#if WITH_EDITOR
        // Close any previously open tab to prevent duplicates on hot-reload.
        if (TSharedPtr<SDockTab> ExistingTab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(DebugTabName)))
        {
            ExistingTab->RequestCloseTab();
        }

        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DebugTabName);
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
