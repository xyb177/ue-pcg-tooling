#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PCGProfilerSettings.generated.h"

UCLASS(config=PCGProfiler, defaultconfig, meta=(DisplayName="PCG Profiler"))
class PCGPROFILER_API UPCGProfilerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// ── Memory & Flush ──────────────────────────────────────────
	UPROPERTY(config, EditAnywhere, Category="Memory", meta=(ClampMin="0.5", Units="GB"))
	double MemoryWarningThresholdGB = 3.0;

	UPROPERTY(config, EditAnywhere, Category="Memory", meta=(ClampMin="1000"))
	int32 MaxEventsInMemoryBeforeFlush = 50000;

	UPROPERTY(config, EditAnywhere, Category="Memory", meta=(ClampMin="1000"))
	int32 FlushChunkEventCount = 20000;

	// ── Convergence Detection ───────────────────────────────────
	UPROPERTY(config, EditAnywhere, Category="Convergence", meta=(ClampMin="0.1", Units="s"))
	double StableWindowSeconds = 2.0;

	UPROPERTY(config, EditAnywhere, Category="Convergence", meta=(ClampMin="1"))
	int32 RequiredIdleTicks = 3;

	// ── One-Click Profile ───────────────────────────────────────
	UPROPERTY(config, EditAnywhere, Category="OneClick", meta=(ClampMin="1.0", Units="s"))
	double OneClickTimeoutSeconds = 600.0;

	UPROPERTY(config, EditAnywhere, Category="OneClick", meta=(ClampMin="0.01", Units="s"))
	double OneClickPollIntervalSeconds = 0.25;

	// ── Batch Profile ───────────────────────────────────────────
	UPROPERTY(config, EditAnywhere, Category="Batch", meta=(ClampMin="1"))
	int32 DefaultBatchIterations = 10;

	UPROPERTY(config, EditAnywhere, Category="Batch", meta=(ClampMin="0.0", Units="s"))
	double BatchCooldownSeconds = 3.0;

	UPROPERTY(config, EditAnywhere, Category="Batch", meta=(ClampMin="1.0", Units="s"))
	double BatchPreStartMaxWaitSeconds = 120.0;

	UPROPERTY(config, EditAnywhere, Category="Batch", meta=(ClampMin="1"))
	int32 BatchPreStartRequiredIdleTicks = 3;

	UPROPERTY(config, EditAnywhere, Category="Batch", meta=(ClampMin="0.1", Units="s"))
	double BatchPreStartComponentStableWindowSeconds = 3.0;

	// ── WaitForRunComplete ──────────────────────────────────────
	UPROPERTY(config, EditAnywhere, Category="Wait", meta=(ClampMin="1.0", Units="s"))
	double WaitForRunCompleteTimeoutSeconds = 120.0;

	UPROPERTY(config, EditAnywhere, Category="Wait", meta=(ClampMin="0.001", Units="s"))
	double WaitForRunCompletePollIntervalSeconds = 0.05;

	// ── UI Defaults ─────────────────────────────────────────────
	UPROPERTY(config, EditAnywhere, Category="UI")
	bool bAutoGenerateHtmlByDefault = true;

	// ── Analysis ────────────────────────────────────────────────
	UPROPERTY(config, EditAnywhere, Category="Analysis")
	bool bStrictCriticalPathModeByDefault = false;

	// ── UDeveloperSettings overrides ────────────────────────────
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	virtual FText GetSectionText() const override { return NSLOCTEXT("PCGProfiler", "SettingsSection", "PCG Profiler"); }

#if WITH_EDITOR
	virtual FText GetSectionDescription() const override
	{
		return NSLOCTEXT("PCGProfiler", "SettingsDescription",
			"Configure default thresholds and parameters for the PCG Profiler plugin.\n"
			"These values are used as defaults when starting profiling runs from the UI or console commands.");
	}
#endif
};
