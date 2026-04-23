#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RSBMetricsCollector.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSpawnBudgetActorClassSpec, "RuntimeSpawnBudget.Metrics.ActorClassStats", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeSpawnBudgetActorClassSpec::RunTest(const FString& Parameters)
{
    URSBMetricsCollector* Collector = NewObject<URSBMetricsCollector>();
    Collector->Initialize(16);

    UClass* ActorClassA = AActor::StaticClass();
    UClass* ActorClassB = APawn::StaticClass();

    Collector->AddActorClassSpawnSample(ActorClassA, 3.0f, true);
    Collector->AddActorClassSpawnSample(ActorClassA, 5.0f, false);
    Collector->AddActorClassSpawnSample(ActorClassB, 9.0f, false);
    Collector->AddActorClassSpawnSample(ActorClassB, 11.0f, false);

    const FRSBWindowStats WindowStats = Collector->BuildWindowStats(0);
    TestTrue(TEXT("Actor class stats present"), WindowStats.ActorClassStats.Num() >= 2);

    const FRSBActorClassStats* ClassAStats = WindowStats.ActorClassStats.FindByPredicate([](const FRSBActorClassStats& Stats)
    {
        return Stats.ActorClassName == AActor::StaticClass()->GetFName();
    });
    const FRSBActorClassStats* ClassBStats = WindowStats.ActorClassStats.FindByPredicate([](const FRSBActorClassStats& Stats)
    {
        return Stats.ActorClassName == APawn::StaticClass()->GetFName();
    });

    TestNotNull(TEXT("Class A stats found"), ClassAStats);
    TestNotNull(TEXT("Class B stats found"), ClassBStats);
    if (ClassAStats && ClassBStats)
    {
        TestEqual(TEXT("Class A sample count"), ClassAStats->SampleCount, 2);
        TestEqual(TEXT("Class A pool hit count"), ClassAStats->PoolHitCount, 1);
        TestEqual(TEXT("Class B sample count"), ClassBStats->SampleCount, 2);
        TestEqual(TEXT("Class B pool hit count"), ClassBStats->PoolHitCount, 0);
        TestTrue(TEXT("Class B is slower than Class A"), ClassBStats->AvgSpawnCostMs > ClassAStats->AvgSpawnCostMs);
    }

    return true;
}

#endif
