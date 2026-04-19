#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RSBTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSpawnBudgetQueueSpec, "RuntimeSpawnBudget.Queue.Defaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeSpawnBudgetQueueSpec::RunTest(const FString& Parameters)
{
    FRSBSpawnRequest SpawnRequest;
    TestTrue(TEXT("Default spawn request allows pooling"), SpawnRequest.bAllowPooling);
    TestEqual(TEXT("Default priority is normal"), static_cast<uint8>(SpawnRequest.Priority), static_cast<uint8>(ERSBRequestPriority::Normal));

    FRSBDestroyRequest DestroyRequest;
    TestEqual(TEXT("Default destroy priority is normal"), static_cast<uint8>(DestroyRequest.Priority), static_cast<uint8>(ERSBRequestPriority::Normal));
    return true;
}

#endif
