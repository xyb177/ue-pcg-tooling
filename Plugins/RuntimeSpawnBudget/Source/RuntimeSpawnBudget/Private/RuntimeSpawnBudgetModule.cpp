#include "Modules/ModuleManager.h"

class FRuntimeSpawnBudgetModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FRuntimeSpawnBudgetModule, RuntimeSpawnBudget)
