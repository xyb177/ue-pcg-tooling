#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RSBConfig.h"
#include "RSBPoolManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSpawnBudgetPoolSpec, "RuntimeSpawnBudget.Pool.Initialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeSpawnBudgetPoolSpec::RunTest(const FString& Parameters)
{
    URSBPoolManager* PoolManager = NewObject<URSBPoolManager>();
    URSBConfig* Config = NewObject<URSBConfig>();
    TestNotNull(TEXT("Pool manager created"), PoolManager);
    TestNotNull(TEXT("Config created"), Config);

    PoolManager->Initialize(nullptr, Config);
    PoolManager->CullIdlePools(FPlatformTime::Seconds());
    TestTrue(TEXT("Pool manager survives no-world initialization"), true);
    return true;
}

#endif
