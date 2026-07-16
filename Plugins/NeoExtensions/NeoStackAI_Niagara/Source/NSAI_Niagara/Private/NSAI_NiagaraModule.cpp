#include "Modules/ModuleManager.h"
#include "Blueprint/AssetTypeAliasRegistry.h"
#include "Lua/LuaGraphResolverExtension.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "NiagaraExtensionBinding.h"
#include "NiagaraStructHandlers.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterDefinitions.h"

namespace
{
	const TCHAR* NiagaraExtensionId = TEXT("neostack.niagara");

	TMap<FString, FString> BuildNiagaraAssetAliases()
	{
		return {
			{ TEXT("niagarasystem"), TEXT("/Script/Niagara.NiagaraSystem") },
			{ TEXT("niagara_system"), TEXT("/Script/Niagara.NiagaraSystem") },
			{ TEXT("niagaraemitter"), TEXT("/Script/Niagara.NiagaraEmitter") },
			{ TEXT("niagara_emitter"), TEXT("/Script/Niagara.NiagaraEmitter") },
			{ TEXT("niagarascript"), TEXT("/Script/Niagara.NiagaraScript") },
			{ TEXT("niagara_script"), TEXT("/Script/Niagara.NiagaraScript") },
			{ TEXT("niagaraparameterdefinitions"), TEXT("/Script/NiagaraEditor.NiagaraParameterDefinitions") },
			{ TEXT("niagara_parameter_definitions"), TEXT("/Script/NiagaraEditor.NiagaraParameterDefinitions") }
		};
	}

	TArray<FString> BuildNiagaraAssetAliasNames()
	{
		return {
			TEXT("niagarasystem"),
			TEXT("niagara_system"),
			TEXT("niagaraemitter"),
			TEXT("niagara_emitter"),
			TEXT("niagarascript"),
			TEXT("niagara_script"),
			TEXT("niagaraparameterdefinitions"),
			TEXT("niagara_parameter_definitions")
		};
	}

	FNeoStackExtensionDescriptor BuildNiagaraDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = NiagaraExtensionId;
		Descriptor.DisplayName = TEXT("Niagara");
		Descriptor.ModuleName = TEXT("NSAI_Niagara");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Build and debug Niagara VFX — emitters, modules, parameter definitions, and node graphs.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("niagara"), TEXT("fx") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_NiagaraModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		NeoAssetTypeAliases::RegisterAliases(BuildNiagaraAssetAliases());

		// Register Niagara-specific struct handlers (FNiagaraBool, FNiagaraPosition, ...)
		// with the core NeoLuaProperty reflection helper before any binding runs.
		NSAINiagaraLCD::RegisterNiagaraStructHandlers();

		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildNiagaraDescriptor());
		FLuaGraphCapabilityBridges GraphBridges;
		GraphBridges.AddNodeFunctionName = TEXT("_niagara_add_node");
		GraphBridges.FindNodesFunctionName = TEXT("_niagara_find_nodes");
		Registrar.RegisterGraphCapability(
			NiagaraExtensionId,
			TEXT("Niagara"),
			[](UEdGraph* Graph) -> bool
			{
				return Cast<UNiagaraGraph>(Graph) != nullptr;
			},
			MoveTemp(GraphBridges));
		TFunction<bool(UObject*, TArray<FResolvedGraphInfo>&)> GraphResolver =
			[](UObject* Asset, TArray<FResolvedGraphInfo>& OutGraphs) -> bool
			{
				if (UNiagaraScript* Script = Cast<UNiagaraScript>(Asset))
				{
					UNiagaraScriptSourceBase* SourceBase = Script->GetLatestSource();
					UNiagaraScriptSource* Source = SourceBase ? Cast<UNiagaraScriptSource>(SourceBase) : nullptr;
					if (Source && Source->NodeGraph)
					{
						OutGraphs.Add(FResolvedGraphInfo(TEXT("NiagaraGraph"), Cast<UEdGraph>(Source->NodeGraph.Get())));
					}
					return true;
				}

				if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset))
				{
					System->EnsureFullyLoaded();
					UNiagaraScript* SpawnScript = System->GetSystemSpawnScript();
					if (SpawnScript)
					{
						UNiagaraScriptSourceBase* SourceBase = SpawnScript->GetLatestSource();
						UNiagaraScriptSource* Source = SourceBase ? Cast<UNiagaraScriptSource>(SourceBase) : nullptr;
						if (Source && Source->NodeGraph)
						{
							OutGraphs.Add(FResolvedGraphInfo(TEXT("SystemGraph"), Cast<UEdGraph>(Source->NodeGraph.Get())));
						}
					}

					const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
					for (int32 i = 0; i < Handles.Num(); i++)
					{
						const FNiagaraEmitterHandle& Handle = Handles[i];
						FString EmitterName = Handle.GetName().ToString();
						FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
						if (!EmitterData) continue;

						UNiagaraScriptSource* EmitterGraphSource = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
						if (EmitterGraphSource && EmitterGraphSource->NodeGraph)
						{
							FString Label = FString::Printf(TEXT("%s/Graph"), *EmitterName);
							OutGraphs.Add(FResolvedGraphInfo(Label, Cast<UEdGraph>(EmitterGraphSource->NodeGraph.Get())));
						}
					}
					return true;
				}

				if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Asset))
				{
					FVersionedNiagaraEmitterData* EmitterData = Emitter->GetLatestEmitterData();
					if (EmitterData)
					{
						UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
						if (Source && Source->NodeGraph)
						{
							OutGraphs.Add(FResolvedGraphInfo(TEXT("EmitterGraph"), Cast<UEdGraph>(Source->NodeGraph.Get())));
						}
					}
					return true;
				}

				return false;
			};
		Registrar.RegisterAssetCapability(
			NiagaraExtensionId,
			TEXT("NiagaraSystem"),
			TEXT("_enrich_niagara"),
			[](UObject* Asset) -> bool
			{
				return Cast<UNiagaraSystem>(Asset) != nullptr;
			},
			GraphResolver);
		Registrar.RegisterAssetCapability(
			NiagaraExtensionId,
			TEXT("NiagaraScript"),
			TEXT(""),
			[](UObject* Asset) -> bool
			{
				return Cast<UNiagaraScript>(Asset) != nullptr;
			},
			GraphResolver);
		Registrar.RegisterAssetCapability(
			NiagaraExtensionId,
			TEXT("NiagaraEmitter"),
			TEXT(""),
			[](UObject* Asset) -> bool
			{
				return Cast<UNiagaraEmitter>(Asset) != nullptr;
			},
			MoveTemp(GraphResolver));
		Registrar.RegisterAssetCapability(
			NiagaraExtensionId,
			TEXT("NiagaraParameterDefinitions"),
			TEXT("_enrich_niagara_param_defs"),
			[](UObject* Asset) -> bool
			{
				return Cast<UNiagaraParameterDefinitions>(Asset) != nullptr;
			},
			TFunction<bool(UObject*, TArray<FResolvedGraphInfo>&)>());
		Registrar.RegisterLuaBinding(
			NiagaraExtensionId,
			TEXT("Niagara"),
			TArray<FLuaFunctionDoc>(NSAINiagaraExtension::GetNiagaraLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAINiagaraExtension::BindNiagaraLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(NiagaraExtensionId);
		NeoAssetTypeAliases::UnregisterAliases(BuildNiagaraAssetAliasNames());
	}
};
IMPLEMENT_MODULE(FNSAI_NiagaraModule, NSAI_Niagara)
