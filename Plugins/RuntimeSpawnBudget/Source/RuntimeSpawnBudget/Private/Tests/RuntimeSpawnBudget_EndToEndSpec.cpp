#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RSBMetricsCollector.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSpawnBudgetEndToEndSpec, "RuntimeSpawnBudget.Metrics.WindowStats", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeSpawnBudgetEndToEndSpec::RunTest(const FString& Parameters)
{
    URSBMetricsCollector* Collector = NewObject<URSBMetricsCollector>();
    Collector->Initialize(32);
    Collector->BeginFrame(1, 3);
    Collector->AddQueueDelayMs(1.0f);
    Collector->AddQueueDelayMs(3.0f);
    Collector->EndFrame(2, 1, 0, 4.0f, 1.0f);

    const FRSBFrameStats FrameStats = Collector->GetLastFrameStats();
    const FRSBWindowStats WindowStats = Collector->BuildWindowStats(0);

    TestEqual(TEXT("Frame number captured"), FrameStats.FrameNumber, static_cast<int64>(1));
    TestEqual(TEXT("Spawn processed captured"), FrameStats.SpawnProcessed, 2);
    TestEqual(TEXT("Destroy processed captured"), FrameStats.DestroyProcessed, 1);
    TestTrue(TEXT("Queue delay stats computed"), WindowStats.AvgQueueDelayMs > 0.0f);
    TestTrue(TEXT("Spawn time stats computed"), WindowStats.AvgSpawnTimeMs > 0.0f);
    return true;
}

#endif
