#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "RSBConfig.h"
#include "RSBMetricsCollector.h"
#include "RSBPoolManager.h"
#include "RSBSpawnBudgetSubsystem.h"
#include "GameFramework/Pawn.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSpawnBudgetQueueLimitSpec, "RuntimeSpawnBudget.Boundary.QueueLimit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeSpawnBudgetQueueLimitSpec::RunTest(const FString& Parameters)
{
    const URSBConfig* DefaultConfig = GetDefault<URSBConfig>();
    TestNotNull(TEXT("Default config exists"), DefaultConfig);

    URSBConfig* MutableConfig = GetMutableDefault<URSBConfig>();
    const int32 PreviousQueueLength = MutableConfig->MaxQueueLength;
    const TMap<FName, int32> PreviousOverrides = MutableConfig->PerClassPoolCapacityOverrides;
    MutableConfig->MaxQueueLength = 1;

    URSBSpawnBudgetSubsystem* Subsystem = NewObject<URSBSpawnBudgetSubsystem>();
    Subsystem->SetConfigForTesting(MutableConfig);

    FRSBSpawnRequest FirstRequest;
    FirstRequest.ActorClass = AActor::StaticClass();
    FRSBSpawnRequest SecondRequest = FirstRequest;

    TestTrue(TEXT("First request accepted"), Subsystem->PrepareSpawnRequest(FirstRequest) > 0);
    TestFalse(TEXT("Second request rejected when queue full"), Subsystem->PrepareSpawnRequest(SecondRequest) > 0);

    MutableConfig->MaxQueueLength = PreviousQueueLength;
    MutableConfig->PerClassPoolCapacityOverrides = PreviousOverrides;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSpawnBudgetPoolCapacitySpec, "RuntimeSpawnBudget.Boundary.PoolCapacity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeSpawnBudgetPoolCapacitySpec::RunTest(const FString& Parameters)
{
    URSBConfig* MutableConfig = GetMutableDefault<URSBConfig>();
    const int32 PreviousDefaultCapacity = MutableConfig->DefaultPoolCapacity;
    const TMap<FName, int32> PreviousOverrides = MutableConfig->PerClassPoolCapacityOverrides;
    MutableConfig->DefaultPoolCapacity = 12;
    MutableConfig->PerClassPoolCapacityOverrides.Add(AActor::StaticClass()->GetFName(), 3);

    URSBPoolManager* PoolManager = NewObject<URSBPoolManager>();
    PoolManager->Initialize(nullptr, MutableConfig);

    TestEqual(TEXT("Per-class capacity override applied"), PoolManager->GetEffectiveCapacityForClass(AActor::StaticClass()), 3);
    TestEqual(TEXT("Fallback capacity applied"), PoolManager->GetEffectiveCapacityForClass(APawn::StaticClass(), FName(TEXT("NotOverridden"))), 12);

    MutableConfig->PerClassPoolCapacityOverrides.Remove(AActor::StaticClass()->GetFName());
    MutableConfig->DefaultPoolCapacity = PreviousDefaultCapacity;
    MutableConfig->PerClassPoolCapacityOverrides = PreviousOverrides;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSpawnBudgetMetricsCacheSpec, "RuntimeSpawnBudget.Boundary.MetricsCache", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeSpawnBudgetMetricsCacheSpec::RunTest(const FString& Parameters)
{
    URSBMetricsCollector* Collector = NewObject<URSBMetricsCollector>();
    Collector->Initialize(16);
    Collector->BeginFrame(1, 2);
    Collector->AddQueueDelayMs(2.0f);
    Collector->AddQueueDelayMs(4.0f);
    Collector->EndFrame(1, 0, 0, 1.0f, 0.5f);

    const FRSBWindowStats FirstStats = Collector->BuildWindowStats(2);
    const FRSBWindowStats SecondStats = Collector->BuildWindowStats(1);

    TestEqual(TEXT("Cached average queue delay is stable"), FirstStats.AvgQueueDelayMs, SecondStats.AvgQueueDelayMs);
    TestEqual(TEXT("Pending queue length is updated per query"), SecondStats.PendingQueueLength, 1);
    return true;
}

#endif
