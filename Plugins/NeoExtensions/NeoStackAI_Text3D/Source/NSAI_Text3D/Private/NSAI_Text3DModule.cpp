#include "Extensions/NeoStackExtensionRegistrar.h"
#include "Modules/ModuleManager.h"

namespace
{
	const TCHAR* Text3DExtensionId = TEXT("neostack.text3d");

	FNeoStackExtensionDescriptor BuildText3DDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = Text3DExtensionId;
		Descriptor.DisplayName = TEXT("Text 3D");
		Descriptor.ModuleName = TEXT("NSAI_Text3D");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("world");
		Descriptor.Description = TEXT("Author Text 3D actors, component state, layout effects, and project settings.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("text3d"), TEXT("motion-design"), TEXT("world") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_Text3DModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar::Get().RegisterExtension(BuildText3DDescriptor());
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(Text3DExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_Text3DModule, NSAI_Text3D)
