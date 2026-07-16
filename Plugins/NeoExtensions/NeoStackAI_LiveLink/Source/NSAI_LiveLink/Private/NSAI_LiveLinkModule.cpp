#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Blueprint/AssetTypeAliasRegistry.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "LiveLinkExtensionBinding.h"

namespace
{
	const TCHAR* LiveLinkExtensionId = TEXT("neostack.livelink");

	FNeoStackExtensionDescriptor BuildLiveLinkDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = LiveLinkExtensionId;
		Descriptor.DisplayName = TEXT("Live Link");
		Descriptor.ModuleName = TEXT("NSAI_LiveLink");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Configure Live Link sources, subjects, and presets — animation capture, virtual cameras, and virtual production.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("livelink"), TEXT("animation") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_LiveLinkModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		NeoAssetTypeAliases::RegisterAliases({
			{ TEXT("livelinkpreset"), TEXT("/Script/LiveLink.LiveLinkPreset") },
			{ TEXT("livelink_preset"), TEXT("/Script/LiveLink.LiveLinkPreset") },
		});

		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildLiveLinkDescriptor());
		Registrar.RegisterLuaBinding(
			LiveLinkExtensionId,
			TEXT("LiveLink"),
			TArray<FLuaFunctionDoc>(NSAILiveLinkExtension::GetLiveLinkLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAILiveLinkExtension::BindLiveLinkLua));
	}

	virtual void ShutdownModule() override
	{
		NeoAssetTypeAliases::UnregisterAliases({
			TEXT("livelinkpreset"),
			TEXT("livelink_preset"),
		});
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(LiveLinkExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_LiveLinkModule, NSAI_LiveLink)
