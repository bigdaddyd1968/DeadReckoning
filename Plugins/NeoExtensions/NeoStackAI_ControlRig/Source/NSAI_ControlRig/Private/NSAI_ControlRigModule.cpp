#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "ControlRigExtensionBinding.h"
#include "Lua/LuaControlRigHelper.h"

namespace
{
	const TCHAR* ControlRigExtensionId = TEXT("neostack.controlrig");

	FNeoStackExtensionDescriptor BuildControlRigDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = ControlRigExtensionId;
		Descriptor.DisplayName = TEXT("Control Rig");
		Descriptor.ModuleName = TEXT("NSAI_ControlRig");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Build and edit Control Rigs — bones, controls, IK chains, and rig logic graphs.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("controlrig"), TEXT("rigging") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_ControlRigModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildControlRigDescriptor());
		FLuaGraphCapabilityBridges GraphBridges;
		GraphBridges.AddNodeFunctionName = TEXT("_cr_add_node");
		GraphBridges.FindNodesFunctionName = TEXT("_cr_find_nodes");
		GraphBridges.ConnectFunctionName = TEXT("_cr_connect");
		GraphBridges.DisconnectFunctionName = TEXT("_cr_disconnect");
		GraphBridges.DisconnectAllFunctionName = TEXT("_cr_disconnect_all");
		GraphBridges.DisconnectFromFunctionName = TEXT("_cr_disconnect_from");
		GraphBridges.SplitPinFunctionName = TEXT("_cr_split_pin");
		GraphBridges.RecombinePinFunctionName = TEXT("_cr_recombine_pin");
		GraphBridges.SetNodeCommentFunctionName = TEXT("_cr_set_node_comment");
		GraphBridges.DeleteNodeFunctionName = TEXT("_cr_delete_node");
		Registrar.RegisterGraphCapability(
			ControlRigExtensionId,
			TEXT("ControlRig"),
			[](UEdGraph* Graph) -> bool
			{
				return LuaControlRig::IsRigVMGraph(Graph);
			},
			MoveTemp(GraphBridges));
		Registrar.RegisterLuaBinding(
			ControlRigExtensionId,
			TEXT("ControlRig"),
			TArray<FLuaFunctionDoc>(NSAIControlRigExtension::GetControlRigLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIControlRigExtension::BindControlRigLua));
		Registrar.RegisterLuaBinding(
			ControlRigExtensionId,
			TEXT("ControlRigGraphOps"),
			TArray<FLuaFunctionDoc>(NSAIControlRigExtension::GetControlRigGraphOpsLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIControlRigExtension::BindControlRigGraphOpsLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(ControlRigExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_ControlRigModule, NSAI_ControlRig)
