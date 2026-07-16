#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "WaterExtensionBinding.h"

namespace
{
	const TCHAR* WaterExtensionId = TEXT("neostack.water");

	FNeoStackExtensionDescriptor BuildWaterDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = WaterExtensionId;
		Descriptor.DisplayName = TEXT("Water");
		Descriptor.ModuleName = TEXT("NSAI_Water");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Place water bodies, sculpt rivers and oceans, and tune water surface materials.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("water"), TEXT("environment") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_WaterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildWaterDescriptor());
		Registrar.RegisterLuaBinding(
			WaterExtensionId,
			TEXT("Water"),
			TArray<FLuaFunctionDoc>(NSAIWaterExtension::GetWaterLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIWaterExtension::BindWaterLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(WaterExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_WaterModule, NSAI_Water)
