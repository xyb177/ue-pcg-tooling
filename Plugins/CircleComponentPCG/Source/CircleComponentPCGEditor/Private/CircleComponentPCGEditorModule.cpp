#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "CircleComponentDetails.h"

class FCircleComponentPCGEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout("CircleComponent", FOnGetDetailCustomizationInstance::CreateStatic(&FCircleComponentDetails::MakeInstance));
		PropertyModule.NotifyCustomizationModuleChanged();
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyModule.UnregisterCustomClassLayout("CircleComponent");
			PropertyModule.NotifyCustomizationModuleChanged();
		}
	}
};

IMPLEMENT_MODULE(FCircleComponentPCGEditorModule, CircleComponentPCGEditor);
