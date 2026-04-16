#include "Modules/ModuleManager.h"

#include "Engine/Engine.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "PCGProfilerSubsystem.h"
#include "SPCGProfilerPanel.h"
#include "Widgets/Docking/SDockTab.h"

#if WITH_EDITOR
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/STextEntryPopup.h"
#endif

class FPCGProfilerModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        StartRunCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("PCGProfiler.StartRun"),
            TEXT("Start a new PCG profiler run. Usage: PCGProfiler.StartRun [RunName]"),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPCGProfilerModule::HandleStartRun));

        EndRunCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("PCGProfiler.EndRun"),
            TEXT("End current PCG profiler run."),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPCGProfilerModule::HandleEndRun));

        ResetRunCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("PCGProfiler.ResetRun"),
            TEXT("Reset profiler run data (equivalent to StartRun with auto name)."),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPCGProfilerModule::HandleResetRun));

        ExportCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("PCGProfiler.ExportJson"),
            TEXT("Export PCG profiler report to JSON. Usage: PCGProfiler.ExportJson [OptionalPath]"),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPCGProfilerModule::HandleExportJson));

        IsRunIdleCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("PCGProfiler.IsRunIdle"),
            TEXT("Print whether PCG run is idle and active component count."),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPCGProfilerModule::HandleIsRunIdle));

        WaitForRunCompleteCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("PCGProfiler.WaitForRunComplete"),
            TEXT("Wait until PCG components become idle. Usage: PCGProfiler.WaitForRunComplete [TimeoutSeconds] [PollIntervalSeconds]"),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPCGProfilerModule::HandleWaitForRunComplete));

        RunBatchCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("PCGProfiler.RunBatch"),
            TEXT("Run one-click profiling in batch mode. Usage: PCGProfiler.RunBatch [Iterations] [TimeoutSeconds] [PollIntervalSeconds]"),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPCGProfilerModule::HandleRunBatch));

        SetMemoryModeCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("PCGProfiler.SetMemoryMode"),
            TEXT("Set memory profiling mode. Usage: PCGProfiler.SetMemoryMode [fast|accurate]"),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPCGProfilerModule::HandleSetMemoryMode));

        SetSamplingEnabledCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("PCGProfiler.SetSamplingEnabled"),
            TEXT("Enable or disable PCG profiler sampling. Usage: PCGProfiler.SetSamplingEnabled [true|false]"),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPCGProfilerModule::HandleSetSamplingEnabled));

#if WITH_EDITOR
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            PCGProfilerPanelTabName,
            FOnSpawnTab::CreateRaw(this, &FPCGProfilerModule::SpawnProfilerPanelTab))
            .SetDisplayName(FText::FromString(TEXT("PCG Profiler Panel")))
            .SetMenuType(ETabSpawnerMenuType::Hidden);

        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPCGProfilerModule::RegisterMenus));
#endif
    }

    virtual void ShutdownModule() override
    {
#if WITH_EDITOR
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PCGProfilerPanelTabName);

        if (UToolMenus::TryGet())
        {
            UToolMenus::UnregisterOwner(this);
        }
#endif

        StartRunCommand.Reset();
        EndRunCommand.Reset();
        ResetRunCommand.Reset();
        ExportCommand.Reset();
        IsRunIdleCommand.Reset();
        WaitForRunCompleteCommand.Reset();
        RunBatchCommand.Reset();
        SetMemoryModeCommand.Reset();
        SetSamplingEnabledCommand.Reset();
    }

private:
    UPCGProfilerSubsystem* GetSubsystem() const
    {
        return GEngine ? GEngine->GetEngineSubsystem<UPCGProfilerSubsystem>() : nullptr;
    }

    void HandleStartRun(const TArray<FString>& Args)
    {
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            const FString RunName = Args.Num() > 0 ? Args[0] : FString::Printf(TEXT("Run_%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
            Subsystem->StartRun(RunName);
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler StartRun: %s"), *RunName);
        }
    }

    void HandleEndRun(const TArray<FString>& Args)
    {
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            Subsystem->EndRun();
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler EndRun"));
        }
    }

    void HandleResetRun(const TArray<FString>& Args)
    {
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            const FString RunName = FString::Printf(TEXT("Run_%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
            Subsystem->StartRun(RunName);
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler ResetRun -> StartRun: %s"), *RunName);
        }
    }

    void HandleExportJson(const TArray<FString>& Args)
    {
        UPCGProfilerSubsystem* Subsystem = GetSubsystem();
        if (!Subsystem)
        {
            return;
        }

        const FString OutputPath = Args.Num() > 0 ? Args[0] : FString();
        FString SavedPath;
        if (Subsystem->ExportJsonReport(OutputPath, SavedPath))
        {
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler JSON exported: %s"), *SavedPath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("PCGProfiler JSON export failed."));
        }
    }

    void HandleIsRunIdle(const TArray<FString>& Args)
    {
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            const int32 ActiveCount = Subsystem->GetActivePCGComponentCount();
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler IsRunIdle: %s (active_components=%d)"),
                ActiveCount == 0 ? TEXT("true") : TEXT("false"), ActiveCount);
        }
    }

    void HandleWaitForRunComplete(const TArray<FString>& Args)
    {
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            const double TimeoutSeconds = Args.Num() > 0 ? FCString::Atod(*Args[0]) : 120.0;
            const double PollIntervalSeconds = Args.Num() > 1 ? FCString::Atod(*Args[1]) : 0.05;
            const bool bCompleted = Subsystem->WaitForRunComplete(TimeoutSeconds, PollIntervalSeconds);
            const int32 ActiveCount = Subsystem->GetActivePCGComponentCount();
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler WaitForRunComplete: %s (active_components=%d, timeout=%.3fs, poll=%.3fs)"),
                bCompleted ? TEXT("completed") : TEXT("timeout"),
                ActiveCount,
                TimeoutSeconds,
                PollIntervalSeconds);
        }
    }

    void HandleRunBatch(const TArray<FString>& Args)
    {
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            const int32 Iterations = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 10;
            const double TimeoutSeconds = Args.Num() > 1 ? FCString::Atod(*Args[1]) : 600.0;
            const double PollIntervalSeconds = Args.Num() > 2 ? FCString::Atod(*Args[2]) : 0.25;
            const bool bStarted = Subsystem->RunBatchProfile(Iterations, TimeoutSeconds, PollIntervalSeconds);
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler RunBatch start: %s (iterations=%d timeout=%.3fs poll=%.3fs)"),
                bStarted ? TEXT("true") : TEXT("false"),
                Iterations,
                TimeoutSeconds,
                PollIntervalSeconds);
        }
    }

    void HandleSetMemoryMode(const TArray<FString>& Args)
    {
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            const FString Mode = Args.Num() > 0 ? Args[0].ToLower() : TEXT("accurate");
            const bool bAccurate = (Mode != TEXT("fast"));
            Subsystem->SetMemoryProfilingModeAccurate(bAccurate);
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler memory mode: %s"), bAccurate ? TEXT("accurate_delta") : TEXT("fast_estimate"));
        }
    }

    void HandleSetSamplingEnabled(const TArray<FString>& Args)
    {
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            const FString Value = Args.Num() > 0 ? Args[0].ToLower() : TEXT("true");
            const bool bEnabled = !(Value == TEXT("0") || Value == TEXT("false") || Value == TEXT("off") || Value == TEXT("disable"));
            Subsystem->SetSamplingEnabled(bEnabled);
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler sampling: %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
        }
    }

#if WITH_EDITOR
    TSharedRef<SDockTab> SpawnProfilerPanelTab(const FSpawnTabArgs& Args)
    {
        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [
                SNew(SPCGProfilerPanel)
            ];
    }

    void OpenProfilerPanelTab()
    {
        FGlobalTabmanager::Get()->TryInvokeTab(PCGProfilerPanelTabName);
    }

    void RegisterMenus()
    {
        UToolMenus* ToolMenus = UToolMenus::TryGet();
        if (!ToolMenus)
        {
            return;
        }

        UToolMenu* ToolsMenu = ToolMenus->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        if (!ToolsMenu)
        {
            return;
        }

        FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("PCGProfiler"));
        Section.AddMenuEntry(
            TEXT("PCGProfiler_OpenPanel"),
            FText::FromString(TEXT("PCG Profiler Panel")),
            FText::FromString(TEXT("Open dockable PCG profiler panel (run control, node table, diagnostics).")),
            FSlateIcon(),
            FToolMenuExecuteAction::CreateRaw(this, &FPCGProfilerModule::HandleOpenPanelFromToolsMenu));

        Section.AddMenuEntry(
            TEXT("PCGProfiler_RunOneClick"),
            FText::FromString(TEXT("PCG Profiler One-Click Run")),
            FText::FromString(TEXT("Run PCGProfiler one-click profile: StartRun -> CleanupAll -> GenerateAll -> WaitIdle -> EndRun -> ExportJson.")),
            FSlateIcon(),
            FToolMenuExecuteAction::CreateRaw(this, &FPCGProfilerModule::HandleRunOneClickFromToolsMenu));

        Section.AddMenuEntry(
            TEXT("PCGProfiler_RunBatch10"),
            FText::FromString(TEXT("PCG Profiler Run Batch x10")),
            FText::FromString(TEXT("Run one-click profiling for 10 iterations in sequence.")),
            FSlateIcon(),
            FToolMenuExecuteAction::CreateRaw(this, &FPCGProfilerModule::HandleRunBatch10FromToolsMenu));

        Section.AddMenuEntry(
            TEXT("PCGProfiler_RunBatchPrompt"),
            FText::FromString(TEXT("PCG Profiler Run Batch...")),
            FText::FromString(TEXT("Prompt for iteration count and run batch profiling.")),
            FSlateIcon(),
            FToolMenuExecuteAction::CreateRaw(this, &FPCGProfilerModule::HandleRunBatchPromptFromToolsMenu));
    }

    void HandleOpenPanelFromToolsMenu(const FToolMenuContext& Context)
    {
        OpenProfilerPanelTab();
    }

    void HandleRunOneClickFromToolsMenu(const FToolMenuContext& Context)
    {
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            const bool bStarted = Subsystem->RunOneClickProfile(600.0, 0.25, FString());
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler Tools menu one-click start: %s"), bStarted ? TEXT("true") : TEXT("false"));
        }
    }

    void HandleRunBatch10FromToolsMenu(const FToolMenuContext& Context)
    {
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            const bool bStarted = Subsystem->RunBatchProfile(10, 600.0, 0.25);
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler Tools menu batch x10 start: %s"), bStarted ? TEXT("true") : TEXT("false"));
        }
    }

    void HandleRunBatchPromptFromToolsMenu(const FToolMenuContext& Context)
    {
        TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
        if (!ActiveWindow.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("PCGProfiler batch prompt failed: no active top-level window."));
            return;
        }

        TSharedRef<STextEntryPopup> TextEntry =
            SNew(STextEntryPopup)
            .Label(FText::FromString(TEXT("Enter batch iterations (e.g. 10)")))
            .DefaultText(FText::FromString(TEXT("10")))
            .OnTextCommitted(FOnTextCommitted::CreateRaw(this, &FPCGProfilerModule::HandleBatchPromptCommitted));

        FSlateApplication::Get().PushMenu(
            ActiveWindow.ToSharedRef(),
            FWidgetPath(),
            TextEntry,
            FSlateApplication::Get().GetCursorPos(),
            FPopupTransitionEffect(FPopupTransitionEffect::TypeInPopup));
    }

    void HandleBatchPromptCommitted(const FText& InText, ETextCommit::Type CommitType)
    {
        if (CommitType != ETextCommit::OnEnter)
        {
            return;
        }

        const FString Raw = InText.ToString().TrimStartAndEnd();
        const int32 Iterations = FMath::Max(1, FCString::Atoi(*Raw));
        if (UPCGProfilerSubsystem* Subsystem = GetSubsystem())
        {
            const bool bStarted = Subsystem->RunBatchProfile(Iterations, 600.0, 0.25);
            UE_LOG(LogTemp, Display, TEXT("PCGProfiler Tools menu batch prompt start: %s (iterations=%d)"),
                bStarted ? TEXT("true") : TEXT("false"),
                Iterations);
        }

        FSlateApplication::Get().DismissAllMenus();
    }
#endif

private:
    static inline const FName PCGProfilerPanelTabName = TEXT("PCGProfiler.Panel");

    TUniquePtr<FAutoConsoleCommand> StartRunCommand;
    TUniquePtr<FAutoConsoleCommand> EndRunCommand;
    TUniquePtr<FAutoConsoleCommand> ResetRunCommand;
    TUniquePtr<FAutoConsoleCommand> ExportCommand;
    TUniquePtr<FAutoConsoleCommand> IsRunIdleCommand;
    TUniquePtr<FAutoConsoleCommand> WaitForRunCompleteCommand;
    TUniquePtr<FAutoConsoleCommand> RunBatchCommand;
    TUniquePtr<FAutoConsoleCommand> SetMemoryModeCommand;
    TUniquePtr<FAutoConsoleCommand> SetSamplingEnabledCommand;
};

IMPLEMENT_MODULE(FPCGProfilerModule, PCGProfiler)
