#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "MetaHumanExtensionBinding.h"

namespace
{
	const TCHAR* MetaHumanExtensionId = TEXT("neostack.metahuman");

	FNeoStackExtensionDescriptor BuildMetaHumanDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = MetaHumanExtensionId;
		Descriptor.DisplayName = TEXT("MetaHuman");
		Descriptor.ModuleName = TEXT("NSAI_MetaHuman");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Edit MetaHuman characters — face and body customization, animation, and MetaHuman SDK asset workflows.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("metahuman"), TEXT("character") };
#ifdef NSAI_METAHUMAN_DISABLED
		Descriptor.State = ENeoStackExtensionState::Unavailable;
		Descriptor.StatusMessage = TEXT("Compiled without MetaHuman dependencies.");
#else
		Descriptor.State = ENeoStackExtensionState::Active;
#endif
		return Descriptor;
	}
}

class FNSAI_MetaHumanModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildMetaHumanDescriptor());
		UE_LOG(LogNeoStackAI, Log,
			TEXT("NeoStack MetaHuman: StartupModule for '%s'"),
			MetaHumanExtensionId);

#ifndef NSAI_METAHUMAN_DISABLED
		Registrar.RegisterLuaBinding(
			MetaHumanExtensionId,
			TEXT("MetaHuman"),
			TArray<FLuaFunctionDoc>(NSAIMetaHumanExtension::GetMetaHumanLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIMetaHumanExtension::BindMetaHumanLua));
		UE_LOG(LogNeoStackAI, Log,
			TEXT("NeoStack MetaHuman: Lua domain registered for '%s'"),
			MetaHumanExtensionId);
#else
		UE_LOG(LogNeoStackAI, Warning,
			TEXT("NeoStack MetaHuman: extension '%s' started without MetaHuman runtime dependencies"),
			MetaHumanExtensionId);
#endif
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogNeoStackAI, Log,
			TEXT("NeoStack MetaHuman: ShutdownModule for '%s'"),
			MetaHumanExtensionId);
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(MetaHumanExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_MetaHumanModule, NSAI_MetaHuman)
