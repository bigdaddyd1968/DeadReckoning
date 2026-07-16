#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "EnhancedInputExtensionBinding.h"

namespace
{
	const TCHAR* EnhancedInputExtensionId = TEXT("neostack.enhancedinput");

	FNeoStackExtensionDescriptor BuildEnhancedInputDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = EnhancedInputExtensionId;
		Descriptor.DisplayName = TEXT("Enhanced Input");
		Descriptor.ModuleName = TEXT("NSAI_EnhancedInput");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Set up Input Actions, Mapping Contexts, and trigger/modifier chains for player input.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("input"), TEXT("controls") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_EnhancedInputModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildEnhancedInputDescriptor());
		Registrar.RegisterAssetCapability(
			EnhancedInputExtensionId,
			TEXT("InputAction"),
			TEXT("_enrich_input_action"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->GetClass()->GetName() == TEXT("InputAction");
			});
		Registrar.RegisterAssetCapability(
			EnhancedInputExtensionId,
			TEXT("InputMappingContext"),
			TEXT("_enrich_mapping_context"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->GetClass()->GetName() == TEXT("InputMappingContext");
			});
		Registrar.RegisterLuaBinding(
			EnhancedInputExtensionId,
			TEXT("EnhancedInput"),
			TArray<FLuaFunctionDoc>(NSAIEnhancedInputExtension::GetEnhancedInputLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIEnhancedInputExtension::BindEnhancedInputLua));
	}

    virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(EnhancedInputExtensionId);
	}
};
IMPLEMENT_MODULE(FNSAI_EnhancedInputModule, NSAI_EnhancedInput)
