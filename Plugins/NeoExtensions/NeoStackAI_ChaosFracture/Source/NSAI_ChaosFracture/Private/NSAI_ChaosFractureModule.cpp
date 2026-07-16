#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "ChaosFractureExtensionBinding.h"

namespace
{
	const TCHAR* ChaosFractureExtensionId = TEXT("neostack.chaosfracture");

	FNeoStackExtensionDescriptor BuildChaosFractureDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = ChaosFractureExtensionId;
		Descriptor.DisplayName = TEXT("Chaos Fracture");
		Descriptor.ModuleName = TEXT("NSAI_ChaosFracture");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Fracture meshes, author geometry collections, and set up destruction simulations.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("chaos"), TEXT("fracture") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_ChaosFractureModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildChaosFractureDescriptor());
		Registrar.RegisterLuaBinding(
			ChaosFractureExtensionId,
			TEXT("ChaosFracture"),
			TArray<FLuaFunctionDoc>(NSAIChaosFractureExtension::GetChaosFractureLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIChaosFractureExtension::BindChaosFractureLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(ChaosFractureExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_ChaosFractureModule, NSAI_ChaosFracture)
