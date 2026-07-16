#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "ChaosOutfitExtensionBinding.h"

namespace
{
	const TCHAR* ChaosOutfitExtensionId = TEXT("neostack.chaosoutfit");

	FNeoStackExtensionDescriptor BuildChaosOutfitDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = ChaosOutfitExtensionId;
		Descriptor.DisplayName = TEXT("Chaos Outfit");
		Descriptor.ModuleName = TEXT("NSAI_ChaosOutfit");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Author Chaos Outfit Assets — body measurements, cloth fitting, and per-character outfit setup.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("chaos"), TEXT("outfit"), TEXT("cloth") };
#if WITH_CHAOS_OUTFIT_ASSET
		Descriptor.State = ENeoStackExtensionState::Active;
#else
		Descriptor.State = ENeoStackExtensionState::Unavailable;
		Descriptor.StatusMessage = TEXT("Compiled without ChaosOutfitAsset dependencies.");
#endif
		return Descriptor;
	}
}

class FNSAI_ChaosOutfitModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildChaosOutfitDescriptor());
		Registrar.RegisterLuaBinding(
			ChaosOutfitExtensionId,
			TEXT("ChaosOutfit"),
			TArray<FLuaFunctionDoc>(NSAIChaosOutfitExtension::GetChaosOutfitLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIChaosOutfitExtension::BindChaosOutfitLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(ChaosOutfitExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_ChaosOutfitModule, NSAI_ChaosOutfit)
