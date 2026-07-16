#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "PythonExtensionBinding.h"

namespace
{
	const TCHAR* PythonExtensionId = TEXT("neostack.python");

	FNeoStackExtensionDescriptor BuildPythonDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = PythonExtensionId;
		Descriptor.DisplayName = TEXT("Python");
		Descriptor.ModuleName = TEXT("NSAI_Python");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Run arbitrary Python scripts in the editor — gives the AI a fallback when no dedicated tool fits.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("python"), TEXT("automation") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_PythonModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildPythonDescriptor());
		Registrar.RegisterLuaBinding(
			PythonExtensionId,
			TEXT("ExecutePython"),
			TArray<FLuaFunctionDoc>(NSAIPythonExtension::GetPythonLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIPythonExtension::BindPythonLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(PythonExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_PythonModule, NSAI_Python)
