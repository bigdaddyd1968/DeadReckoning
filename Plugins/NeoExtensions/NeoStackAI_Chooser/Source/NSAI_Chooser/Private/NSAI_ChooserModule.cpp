#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "ChooserExtensionBinding.h"

namespace
{
	const TCHAR* ChooserExtensionId = TEXT("neostack.chooser");

	FNeoStackExtensionDescriptor BuildChooserDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = ChooserExtensionId;
		Descriptor.DisplayName = TEXT("Chooser");
		Descriptor.ModuleName = TEXT("NSAI_Chooser");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Build Chooser Tables — selection rules that decide which animation, mesh, or asset plays at runtime based on context.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("chooser"), TEXT("ai") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_ChooserModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildChooserDescriptor());
		Registrar.RegisterAssetCapability(
			ChooserExtensionId,
			TEXT("ChooserTable"),
			TEXT("_enrich_chooser_table"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->GetClass()->GetName() == TEXT("ChooserTable");
			});
		Registrar.RegisterAssetCapability(
			ChooserExtensionId,
			TEXT("ProxyAsset"),
			TEXT("_enrich_proxy_asset"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->GetClass()->GetName() == TEXT("ProxyAsset");
			});
		Registrar.RegisterAssetCapability(
			ChooserExtensionId,
			TEXT("ProxyTable"),
			TEXT("_enrich_proxy_table"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->GetClass()->GetName() == TEXT("ProxyTable");
			});
		Registrar.RegisterLuaBinding(
			ChooserExtensionId,
			TEXT("ChooserTable"),
			TArray<FLuaFunctionDoc>(NSAIChooserExtension::GetChooserLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIChooserExtension::BindChooserLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(ChooserExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_ChooserModule, NSAI_Chooser)
