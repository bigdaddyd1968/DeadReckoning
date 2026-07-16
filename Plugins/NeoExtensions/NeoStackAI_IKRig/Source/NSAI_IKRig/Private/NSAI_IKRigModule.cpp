#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "IKRigExtensionBinding.h"

namespace
{
	const TCHAR* IKRigExtensionId = TEXT("neostack.ikrig");

	FNeoStackExtensionDescriptor BuildIKRigDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = IKRigExtensionId;
		Descriptor.DisplayName = TEXT("IK Rig");
		Descriptor.ModuleName = TEXT("NSAI_IKRig");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Set up IK Rigs and retarget animations between skeletons of different proportions.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("ikrig"), TEXT("retargeting") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_IKRigModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildIKRigDescriptor());
		Registrar.RegisterAssetCapability(
			IKRigExtensionId,
			TEXT("IKRigDefinition"),
			TEXT("_enrich_ikrig"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->GetClass()->GetName() == TEXT("IKRigDefinition");
			});
		Registrar.RegisterAssetCapability(
			IKRigExtensionId,
			TEXT("IKRetargeter"),
			TEXT("_enrich_ik_retargeter"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->GetClass()->GetName() == TEXT("IKRetargeter");
			});
		Registrar.RegisterLuaBinding(
			IKRigExtensionId,
			TEXT("IKRig"),
			TArray<FLuaFunctionDoc>(NSAIIKRigExtension::GetIKRigLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIIKRigExtension::BindIKRigLua));
		Registrar.RegisterLuaBinding(
			IKRigExtensionId,
			TEXT("IKRetargeter"),
			TArray<FLuaFunctionDoc>(NSAIIKRigExtension::GetIKRetargeterLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIIKRigExtension::BindIKRetargeterLua));
	}

    virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(IKRigExtensionId);
	}
};
IMPLEMENT_MODULE(FNSAI_IKRigModule, NSAI_IKRig)
