#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RSBConfig.h"
#include "RSBPolicyManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSpawnBudgetBudgetSpec, "RuntimeSpawnBudget.Policy.FromConfig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeSpawnBudgetBudgetSpec::RunTest(const FString& Parameters)
{
    URSBConfig* Config = NewObject<URSBConfig>();
    Config->MaxSpawnOpsPerFrame = 12;
    Config->MaxDestroyOpsPerFrame = 8;
    Config->MaxSpawnTimeMsPerFrame = 0.75f;
    Config->MaxDestroyTimeMsPerFrame = 0.5f;

    URSBPolicyManager* PolicyManager = NewObject<URSBPolicyManager>();
    PolicyManager->Initialize(Config);

    const FRSBBudgetSnapshot Budget = PolicyManager->GetBudget();
    TestEqual(TEXT("Spawn ops copied from config"), Budget.MaxSpawnOpsPerFrame, 12);
    TestEqual(TEXT("Destroy ops copied from config"), Budget.MaxDestroyOpsPerFrame, 8);
    TestEqual(TEXT("Spawn time copied from config"), Budget.MaxSpawnTimeMsPerFrame, 0.75f);
    TestEqual(TEXT("Destroy time copied from config"), Budget.MaxDestroyTimeMsPerFrame, 0.5f);
    return true;
}

#endif
