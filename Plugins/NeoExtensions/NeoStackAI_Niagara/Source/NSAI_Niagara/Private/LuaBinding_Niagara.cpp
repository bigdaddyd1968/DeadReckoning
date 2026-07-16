#include "Lua/LuaBindingRegistry.h"
#include "Lua/LuaPropertyHelper.h"
#include "Lua/LuaPropertyTable.h"
#include "Lua/LuaAssetResolver.h"
#include "Lua/LuaEnumReflection.h"
#include "Lua/LuaStr.h"
#include "NiagaraStructHandlers.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "NiagaraEmitterBase.h"
#endif
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraCommon.h"
#include "NiagaraTypes.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "NiagaraTypeRegistry.h"
#endif
#include "NiagaraEditorUtilities.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraDecalRendererProperties.h"
#include "NiagaraVolumeRendererProperties.h"
#include "NiagaraComponentRendererProperties.h"
#include "Lua/LuaDynamicTypeHelper.h"
#include "NiagaraParameterStore.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraParameterDefinitionsBase.h"
#include "NiagaraParameterDefinitionsSubscriber.h"
#include "NiagaraParameterDefinitions.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraVariableMetaData.h"
#include "NiagaraBakerSettings.h"
#include "NiagaraBakerOutput.h"
#include "INiagaraMergeManager.h"
#include "NiagaraValidationRule.h"
#include "NiagaraValidationRules.h"
#include "NiagaraValidationRuleSet.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#ifndef NSAI_NIAGARA_HAS_EXTERNAL_EDIT_UTILITIES
#define NSAI_NIAGARA_HAS_EXTERNAL_EDIT_UTILITIES 0
#endif

#ifndef NSAI_NIAGARA_HAS_STATELESS_INTERNAL
#define NSAI_NIAGARA_HAS_STATELESS_INTERNAL 0
#endif

#if NSAI_NIAGARA_HAS_EXTERNAL_EDIT_UTILITIES
#include "NiagaraExternalSystemEditorUtilities.h"
#endif
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraDataInterfaceColorCurve.h"
#include "NiagaraDataInterfaceVectorCurve.h"
#include "NiagaraDataInterfaceVector2DCurve.h"
#include "NiagaraDataInterfaceVector4Curve.h"
#if NSAI_NIAGARA_HAS_STATELESS_INTERNAL
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/NiagaraStatelessModule.h"
#endif
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraParameterMapHistory.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Modules/ModuleManager.h"
#include "NiagaraExtensionBinding.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

// ============================================================================
// Helpers
// ============================================================================

namespace
{

ENiagaraScriptUsage ParseUsage(const FString& S)
{
	// Lowercase for case-insensitive matching of all variants
	FString L = S.ToLower();
	// System-level (check first — "systemspawn" contains "spawn")
	if (L.Contains(TEXT("system_spawn")) || L.Contains(TEXT("systemspawn"))) return ENiagaraScriptUsage::SystemSpawnScript;
	if (L.Contains(TEXT("system_update")) || L.Contains(TEXT("systemupdate"))) return ENiagaraScriptUsage::SystemUpdateScript;
	// Emitter-level (check before generic "spawn"/"update")
	if (L.Contains(TEXT("emitter_spawn")) || L.Contains(TEXT("emitterspawn"))) return ENiagaraScriptUsage::EmitterSpawnScript;
	if (L.Contains(TEXT("emitter_update")) || L.Contains(TEXT("emitterupdate"))) return ENiagaraScriptUsage::EmitterUpdateScript;
	// Event
	if (L.Contains(TEXT("particle_event")) || L.Contains(TEXT("particleevent")) || L.Contains(TEXT("event"))) return ENiagaraScriptUsage::ParticleEventScript;
	// Simulation stage
	if (L.Contains(TEXT("simstage")) || L.Contains(TEXT("simulation_stage")) || L.Contains(TEXT("simulationstage"))) return ENiagaraScriptUsage::ParticleSimulationStageScript;
	// Particle-level (generic "spawn"/"update" as fallback)
	if (L.Contains(TEXT("particle_spawn")) || L.Contains(TEXT("particlespawn")) || L.Contains(TEXT("spawn"))) return ENiagaraScriptUsage::ParticleSpawnScript;
	if (L.Contains(TEXT("particle_update")) || L.Contains(TEXT("particleupdate")) || L.Contains(TEXT("update"))) return ENiagaraScriptUsage::ParticleUpdateScript;
	return ENiagaraScriptUsage::ParticleUpdateScript;
}

FString UsageToStr(ENiagaraScriptUsage U)
{
	switch (U)
	{
	case ENiagaraScriptUsage::ParticleSpawnScript: return TEXT("particle_spawn");
	case ENiagaraScriptUsage::ParticleUpdateScript: return TEXT("particle_update");
	case ENiagaraScriptUsage::EmitterSpawnScript: return TEXT("emitter_spawn");
	case ENiagaraScriptUsage::EmitterUpdateScript: return TEXT("emitter_update");
	case ENiagaraScriptUsage::SystemSpawnScript: return TEXT("system_spawn");
	case ENiagaraScriptUsage::SystemUpdateScript: return TEXT("system_update");
	case ENiagaraScriptUsage::ParticleEventScript: return TEXT("particle_event");
	case ENiagaraScriptUsage::ParticleSimulationStageScript: return TEXT("simulation_stage");
	default: return TEXT("unknown");
	}
}

int32 FindEmitterIndex(UNiagaraSystem* System, const FString& Name)
{
	if (!System || Name.IsEmpty()) return INDEX_NONE;
	for (int32 i = 0; i < System->GetNumEmitters(); i++)
	{
		const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(i);
		if (Handle.GetName().ToString().Equals(Name, ESearchCase::IgnoreCase))
			return i;
	}
	return INDEX_NONE;
}

struct FNiagaraRendererStorage
{
	TArray<UNiagaraRendererProperties*> Renderers;
	UObject* OwnerObject = nullptr;
	bool bStateless = false;
};

bool ResolveRendererStorage(FNiagaraEmitterHandle& Handle, const FString& EmitterName, const TCHAR* Context, FNiagaraRendererStorage& OutStorage, FString& OutError)
{
	OutStorage = FNiagaraRendererStorage();
	OutStorage.bStateless = (Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless);

	if (OutStorage.bStateless)
	{
		UNiagaraStatelessEmitter* StatelessEmitter = Handle.GetStatelessEmitter();
		if (!StatelessEmitter)
		{
			OutError = FString::Printf(TEXT("%s -> stateless emitter '%s' has no stateless instance"), Context, *EmitterName);
			return false;
		}

#if NSAI_NIAGARA_HAS_STATELESS_INTERNAL
		OutStorage.Renderers = StatelessEmitter->GetRenderers();
		OutStorage.OwnerObject = StatelessEmitter;
#else
		UObject* StatelessOwner = (UObject*)StatelessEmitter;
		FArrayProperty* ArrProp = CastField<FArrayProperty>(StatelessOwner->GetClass()->FindPropertyByName(TEXT("RendererProperties")));
		if (!ArrProp)
		{
			OutError = FString::Printf(TEXT("%s -> RendererProperties UPROPERTY not found on stateless emitter"), Context);
			return false;
		}

		FObjectPropertyBase* InnerProp = CastField<FObjectPropertyBase>(ArrProp->Inner);
		if (!InnerProp)
		{
			OutError = FString::Printf(TEXT("%s -> RendererProperties inner is not an object"), Context);
			return false;
		}

		FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(StatelessOwner));
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			UObject* Obj = InnerProp->GetObjectPropertyValue(Helper.GetRawPtr(i));
			OutStorage.Renderers.Add(Cast<UNiagaraRendererProperties>(Obj));
		}
		OutStorage.OwnerObject = StatelessOwner;
#endif
		return true;
	}

	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!EmitterData)
	{
		OutError = FString::Printf(TEXT("%s -> no emitter data"), Context);
		return false;
	}

	OutStorage.Renderers = EmitterData->GetRenderers();
	return true;
}

bool IsParameterMapPin(const UEdGraphPin* Pin)
{
	if (!Pin) return false;
	FNiagaraTypeDefinition PinDef = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
	return PinDef == FNiagaraTypeDefinition::GetParameterMapDef();
}

UEdGraphPin* GetParamMapInputPin(UEdGraphNode* Node)
{
	if (!Node) return nullptr;
	for (UEdGraphPin* Pin : Node->GetAllPins())
	{
		if (Pin->Direction == EGPD_Input && IsParameterMapPin(Pin))
			return Pin;
	}
	return nullptr;
}

UEdGraphPin* GetParamMapOutputPin(UEdGraphNode* Node)
{
	if (!Node) return nullptr;
	for (UEdGraphPin* Pin : Node->GetAllPins())
	{
		if (Pin->Direction == EGPD_Output && IsParameterMapPin(Pin))
			return Pin;
	}
	return nullptr;
}

void CollectSubGraphNodes(UEdGraphNode* Node, TArray<UEdGraphNode*>& Out, TSet<UEdGraphNode*>& Visited)
{
	if (!Node || Visited.Contains(Node)) return;
	Visited.Add(Node);
	Out.Add(Node);
	for (UEdGraphPin* Pin : Node->GetAllPins())
	{
		if (Pin->Direction == EGPD_Input)
		{
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (UEdGraphNode* LN = Linked->GetOwningNode())
					CollectSubGraphNodes(LN, Out, Visited);
			}
		}
	}
}

// -- CustomHlsl reflection helpers (MinimalAPI class, methods not exported) --------
// The CustomHlsl UPROPERTY is private; access via UE reflection.
FString GetCustomHlslText(const UNiagaraNodeCustomHlsl* Node)
{
	if (!Node) return FString();
	const FStrProperty* Prop = FindFProperty<FStrProperty>(UNiagaraNodeCustomHlsl::StaticClass(), TEXT("CustomHlsl"));
	if (!Prop) return FString();
	return Prop->GetPropertyValue_InContainer(Node);
}

void SetCustomHlslText(UNiagaraNodeCustomHlsl* Node, const FString& InHlsl)
{
	if (!Node) return;
	FProperty* Prop = NeoLuaProperty::FindPropertyByName(UNiagaraNodeCustomHlsl::StaticClass(), TEXT("CustomHlsl"));
	if (!Prop) return;
	Node->Modify();
	NeoLuaProperty::FPropertyValueInput Input;
	Input.bIsString = true;
	Input.StringValue = InHlsl;
	FString Error;
	NeoLuaProperty::SetPropertyValue(Prop, Prop->ContainerPtrToValuePtr<void>(Node), Node, Input, Error);
	// Mirror what SetCustomHlsl() does after setting the value:
	Node->RefreshFromExternalChanges(); // virtual — dispatches through vtable (MinimalAPI exports vtable)
	if (Node->GetOuter() && Node->GetOuter()->IsA<UNiagaraGraph>())
	{
		Node->MarkNodeRequiresSynchronization(TEXT("SetCustomHLSLInput"), true); // NIAGARAEDITOR_API
	}
}

// Replicate InitAsCustomHlslDynamicInput() without calling unexported methods.
// Sets up Signature with a ParameterMap input + typed output, sets ScriptUsage, then ReallocatePins().
void InitCustomHlslAsDynamicInput(UNiagaraNodeCustomHlsl* Node, const FNiagaraTypeDefinition& OutputType)
{
	if (!Node) return;
	Node->Modify();

	// Clear and rebuild the function signature to match what InitAsCustomHlslDynamicInput does:
	// Input 0: ParameterMap ("Map"), Output 0: OutputType ("CustomHLSLOutput")
	Node->Signature.Inputs.Reset();
	Node->Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Map")));
	Node->Signature.Outputs.Reset();
	Node->Signature.Outputs.Add(FNiagaraVariableBase(OutputType, TEXT("CustomHLSLOutput")));

	// ScriptUsage is a public UPROPERTY
	Node->ScriptUsage = ENiagaraScriptUsage::DynamicInput;

	// ReallocatePins() is protected — use public UEdGraphNode::ReconstructNode() instead
	Node->ReconstructNode();
}

// -- Graph parameter helpers (AddParameter/HasVariable not exported) ---------------
bool GraphHasVariable(UNiagaraGraph* Graph, const FNiagaraVariable& Var)
{
	// GetScriptVariable(FName) is NIAGARAEDITOR_API
	return Graph && Graph->GetScriptVariable(Var.GetName()) != nullptr;
}

void GraphAddParameter(UNiagaraGraph* Graph, const FNiagaraVariable& Var)
{
	if (!Graph) return;
	// Create a UNiagaraScriptVariable and insert into the exported mutable metadata map
	UNiagaraScriptVariable* ScriptVar = NewObject<UNiagaraScriptVariable>(Graph, NAME_None, RF_Transactional);
	FNiagaraVariableMetaData MetaData;
	MetaData.SetVariableGuid(FGuid::NewGuid());
	ScriptVar->Init(Var, MetaData); // NIAGARAEDITOR_API
	Graph->GetAllMetaData().Add(Var, ScriptVar); // GetAllMetaData() mutable ref is NIAGARAEDITOR_API
	Graph->NotifyGraphChanged(); // virtual — dispatches through vtable, triggers recompile
}

UNiagaraNodeOutput* FindOutputNode(UNiagaraSystem* System, int32 EmitterIdx, ENiagaraScriptUsage Usage, const FGuid& UsageId = FGuid(), bool bCreateIfMissing = true)
{
	UNiagaraScript* Script = nullptr;
	if (EmitterIdx == INDEX_NONE)
	{
		if (Usage == ENiagaraScriptUsage::SystemSpawnScript)
			Script = System->GetSystemSpawnScript();
		else if (Usage == ENiagaraScriptUsage::SystemUpdateScript)
			Script = System->GetSystemUpdateScript();
	}
	else
	{
		FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmitterIdx).GetEmitterData();
		if (ED)
		{
			TArray<UNiagaraScript*> Scripts;
			ED->GetScripts(Scripts, false);
			for (UNiagaraScript* S : Scripts)
			{
				if (S && S->GetUsage() == Usage)
				{
					if (!UsageId.IsValid() || S->GetUsageId() == UsageId)
					{
						Script = S;
						break;
					}
				}
			}
		}
	}
	// Try the script's own source first, then fall back to the emitter's shared GraphSource.
	// Emitter-embedded scripts (ParticleSpawn, ParticleUpdate, EmitterSpawn, EmitterUpdate)
	// share one graph source stored at EmitterData->GraphSource, not on the individual script.
	// For versioned template emitters, the ParticleSpawn script may not exist as a separate object
	// but the shared GraphSource still has the graph where we can create an output node.
	UNiagaraScriptSource* Src = Script ? Cast<UNiagaraScriptSource>(Script->GetLatestSource()) : nullptr;
	if (!Src && EmitterIdx != INDEX_NONE)
	{
		FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmitterIdx).GetEmitterData();
		if (ED) Src = Cast<UNiagaraScriptSource>(ED->GraphSource);
	}
	if (!Src) return nullptr;
	UNiagaraGraph* Graph = Src->NodeGraph;
	if (!Graph) return nullptr;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UNiagaraNodeOutput* Out = Cast<UNiagaraNodeOutput>(Node);
		if (Out && Out->GetUsage() == Usage)
			return Out;
	}

	if (!bCreateIfMissing) return nullptr;

	// Auto-create missing output node (some templates ship without ParticleSpawnScript output nodes)
	// ResetGraphForOutput is not exported from NiagaraEditor, so we replicate it inline
	Graph->Modify();

	FGraphNodeCreator<UNiagaraNodeOutput> OutputNodeCreator(*Graph);
	UNiagaraNodeOutput* NewOutput = OutputNodeCreator.CreateNode();
	NewOutput->SetUsage(Usage);
	NewOutput->SetUsageId(Script ? Script->GetUsageId() : UsageId);
	NewOutput->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Out")));
	OutputNodeCreator.Finalize();

	FGraphNodeCreator<UNiagaraNodeInput> InputNodeCreator(*Graph);
	UNiagaraNodeInput* InputNode = InputNodeCreator.CreateNode();
	InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap"));
	InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
	InputNodeCreator.Finalize();

	// Connect InputNode output → NewOutput input (parameter map pins)
	UEdGraphPin* OutInputPin = nullptr;
	for (UEdGraphPin* Pin : NewOutput->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input) { OutInputPin = Pin; break; }
	}
	UEdGraphPin* InOutputPin = nullptr;
	for (UEdGraphPin* Pin : InputNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output) { InOutputPin = Pin; break; }
	}
	if (OutInputPin && InOutputPin)
	{
		OutInputPin->MakeLinkTo(InOutputPin);
	}

	Graph->NotifyGraphChanged();
	UE_LOG(LogTemp, Log, TEXT("AIK: Auto-created missing output node for usage %d in Niagara graph"), static_cast<int32>(Usage));
	return NewOutput;
}

UNiagaraNodeFunctionCall* FindModuleByName(UNiagaraNodeOutput* OutputNode, const FString& ModuleName)
{
	if (!OutputNode || ModuleName.IsEmpty()) return nullptr;
	UEdGraphPin* InputPin = GetParamMapInputPin(OutputNode);
	if (!InputPin) return nullptr;

	TSet<UEdGraphNode*> Visited;
	TQueue<UEdGraphNode*> Queue;
	for (UEdGraphPin* Link : InputPin->LinkedTo)
	{
		if (Link && Link->GetOwningNode()) Queue.Enqueue(Link->GetOwningNode());
	}

	while (!Queue.IsEmpty())
	{
		UEdGraphNode* Node = nullptr;
		Queue.Dequeue(Node);
		if (!Node || Visited.Contains(Node)) continue;
		Visited.Add(Node);

		if (UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(Node))
		{
			FString NodeName = FC->GetFunctionName();
			if (NodeName.Equals(ModuleName, ESearchCase::IgnoreCase) ||
				NodeName.Contains(ModuleName))
				return FC;
		}

		UEdGraphPin* PrevPin = GetParamMapInputPin(Node);
		if (PrevPin)
		{
			for (UEdGraphPin* Link : PrevPin->LinkedTo)
			{
				if (Link && Link->GetOwningNode())
					Queue.Enqueue(Link->GetOwningNode());
			}
		}
	}
	return nullptr;
}

TArray<FString> ListModulesInStack(UNiagaraNodeOutput* OutputNode)
{
	TArray<FString> Result;
	if (!OutputNode) return Result;
	UEdGraphPin* InputPin = GetParamMapInputPin(OutputNode);
	if (!InputPin) return Result;

	UEdGraphNode* Current = nullptr;
	if (InputPin->LinkedTo.Num() > 0 && InputPin->LinkedTo[0])
		Current = InputPin->LinkedTo[0]->GetOwningNode();

	TSet<UEdGraphNode*> Visited;
	while (Current && !Visited.Contains(Current))
	{
		Visited.Add(Current);
		if (UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(Current))
		{
			Result.Insert(FC->GetFunctionName(), 0);
		}
		UEdGraphPin* PrevPin = GetParamMapInputPin(Current);
		Current = nullptr;
		if (PrevPin && PrevPin->LinkedTo.Num() > 0 && PrevPin->LinkedTo[0])
			Current = PrevPin->LinkedTo[0]->GetOwningNode();
	}
	return Result;
}

static const TArray<FString> RendererPrefixes = { TEXT("Niagara") };
static const TArray<FString> RendererSuffixes = { TEXT("RendererProperties") };

UNiagaraRendererProperties* CreateRendererByType(UNiagaraEmitter* Emitter, const FString& Type)
{
	if (!Emitter) return nullptr;
	UClass* Cls = LuaDynamicType::FindDerivedClass(UNiagaraRendererProperties::StaticClass(), Type, RendererPrefixes, RendererSuffixes);
	if (!Cls) return nullptr;
	return NewObject<UNiagaraRendererProperties>(Emitter, Cls);
}

FString GetRendererTypeName(const UNiagaraRendererProperties* R)
{
	if (!R) return TEXT("Unknown");
	return LuaDynamicType::GetFriendlyTypeName(R, RendererPrefixes, RendererSuffixes);
}

sol::table ReadObjectPropertiesAsTable(UObject* Object, sol::state_view Lua)
{
	sol::table Out = Lua.create_table();
	if (!Object) return Out;

	for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;

		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Object);
		sol::object Value = NeoLuaProperty::ReadPropertyAsSol(Prop, ValuePtr, Lua);
		if (Value.valid() && !Value.is<sol::lua_nil_t>())
		{
			Out[TCHAR_TO_UTF8(*Prop->GetAuthoredName())] = Value;
		}
	}

	return Out;
}

sol::table BuildRendererSummary(UNiagaraRendererProperties* Renderer, int32 Index, sol::state_view Lua)
{
	sol::table R = Lua.create_table();
	R["index"] = Index;
	R["type"] = Renderer ? TCHAR_TO_UTF8(*GetRendererTypeName(Renderer)) : "";
	R["enabled"] = Renderer ? Renderer->GetIsEnabled() : false;
	R["class"] = Renderer ? TCHAR_TO_UTF8(*Renderer->GetClass()->GetName()) : "";
	if (!Renderer) return R;

	R["properties"] = ReadObjectPropertiesAsTable(Renderer, Lua);

	// Retain the small legacy summary fields for existing scripts.
	if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
	{
		if (Sprite->Material) R["material"] = TCHAR_TO_UTF8(*Sprite->Material->GetName());
	}
	else if (UNiagaraMeshRendererProperties* Mesh = Cast<UNiagaraMeshRendererProperties>(Renderer))
	{
		R["mesh_count"] = static_cast<int>(Mesh->Meshes.Num());
	}

	return R;
}

TArray<UNiagaraScript*> FindAffectedScripts(UNiagaraSystem* System, const FNiagaraEmitterHandle& EmitterHandle, ENiagaraScriptUsage Usage, const FGuid& UsageId = FGuid())
{
	TArray<UNiagaraScript*> Result;
	if (UNiagaraScript* S = System->GetSystemSpawnScript()) Result.AddUnique(S);
	if (UNiagaraScript* S = System->GetSystemUpdateScript()) Result.AddUnique(S);

	FVersionedNiagaraEmitterData* ED = EmitterHandle.GetEmitterData();
	if (ED)
	{
		TArray<UNiagaraScript*> EmitterScripts;
		ED->GetScripts(EmitterScripts, false);
		for (UNiagaraScript* S : EmitterScripts)
		{
			if (!S || !S->ContainsUsage(Usage)) continue;
			// For event handlers and simulation stages, also match by UsageId to target the specific script
			if (UsageId.IsValid() &&
				(Usage == ENiagaraScriptUsage::ParticleEventScript || Usage == ENiagaraScriptUsage::ParticleSimulationStageScript) &&
				S->GetUsageId() != UsageId)
			{
				continue;
			}
			Result.AddUnique(S);
		}
	}
	return Result;
}

void CleanupOverridePinNodes(UEdGraphPin& OverridePin)
{
	if (OverridePin.LinkedTo.Num() == 0) return;
	UEdGraphPin* LinkedPin = OverridePin.LinkedTo[0];
	if (!LinkedPin) return;
	UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
	if (!LinkedNode) { OverridePin.BreakAllPinLinks(true); return; }
	UEdGraph* Graph = LinkedNode->GetGraph();
	if (!Graph) { OverridePin.BreakAllPinLinks(true); return; }

	// Engine parity: use IsA<> for types with public headers, string match only for private types
	if (LinkedNode->IsA<UNiagaraNodeInput>() || LinkedNode->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapGet"))
	{
		OverridePin.BreakAllPinLinks(true);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		LinkedNode->BreakAllNodeLinks(true);
#else
		LinkedNode->BreakAllNodeLinks();
#endif
		Graph->RemoveNode(LinkedNode);
	}
	else if (LinkedNode->IsA<UNiagaraNodeFunctionCall>() || LinkedNode->IsA<UNiagaraNodeCustomHlsl>())
	{
		OverridePin.BreakAllPinLinks(true);
		UEdGraphPin* DIInputMap = GetParamMapInputPin(LinkedNode);
		UEdGraphPin* DIOutputMap = GetParamMapOutputPin(LinkedNode);
		if (DIInputMap) DIInputMap->BreakAllPinLinks(true);
		if (DIOutputMap) DIOutputMap->BreakAllPinLinks(true);

		TArray<UEdGraphNode*> NodesToRemove;
		TSet<UEdGraphNode*> Visited;
		CollectSubGraphNodes(LinkedNode, NodesToRemove, Visited);
		for (UEdGraphNode* N : NodesToRemove)
		{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			if (N) { N->BreakAllNodeLinks(true); Graph->RemoveNode(N); }
#else
			if (N) { N->BreakAllNodeLinks(); Graph->RemoveNode(N); }
#endif
		}
	}
	else
	{
		OverridePin.BreakAllPinLinks(true);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		LinkedNode->BreakAllNodeLinks(true);
#else
		LinkedNode->BreakAllNodeLinks();
#endif
		Graph->RemoveNode(LinkedNode);
	}
}

// Engine's SetDataInterfaceValueForFunctionInput / SetDynamicInputForFunctionInput / SetLinkedParameterValueForFunctionInput
// all do CastChecked<UNiagaraNodeParameterMapSet>(OverridePin.GetOwningNode()).
// Static switch pins live on UNiagaraNodeFunctionCall instead — passing those crashes.
// The engine's stack UI prevents this by never offering DI/dynamic/linked modes for static switches.
// We replicate that check here. Private header → class name check.
bool IsOverridePinOnParameterMapSet(UEdGraphPin& Pin)
{
	UEdGraphNode* Owner = Pin.GetOwningNode();
	return Owner && Owner->GetClass()->GetName().Contains(TEXT("ParameterMapSet"));
}

FString ShortInputName(const FString& Name)
{
	FString Short = Name;
	int32 LastDot = INDEX_NONE;
	if (Short.FindLastChar(TEXT('.'), LastDot))
	{
		Short = Short.RightChop(LastDot + 1);
	}
	return Short;
}

UEdGraphPin* FindDirectModuleInputPin(UNiagaraNodeFunctionCall* ModuleNode, const FString& ParamName, const FName& CanonicalInputName)
{
	if (!ModuleNode) return nullptr;

	const FString Canonical = CanonicalInputName.ToString();
	const FString CanonicalShort = ShortInputName(Canonical);
	for (UEdGraphPin* Pin : ModuleNode->GetAllPins())
	{
		if (!Pin || Pin->Direction != EGPD_Input || IsParameterMapPin(Pin)) continue;

		const FString PinName = Pin->GetName();
		if (PinName.Equals(ParamName, ESearchCase::IgnoreCase) ||
			PinName.Equals(Canonical, ESearchCase::IgnoreCase) ||
			PinName.Equals(CanonicalShort, ESearchCase::IgnoreCase) ||
			(FString(TEXT("Module.")) + PinName).Equals(Canonical, ESearchCase::IgnoreCase))
		{
			return Pin;
		}
	}
	return nullptr;
}

FString EnumValueToReadableString(UEnum* Enum, int64 Value)
{
	if (!Enum) return FString::FromInt(static_cast<int32>(Value));

	const FText Display = Enum->GetDisplayNameTextByValue(Value);
	const FString DisplayString = Display.ToString();
	if (!DisplayString.IsEmpty())
	{
		return DisplayString;
	}

	const FString NameString = Enum->GetNameStringByValue(Value);
	return NameString.IsEmpty() ? FString::FromInt(static_cast<int32>(Value)) : NameString;
}

sol::object PinDefaultToSolValue(const FString& DefaultValue, const FNiagaraTypeDefinition& TypeDef, sol::state_view Lua)
{
	if (DefaultValue.IsEmpty()) return sol::make_object(Lua, sol::lua_nil);

	if (TypeDef.IsEnum())
	{
		if (UEnum* Enum = TypeDef.GetEnum())
		{
			int64 ResolvedValue = INDEX_NONE;
			if (NeoLuaEnum::ParseRuntime(Enum, DefaultValue, ResolvedValue))
			{
				return sol::make_object(Lua, std::string(TCHAR_TO_UTF8(*EnumValueToReadableString(Enum, ResolvedValue))));
			}
		}
		return sol::make_object(Lua, std::string(TCHAR_TO_UTF8(*DefaultValue)));
	}

	if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
	{
		if (DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || DefaultValue == TEXT("1"))
			return sol::make_object(Lua, true);
		if (DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) || DefaultValue == TEXT("0"))
			return sol::make_object(Lua, false);
	}

	double Numeric = 0.0;
	if (LexTryParseString(Numeric, *DefaultValue))
	{
		return sol::make_object(Lua, Numeric);
	}

	return sol::make_object(Lua, std::string(TCHAR_TO_UTF8(*DefaultValue)));
}

bool TypedBytesToPinDefault(const TArray<uint8>& Bytes, const FNiagaraTypeDefinition& TypeDef, sol::state_view Lua, FString& OutDefault)
{
	if (Bytes.Num() <= 0 || !TypeDef.IsValid()) return false;

	if (TypeDef.IsEnum())
	{
		if (UEnum* Enum = TypeDef.GetEnum())
		{
			const int32 IntValue = *reinterpret_cast<const int32*>(Bytes.GetData());
			OutDefault = Enum->GetNameStringByValue(IntValue);
			if (OutDefault.IsEmpty())
			{
				OutDefault = FString::FromInt(IntValue);
			}
			return true;
		}
	}

	sol::object Value = NSAINiagaraLCD::ReadTypedBytesAsSolValue(Bytes.GetData(), TypeDef, Lua);
	if (Value.is<bool>())
	{
		OutDefault = Value.as<bool>() ? TEXT("true") : TEXT("false");
		return true;
	}
	if (Value.is<double>())
	{
		OutDefault = FString::SanitizeFloat(Value.as<double>());
		return true;
	}
	if (Value.is<std::string>())
	{
		OutDefault = NeoLuaStr::ToFString(Value.as<std::string>());
		return true;
	}
	return false;
}

void RemoveRapidIterationParam(UNiagaraSystem* System, const FNiagaraEmitterHandle& EmitterHandle, UNiagaraScript* Script, const FNiagaraVariable& AliasedVar)
{
	if (!System || !Script) return;

	const TCHAR* EmitterNamePtr = nullptr;
	FString UniqueEmitterName;
	if (EmitterHandle.IsValid() && EmitterHandle.GetInstance().Emitter)
	{
		UniqueEmitterName = EmitterHandle.GetInstance().Emitter->GetUniqueEmitterName();
		if (!UniqueEmitterName.IsEmpty())
			EmitterNamePtr = *UniqueEmitterName;
	}

	FNiagaraVariable RIParam = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
		AliasedVar,
		EmitterNamePtr,
		Script->GetUsage());

	TArray<UNiagaraScript*> Affected = FindAffectedScripts(System, EmitterHandle, Script->GetUsage());
	for (UNiagaraScript* S : Affected)
	{
		if (S) { S->Modify(); S->RapidIterationParameters.RemoveParameter(RIParam); }
	}
}

TArray<FNiagaraVariable> DiscoverModuleInputs(UNiagaraNodeFunctionCall* ModuleNode, UNiagaraScript* RIScript = nullptr,
	const FNiagaraEmitterHandle* EmitterHandle = nullptr, ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleUpdateScript)
{
	TArray<FNiagaraVariable> Result;
	if (!ModuleNode) return Result;

	// Use the engine's GetStackFunctionInputs API which properly traverses the module's internal
	// graph and resolves static switches to discover ALL inputs including gated ones
	// (e.g. Lifetime, Color, Sprite Size behind mode selectors on V2 InitializeParticle)
	if (EmitterHandle && EmitterHandle->IsValid())
	{
		FVersionedNiagaraEmitter VersionedEmitter = EmitterHandle->GetInstance();
		FCompileConstantResolver Resolver(VersionedEmitter, Usage);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		// 5.6+: Use the overload with OutHiddenVariables so we also discover static-switch-gated inputs
		TSet<FNiagaraVariable> HiddenVars;
		FNiagaraStackGraphUtilities::GetStackFunctionInputs(*ModuleNode, Result, HiddenVars, Resolver,
			FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);
		for (const FNiagaraVariable& HV : HiddenVars) Result.AddUnique(HV);
#else
		// 5.4/5.5: GetStackFunctionInputs with resolver not available — skip, rely on pin+RI fallback below
		(void)Resolver;
#endif
	}

	// Also discover via pins — catches data interface pins and other inputs
	// that GetStackFunctionInputs might not return
	for (UEdGraphPin* Pin : ModuleNode->GetAllPins())
	{
		if (Pin->Direction == EGPD_Input && !IsParameterMapPin(Pin))
		{
			FNiagaraTypeDefinition PinType = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
			Result.AddUnique(FNiagaraVariable(PinType, Pin->GetFName()));
		}
	}

	if (UNiagaraScript* FuncScript = ModuleNode->FunctionScript)
	{
		for (const FNiagaraVariable& ScriptVar : FuncScript->GetVMExecutableData().Parameters.Parameters)
		{
			FString VarName = ScriptVar.GetName().ToString();
			if (!VarName.StartsWith(TEXT("Module."))) continue;

			FString ShortName = VarName.RightChop(7);
			bool bAlreadyFound = false;
			for (const FNiagaraVariable& Existing : Result)
			{
				if (Existing.GetName().ToString().Equals(VarName, ESearchCase::IgnoreCase) ||
					Existing.GetName().ToString().EndsWith(ShortName))
				{
					bAlreadyFound = true; break;
				}
			}
			if (!bAlreadyFound)
				Result.Add(ScriptVar);
		}
	}

	if (RIScript)
	{
		FString ModFuncName = ModuleNode->GetFunctionName();
		for (const FNiagaraVariableWithOffset& RIVar : RIScript->RapidIterationParameters.ReadParameterVariables())
		{
			FString RIName = RIVar.GetName().ToString();
			if (!RIName.Contains(ModFuncName)) continue;

			FString ShortName = RIName;
			int32 LastDot = INDEX_NONE;
			if (ShortName.FindLastChar(TEXT('.'), LastDot))
				ShortName = ShortName.RightChop(LastDot + 1);

			bool bAlreadyFound = false;
			for (const FNiagaraVariable& Existing : Result)
			{
				FString ExName = Existing.GetName().ToString();
				if (ExName.StartsWith(TEXT("Module.")))
					ExName = ExName.RightChop(7);
				if (ExName.Equals(ShortName, ESearchCase::IgnoreCase)) { bAlreadyFound = true; break; }
			}
			if (bAlreadyFound) continue;

			FNiagaraVariable ModVar(RIVar.GetType(), FName(*FString::Printf(TEXT("Module.%s"), *ShortName)));
			Result.Add(ModVar);
		}
	}

	return Result;
}

UNiagaraScript* FindScratchPadScript(UNiagaraSystem* System, const FString& Name)
{
	if (!System) return nullptr;
	for (UNiagaraScript* S : System->ScratchPadScripts)
	{
		if (S && S->GetName().Equals(Name, ESearchCase::IgnoreCase))
			return S;
	}
	return nullptr;
}

struct FScratchPadTarget
{
	UNiagaraNodeOutput* OutputNode = nullptr;
	UNiagaraScript*     Script     = nullptr;
	UNiagaraGraph*      Graph      = nullptr;
};

// Resolve a scratch-pad script by name + its graph's output node. Logs and returns
// a zeroed target on failure. FindOutputNode (NIAGARA_API-unexported) is avoided —
// we walk Graph->Nodes for UNiagaraNodeOutput directly.
FScratchPadTarget ResolveScratchPadTarget(UNiagaraSystem* System, FLuaSessionData& Session,
	const FString& Name, const FString& Context)
{
	FScratchPadTarget Out;
	UNiagaraScript* Script = FindScratchPadScript(System, Name);
	if (!Script)
	{
		Session.Log(FString::Printf(TEXT("[FAIL] %s -> scratch_pad '%s' not found"), *Context, *Name));
		return Out;
	}
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	UNiagaraGraph* Graph = Source ? Source->NodeGraph : nullptr;
	if (!Graph)
	{
		Session.Log(FString::Printf(TEXT("[FAIL] %s -> scratch_pad '%s' has no graph"), *Context, *Name));
		return Out;
	}
	UNiagaraNodeOutput* OutputNode = nullptr;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (UNiagaraNodeOutput* ON = Cast<UNiagaraNodeOutput>(N))
		{
			OutputNode = ON;
			break;
		}
	}
	if (!OutputNode)
	{
		Session.Log(FString::Printf(TEXT("[FAIL] %s -> scratch_pad '%s' has no output node"), *Context, *Name));
		return Out;
	}
	Out.Script = Script;
	Out.Graph = Graph;
	Out.OutputNode = OutputNode;
	return Out;
}

// Build a transient FNiagaraSystemViewModel suitable for stack inspection.
// Mirrors the pattern in run_validation (~5137) — data-only, no auto-compile/simulate.
// `Options.MessageLogGuid` MUST be set; FNiagaraMessageManager asserts otherwise.
TSharedRef<FNiagaraSystemViewModel> BuildSystemViewModelForStackInspection(UNiagaraSystem* System)
{
	FNiagaraSystemViewModelOptions Options;
	Options.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;
	Options.bCanAutoCompile = false;
	Options.bCanSimulate = false;
	Options.bCanModifyEmittersFromTimeline = false;
	Options.bIsForDataProcessingOnly = true;
	Options.MessageLogGuid = System->GetAssetGuid();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	Options.bCompileForEdit = false;
#endif
	TSharedRef<FNiagaraSystemViewModel> VM = MakeShared<FNiagaraSystemViewModel>();
	VM->Initialize(*System, Options);
	return VM;
}

// All stack roots: the system root + each emitter handle root.
TArray<UNiagaraStackEntry*> CollectAllStackRoots(const TSharedRef<FNiagaraSystemViewModel>& VM)
{
	TArray<UNiagaraStackEntry*> Roots;
	if (UNiagaraStackViewModel* SysStack = VM->GetSystemStackViewModel())
	{
		if (UNiagaraStackEntry* Root = SysStack->GetRootEntry()) Roots.Add(Root);
	}
	for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EHVM : VM->GetEmitterHandleViewModels())
	{
		if (UNiagaraStackViewModel* EmStack = EHVM->GetEmitterStackViewModel())
		{
			if (UNiagaraStackEntry* Root = EmStack->GetRootEntry()) Roots.Add(Root);
		}
	}
	return Roots;
}

const TCHAR* StackIssueSeverityToString(EStackIssueSeverity Severity)
{
	switch (Severity)
	{
		case EStackIssueSeverity::Error:   return TEXT("error");
		case EStackIssueSeverity::Warning: return TEXT("warning");
		case EStackIssueSeverity::Info:    return TEXT("info");
		case EStackIssueSeverity::None:    return TEXT("none");
		default: return TEXT("none");
	}
}

const TCHAR* StackIssueFixStyleToString(UNiagaraStackEntry::EStackIssueFixStyle Style)
{
	switch (Style)
	{
		case UNiagaraStackEntry::EStackIssueFixStyle::Fix:  return TEXT("fix");
		case UNiagaraStackEntry::EStackIssueFixStyle::Link: return TEXT("link");
		default: return TEXT("fix");
	}
}

#if NSAI_NIAGARA_HAS_EXTERNAL_EDIT_UTILITIES
const TCHAR* StackIssueSeverityToString(ENiagaraExt_StackIssueSeverity Severity)
{
	switch (Severity)
	{
		case ENiagaraExt_StackIssueSeverity::Error:   return TEXT("error");
		case ENiagaraExt_StackIssueSeverity::Warning: return TEXT("warning");
		case ENiagaraExt_StackIssueSeverity::Info:    return TEXT("info");
		case ENiagaraExt_StackIssueSeverity::None:    return TEXT("none");
		default: return TEXT("none");
	}
}

const TCHAR* StackIssueFixStyleToString(ENiagaraExt_StackIssueFixStyle Style)
{
	switch (Style)
	{
		case ENiagaraExt_StackIssueFixStyle::Fix:  return TEXT("fix");
		case ENiagaraExt_StackIssueFixStyle::Link: return TEXT("link");
		default: return TEXT("fix");
	}
}

void LogExternalEditErrors(FLuaSessionData& Session, const FNiagaraExternalEditContext& Context, const TCHAR* VerbName)
{
	for (const FText& Error : Context.Errors)
	{
		Session.Log(FString::Printf(TEXT("[FAIL] %s -> %s"), VerbName, *Error.ToString()));
	}
}

sol::table BuildStackItemLocationTable(sol::state_view Lua, const FNiagaraExt_StackItemReference& Location)
{
	sol::table Out = Lua.create_table();
	if (!Location.EmitterName.IsNone()) Out["emitter_name"] = TCHAR_TO_UTF8(*Location.EmitterName.ToString());
	if (!Location.ScriptName.IsNone()) Out["script_name"] = TCHAR_TO_UTF8(*Location.ScriptName.ToString());
	if (!Location.ModuleName.IsNone()) Out["module_name"] = TCHAR_TO_UTF8(*Location.ModuleName.ToString());
	if (Location.RendererIndex != INDEX_NONE) Out["renderer_index"] = Location.RendererIndex;

	sol::table Inputs = Lua.create_table();
	int32 InputIdx = 1;
	for (const FName& InputName : Location.InputNameStack)
	{
		Inputs[InputIdx++] = TCHAR_TO_UTF8(*InputName.ToString());
	}
	Out["input_name_stack"] = Inputs;
	return Out;
}
#endif

// Walk a stack root + every descendant via GetUnfilteredChildren, calling Visitor on each entry.
// RefreshChildren is called pre-order so issues populate even in a freshly-built VM.
void TraverseStackEntry(UNiagaraStackEntry* Entry, const TFunctionRef<void(UNiagaraStackEntry*)>& Visitor)
{
	if (!Entry || Entry->IsFinalized()) return;
	Entry->RefreshChildren();
	Visitor(Entry);
	TArray<UNiagaraStackEntry*> Children;
	Entry->GetUnfilteredChildren(Children);
	for (UNiagaraStackEntry* Child : Children)
	{
		TraverseStackEntry(Child, Visitor);
	}
}

// Aggregate issue counts across all roots.
struct FStackIssueCounts
{
	int32 Total = 0;
	int32 Errors = 0;
	int32 Warnings = 0;
	int32 Infos = 0;
	int32 Fixable = 0;
};

void CountStackIssues(const TArray<UNiagaraStackEntry*>& Roots, FStackIssueCounts& Out)
{
	for (UNiagaraStackEntry* Root : Roots)
	{
		TraverseStackEntry(Root, [&Out](UNiagaraStackEntry* E)
		{
			for (const UNiagaraStackEntry::FStackIssue& Issue : E->GetIssues())
			{
				Out.Total++;
				switch (Issue.GetSeverity())
				{
					case EStackIssueSeverity::Error:   Out.Errors++;   break;
					case EStackIssueSeverity::Warning: Out.Warnings++; break;
					case EStackIssueSeverity::Info:    Out.Infos++;    break;
					default: break;
				}
				if (Issue.GetFixes().Num() > 0) Out.Fixable++;
			}
		});
	}
}

// Lookup-and-fire (IssueId, FixId) on the freshly-built VM. Out reasons distinguish failure modes.
enum class EStackFixApplyResult : uint8 { IssueNotFound, FixNotFound, FixNotBound, Fired };

EStackFixApplyResult FindAndFireStackFix(const TArray<UNiagaraStackEntry*>& Roots,
	const FString& IssueId, const FString& FixId, FString& OutFiredFixDescription)
{
	bool bIssueSeen = false;
	EStackFixApplyResult Result = EStackFixApplyResult::IssueNotFound;
	for (UNiagaraStackEntry* Root : Roots)
	{
		TraverseStackEntry(Root, [&](UNiagaraStackEntry* E)
		{
			if (Result == EStackFixApplyResult::Fired) return;
			for (const UNiagaraStackEntry::FStackIssue& Issue : E->GetIssues())
			{
				if (!Issue.GetUniqueIdentifier().Equals(IssueId)) continue;
				bIssueSeen = true;
				for (const UNiagaraStackEntry::FStackIssueFix& Fix : Issue.GetFixes())
				{
					if (!Fix.GetUniqueIdentifier().Equals(FixId)) continue;
					const UNiagaraStackEntry::FStackIssueFixDelegate& Delegate = Fix.GetFixDelegate();
					if (!Delegate.IsBound())
					{
						Result = EStackFixApplyResult::FixNotBound;
						return;
					}
					OutFiredFixDescription = Fix.GetDescription().ToString();
					Delegate.ExecuteIfBound();
					Result = EStackFixApplyResult::Fired;
					return;
				}
				if (Result != EStackFixApplyResult::Fired) Result = EStackFixApplyResult::FixNotFound;
			}
		});
		if (Result == EStackFixApplyResult::Fired) break;
	}
	if (!bIssueSeen) Result = EStackFixApplyResult::IssueNotFound;
	return Result;
}

// Warn about any keys in a sol::table that weren't consumed.
// ConsumedKeys: keys we actually read; Context: e.g. "list(\"module_inputs\")"
void WarnUnconsumedKeys(FLuaSessionData& Session, const sol::table& T, const TSet<FString>& ConsumedKeys, const FString& Context)
{
	for (auto& Pair : T)
	{
		if (!Pair.first.is<std::string>()) continue;
		FString Key = NeoLuaStr::ToFString(Pair.first.as<std::string>());
		if (!ConsumedKeys.Contains(Key))
		{
			Session.Log(FString::Printf(TEXT("[WARN] %s -> key '%s' was not consumed. Keys used: %s"),
				*Context, *Key, ConsumedKeys.Num() > 0 ? *FString::Join(ConsumedKeys.Array(), TEXT(", ")) : TEXT("(none)")));
		}
	}
}

} // namespace

// ============================================================================
// Lua Binding
// ============================================================================

static TArray<FLuaFunctionDoc> NiagaraDocs = {};

static void BindNiagara(sol::state& Lua, FLuaSessionData& Session)
{
	Lua.set_function("_enrich_niagara", [&Session](sol::table AssetObj, sol::this_state S)
	{
		sol::state_view LuaView(S);
		std::string PathStr = AssetObj.get<std::string>("path");
		FString FPath = NeoLuaStr::ToFString(PathStr);
		UNiagaraSystem* System = NeoLuaAsset::Resolve<UNiagaraSystem>(FPath);
		if (!System) return;

		AssetObj["_help_text"] =
			"Element types for add/remove/list/configure/get:\n"
			"  module, assignment_module, emitter, stateless_emitter, stateless_module,\n"
			"  spawn_info, renderer, user_parameter, event_handler, simulation_stage\n"
			"\n"
			"add(\"assignment_module\", {emitter, stage, index?=-1, targets=[\n"
			"  {name=\"Particles.Position\", type=\"Vector\", default=\"(X=0,Y=0,Z=0)\"},\n"
			"  {name=\"Particles.Lifetime\", type=\"Float\",  default=\"1.0\"}]})\n"
			"\n"
			"add/remove/list/configure/get(type, id, params)\n"
			"\n"
			"Renderer types: dynamically discovered (use list(\"renderer_types\") to see available)\n"
			"\n"
			"configure(\"module\", \"ModName\", {emitter, stage, parameters={\n"
			"  Speed=1.0, Vel={mode=\"dynamic_input\", script=\"...\"},\n"
			"  Pos={mode=\"linked\", parameter=\"Particles.Position\"},\n"
			"  X={mode=\"hlsl\", code=\"...\"}, M={mode=\"data_interface\", type=\"...\"},\n"
			"  T={mode=\"object_asset\", path=\"/Game/T_Foo\"},  -- textures, meshes, materials\n"
			"  A={mode=\"curve\",          keys={{time=0, value=1, interp=\"linear\"}}},\n"
			"  C={mode=\"color_curve\",    keys={{time=0, color={r=1,g=0,b=0,a=1}}}},\n"
			"  V2={mode=\"vector2d_curve\", keys={{time=0, x=0, y=0}}},\n"
			"  V={mode=\"vector_curve\",    keys={{time=0, x=1, y=0, z=0}}},\n"
			"  V4={mode=\"vector4_curve\", keys={{time=0, x=0, y=0, z=0, w=0}}},\n"
			"  R={mode=\"reset\"}}})\n"
			"configure(\"emitter\"|\"renderer\"|\"system\"|\"simulation_stage\", id, {properties={...}})\n"
			"  — LCD: full struct/array/enum/object-asset-path support via reflection.\n"
			"  Field names resolve through snake_case + b-prefix variants (fixed_bounds →\n"
			"  FixedBounds, local_space → bLocalSpace, determinism → bDeterminism).\n"
			"  Enum values use authored names exactly (SortMode=\"ViewDepth\", SimTarget=\"GPUComputeSim\").\n"
			"\n"
			"  configure(\"emitter\", name, {enabled?=bool, properties={SimTarget=\"GPUComputeSim\",\n"
			"    bLocalSpace=true, CalculateBoundsMode=\"Fixed\", FixedBounds={min={x,y,z}, max={x,y,z}}}})\n"
			"  configure(\"system\", _, {warmup_time?, bake_out_rapid_iteration?, trim_attributes?,\n"
			"    properties={bFixedRandomSeed=true, RandomSeed=42, ...}})\n"
			"  configure_simulation_stage({emitter, index, IterationSource=\"DataInterface\",\n"
			"    DirectDispatchType=\"TwoD\", ElementCountX={DefaultValue={Value=512}}, ...})\n"
			"  configure_event_handler({emitter, index, ExecutionMode=\"SpawnedParticles\",\n"
			"    SpawnNumber=5, SourceEventName=\"Collision\"})\n"
			"\n"
			"get(\"system\"|\"emitter\"|\"renderer\", id)            — full editable-property table\n"
			"get(\"system\", _, \"PropName\")                      — single property (any reflected type)\n"
			"get(\"emitter\", name, \"PropName\")                  — walks FVersionedNiagaraEmitterData, then UObject\n"
			"get(\"renderer\", {emitter, index}, \"PropName\")     — single renderer property\n"
			"get(\"user_parameter\", name)                      — typed value round-trip (Float/Int/Bool/Vec*/Color/Quat/Position/Matrix)\n"
			"get(\"module\", {emitter, stage, module_name}, \"input\")\n"
			"\n"
			"set_user_parameter({name, value}) — LCD: accepts any allowed Niagara type\n"
			"\n"
			"list: emitters, modules, renderers, user_parameters, event_handlers,\n"
			"  simulation_stages, module_inputs, dynamic_inputs, scratch_pad_scripts,\n"
			"  available_modules, emitter_templates, stateless_modules, spawn_infos\n"
			"\n"
			"Action methods:\n"
			"  enable_module / move_module / rename_emitter / reorder_emitter / duplicate_emitter\n"
			"  reorder_renderers / reorder_simulation_stages\n"
			"  set_user_parameter / rename_user_parameter  -- rename propagates through scripts\n"
			"  configure_event_handler / configure_simulation_stage\n"
			"  subscribe/unsubscribe/synchronize_parameter_definitions\n"
			"  list_parameter_definitions / compile / run_validation\n"
			"  create/delete/rename_scratch_pad_script\n"
			"  validate() / info()\n"
			"\n"
			"Stack auto-fix loop:\n"
			"  get_stack_issues() -> {issue_count, error_count, warning_count, fixable_count,\n"
			"    issues=[{id, severity, short_description, long_description, owner_path,\n"
			"             can_be_dismissed, fix_count, fixes=[{id, description, style, bound}]}]}\n"
			"  apply_stack_fix({issue_id, fix_id, dry_run?=false}) -> fires the engine quick-fix.\n"
			"  Loop: while get_stack_issues().error_count > 0 do … apply_stack_fix(...) end\n"
			"\n"
			"configure(\"emitter_mode\", name, {mode=\"stateless\"|\"standard\"})\n"
			"version(\"list\"|\"add\"|\"expose\"|\"delete\", {emitter, major?, minor?, version_guid?})\n";

		// ==================================================================
		// add(type, params)
		// ==================================================================
		AssetObj.set_function("add", [System, &Session](sol::table /*self*/,
			const std::string& Type, sol::optional<sol::table> Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);

			if (!Params.has_value()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> params required"), *FType)); return sol::lua_nil; }
			sol::table P = Params.value();

			// ---- add("module") ----
			// Engine parity: uses AddScriptModuleToStack which handles node creation,
			// script versioning, pin creation, and ConnectStackNodeGroup internally.
			//
			// Targets emitter/system stage by default; pass `scratch_pad="ScriptName"` to
			// insert into a scratch pad script's graph instead (used to build reusable
			// module / dynamic input scripts programmatically).
			if (FType.Equals(TEXT("module"), ESearchCase::IgnoreCase))
			{
				std::string EmitterName = P.get_or<std::string>("emitter", "");
				std::string StageStr = P.get_or<std::string>("stage", "particle_update");
				std::string ScratchPad = P.get_or<std::string>("scratch_pad", "");
				std::string ModulePath = P.get_or<std::string>("module_path", "");
				if (ModulePath.empty()) ModulePath = P.get_or<std::string>("script", "");
				if (ModulePath.empty()) ModulePath = P.get_or<std::string>("path", "");
				int32 TargetIndex = static_cast<int32>(P.get_or("index", -1));
				if (ModulePath.empty()) { Session.Log(TEXT("[FAIL] add(\"module\") -> module_path (or script/path) required")); return sol::lua_nil; }

				UNiagaraNodeOutput* OutputNode = nullptr;
				UNiagaraScript*     ScratchScript = nullptr;
				FString FLogTarget;

				if (!ScratchPad.empty())
				{
					FString FScratchName = NeoLuaStr::ToFString(ScratchPad);
					FScratchPadTarget Target = ResolveScratchPadTarget(System, Session, FScratchName, TEXT("add(\"module\")"));
					if (!Target.OutputNode) return sol::lua_nil;
					OutputNode    = Target.OutputNode;
					ScratchScript = Target.Script;
					FLogTarget    = FString::Printf(TEXT("scratch_pad=\"%s\""), *FScratchName);
				}
				else
				{
					FString FEmitter = NeoLuaStr::ToFString(EmitterName);
					FString FStage = NeoLuaStr::ToFString(StageStr);
					ENiagaraScriptUsage Usage = ParseUsage(FStage);

					int32 EmIdx = INDEX_NONE;
					const bool bSystemLevel = (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript);
					if (!bSystemLevel)
					{
						if (FEmitter.IsEmpty()) { Session.Log(TEXT("[FAIL] add(\"module\") -> emitter required (or pass scratch_pad)")); return sol::lua_nil; }
						EmIdx = FindEmitterIndex(System, FEmitter);
						if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"module\") -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }
					}

					OutputNode = FindOutputNode(System, EmIdx, Usage);
					if (!OutputNode) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"module\") -> no output node for stage '%s'"), *FStage)); return sol::lua_nil; }
					FLogTarget = FString::Printf(TEXT("stage=\"%s\""), *FStage);
				}

				FString FModPath = NeoLuaStr::ToFString(ModulePath);
				UNiagaraScript* ModScript = NeoLuaAsset::Resolve<UNiagaraScript>(FModPath);
				if (!ModScript) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"module\") -> script '%s' not found"), *FModPath)); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNModule", "Add Niagara Module"));
				if (ScratchScript) ScratchScript->Modify();

				// Engine API: handles node creation, versioning, pin fallback, and stack node group wiring
				UNiagaraNodeFunctionCall* NewNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
					ModScript, *OutputNode, TargetIndex);

				if (!NewNode)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"module\") -> AddScriptModuleToStack failed for '%s'"), *FModPath));
					return sol::lua_nil;
				}

				UNiagaraGraph* Graph = Cast<UNiagaraGraph>(OutputNode->GetGraph());
				if (Graph) Graph->NotifyGraphChanged();
				if (ScratchScript) ScratchScript->RequestCompile(FGuid());
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"module\", path=\"%s\", %s)"), *FModPath, *FLogTarget));
				return sol::make_object(Lua, true);
			}
			// ---- add("emitter") ----
			else if (FType.Equals(TEXT("emitter"), ESearchCase::IgnoreCase))
			{
				std::string Name = P.get_or<std::string>("name", "");
				if (Name.empty()) { Session.Log(TEXT("[FAIL] add(\"emitter\") -> name required")); return sol::lua_nil; }
				std::string TemplateStr = P.get_or<std::string>("template_asset", "");

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNEmitter", "Add Niagara Emitter"));
				System->Modify();

				FString FName = NeoLuaStr::ToFString(Name);
				if (!TemplateStr.empty())
				{
					FString FTemplate = NeoLuaStr::ToFString(TemplateStr);
					UNiagaraEmitter* TemplateEmitter = NeoLuaAsset::Resolve<UNiagaraEmitter>(FTemplate);
					if (!TemplateEmitter) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"emitter\") -> template '%s' not found"), *FTemplate)); return sol::lua_nil; }
					FGuid NewGuid = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *TemplateEmitter, TemplateEmitter->GetExposedVersion().VersionGuid, true);
					if (NewGuid.IsValid())
					{
						// Apply the user-specified name to the newly added emitter
						for (int32 i = 0; i < System->GetNumEmitters(); ++i)
						{
							if (System->GetEmitterHandle(i).GetId() == NewGuid)
							{
								System->GetEmitterHandle(i).SetName(::FName(*FName), *System);
								break;
							}
						}
						System->MarkPackageDirty();
						Session.Log(FString::Printf(TEXT("[OK] add(\"emitter\", name=\"%s\", template=\"%s\")"), *FName, *FTemplate));
						return sol::make_object(Lua, true);
					}
				}
				else
				{
					Session.Log(TEXT("[FAIL] add(\"emitter\") -> template_asset required (cannot create empty emitter)"));
					return sol::lua_nil;
				}
				Session.Log(FString::Printf(TEXT("[FAIL] add(\"emitter\") -> template '%s' resolved but AddEmitterToSystem returned an invalid GUID for '%s'. Verify the template has a valid exposed version and that the source asset loads via find_assets."), *NeoLuaStr::ToFString(TemplateStr), *FName));
				return sol::lua_nil;
			}
			// ---- add("renderer") ----
			// Engine splits renderer ownership by emitter mode: stateful emitters use
			// UNiagaraEmitter::AddRenderer, stateless emitters use
			// UNiagaraStatelessEmitter::AddRenderer.
			else if (FType.Equals(TEXT("renderer"), ESearchCase::IgnoreCase))
			{
				std::string EmitterName = P.get_or<std::string>("emitter", "");
				std::string RType = P.get_or<std::string>("type", "sprite");
				if (EmitterName.empty()) { Session.Log(TEXT("[FAIL] add(\"renderer\") -> emitter required")); return sol::lua_nil; }

				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"renderer\") -> emitter '%s' not found. Use list(\"emitters\") to see available."), *FEmitter)); return sol::lua_nil; }

				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				FString FRType = NeoLuaStr::ToFString(RType);

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNRenderer", "Add Niagara Renderer"));
				System->Modify();

				UNiagaraRendererProperties* Renderer = nullptr;
				UObject* StatelessOwner = nullptr;  // set only on the stateless path; drives PostEditChange
				const bool bStateless = (Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless);

				if (bStateless)
				{
					UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
					if (!SLE) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"renderer\") -> stateless emitter '%s' has no stateless instance"), *FEmitter)); return sol::lua_nil; }

					UClass* Cls = LuaDynamicType::FindDerivedClass(UNiagaraRendererProperties::StaticClass(), FRType, RendererPrefixes, RendererSuffixes);
					if (!Cls) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"renderer\") -> unknown type '%s'. Valid: %s"), *FRType, *LuaDynamicType::FormatAvailableTypes(UNiagaraRendererProperties::StaticClass(), RendererPrefixes, RendererSuffixes))); return sol::lua_nil; }

#if NSAI_NIAGARA_HAS_STATELESS_INTERNAL
					Renderer = NewObject<UNiagaraRendererProperties>((UObject*)SLE, Cls, NAME_None, RF_Transactional);
					if (!Renderer) { Session.Log(TEXT("[FAIL] add(\"renderer\") -> NewObject failed for stateless emitter")); return sol::lua_nil; }
					SLE->AddRenderer(Renderer, Handle.GetInstance().Version);
#else
					FArrayProperty* ArrProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("RendererProperties")));
					if (!ArrProp) { Session.Log(TEXT("[FAIL] add(\"renderer\") -> RendererProperties UPROPERTY not found on stateless emitter")); return sol::lua_nil; }
					FObjectPropertyBase* InnerProp = CastField<FObjectPropertyBase>(ArrProp->Inner);
					if (!InnerProp) { Session.Log(TEXT("[FAIL] add(\"renderer\") -> RendererProperties inner is not an object")); return sol::lua_nil; }

					((UObject*)SLE)->Modify();
					Renderer = NewObject<UNiagaraRendererProperties>((UObject*)SLE, Cls, NAME_None, RF_Transactional);
					if (!Renderer) { Session.Log(TEXT("[FAIL] add(\"renderer\") -> NewObject failed for stateless emitter")); return sol::lua_nil; }
					Renderer->OuterEmitterVersion = Handle.GetInstance().Version;

					FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
					const int32 NewIdx = Helper.AddValue();
					InnerProp->SetObjectPropertyValue(Helper.GetRawPtr(NewIdx), Renderer);
#endif
					StatelessOwner = (UObject*)SLE;
				}
				else
				{
					FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData();
					if (!ED) { Session.Log(TEXT("[FAIL] add(\"renderer\") -> no emitter data")); return sol::lua_nil; }

					FVersionedNiagaraEmitter Instance = Handle.GetInstance();
					UNiagaraEmitter* Emitter = Instance.Emitter;
					if (!Emitter) { Session.Log(TEXT("[FAIL] add(\"renderer\") -> no emitter object")); return sol::lua_nil; }

					Renderer = CreateRendererByType(Emitter, FRType);
					if (!Renderer) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"renderer\") -> unknown type '%s'. Valid: %s"), *FRType, *LuaDynamicType::FormatAvailableTypes(UNiagaraRendererProperties::StaticClass(), RendererPrefixes, RendererSuffixes))); return sol::lua_nil; }

					Emitter->AddRenderer(Renderer, Instance.Version);
				}

				// Apply properties via the plugin-wide LCD (handles structs, arrays, enums,
				// object asset paths — fixes silent material-assign that the old flat setter dropped).
				sol::optional<sol::table> PropsOpt = P.get<sol::optional<sol::table>>("properties");
				if (PropsOpt.has_value())
				{
					FString ApplyErr;
					TArray<FString> Warnings;
					NeoLuaProperty::ApplyTable(Renderer, PropsOpt.value(), ApplyErr, &Warnings);
					for (const FString& W : Warnings)
						Session.Log(FString::Printf(TEXT("[WARN] add(\"renderer\") %s"), *W));
				}

				// Flush attribute-binding caches (FNiagaraVariableAttributeBinding /
				// FNiagaraMaterialAttributeBinding). The LCD writes RootName into each
				// binding struct but can't reach the emitter context needed to resolve
				// the namespace and cache derived fields.
				NSAINiagaraLCD::RefreshRendererBindings(Renderer);

				if (StatelessOwner) StatelessOwner->PostEditChange();
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"renderer\", emitter=\"%s\", type=\"%s\"%s)"), *FEmitter, *FRType, bStateless ? TEXT(", mode=stateless") : TEXT("")));
				return sol::make_object(Lua, true);
			}
			// ---- add("user_parameter") ----
			// LCD: type resolution via NiagaraTypeRegistry-backed helper;
			// default value applied via reflection walker (covers every allowed type incl.
			// Position, Matrix, NiagaraID, UObject-bindings).
			else if (FType.Equals(TEXT("user_parameter"), ESearchCase::IgnoreCase))
			{
				std::string ParamName = P.get_or<std::string>("name", "");
				std::string ParamType = P.get_or<std::string>("type", "Float");
				if (ParamName.empty()) { Session.Log(TEXT("[FAIL] add(\"user_parameter\") -> name required")); return sol::lua_nil; }

				FString FParamName = NeoLuaStr::ToFString(ParamName);
				FString FParamType = NeoLuaStr::ToFString(ParamType);

				FNiagaraTypeDefinition TypeDef = NSAINiagaraLCD::ResolveTypeDefinition(FParamType);
				if (!TypeDef.IsValid())
				{
					const TArray<FString> Allowed = NSAINiagaraLCD::GetAllowedUserParameterTypeNames();
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"user_parameter\") -> unknown type '%s'. Allowed: %s"),
						*FParamType, *FString::Join(Allowed, TEXT(", "))));
					return sol::lua_nil;
				}

				FNiagaraVariable NewVar(TypeDef, FName(*(TEXT("User.") + FParamName)));

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNUserParam", "Add Niagara User Parameter"));
				System->Modify();

				FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
				if (Store.FindParameterOffset(NewVar) != nullptr)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"user_parameter\") -> '%s' already exists"), *FParamName));
					return sol::lua_nil;
				}

				Store.AddParameter(NewVar, /*bInitialize=*/true);

				// Default value: accept both "default" and "default_value" keys.
				sol::object DefObj = P["default_value"];
				if (!DefObj.valid() || DefObj.is<sol::lua_nil_t>()) DefObj = P["default"];
				if (DefObj.valid() && !DefObj.is<sol::lua_nil_t>())
				{
					TArray<uint8> Bytes;
					FString SerErr;
					if (NSAINiagaraLCD::SerializeSolValueToTypedBytes(DefObj, TypeDef, Bytes, SerErr))
					{
						Store.SetParameterData(Bytes.GetData(), NewVar, /*bAdd=*/false);
					}
					else
					{
						Session.Log(FString::Printf(TEXT("[WARN] add(\"user_parameter\") default: %s"), *SerErr));
					}
				}

				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"user_parameter\", name=\"%s\", type=\"%s\")"), *FParamName, *FParamType));
				return sol::make_object(Lua, true);
			}
			// ---- add("event_handler") ----
			else if (FType.Equals(TEXT("event_handler"), ESearchCase::IgnoreCase))
			{
				std::string EmitterName = P.get_or<std::string>("emitter", "");
				std::string SourceEvent = P.get_or<std::string>("source_event_name", "");
				if (EmitterName.empty() || SourceEvent.empty()) { Session.Log(TEXT("[FAIL] add(\"event_handler\") -> emitter and source_event_name required")); return sol::lua_nil; }

				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"event_handler\") -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }

				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData();
				UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
				if (!ED || !Emitter) { Session.Log(TEXT("[FAIL] add(\"event_handler\") -> no emitter data")); return sol::lua_nil; }

				UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(ED->GraphSource);
				if (!Source) { Session.Log(TEXT("[FAIL] add(\"event_handler\") -> no graph source")); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNEventHandler", "Add Niagara Event Handler"));
				System->Modify();
				Emitter->Modify();

				FNiagaraEventScriptProperties EventProps;
				EventProps.SourceEventName = FName(UTF8_TO_TCHAR(SourceEvent.c_str()));
				std::string SourceEmitter = P.get_or<std::string>("source_emitter", "");
				if (!SourceEmitter.empty())
				{
					int32 SrcIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(SourceEmitter));
					if (SrcIdx != INDEX_NONE)
						EventProps.SourceEmitterID = System->GetEmitterHandle(SrcIdx).GetId();
				}

				EventProps.Script = NewObject<UNiagaraScript>(Emitter, NAME_None, RF_Transactional);
				EventProps.Script->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
				FGuid EventUsageId = FGuid::NewGuid();
				EventProps.Script->SetUsageId(EventUsageId);
				EventProps.Script->SetLatestSource(Source);

				// Create output node for the event handler so it has a module stack
				UNiagaraGraph* EvtGraph = Source->NodeGraph;
				if (EvtGraph)
				{
					FGraphNodeCreator<UNiagaraNodeOutput> EvtOutputCreator(*EvtGraph);
					UNiagaraNodeOutput* EvtOutput = EvtOutputCreator.CreateNode();
					EvtOutput->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
					EvtOutput->SetUsageId(EventUsageId);
					EvtOutput->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Output")));
					EvtOutputCreator.Finalize();

					// Create and connect an input node
					FGraphNodeCreator<UNiagaraNodeInput> EvtInputCreator(*EvtGraph);
					UNiagaraNodeInput* EvtInput = EvtInputCreator.CreateNode();
					EvtInput->Usage = ENiagaraInputNodeUsage::Parameter;
					EvtInput->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn"));
					EvtInputCreator.Finalize();

					const UEdGraphSchema_Niagara* EvtSchema = Cast<UEdGraphSchema_Niagara>(EvtGraph->GetSchema());
					if (EvtSchema) EvtSchema->TryCreateConnection(EvtInput->GetOutputPin(0), EvtOutput->GetInputPin(0));
				}

				Emitter->AddEventHandler(EventProps, Handle.GetInstance().Version);
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"event_handler\", emitter=\"%s\", event=\"%s\")"), *FEmitter, UTF8_TO_TCHAR(SourceEvent.c_str())));
				return sol::make_object(Lua, true);
			}
			// ---- add("simulation_stage") ----
			else if (FType.Equals(TEXT("simulation_stage"), ESearchCase::IgnoreCase))
			{
				std::string EmitterName = P.get_or<std::string>("emitter", "");
				if (EmitterName.empty()) { Session.Log(TEXT("[FAIL] add(\"simulation_stage\") -> emitter required")); return sol::lua_nil; }

				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"simulation_stage\") -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }

				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData();
				UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
				if (!ED || !Emitter) { Session.Log(TEXT("[FAIL] add(\"simulation_stage\") -> no emitter data")); return sol::lua_nil; }

				UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(ED->GraphSource);
				if (!Source) { Session.Log(TEXT("[FAIL] add(\"simulation_stage\") -> no graph source")); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNSimStage", "Add Niagara Simulation Stage"));
				System->Modify();
				Emitter->Modify();

				UNiagaraSimulationStageGeneric* NewStage = NewObject<UNiagaraSimulationStageGeneric>(Emitter, UNiagaraSimulationStageGeneric::StaticClass(), NAME_None, RF_Transactional);
				if (NewStage)
				{
					std::string StageName = P.get_or<std::string>("name", "");
					if (!StageName.empty())
					{
						FName DesiredName = FName(UTF8_TO_TCHAR(StageName.c_str()));
						// Check for duplicate simulation stage names
						const TArray<UNiagaraSimulationStageBase*>& ExistingStages = ED->GetSimulationStages();
						for (const UNiagaraSimulationStageBase* ExStage : ExistingStages)
						{
							if (ExStage && ExStage->SimulationStageName == DesiredName)
							{
								Session.Log(FString::Printf(TEXT("[WARN] add(\"simulation_stage\") -> stage name '%s' already exists on emitter '%s'"), *DesiredName.ToString(), *FEmitter));
								break;
							}
						}
						NewStage->SimulationStageName = DesiredName;
					}

					NewStage->Script = NewObject<UNiagaraScript>(NewStage, NAME_None, RF_Transactional);
					NewStage->Script->SetUsage(ENiagaraScriptUsage::ParticleSimulationStageScript);
					FGuid SimStageUsageId = FGuid::NewGuid();
					NewStage->Script->SetUsageId(SimStageUsageId);
					NewStage->Script->SetLatestSource(Source);

					// Create output node for the simulation stage so it has a module stack
					UNiagaraGraph* SimGraph = Source->NodeGraph;
					if (SimGraph)
					{
						FGraphNodeCreator<UNiagaraNodeOutput> SimOutputCreator(*SimGraph);
						UNiagaraNodeOutput* SimOutput = SimOutputCreator.CreateNode();
						SimOutput->SetUsage(ENiagaraScriptUsage::ParticleSimulationStageScript);
						SimOutput->SetUsageId(SimStageUsageId);
						SimOutput->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Output")));
						SimOutputCreator.Finalize();

						FGraphNodeCreator<UNiagaraNodeInput> SimInputCreator(*SimGraph);
						UNiagaraNodeInput* SimInput = SimInputCreator.CreateNode();
						SimInput->Usage = ENiagaraInputNodeUsage::Parameter;
						SimInput->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn"));
						SimInputCreator.Finalize();

						const UEdGraphSchema_Niagara* SimSchema = Cast<UEdGraphSchema_Niagara>(SimGraph->GetSchema());
						if (SimSchema) SimSchema->TryCreateConnection(SimInput->GetOutputPin(0), SimOutput->GetInputPin(0));
					}

						Emitter->AddSimulationStage(NewStage, Handle.GetInstance().Version);
						Emitter->PostEditChange();
					System->MarkPackageDirty();
					Session.Log(FString::Printf(TEXT("[OK] add(\"simulation_stage\", emitter=\"%s\")"), *FEmitter));
					return sol::make_object(Lua, true);
				}
				Session.Log(FString::Printf(TEXT("[FAIL] add(\"simulation_stage\") -> NewObject<UNiagaraSimulationStageGeneric> failed for emitter '%s'. Verify the emitter has a valid graph source and retry with a unique {name=\"...\"}."), *FEmitter));
				return sol::lua_nil;
			}

			// ---- add("stateless_emitter") ----
			else if (FType.Equals(TEXT("stateless_emitter"), ESearchCase::IgnoreCase))
			{
				std::string Name = P.get_or<std::string>("name", "");
				if (Name.empty()) { Session.Log(TEXT("[FAIL] add(\"stateless_emitter\") -> name required")); return sol::lua_nil; }
				std::string TemplateStr = P.get_or<std::string>("template_asset", "");

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNStatelessEmitter", "Add Stateless Niagara Emitter"));
				System->Modify();

				FString FName = NeoLuaStr::ToFString(Name);

				if (!TemplateStr.empty())
				{
					FString FTemplate = NeoLuaStr::ToFString(TemplateStr);
					UNiagaraEmitter* TemplateEmitter = NeoLuaAsset::Resolve<UNiagaraEmitter>(FTemplate);
					if (!TemplateEmitter) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"stateless_emitter\") -> template '%s' not found"), *FTemplate)); return sol::lua_nil; }
					FGuid NewGuid = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *TemplateEmitter, TemplateEmitter->GetExposedVersion().VersionGuid, true);
					if (NewGuid.IsValid())
					{
						for (int32 i = 0; i < System->GetNumEmitters(); ++i)
						{
							FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(i);
							if (Handle.GetId() == NewGuid)
							{
								Handle.SetName(::FName(*FName), *System);
								Handle.SetEmitterMode(*System, ENiagaraEmitterMode::Stateless);
								break;
							}
						}
						System->MarkPackageDirty();
						Session.Log(FString::Printf(TEXT("[OK] add(\"stateless_emitter\", name=\"%s\", template=\"%s\")"), *FName, *FTemplate));
						return sol::make_object(Lua, true);
					}
				}
				else
				{
					Session.Log(TEXT("[FAIL] add(\"stateless_emitter\") -> template_asset required"));
					return sol::lua_nil;
				}
				Session.Log(FString::Printf(TEXT("[FAIL] add(\"stateless_emitter\") -> failed to add '%s'"), *FName));
				return sol::lua_nil;
			}

			// ---- add("stateless_module") ----
			// Add a UNiagaraStatelessModule subclass (InitializeParticle, GravityForce, Drag,
			// ShapeLocation, SubUVAnimation, ... 26 total) to a stateless emitter's Modules array.
			// Class lookup is prefix-stripped (NiagaraStatelessModule_InitializeParticle → "InitializeParticle").
			// The emitter's Modules UPROPERTY is in Niagara/Internal/, so we work through reflection.
			else if (FType.Equals(TEXT("stateless_module"), ESearchCase::IgnoreCase))
			{
				std::string EmitterName = P.get_or<std::string>("emitter", "");
				std::string ClassName = P.get_or<std::string>("class", "");
				if (EmitterName.empty() || ClassName.empty())
				{
					Session.Log(TEXT("[FAIL] add(\"stateless_module\") -> emitter and class required"));
					return sol::lua_nil;
				}

				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"stateless_module\") -> emitter '%s' not found"), *FEmitter));
					return sol::lua_nil;
				}

				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				if (Handle.GetEmitterMode() != ENiagaraEmitterMode::Stateless)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"stateless_module\") -> emitter '%s' is not in stateless mode; run configure(\"emitter_mode\", \"%s\", {mode=\"stateless\"}) first"), *FEmitter, *FEmitter));
					return sol::lua_nil;
				}

				UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
				if (!SLE) { Session.Log(TEXT("[FAIL] add(\"stateless_module\") -> no stateless emitter instance")); return sol::lua_nil; }

				FArrayProperty* ModProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("Modules")));
				if (!ModProp) { Session.Log(TEXT("[FAIL] add(\"stateless_module\") -> Modules UPROPERTY not found")); return sol::lua_nil; }
				FObjectPropertyBase* InnerProp = CastField<FObjectPropertyBase>(ModProp->Inner);
				if (!InnerProp) { Session.Log(TEXT("[FAIL] add(\"stateless_module\") -> Modules inner is not an object")); return sol::lua_nil; }
				UClass* BaseClass = InnerProp->PropertyClass;

				static const TArray<FString> StatelessModulePrefixes = { TEXT("NiagaraStatelessModule_") };
				FString FClassName = NeoLuaStr::ToFString(ClassName);
				UClass* ModClass = LuaDynamicType::FindDerivedClass(BaseClass, FClassName, StatelessModulePrefixes, {});
				if (!ModClass)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"stateless_module\") -> unknown class '%s'. Valid: %s"),
						*FClassName, *LuaDynamicType::FormatAvailableTypes(BaseClass, StatelessModulePrefixes, {})));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNStatelessMod", "Add Stateless Module"));
				System->Modify();
				((UObject*)SLE)->Modify();

				UObject* NewMod = NewObject<UObject>((UObject*)SLE, ModClass, NAME_None, RF_Transactional);
				if (!NewMod) { Session.Log(TEXT("[FAIL] add(\"stateless_module\") -> NewObject failed")); return sol::lua_nil; }

				// Apply properties via the LCD walker (any UPROPERTY on the module class)
				sol::optional<sol::table> PropsOpt = P.get<sol::optional<sol::table>>("properties");
				if (PropsOpt.has_value())
				{
					FString ApplyErr;
					TArray<FString> Warnings;
					NeoLuaProperty::ApplyTable(NewMod, PropsOpt.value(), ApplyErr, &Warnings);
					for (const FString& W : Warnings)
						Session.Log(FString::Printf(TEXT("[WARN] add(\"stateless_module\") %s"), *W));
				}

				FScriptArrayHelper Helper(ModProp, ModProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
				const int32 NewIdx = Helper.AddValue();
				InnerProp->SetObjectPropertyValue(Helper.GetRawPtr(NewIdx), NewMod);

				((UObject*)SLE)->PostEditChange();
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"stateless_module\", emitter=\"%s\", class=\"%s\") -> index %d"),
					*FEmitter, *FClassName, NewIdx));
				return sol::make_object(Lua, true);
			}

			// ---- add("assignment_module") ----
			// The "Set new or existing parameter directly" module from the stack UI —
			// creates a UNiagaraNodeAssignment that writes a list of parameters.
			// Uses engine-exported FNiagaraStackGraphUtilities::AddParameterModuleToStack.
			//
			// Targets are {name="Particles.Foo", type="Float"|"Vector"|..., default="T3D string"}.
			// Default is a T3D-format string (e.g. "1.0" for float, "(X=1,Y=2,Z=3)" for vector) —
			// matching what the engine's stack UI produces for user-entered defaults. Empty
			// string = use the type's default.
			else if (FType.Equals(TEXT("assignment_module"), ESearchCase::IgnoreCase))
			{
				std::string EmitterName = P.get_or<std::string>("emitter", "");
				std::string StageStr = P.get_or<std::string>("stage", "particle_update");
				std::string ScratchPad = P.get_or<std::string>("scratch_pad", "");
				int32 TargetIndex = static_cast<int32>(P.get_or("index", -1));
				sol::optional<sol::table> TargetsOpt = P.get<sol::optional<sol::table>>("targets");
				if (!TargetsOpt.has_value())
				{
					Session.Log(TEXT("[FAIL] add(\"assignment_module\") -> targets=[{name, type, default?}] required"));
					return sol::lua_nil;
				}

				UNiagaraNodeOutput* OutputNode = nullptr;
				UNiagaraScript*     ScratchScript = nullptr;
				ENiagaraScriptUsage Usage = ENiagaraScriptUsage::Module;

				if (!ScratchPad.empty())
				{
					FScratchPadTarget Target = ResolveScratchPadTarget(System, Session,
						NeoLuaStr::ToFString(ScratchPad), TEXT("add(\"assignment_module\")"));
					if (!Target.OutputNode) return sol::lua_nil;
					OutputNode    = Target.OutputNode;
					ScratchScript = Target.Script;
				}
				else
				{
					Usage = ParseUsage(NeoLuaStr::ToFString(StageStr));
					int32 EmIdx = INDEX_NONE;
					const bool bSystemLevel = (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript);
					if (!bSystemLevel)
					{
						if (EmitterName.empty())
						{
							Session.Log(TEXT("[FAIL] add(\"assignment_module\") -> emitter required (or pass scratch_pad)"));
							return sol::lua_nil;
						}
						EmIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(EmitterName));
						if (EmIdx == INDEX_NONE)
						{
							Session.Log(FString::Printf(TEXT("[FAIL] add(\"assignment_module\") -> emitter '%s' not found"), UTF8_TO_TCHAR(EmitterName.c_str())));
							return sol::lua_nil;
						}
					}

					OutputNode = FindOutputNode(System, EmIdx, Usage);
					if (!OutputNode)
					{
						Session.Log(FString::Printf(TEXT("[FAIL] add(\"assignment_module\") -> no output node for stage '%s'"), UTF8_TO_TCHAR(StageStr.c_str())));
						return sol::lua_nil;
					}
				}

				TArray<FNiagaraVariable> Vars;
				TArray<FString> Defaults;
				sol::table Targets = TargetsOpt.value();
				for (auto& kv : Targets)
				{
					if (!kv.second.is<sol::table>()) continue;
					sol::table Target = kv.second.as<sol::table>();
					FString TargetName = NeoLuaStr::ToFString(Target.get_or<std::string>("name", ""));
					FString TypeName = NeoLuaStr::ToFString(Target.get_or<std::string>("type", ""));
					FString DefaultStr = NeoLuaStr::ToFString(Target.get_or<std::string>("default", ""));

					if (TargetName.IsEmpty() || TypeName.IsEmpty())
					{
						Session.Log(TEXT("[WARN] assignment target missing name or type; skipping"));
						continue;
					}

					FNiagaraTypeDefinition TypeDef = NSAINiagaraLCD::ResolveTypeDefinition(TypeName);
					if (!TypeDef.IsValid())
					{
						Session.Log(FString::Printf(TEXT("[WARN] assignment target '%s' has unknown type '%s'; skipping"), *TargetName, *TypeName));
						continue;
					}

					Vars.Add(FNiagaraVariable(TypeDef, FName(*TargetName)));
					Defaults.Add(DefaultStr);
				}

				if (Vars.Num() == 0)
				{
					Session.Log(TEXT("[FAIL] add(\"assignment_module\") -> no valid targets"));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNAssignMod", "Add Assignment Module"));
				if (ScratchScript) ScratchScript->Modify();
				UNiagaraNodeAssignment* Node = FNiagaraStackGraphUtilities::AddParameterModuleToStack(
					Vars, *OutputNode, TargetIndex, Defaults);
				if (!Node)
				{
					Session.Log(TEXT("[FAIL] add(\"assignment_module\") -> AddParameterModuleToStack failed"));
					return sol::lua_nil;
				}

				UNiagaraGraph* Graph = Cast<UNiagaraGraph>(OutputNode->GetGraph());
				if (Graph) Graph->NotifyGraphChanged();
				if (ScratchScript) ScratchScript->RequestCompile(FGuid());
				System->MarkPackageDirty();
				const FString LogTarget = ScratchScript
					? FString::Printf(TEXT("scratch_pad=\"%s\""), *ScratchScript->GetName())
					: FString::Printf(TEXT("stage=\"%s\""), *UsageToStr(Usage));
				Session.Log(FString::Printf(TEXT("[OK] add(\"assignment_module\", %s, targets=%d)"),
					*LogTarget, Vars.Num()));
				return sol::make_object(Lua, true);
			}

			// ---- add("spawn_info") ----
			// Append a new FNiagaraStatelessSpawnInfo to a stateless emitter's SpawnInfos array.
			// Type = "Burst" (default) or "Rate". SourceId auto-generated. Properties flow
			// through ApplyTableToStruct so any reflected field is reachable (Type, SpawnTime,
			// Amount={Min,Max}, Rate={Min,Max}, SpawnProbability, LoopCountLimit, etc.).
			else if (FType.Equals(TEXT("spawn_info"), ESearchCase::IgnoreCase))
			{
				std::string EmitterName = P.get_or<std::string>("emitter", "");
				if (EmitterName.empty())
				{
					Session.Log(TEXT("[FAIL] add(\"spawn_info\") -> emitter required"));
					return sol::lua_nil;
				}

				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"spawn_info\") -> emitter '%s' not found"), *FEmitter));
					return sol::lua_nil;
				}

				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				if (Handle.GetEmitterMode() != ENiagaraEmitterMode::Stateless)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"spawn_info\") -> emitter '%s' is not in stateless mode"), *FEmitter));
					return sol::lua_nil;
				}

				UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
				if (!SLE) { Session.Log(TEXT("[FAIL] add(\"spawn_info\") -> no stateless emitter")); return sol::lua_nil; }

				FArrayProperty* ArrProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("SpawnInfos")));
				if (!ArrProp) { Session.Log(TEXT("[FAIL] add(\"spawn_info\") -> SpawnInfos UPROPERTY not found")); return sol::lua_nil; }
				FStructProperty* InnerProp = CastField<FStructProperty>(ArrProp->Inner);
				if (!InnerProp) { Session.Log(TEXT("[FAIL] add(\"spawn_info\") -> inner is not a struct")); return sol::lua_nil; }
				UScriptStruct* SpawnInfoStruct = InnerProp->Struct;

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNSpawnInfo", "Add Stateless Spawn Info"));
				System->Modify();
				((UObject*)SLE)->Modify();

				FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
				const int32 NewIdx = Helper.AddValue();
				void* ElemPtr = Helper.GetRawPtr(NewIdx);

				// Assign a fresh SourceId guid (editor convention for spawn-info identity)
				if (FProperty* SourceIdProp = NeoLuaProperty::FindPropertyByName(SpawnInfoStruct, TEXT("SourceId")))
				{
					if (FStructProperty* GuidProp = CastField<FStructProperty>(SourceIdProp))
					{
						const FGuid NewGuid = FGuid::NewGuid();
						FGuid* GuidMem = GuidProp->ContainerPtrToValuePtr<FGuid>(ElemPtr);
						if (GuidMem) *GuidMem = NewGuid;
					}
				}

				// Apply user properties
				sol::optional<sol::table> PropsOpt = P.get<sol::optional<sol::table>>("properties");
				if (PropsOpt.has_value())
				{
					FString ApplyErr;
					TArray<FString> Warnings;
					NeoLuaProperty::ApplyTableToStruct(SpawnInfoStruct, ElemPtr, (UObject*)SLE,
						PropsOpt.value(), ApplyErr, &Warnings);
					for (const FString& W : Warnings)
						Session.Log(FString::Printf(TEXT("[WARN] add(\"spawn_info\") %s"), *W));
				}

				((UObject*)SLE)->PostEditChange();
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"spawn_info\", emitter=\"%s\") -> index %d"), *FEmitter, NewIdx));
				return sol::make_object(Lua, true);
			}

			Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> unknown type. Valid: module, assignment_module, emitter, stateless_emitter, stateless_module, spawn_info, renderer, user_parameter, event_handler, simulation_stage"), *FType));
			return sol::lua_nil;
		});

		// ==================================================================
		// remove(type, id)
		// ==================================================================
		AssetObj.set_function("remove", [System, &Session](sol::table /*self*/,
			const std::string& Type, sol::object Id, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);

			// ---- remove("module") ----
			if (FType.Equals(TEXT("module"), ESearchCase::IgnoreCase))
			{
				if (!Id.is<sol::table>()) { Session.Log(TEXT("[FAIL] remove(\"module\") -> {emitter, stage, module_name} or {scratch_pad, module_name} required")); return sol::lua_nil; }
				sol::table T = Id.as<sol::table>();
				std::string EmitterName = T.get_or<std::string>("emitter", "");
				std::string StageStr = T.get_or<std::string>("stage", "particle_update");
				std::string ScratchPad = T.get_or<std::string>("scratch_pad", "");
				std::string ModName = T.get_or<std::string>("module_name", "");
				WarnUnconsumedKeys(Session, T, { TEXT("emitter"), TEXT("stage"), TEXT("scratch_pad"), TEXT("module_name") }, TEXT("remove(\"module\")"));
				if (ModName.empty()) { Session.Log(TEXT("[FAIL] remove(\"module\") -> module_name required")); return sol::lua_nil; }

				UNiagaraNodeOutput* OutputNode = nullptr;
				UNiagaraScript*     ScratchScript = nullptr;
				int32 EmIdx = INDEX_NONE;

				if (!ScratchPad.empty())
				{
					FScratchPadTarget Target = ResolveScratchPadTarget(System, Session,
						NeoLuaStr::ToFString(ScratchPad), TEXT("remove(\"module\")"));
					if (!Target.OutputNode) return sol::lua_nil;
					OutputNode    = Target.OutputNode;
					ScratchScript = Target.Script;
				}
				else
				{
					FString FEmitter = NeoLuaStr::ToFString(EmitterName);
					ENiagaraScriptUsage Usage = ParseUsage(NeoLuaStr::ToFString(StageStr));
					const bool bSystemLevel = (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript);
					if (!bSystemLevel)
					{
						EmIdx = FindEmitterIndex(System, FEmitter);
						if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"module\") -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }
					}

					OutputNode = FindOutputNode(System, EmIdx, Usage);
					if (!OutputNode) { Session.Log(TEXT("[FAIL] remove(\"module\") -> no output node")); return sol::lua_nil; }
				}

				FString FModName = NeoLuaStr::ToFString(ModName);
				UNiagaraNodeFunctionCall* ModNode = FindModuleByName(OutputNode, FModName);
				if (!ModNode)
				{
					TArray<FString> Available = ListModulesInStack(OutputNode);
					Session.Log(FString::Printf(TEXT("[FAIL] remove(\"module\") -> '%s' not found. Available: %s"), *FModName, *FString::Join(Available, TEXT(", "))));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNModule", "Remove Niagara Module"));
				UEdGraph* Graph = ModNode->GetGraph();
				if (Graph) Graph->Modify();

				// Clean up RI params for this module before removing it. For scratch pad
				// scripts, RI params are owned by the scratch script itself — node removal
				// plus RequestCompile at the end lets CleanUpStaleRapidIterationParameters
				// sweep them, so we skip the manual aliased-param walk (which assumes an
				// emitter-alias scheme that doesn't apply here).
				if (!ScratchScript)
				{
					ENiagaraScriptUsage Usage = ParseUsage(NeoLuaStr::ToFString(StageStr));
					UNiagaraScript* OwningScript = nullptr;
					const FNiagaraEmitterHandle* EmHandlePtr = (EmIdx != INDEX_NONE) ? &System->GetEmitterHandle(EmIdx) : nullptr;
					if (EmIdx != INDEX_NONE)
					{
						FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
						if (ED)
						{
							TArray<UNiagaraScript*> Scripts;
							ED->GetScripts(Scripts, false);
							for (UNiagaraScript* Sc : Scripts) { if (Sc && Sc->GetUsage() == Usage) { OwningScript = Sc; break; } }
						}
					}
					else
					{
						OwningScript = (Usage == ENiagaraScriptUsage::SystemSpawnScript) ? System->GetSystemSpawnScript() : System->GetSystemUpdateScript();
					}

					if (OwningScript)
					{
						OwningScript->Modify();
						FNiagaraEmitterHandle DummyHandle;
						const FNiagaraEmitterHandle& RIHandle = EmHandlePtr ? *EmHandlePtr : DummyHandle;
						TArray<FNiagaraVariable> ModInputs = DiscoverModuleInputs(ModNode, OwningScript, EmHandlePtr, Usage);
						for (const FNiagaraVariable& Input : ModInputs)
						{
							FNiagaraParameterHandle InputHandle(Input.GetName());
							FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModNode);
							FNiagaraVariable AliasedVar(Input.GetType(), AliasedHandle.GetParameterHandleString());
							RemoveRapidIterationParam(System, RIHandle, OwningScript, AliasedVar);
						}
					}
				}

				// Break param map links and reconnect chain
				UEdGraphPin* ModMapIn = GetParamMapInputPin(ModNode);
				UEdGraphPin* ModMapOut = GetParamMapOutputPin(ModNode);

				UEdGraphPin* UpstreamOut = nullptr;
				TArray<UEdGraphPin*> DownstreamIns;

				if (ModMapIn && ModMapIn->LinkedTo.Num() > 0)
					UpstreamOut = ModMapIn->LinkedTo[0];
				if (ModMapOut)
					DownstreamIns = ModMapOut->LinkedTo;

				// Break param map links (notify linked nodes)
				if (ModMapIn) ModMapIn->BreakAllPinLinks(true);
				if (ModMapOut) ModMapOut->BreakAllPinLinks(true);

				// Reconnect chain
				if (UpstreamOut)
				{
					for (UEdGraphPin* DownIn : DownstreamIns)
					{
						if (DownIn)
						{
							UpstreamOut->MakeLinkTo(DownIn);
							if (UEdGraphNode* UpOwner2 = UpstreamOut->GetOwningNode())
								UpOwner2->PinConnectionListChanged(UpstreamOut);
							if (UEdGraphNode* DownOwner2 = DownIn->GetOwningNode())
								DownOwner2->PinConnectionListChanged(DownIn);
						}
					}
				}

				// Collect and remove subgraph nodes
				TArray<UEdGraphNode*> NodesToRemove;
				TSet<UEdGraphNode*> Visited;
				CollectSubGraphNodes(ModNode, NodesToRemove, Visited);

				for (UEdGraphNode* Node : NodesToRemove)
				{
					if (Node && Graph)
					{
						Node->Modify();
						Node->BreakAllNodeLinks();
						Graph->RemoveNode(Node);
					}
				}

				if (Graph) Graph->NotifyGraphChanged();
				if (ScratchScript) ScratchScript->RequestCompile(FGuid());
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove(\"module\", \"%s\")"), *FModName));
				return sol::make_object(Lua, true);
			}
			// ---- remove("emitter") ----
			else if (FType.Equals(TEXT("emitter"), ESearchCase::IgnoreCase))
			{
				if (!Id.is<std::string>()) { Session.Log(TEXT("[FAIL] remove(\"emitter\") -> name required")); return sol::lua_nil; }
				FString Name = NeoLuaStr::ToFString(Id.as<std::string>());
				int32 EmIdx = FindEmitterIndex(System, Name);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"emitter\") -> '%s' not found"), *Name)); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNEmitter", "Remove Niagara Emitter"));
				System->Modify();
				TSet<FGuid> IdsToRemove;
				IdsToRemove.Add(System->GetEmitterHandle(EmIdx).GetId());
				System->RemoveEmitterHandlesById(IdsToRemove);
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove(\"emitter\", \"%s\")"), *Name));
				return sol::make_object(Lua, true);
			}
			// ---- remove("renderer") ----
			// Engine's UNiagaraStatelessEmitter::RemoveRenderer lives on the
			// stateless-emitter side; stateful goes through UNiagaraEmitter::RemoveRenderer.
			else if (FType.Equals(TEXT("renderer"), ESearchCase::IgnoreCase))
			{
				if (!Id.is<sol::table>()) { Session.Log(TEXT("[FAIL] remove(\"renderer\") -> {emitter, index} required")); return sol::lua_nil; }
				sol::table T = Id.as<sol::table>();
				std::string EmitterName = T.get_or<std::string>("emitter", "");
				int32 Idx = T.get_or("index", -1);

				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"renderer\") -> emitter '%s' not found. Use list(\"emitters\") to see available."), *FEmitter)); return sol::lua_nil; }

				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				const bool bStateless = (Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless);

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNRenderer", "Remove Niagara Renderer"));
				System->Modify();

				if (bStateless)
				{
					UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
					if (!SLE) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"renderer\") -> stateless emitter '%s' has no stateless instance"), *FEmitter)); return sol::lua_nil; }

#if NSAI_NIAGARA_HAS_STATELESS_INTERNAL
					const TArray<UNiagaraRendererProperties*>& Renderers = SLE->GetRenderers();
					const int32 Count = Renderers.Num();
					if (Idx < 0) Idx = Count - 1;
					if (Idx < 0 || Idx >= Count) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"renderer\") -> index %d out of range for stateless emitter '%s' (valid: 0-%d). Use list(\"renderers\", {emitter=\"%s\"}) first."), Idx, *FEmitter, Count - 1, *FEmitter)); return sol::lua_nil; }

					SLE->RemoveRenderer(Renderers[Idx], Handle.GetInstance().Version);
#else
					FArrayProperty* ArrProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("RendererProperties")));
					if (!ArrProp) { Session.Log(TEXT("[FAIL] remove(\"renderer\") -> RendererProperties UPROPERTY not found on stateless emitter")); return sol::lua_nil; }
					FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
					const int32 Count = Helper.Num();
					if (Idx < 0) Idx = Count - 1;
					if (Idx < 0 || Idx >= Count) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"renderer\") -> index %d out of range for stateless emitter '%s' (valid: 0-%d). Use list(\"renderers\", {emitter=\"%s\"}) first."), Idx, *FEmitter, Count - 1, *FEmitter)); return sol::lua_nil; }

					((UObject*)SLE)->Modify();
					Helper.RemoveValues(Idx, 1);
					((UObject*)SLE)->PostEditChange();
#endif
				}
				else
				{
					FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData();
					UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
					if (!ED || !Emitter) { Session.Log(TEXT("[FAIL] remove(\"renderer\") -> no emitter data")); return sol::lua_nil; }

					const TArray<UNiagaraRendererProperties*>& Renderers = ED->GetRenderers();
					if (Idx < 0) Idx = Renderers.Num() - 1;
					if (Idx < 0 || Idx >= Renderers.Num()) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"renderer\") -> index %d out of range for emitter '%s' (valid: 0-%d). Use list(\"renderers\", {emitter=\"%s\"}) first."), Idx, *FEmitter, Renderers.Num() - 1, *FEmitter)); return sol::lua_nil; }

					Emitter->RemoveRenderer(Renderers[Idx], Handle.GetInstance().Version);
				}

				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove(\"renderer\", emitter=\"%s\", index=%d%s)"), *FEmitter, Idx, bStateless ? TEXT(", mode=stateless") : TEXT("")));
				return sol::make_object(Lua, true);
			}
			// ---- remove("user_parameter") ----
			else if (FType.Equals(TEXT("user_parameter"), ESearchCase::IgnoreCase))
			{
				// Accept both string and table {name=...}
				FString ParamName;
				if (Id.is<std::string>())
					ParamName = NeoLuaStr::ToFString(Id.as<std::string>());
				else if (Id.is<sol::table>())
					ParamName = NeoLuaStr::ToFString(Id.as<sol::table>().get_or<std::string>("name", ""));
				if (ParamName.IsEmpty()) { Session.Log(TEXT("[FAIL] remove(\"user_parameter\") -> name required")); return sol::lua_nil; }
				FString FullName = TEXT("User.") + ParamName;

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNUserParam", "Remove Niagara User Parameter"));
				System->Modify();
				FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();

				bool bFound = false;
				TArray<FNiagaraVariable> Params;
				Store.GetUserParameters(Params);
				for (const FNiagaraVariable& V : Params)
				{
					FString VName = V.GetName().ToString();
					if (VName.Equals(ParamName, ESearchCase::IgnoreCase) || VName.Equals(FullName, ESearchCase::IgnoreCase))
					{
						Store.RemoveParameter(V);
						bFound = true;
						break;
					}
				}

				if (!bFound) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"user_parameter\") -> '%s' not found"), *ParamName)); return sol::lua_nil; }
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove(\"user_parameter\", \"%s\")"), *ParamName));
				return sol::make_object(Lua, true);
			}
			// ---- remove("event_handler") ----
			else if (FType.Equals(TEXT("event_handler"), ESearchCase::IgnoreCase))
			{
				if (!Id.is<sol::table>()) { Session.Log(TEXT("[FAIL] remove(\"event_handler\") -> {emitter, usage_id} required")); return sol::lua_nil; }
				sol::table T = Id.as<sol::table>();
				std::string EmitterName = T.get_or<std::string>("emitter", "");
				std::string UsageIdStr = T.get_or<std::string>("usage_id", "");
				int32 Index = T.get_or("index", -1);

				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"event_handler\") -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }

				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData();
				UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
				if (!ED || !Emitter) { Session.Log(TEXT("[FAIL] remove(\"event_handler\") -> no emitter data")); return sol::lua_nil; }

				const TArray<FNiagaraEventScriptProperties>& Events = ED->GetEventHandlers();
				if (Index < 0) Index = 0;
				if (Index >= Events.Num()) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"event_handler\") -> index %d out of range (%d)"), Index, Events.Num())); return sol::lua_nil; }

				FGuid UsageId = Events[Index].Script ? Events[Index].Script->GetUsageId() : FGuid();
				if (!UsageId.IsValid()) { Session.Log(TEXT("[FAIL] remove(\"event_handler\") -> could not resolve usage id")); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNEvent", "Remove Niagara Event Handler"));
				System->Modify();
				Emitter->Modify();

				// Clean up orphaned output node and its module subgraph for this event handler
				UNiagaraScriptSource* EvtSource = Cast<UNiagaraScriptSource>(ED->GraphSource);
				if (EvtSource && EvtSource->NodeGraph)
				{
					UNiagaraGraph* EvtGraph = EvtSource->NodeGraph;
					EvtGraph->Modify();
					TArray<UEdGraphNode*> NodesToRemove;
					for (UEdGraphNode* GNode : EvtGraph->Nodes)
					{
						UNiagaraNodeOutput* OutNode = Cast<UNiagaraNodeOutput>(GNode);
						if (OutNode && OutNode->GetUsage() == ENiagaraScriptUsage::ParticleEventScript && OutNode->GetUsageId() == UsageId)
						{
							// Collect the output node and all nodes feeding into it
							TSet<UEdGraphNode*> Visited;
							CollectSubGraphNodes(OutNode, NodesToRemove, Visited);
							break;
						}
					}
					for (UEdGraphNode* N : NodesToRemove)
					{
						if (N) { N->BreakAllNodeLinks(); EvtGraph->RemoveNode(N); }
					}
					if (NodesToRemove.Num() > 0) EvtGraph->NotifyGraphChanged();
				}

				Emitter->RemoveEventHandlerByUsageId(UsageId, Handle.GetInstance().Version);
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove(\"event_handler\", emitter=\"%s\", index=%d)"), *FEmitter, Index));
				return sol::make_object(Lua, true);
			}
			// ---- remove("simulation_stage") ----
			else if (FType.Equals(TEXT("simulation_stage"), ESearchCase::IgnoreCase))
			{
				if (!Id.is<sol::table>()) { Session.Log(TEXT("[FAIL] remove(\"simulation_stage\") -> {emitter, index} required")); return sol::lua_nil; }
				sol::table T = Id.as<sol::table>();
				std::string EmitterName = T.get_or<std::string>("emitter", "");
				int32 Index = T.get_or("index", 0);

				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"simulation_stage\") -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }

				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData();
				UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
				if (!ED || !Emitter) { Session.Log(TEXT("[FAIL] remove(\"simulation_stage\") -> no emitter data")); return sol::lua_nil; }

				const TArray<UNiagaraSimulationStageBase*>& Stages = ED->GetSimulationStages();
				if (Index < 0 || Index >= Stages.Num()) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"simulation_stage\") -> index %d out of range for emitter '%s' (valid: 0-%d). Use list(\"simulation_stages\", {emitter=\"%s\"}) first."), Index, *FEmitter, Stages.Num() - 1, *FEmitter)); return sol::lua_nil; }

				// Engine checks SimulationStages[i] && SimulationStages[i]->Script before
				// traversal (NiagaraEmitter.cpp:1601); RemoveSimulationStage dereferences
				// the pointer (NiagaraEmitter.cpp:3236/3237). A damaged asset can leave a
				// null slot — bail with actionable guidance before deref.
				UNiagaraSimulationStageBase* Stage = Stages[Index];
				if (!Stage)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] remove(\"simulation_stage\") -> stage %d on emitter '%s' is null. Use list(\"simulation_stages\", {emitter=\"%s\"}) to inspect and re-add with add(\"simulation_stage\", {emitter=\"%s\"})."), Index, *FEmitter, *FEmitter, *FEmitter));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNSimStage", "Remove Niagara Simulation Stage"));
				System->Modify();
				Emitter->Modify();
				Emitter->RemoveSimulationStage(Stage, Handle.GetInstance().Version);
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove(\"simulation_stage\", emitter=\"%s\", index=%d)"), *FEmitter, Index));
				return sol::make_object(Lua, true);
			}
			// ---- remove("spawn_info") ----
			else if (FType.Equals(TEXT("spawn_info"), ESearchCase::IgnoreCase))
			{
				if (!Id.is<sol::table>()) { Session.Log(TEXT("[FAIL] remove(\"spawn_info\") -> {emitter, index} required")); return sol::lua_nil; }
				sol::table T = Id.as<sol::table>();
				std::string EmitterName = T.get_or<std::string>("emitter", "");
				int32 Index = T.get_or("index", -1);
				if (EmitterName.empty() || Index < 0) { Session.Log(TEXT("[FAIL] remove(\"spawn_info\") -> emitter and non-negative index required")); return sol::lua_nil; }

				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"spawn_info\") -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }

				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				if (Handle.GetEmitterMode() != ENiagaraEmitterMode::Stateless) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"spawn_info\") -> '%s' not stateless"), *FEmitter)); return sol::lua_nil; }

				UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
				if (!SLE) { Session.Log(TEXT("[FAIL] remove(\"spawn_info\") -> no stateless emitter")); return sol::lua_nil; }

				FArrayProperty* ArrProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("SpawnInfos")));
				if (!ArrProp) { Session.Log(TEXT("[FAIL] remove(\"spawn_info\") -> SpawnInfos UPROPERTY not found")); return sol::lua_nil; }
				FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
				if (Index >= Helper.Num()) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"spawn_info\") -> index %d out of range (%d)"), Index, Helper.Num())); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNSpawnInfo", "Remove Stateless Spawn Info"));
				System->Modify();
				((UObject*)SLE)->Modify();
				Helper.RemoveValues(Index, 1);
				((UObject*)SLE)->PostEditChange();
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove(\"spawn_info\", emitter=\"%s\", index=%d)"), *FEmitter, Index));
				return sol::make_object(Lua, true);
			}
			// ---- remove("stateless_module") ----
			else if (FType.Equals(TEXT("stateless_module"), ESearchCase::IgnoreCase))
			{
				if (!Id.is<sol::table>()) { Session.Log(TEXT("[FAIL] remove(\"stateless_module\") -> {emitter, index} required")); return sol::lua_nil; }
				sol::table T = Id.as<sol::table>();
				std::string EmitterName = T.get_or<std::string>("emitter", "");
				int32 Index = T.get_or("index", -1);
				if (EmitterName.empty() || Index < 0)
				{
					Session.Log(TEXT("[FAIL] remove(\"stateless_module\") -> emitter and non-negative index required"));
					return sol::lua_nil;
				}

				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] remove(\"stateless_module\") -> emitter '%s' not found"), *FEmitter));
					return sol::lua_nil;
				}

				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				if (Handle.GetEmitterMode() != ENiagaraEmitterMode::Stateless)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] remove(\"stateless_module\") -> emitter '%s' is not in stateless mode"), *FEmitter));
					return sol::lua_nil;
				}

				UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
				if (!SLE) { Session.Log(TEXT("[FAIL] remove(\"stateless_module\") -> no stateless emitter")); return sol::lua_nil; }

				FArrayProperty* ModProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("Modules")));
				if (!ModProp) { Session.Log(TEXT("[FAIL] remove(\"stateless_module\") -> Modules UPROPERTY not found")); return sol::lua_nil; }
				FScriptArrayHelper Helper(ModProp, ModProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
				if (Index >= Helper.Num())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] remove(\"stateless_module\") -> index %d out of range (%d)"), Index, Helper.Num()));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNStatelessMod", "Remove Stateless Module"));
				System->Modify();
				((UObject*)SLE)->Modify();
				Helper.RemoveValues(Index, 1);
				((UObject*)SLE)->PostEditChange();
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove(\"stateless_module\", emitter=\"%s\", index=%d)"), *FEmitter, Index));
				return sol::make_object(Lua, true);
			}

			Session.Log(FString::Printf(TEXT("[FAIL] remove(\"%s\") -> unknown type. Valid: emitter, renderer, module, user_parameter, event_handler, simulation_stage, spawn_info, stateless_module."), *FType));
			return sol::lua_nil;
		});

		// ==================================================================
		// list(type, params?)
		// ==================================================================
		AssetObj.set_function("list", [System, &Session](sol::table self,
			sol::optional<std::string> TypeOpt, sol::optional<sol::table> Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFStringOpt(TypeOpt, TEXT("all"));

			if (FType.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				sol::protected_function InfoFn = self["info"];
				if (InfoFn.valid()) return InfoFn(self);
				return sol::lua_nil;
			}

			// ---- list("emitters") ----
			if (FType.Contains(TEXT("emitter"), ESearchCase::IgnoreCase) && !FType.Contains(TEXT("template"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lua.create_table();
				for (int32 i = 0; i < System->GetNumEmitters(); i++)
				{
					const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(i);
					FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData();
					sol::table E = Lua.create_table();
					E["index"] = i;
					E["name"] = TCHAR_TO_UTF8(*Handle.GetName().ToString());
					E["enabled"] = Handle.GetIsEnabled();
					E["emitter_mode"] = (Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless) ? "stateless" : "standard";
					if (Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless)
					{
						UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
						if (SLE)
						{
							// Access via reflection since headers are in Internal/
							FArrayProperty* ModProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("Modules")));
							if (ModProp)
							{
								FScriptArrayHelper ModArr(ModProp, ModProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
								E["stateless_modules"] = ModArr.Num();
							}
							FArrayProperty* RendProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("RendererProperties")));
							if (RendProp)
							{
								FScriptArrayHelper RendArr(RendProp, RendProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
								E["renderers"] = RendArr.Num();
							}
						}
					}
					else if (ED)
					{
						E["renderers"] = ED->GetRenderers().Num();
						E["event_handlers"] = ED->GetEventHandlers().Num();
						E["simulation_stages"] = ED->GetSimulationStages().Num();
						E["sim_target"] = (ED->SimTarget == ENiagaraSimTarget::GPUComputeSim) ? "gpu" : "cpu";
						E["local_space"] = ED->bLocalSpace;
						E["determinism"] = ED->bDeterminism;
						E["requires_persistent_ids"] = (bool)ED->bRequiresPersistentIDs;
					}
					Result[i + 1] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"emitters\") -> %d"), System->GetNumEmitters()));
				return Result;
			}
			// ---- list("modules") ----
			if (FType.Contains(TEXT("module"), ESearchCase::IgnoreCase)
				&& !FType.Contains(TEXT("input"), ESearchCase::IgnoreCase)
				&& !FType.Contains(TEXT("available"), ESearchCase::IgnoreCase)
				&& !FType.Contains(TEXT("stateless"), ESearchCase::IgnoreCase))
			{
				if (!Params.has_value()) { Session.Log(TEXT("[FAIL] list(\"modules\") -> {emitter, stage?} or {scratch_pad} required")); return sol::lua_nil; }
				sol::table P = Params.value();
				std::string EmitterName = P.get_or<std::string>("emitter", "");
				std::string StageStr = P.get_or<std::string>("stage", "");
				std::string ScratchPad = P.get_or<std::string>("scratch_pad", "");

				// Scratch pad target: list modules in the scratch pad's own graph.
				if (!ScratchPad.empty())
				{
					FScratchPadTarget Target = ResolveScratchPadTarget(System, Session,
						NeoLuaStr::ToFString(ScratchPad), TEXT("list(\"modules\")"));
					if (!Target.OutputNode) return sol::lua_nil;

					sol::table Result = Lua.create_table();
					int32 Out = 1;
					TArray<FString> ModNames = ListModulesInStack(Target.OutputNode);
					for (const FString& ModName : ModNames)
					{
						sol::table E = Lua.create_table();
						E["name"] = TCHAR_TO_UTF8(*ModName);
						E["scratch_pad"] = ScratchPad;
						Result[Out++] = E;
					}
					Session.Log(FString::Printf(TEXT("[OK] list(\"modules\", scratch_pad=\"%s\") -> %d"),
						UTF8_TO_TCHAR(ScratchPad.c_str()), Out - 1));
					return sol::make_object(Lua, Result);
				}

				// Build list of stages to iterate
				TArray<ENiagaraScriptUsage> UsagesToCheck;
				if (StageStr.empty())
				{
					// No stage specified — iterate all emitter stages
					UsagesToCheck.Add(ENiagaraScriptUsage::EmitterSpawnScript);
					UsagesToCheck.Add(ENiagaraScriptUsage::EmitterUpdateScript);
					UsagesToCheck.Add(ENiagaraScriptUsage::ParticleSpawnScript);
					UsagesToCheck.Add(ENiagaraScriptUsage::ParticleUpdateScript);
				}
				else
				{
					UsagesToCheck.Add(ParseUsage(NeoLuaStr::ToFString(StageStr)));
				}

				int32 EmIdx = INDEX_NONE;
				bool bSystemLevel = false;
				if (UsagesToCheck.Num() == 1)
				{
					bSystemLevel = (UsagesToCheck[0] == ENiagaraScriptUsage::SystemSpawnScript || UsagesToCheck[0] == ENiagaraScriptUsage::SystemUpdateScript);
				}
				if (!bSystemLevel)
				{
					EmIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(EmitterName));
					if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] list(\"modules\") -> emitter '%s' not found"), UTF8_TO_TCHAR(EmitterName.c_str()))); return sol::lua_nil; }
				}

				TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
				TArray<FString> ModuleStages;
				for (ENiagaraScriptUsage Usage : UsagesToCheck)
				{
					UNiagaraNodeOutput* OutputNode = FindOutputNode(System, EmIdx, Usage, FGuid(), false);
					if (!OutputNode) continue;

					FString StageName;
					switch (Usage)
					{
					case ENiagaraScriptUsage::EmitterSpawnScript: StageName = TEXT("EmitterSpawn"); break;
					case ENiagaraScriptUsage::EmitterUpdateScript: StageName = TEXT("EmitterUpdate"); break;
					case ENiagaraScriptUsage::ParticleSpawnScript: StageName = TEXT("ParticleSpawn"); break;
					case ENiagaraScriptUsage::ParticleUpdateScript: StageName = TEXT("ParticleUpdate"); break;
					default: StageName = TEXT("Unknown"); break;
					}

					UEdGraphPin* InputPin = GetParamMapInputPin(OutputNode);
					if (InputPin)
					{
						TArray<UNiagaraNodeFunctionCall*> StageModules;
						UEdGraphNode* Current = (InputPin->LinkedTo.Num() > 0 && InputPin->LinkedTo[0]) ? InputPin->LinkedTo[0]->GetOwningNode() : nullptr;
						TSet<UEdGraphNode*> Visited;
						while (Current && !Visited.Contains(Current))
						{
							Visited.Add(Current);
							if (UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(Current))
								StageModules.Insert(FC, 0);
							UEdGraphPin* PrevPin = GetParamMapInputPin(Current);
							Current = nullptr;
							if (PrevPin && PrevPin->LinkedTo.Num() > 0 && PrevPin->LinkedTo[0])
								Current = PrevPin->LinkedTo[0]->GetOwningNode();
						}
						for (UNiagaraNodeFunctionCall* FC : StageModules)
						{
							ModuleNodes.Add(FC);
							ModuleStages.Add(StageName);
						}
					}
				}

				sol::table Result = Lua.create_table();
				for (int32 i = 0; i < ModuleNodes.Num(); i++)
				{
					sol::table M = Lua.create_table();
					M["index"] = i;
					M["name"] = TCHAR_TO_UTF8(*ModuleNodes[i]->GetFunctionName());
					M["enabled"] = ModuleNodes[i]->GetDesiredEnabledState() == ENodeEnabledState::Enabled;
					if (i < ModuleStages.Num())
						M["stage"] = TCHAR_TO_UTF8(*ModuleStages[i]);
					if (ModuleNodes[i]->FunctionScript)
						M["script"] = TCHAR_TO_UTF8(*ModuleNodes[i]->FunctionScript->GetPathName());
					Result[i + 1] = M;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"modules\") -> %d"), ModuleNodes.Num()));
				return Result;
			}
			// ---- list("renderers") ----
			// Stateful and stateless emitters store renderers in different arrays
			// (stateless: UNiagaraStatelessEmitter::RendererProperties UPROPERTY).
			if (FType.Contains(TEXT("renderer"), ESearchCase::IgnoreCase))
			{
				if (!Params.has_value()) { Session.Log(TEXT("[FAIL] list(\"renderers\") -> {emitter} required")); return sol::lua_nil; }
				std::string EmitterName = Params.value().get_or<std::string>("emitter", "");
				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] list(\"renderers\") -> emitter '%s' not found. Use list(\"emitters\") to see available."), *FEmitter)); return sol::lua_nil; }

				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				FNiagaraRendererStorage Storage;
				FString ResolveError;
				if (!ResolveRendererStorage(Handle, FEmitter, TEXT("list(\"renderers\")"), Storage, ResolveError))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] %s"), *ResolveError));
					return sol::lua_nil;
				}

				sol::table Result = Lua.create_table();
				for (int32 i = 0; i < Storage.Renderers.Num(); i++)
				{
					Result[i + 1] = BuildRendererSummary(Storage.Renderers[i], i, Lua);
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"renderers\") -> %d%s"), Storage.Renderers.Num(), Storage.bStateless ? TEXT(" (stateless)") : TEXT("")));
				return Result;
			}
			// ---- list("user_parameters") ----
			if (FType.Contains(TEXT("user_param"), ESearchCase::IgnoreCase) || FType.Contains(TEXT("parameter"), ESearchCase::IgnoreCase))
			{
				FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
				TArray<FNiagaraVariable> Params2;
				Store.GetUserParameters(Params2);

				sol::table Result = Lua.create_table();
				int32 Idx = 1;
				for (const FNiagaraVariable& V : Params2)
				{
					sol::table E = Lua.create_table();
					E["name"] = TCHAR_TO_UTF8(*V.GetName().ToString());
					E["type"] = TCHAR_TO_UTF8(*V.GetType().GetName());

					// Include current value for known types
					const int32* Offset = Store.FindParameterOffset(V);
					if (Offset)
					{
						if (V.GetType() == FNiagaraTypeDefinition::GetFloatDef())
							E["value"] = Store.GetParameterValue<float>(V);
						else if (V.GetType() == FNiagaraTypeDefinition::GetIntDef())
							E["value"] = Store.GetParameterValue<int32>(V);
						else if (V.GetType() == FNiagaraTypeDefinition::GetBoolDef())
							E["value"] = Store.GetParameterValue<FNiagaraBool>(V).GetValue();
					}

					Result[Idx++] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"user_parameters\") -> %d"), Idx - 1));
				return Result;
			}
			// ---- list("event_handlers") ----
			if (FType.Contains(TEXT("event"), ESearchCase::IgnoreCase))
			{
				if (!Params.has_value()) { Session.Log(TEXT("[FAIL] list(\"event_handlers\") -> {emitter} required")); return sol::lua_nil; }
				std::string EmitterName = Params.value().get_or<std::string>("emitter", "");
				int32 EmIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(EmitterName));
				if (EmIdx == INDEX_NONE) { Session.Log(TEXT("[FAIL] list(\"event_handlers\") -> emitter not found")); return sol::lua_nil; }

				FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
				if (!ED) { Session.Log(TEXT("[FAIL] list(\"event_handlers\") -> no emitter data")); return sol::lua_nil; }

				const TArray<FNiagaraEventScriptProperties>& Events = ED->GetEventHandlers();
				sol::table Result = Lua.create_table();
				for (int32 i = 0; i < Events.Num(); i++)
				{
					sol::table E = Lua.create_table();
					E["index"] = i;
					E["source_event"] = TCHAR_TO_UTF8(*Events[i].SourceEventName.ToString());
					if (Events[i].Script)
						E["usage_id"] = TCHAR_TO_UTF8(*Events[i].Script->GetUsageId().ToString());
					Result[i + 1] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"event_handlers\") -> %d"), Events.Num()));
				return Result;
			}
			// ---- list("simulation_stages") ----
			if (FType.Contains(TEXT("simulation"), ESearchCase::IgnoreCase) || FType.Contains(TEXT("simstage"), ESearchCase::IgnoreCase))
			{
				if (!Params.has_value()) { Session.Log(TEXT("[FAIL] list(\"simulation_stages\") -> {emitter} required")); return sol::lua_nil; }
				std::string EmitterName = Params.value().get_or<std::string>("emitter", "");
				int32 EmIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(EmitterName));
				if (EmIdx == INDEX_NONE) { Session.Log(TEXT("[FAIL] list(\"simulation_stages\") -> emitter not found")); return sol::lua_nil; }

				FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
				if (!ED) { Session.Log(TEXT("[FAIL] list(\"simulation_stages\") -> no emitter data")); return sol::lua_nil; }

				const TArray<UNiagaraSimulationStageBase*>& Stages = ED->GetSimulationStages();
				sol::table Result = Lua.create_table();
				for (int32 i = 0; i < Stages.Num(); i++)
				{
					sol::table E = Lua.create_table();
					E["index"] = i;
					E["name"] = Stages[i] ? TCHAR_TO_UTF8(*Stages[i]->SimulationStageName.ToString()) : "";
					E["enabled"] = Stages[i] ? Stages[i]->bEnabled : false;
					Result[i + 1] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"simulation_stages\") -> %d"), Stages.Num()));
				return Result;
			}
			// ---- list("module_inputs") ----
			if (FType.Contains(TEXT("input"), ESearchCase::IgnoreCase))
			{
				if (!Params.has_value()) { Session.Log(TEXT("[FAIL] list(\"module_inputs\") -> {emitter, stage, module_name} or {scratch_pad, module_name} required")); return sol::lua_nil; }
				sol::table P = Params.value();
				std::string EmitterName = P.get_or<std::string>("emitter", "");
				std::string StageStr = P.get_or<std::string>("stage", "particle_update");
				std::string ScratchPad = P.get_or<std::string>("scratch_pad", "");
				std::string ModName = P.get_or<std::string>("module_name", "");

				// Track consumed keys and warn about unrecognized ones
				TSet<FString> ConsumedKeys = { TEXT("emitter"), TEXT("stage"), TEXT("scratch_pad"), TEXT("module_name") };
				WarnUnconsumedKeys(Session, P, ConsumedKeys, TEXT("list(\"module_inputs\")"));

				if (ModName.empty()) { Session.Log(TEXT("[FAIL] list(\"module_inputs\") -> module_name is required (was empty)")); return sol::lua_nil; }

				UNiagaraNodeOutput* OutputNode = nullptr;
				UNiagaraScript*     ScratchScript = nullptr;
				ENiagaraScriptUsage Usage = ENiagaraScriptUsage::Module;
				int32 EmIdx = INDEX_NONE;
				if (!ScratchPad.empty())
				{
					FScratchPadTarget Target = ResolveScratchPadTarget(System, Session,
						NeoLuaStr::ToFString(ScratchPad), TEXT("list(\"module_inputs\")"));
					if (!Target.OutputNode) return sol::lua_nil;
					OutputNode    = Target.OutputNode;
					ScratchScript = Target.Script;
				}
				else
				{
					Usage = ParseUsage(NeoLuaStr::ToFString(StageStr));
					bool bSys = (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript);
					if (!bSys) EmIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(EmitterName));

					OutputNode = FindOutputNode(System, EmIdx, Usage, FGuid(), false);
					if (!OutputNode) { Session.Log(TEXT("[FAIL] list(\"module_inputs\") -> no output node")); return sol::lua_nil; }
				}

				UNiagaraNodeFunctionCall* ModNode = FindModuleByName(OutputNode, NeoLuaStr::ToFString(ModName));
				if (!ModNode) { Session.Log(FString::Printf(TEXT("[FAIL] list(\"module_inputs\") -> module '%s' not found"), UTF8_TO_TCHAR(ModName.c_str()))); return sol::lua_nil; }

				// List input pins — include hidden pins so static-switch-gated inputs are visible
				sol::table Result = Lua.create_table();
				int32 Idx = 1;
				for (UEdGraphPin* Pin : ModNode->GetAllPins())
				{
					if (Pin->Direction == EGPD_Input && !IsParameterMapPin(Pin))
					{
						sol::table E = Lua.create_table();
						E["name"] = TCHAR_TO_UTF8(*Pin->GetName());
						FNiagaraTypeDefinition PinType = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
						E["type"] = TCHAR_TO_UTF8(*PinType.GetName());
						E["value_mode"] = "pin";
						E["has_default"] = !Pin->DefaultValue.IsEmpty();
						if (!Pin->DefaultValue.IsEmpty())
						{
							E["default_value"] = TCHAR_TO_UTF8(*Pin->DefaultValue);
							E["value"] = PinDefaultToSolValue(Pin->DefaultValue, PinType, Lua);
						}
						if (Pin->bHidden)
							E["hidden"] = true;
						Result[Idx++] = E;
					}
				}

				// Also list inputs from the called graph (catches versioned template modules)
				if (UNiagaraScript* FuncScript = ModNode->FunctionScript)
				{
					TSet<FString> ExistingNames;
					for (int32 i = 1; i < Idx; i++)
					{
						sol::table Ex = Result[i];
						ExistingNames.Add(NeoLuaStr::ToFString(Ex.get_or<std::string>("name", "")));
					}
					for (const FNiagaraVariable& ScriptVar : FuncScript->GetVMExecutableData().Parameters.Parameters)
					{
						FString VarName = ScriptVar.GetName().ToString();
						if (!VarName.StartsWith(TEXT("Module."))) continue;
						FString ShortName = VarName.RightChop(7);
						if (ExistingNames.Contains(ShortName)) continue;
						ExistingNames.Add(ShortName);

						sol::table E = Lua.create_table();
						E["name"] = TCHAR_TO_UTF8(*ShortName);
						E["type"] = TCHAR_TO_UTF8(*ScriptVar.GetType().GetName());
						E["value_mode"] = "script_param";
						Result[Idx++] = E;
					}
				}

				// Also list rapid iteration parameters belonging to this module.
				// For scratch pad scripts RI params live on the scratch script itself;
				// for emitter/system stages we resolve the owning stage script by usage.
				UNiagaraScript* RIScript = nullptr;
				if (ScratchScript)
				{
					RIScript = ScratchScript;
				}
				else if (EmIdx != INDEX_NONE)
				{
					FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
					if (ED)
					{
						TArray<UNiagaraScript*> Scripts;
						ED->GetScripts(Scripts, false);
						for (UNiagaraScript* Sc : Scripts) { if (Sc && Sc->GetUsage() == Usage) { RIScript = Sc; break; } }
					}
				}
				else
				{
					RIScript = (Usage == ENiagaraScriptUsage::SystemSpawnScript) ? System->GetSystemSpawnScript() : System->GetSystemUpdateScript();
				}

				// Always list RI params — versioned template modules may ONLY expose inputs as RI params
				if (RIScript)
				{
					FString ModFuncName = ModNode->GetFunctionName();
					for (const FNiagaraVariableWithOffset& RIVar : RIScript->RapidIterationParameters.ReadParameterVariables())
					{
						FString RIName = RIVar.GetName().ToString();
						// RI param names contain the module function name as part of the path
						if (!RIName.Contains(ModFuncName)) continue;

						// Extract the short parameter name (last segment after .)
						FString ShortName = RIName;
						int32 LastDot = INDEX_NONE;
						if (ShortName.FindLastChar(TEXT('.'), LastDot))
							ShortName = ShortName.RightChop(LastDot + 1);

						sol::table E = Lua.create_table();
						E["name"] = TCHAR_TO_UTF8(*ShortName);
						E["full_name"] = TCHAR_TO_UTF8(*RIName);
						E["type"] = TCHAR_TO_UTF8(*RIVar.GetType().GetName());
						E["value_mode"] = "rapid_iteration";

						// Read current value for known types
						const int32* Offset = RIScript->RapidIterationParameters.FindParameterOffset(RIVar);
						if (Offset)
						{
							if (RIVar.GetType() == FNiagaraTypeDefinition::GetFloatDef())
								E["value"] = RIScript->RapidIterationParameters.GetParameterValue<float>(RIVar);
							else if (RIVar.GetType() == FNiagaraTypeDefinition::GetIntDef())
								E["value"] = RIScript->RapidIterationParameters.GetParameterValue<int32>(RIVar);
							else if (RIVar.GetType() == FNiagaraTypeDefinition::GetBoolDef())
								E["value"] = RIScript->RapidIterationParameters.GetParameterValue<FNiagaraBool>(RIVar).GetValue();
						}

						Result[Idx++] = E;
					}
				}

				Session.Log(FString::Printf(TEXT("[OK] list(\"module_inputs\") -> %d"), Idx - 1));
				return Result;
			}
			// `list("reflected_properties")` was removed: use `get("system"|"emitter"|"renderer", ...)`
			// with no third arg to fetch the full property table, then iterate with `pairs()` in Lua.

			// ---- list("dynamic_inputs") ----
			if (FType.Contains(TEXT("dynamic"), ESearchCase::IgnoreCase))
			{
				FString TypeFilter;
				if (Params.has_value())
					TypeFilter = NeoLuaStr::ToFString(Params.value().get_or<std::string>("type_filter", ""));

				FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions Options;
				Options.ScriptUsageToInclude = ENiagaraScriptUsage::DynamicInput;
				TArray<FAssetData> Assets;
				FNiagaraEditorUtilities::GetFilteredScriptAssets(Options, Assets);

				sol::table Result = Lua.create_table();
				int32 Idx = 1;
				for (const FAssetData& Asset : Assets)
				{
					FString AssetName = Asset.AssetName.ToString();
					if (!TypeFilter.IsEmpty() && !AssetName.Contains(TypeFilter, ESearchCase::IgnoreCase))
						continue;
					sol::table E = Lua.create_table();
					E["name"] = TCHAR_TO_UTF8(*AssetName);
					E["path"] = TCHAR_TO_UTF8(*Asset.GetObjectPathString());
					Result[Idx++] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"dynamic_inputs\") -> %d"), Idx - 1));
				return Result;
			}
			// ---- list("scratch_pad_scripts") ----
			if (FType.Contains(TEXT("scratch"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lua.create_table();
				int32 Idx = 1;
				for (UNiagaraScript* ScratchScript : System->ScratchPadScripts)
				{
					if (!ScratchScript) continue;
					sol::table E = Lua.create_table();
					E["name"] = TCHAR_TO_UTF8(*ScratchScript->GetName());
					E["usage"] = TCHAR_TO_UTF8(*StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue((int64)ScratchScript->GetUsage()));
					Result[Idx++] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"scratch_pad_scripts\") -> %d"), Idx - 1));
				return Result;
			}
			// ---- list("available_modules") ----
			if (FType.Contains(TEXT("available_module"), ESearchCase::IgnoreCase))
			{
				FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions Options;
				Options.ScriptUsageToInclude = ENiagaraScriptUsage::Module;

				if (Params.has_value())
				{
					// "usage" overrides the script usage filter (Module, DynamicInput)
					// "stage" is accepted but does NOT change the usage — modules are always usage=Module
					std::string UsageFilter = Params.value().get_or<std::string>("usage", "");
					if (!UsageFilter.empty())
					{
						FString FUsage = NeoLuaStr::ToFString(UsageFilter);
						if (FUsage.Contains(TEXT("DynamicInput"), ESearchCase::IgnoreCase) || FUsage.Contains(TEXT("dynamic_input"), ESearchCase::IgnoreCase))
							Options.ScriptUsageToInclude = ENiagaraScriptUsage::DynamicInput;
						else if (FUsage.Contains(TEXT("Module"), ESearchCase::IgnoreCase))
							Options.ScriptUsageToInclude = ENiagaraScriptUsage::Module;
					}
				}

				TArray<FAssetData> Assets;
				FNiagaraEditorUtilities::GetFilteredScriptAssets(Options, Assets);

				sol::table Result = Lua.create_table();
				int32 Idx = 1;
				for (const FAssetData& Asset : Assets)
				{
					sol::table E = Lua.create_table();
					E["name"] = TCHAR_TO_UTF8(*Asset.AssetName.ToString());
					E["path"] = TCHAR_TO_UTF8(*Asset.GetObjectPathString());
					FString Desc;
					if (Asset.GetTagValue(TEXT("Description"), Desc) && !Desc.IsEmpty())
						E["description"] = TCHAR_TO_UTF8(*Desc);
					Result[Idx++] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"available_modules\") -> %d"), Idx - 1));
				return Result;
			}
			// ---- list("emitter_templates") ----
			if (FType.Contains(TEXT("emitter_template"), ESearchCase::IgnoreCase))
			{
				IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
				TArray<FAssetData> EmitterAssets;

				// In UE 5.7, emitters are sub-objects of NiagaraSystem — not standalone assets.
				// Search by class AND by known template directories.
				Registry.GetAssetsByClass(UNiagaraEmitter::StaticClass()->GetClassPathName(), EmitterAssets, true);

				// Also search known Niagara template paths
				static const TCHAR* TemplatePaths[] = {
					TEXT("/Niagara/DefaultAssets/Templates"),
					TEXT("/Niagara/Templates"),
					TEXT("/Game"),
				};
				for (const TCHAR* SearchPath : TemplatePaths)
				{
					TArray<FAssetData> PathAssets;
					Registry.GetAssetsByPath(FName(SearchPath), PathAssets, true);
					for (const FAssetData& A : PathAssets)
					{
						FString ClassName = A.AssetClassPath.GetAssetName().ToString();
						if (ClassName.Contains(TEXT("NiagaraEmitter")))
						{
							EmitterAssets.AddUnique(A);
						}
					}
				}

				sol::table Result = Lua.create_table();
				int32 Idx = 1;
				for (const FAssetData& Asset : EmitterAssets)
				{
					FString AssetPath = Asset.GetObjectPathString();
					bool bIsTemplate = AssetPath.Contains(TEXT("Template")) || AssetPath.Contains(TEXT("DefaultAssets")) || AssetPath.Contains(TEXT("Niagara"));
					if (!bIsTemplate && EmitterAssets.Num() > 50) continue;

					sol::table E = Lua.create_table();
					E["name"] = TCHAR_TO_UTF8(*Asset.AssetName.ToString());
					E["path"] = TCHAR_TO_UTF8(*AssetPath);
					Result[Idx++] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"emitter_templates\") -> %d"), Idx - 1));
				return Result;
			}
			// ---- list("stateless_modules") ----
			if (FType.Contains(TEXT("stateless_module"), ESearchCase::IgnoreCase))
			{
				if (!Params.has_value()) { Session.Log(TEXT("[FAIL] list(\"stateless_modules\") -> {emitter} required")); return sol::lua_nil; }
				std::string EmitterName = Params.value().get_or<std::string>("emitter", "");
				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] list(\"stateless_modules\") -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }

				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				if (Handle.GetEmitterMode() != ENiagaraEmitterMode::Stateless)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] list(\"stateless_modules\") -> emitter '%s' is not in stateless mode"), *FEmitter));
					return sol::lua_nil;
				}

				UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
				if (!SLE) { Session.Log(TEXT("[FAIL] list(\"stateless_modules\") -> no stateless emitter")); return sol::lua_nil; }

				// Access Modules array via reflection (header is in Internal/)
				FArrayProperty* ModProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("Modules")));
				if (!ModProp) { Session.Log(TEXT("[FAIL] list(\"stateless_modules\") -> Modules property not found")); return sol::lua_nil; }

				FScriptArrayHelper ModArr(ModProp, ModProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
				FObjectPropertyBase* InnerProp = CastField<FObjectPropertyBase>(ModProp->Inner);

				sol::table Result = Lua.create_table();
				int32 Idx = 1;
				for (int32 i = 0; i < ModArr.Num(); ++i)
				{
					UObject* Mod = InnerProp ? InnerProp->GetObjectPropertyValue(ModArr.GetRawPtr(i)) : nullptr;
					if (!Mod) continue;
					sol::table E = Lua.create_table();
					E["index"] = i;
					E["class"] = TCHAR_TO_UTF8(*Mod->GetClass()->GetName());
					// Check bModuleEnabled via reflection
					FBoolProperty* EnabledProp = CastField<FBoolProperty>(Mod->GetClass()->FindPropertyByName(TEXT("bModuleEnabled")));
					E["enabled"] = EnabledProp ? EnabledProp->GetPropertyValue_InContainer(Mod) : true;
					// Strip "NiagaraStatelessModule_" prefix for friendly name
					FString ClassName = Mod->GetClass()->GetName();
					FString FriendlyName = ClassName;
					FriendlyName.RemoveFromStart(TEXT("NiagaraStatelessModule_"));
					E["name"] = TCHAR_TO_UTF8(*FriendlyName);
					Result[Idx++] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"stateless_modules\") -> %d"), Idx - 1));
				return Result;
			}
			// ---- list("spawn_infos") ----
			if (FType.Contains(TEXT("spawn_info"), ESearchCase::IgnoreCase))
			{
				if (!Params.has_value()) { Session.Log(TEXT("[FAIL] list(\"spawn_infos\") -> {emitter} required")); return sol::lua_nil; }
				std::string EmitterName = Params.value().get_or<std::string>("emitter", "");
				FString FEmitter = NeoLuaStr::ToFString(EmitterName);
				int32 EmIdx = FindEmitterIndex(System, FEmitter);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] list(\"spawn_infos\") -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }

				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				if (Handle.GetEmitterMode() != ENiagaraEmitterMode::Stateless) { Session.Log(FString::Printf(TEXT("[FAIL] list(\"spawn_infos\") -> '%s' not stateless"), *FEmitter)); return sol::lua_nil; }

				UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
				if (!SLE) { Session.Log(TEXT("[FAIL] list(\"spawn_infos\") -> no stateless emitter")); return sol::lua_nil; }

				FArrayProperty* ArrProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("SpawnInfos")));
				if (!ArrProp) { Session.Log(TEXT("[FAIL] list(\"spawn_infos\") -> SpawnInfos UPROPERTY not found")); return sol::lua_nil; }
				FStructProperty* InnerProp = CastField<FStructProperty>(ArrProp->Inner);
				if (!InnerProp) { Session.Log(TEXT("[FAIL] list(\"spawn_infos\") -> inner not a struct")); return sol::lua_nil; }

				FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
				sol::table Result = Lua.create_table();
				for (int32 i = 0; i < Helper.Num(); ++i)
				{
					sol::table Info = NeoLuaProperty::ReadStructAsTable(InnerProp->Struct, Helper.GetRawPtr(i), Lua);
					Info["index"] = i;
					Result[i + 1] = Info;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"spawn_infos\") -> %d"), Helper.Num()));
				return Result;
			}

			Session.Log(FString::Printf(TEXT("[FAIL] list(\"%s\") -> unknown type"), *FType));
			return sol::lua_nil;
		});

		// ==================================================================
		// configure(type, id, params)
		// ==================================================================
		AssetObj.set_function("configure", [System, &Session](sol::table /*self*/,
			const std::string& Type, sol::object Id, sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);

			// ---- configure("emitter", name, {enabled?, properties={...}}) ----
			// LCD: all reflected fields (SimTarget, bLocalSpace, FixedBounds, CalculateBoundsMode,
			// AllocationMode, InterpolatedSpawnMode, ...) go through {properties=...}. snake_case
			// + b-prefix variants auto-resolve via FindPropertyByName. Only "enabled" is treated
			// specially because it's a method call on FNiagaraEmitterHandle, not a struct field.
			if (FType.Equals(TEXT("emitter"), ESearchCase::IgnoreCase))
			{
				FString EmName = Id.is<std::string>() ? NeoLuaStr::ToFString(Id.as<std::string>()) : TEXT("");
				if (EmName.IsEmpty()) EmName = NeoLuaStr::ToFString(Params.get_or<std::string>("emitter", ""));
				int32 EmIdx = FindEmitterIndex(System, EmName);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"emitter\") -> '%s' not found"), *EmName)); return sol::lua_nil; }

				FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
				if (!ED) { Session.Log(TEXT("[FAIL] configure(\"emitter\") -> no emitter data")); return sol::lua_nil; }
				UNiagaraEmitter* EmitterObj = System->GetEmitterHandle(EmIdx).GetInstance().Emitter;

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNEmitter", "Configure Niagara Emitter"));
				System->Modify();

				// `enabled` — handle-level method with recompile semantics (not a reflected field).
				sol::optional<bool> Enabled = Params.get<sol::optional<bool>>("enabled");
				if (Enabled.has_value())
				{
					System->GetEmitterHandle(EmIdx).SetIsEnabled(Enabled.value(), *System, /*bRecompile=*/false);
				}

				// All reflected fields on FVersionedNiagaraEmitterData go through the LCD walker.
				sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties");
				if (Props.has_value())
				{
					FString ApplyErr;
					TArray<FString> Warnings;
					NeoLuaProperty::ApplyTableToStruct(FVersionedNiagaraEmitterData::StaticStruct(), ED,
						EmitterObj, Props.value(), ApplyErr, &Warnings);
					for (const FString& W : Warnings)
						Session.Log(FString::Printf(TEXT("[WARN] configure(\"emitter\") %s"), *W));
				}

				if (EmitterObj) EmitterObj->PostEditChange();
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] configure(\"emitter\", \"%s\")"), *EmName));
				return sol::make_object(Lua, true);
			}
			// ---- configure("system", _, {warmup_time?, bake_out_rapid_iteration?, trim_attributes?, effect_type?, properties={...}}) ----
			// LCD: reflected fields go through {properties=...}. The retained top-level keys wrap
			// engine behavior that can't be expressed as a single UPROPERTY write:
			//   - warmup_time:               SetWarmupTime() also calls ResolveWarmupTickCount
			//   - bake_out_rapid_iteration:  SetBakeOutRapidIterationOnCook writes BOTH edit- and cook-time flags
			//   - trim_attributes:           SetTrimAttributesOnCook writes BOTH edit- and cook-time flags
			//   - effect_type:               SetEffectType() triggers scalability-settings rebuild
			else if (FType.Equals(TEXT("system"), ESearchCase::IgnoreCase))
			{
				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNSystem", "Configure Niagara System"));
				System->Modify();

				sol::optional<double> WarmupTime = Params.get<sol::optional<double>>("warmup_time");
				if (WarmupTime.has_value()) System->SetWarmupTime(static_cast<float>(WarmupTime.value()));

				sol::optional<bool> BakeOutRI = Params.get<sol::optional<bool>>("bake_out_rapid_iteration");
				if (BakeOutRI.has_value()) System->SetBakeOutRapidIterationOnCook(BakeOutRI.value());

				sol::optional<bool> TrimAttrs = Params.get<sol::optional<bool>>("trim_attributes");
				if (TrimAttrs.has_value()) System->SetTrimAttributesOnCook(TrimAttrs.value());

				sol::optional<std::string> EffectTypePath = Params.get<sol::optional<std::string>>("effect_type");
				if (EffectTypePath.has_value())
				{
					UNiagaraEffectType* EffectType = nullptr;
					const FString Path = NeoLuaStr::ToFStringOpt(EffectTypePath);
					if (!Path.IsEmpty())
					{
						EffectType = NeoLuaAsset::Resolve<UNiagaraEffectType>(Path);
						if (!EffectType)
						{
							Session.Log(FString::Printf(TEXT("[WARN] configure(\"system\") effect_type: could not load '%s'"), *Path));
						}
					}
					System->SetEffectType(EffectType);
				}

				sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties");
				if (Props.has_value())
				{
					FString ApplyErr;
					TArray<FString> Warnings;
					NeoLuaProperty::ApplyTable(System, Props.value(), ApplyErr, &Warnings);
					for (const FString& W : Warnings)
						Session.Log(FString::Printf(TEXT("[WARN] configure(\"system\") %s"), *W));
				}

				System->MarkPackageDirty();
				Session.Log(TEXT("[OK] configure(\"system\")"));
				return sol::make_object(Lua, true);
			}
			// ---- configure("renderer") ----
			else if (FType.Equals(TEXT("renderer"), ESearchCase::IgnoreCase))
			{
				// Accept emitter/index from Id table (2nd arg) or Params table (3rd arg)
				std::string EmName;
				int32 RIdx = 0;
				if (Id.is<sol::table>())
				{
					sol::table IdT = Id.as<sol::table>();
					EmName = IdT.get_or<std::string>("emitter", "");
					RIdx = IdT.get_or("index", 0);
				}
				if (EmName.empty()) EmName = Params.get_or<std::string>("emitter", "");
				if (Id.is<int>()) RIdx = Id.as<int>();
				else if (RIdx == 0) RIdx = Params.get_or("index", 0);
				FString FEmName = NeoLuaStr::ToFString(EmName);
				int32 EmIdx = FindEmitterIndex(System, FEmName);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"renderer\") -> emitter '%s' not found. Use list(\"emitters\") to see available."), *FEmName)); return sol::lua_nil; }

				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				FNiagaraRendererStorage Storage;
				FString ResolveError;
				if (!ResolveRendererStorage(Handle, FEmName, TEXT("configure(\"renderer\")"), Storage, ResolveError))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] %s"), *ResolveError));
					return sol::lua_nil;
				}

				if (RIdx < 0 || RIdx >= Storage.Renderers.Num()) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"renderer\") -> index %d out of range for emitter '%s' (valid: 0-%d). Use list(\"renderers\", {emitter=\"%s\"}) first."), RIdx, *FEmName, Storage.Renderers.Num() - 1, *FEmName)); return sol::lua_nil; }

				UNiagaraRendererProperties* Renderer = Storage.Renderers[RIdx];
				if (!Renderer) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"renderer\") -> renderer %d on emitter '%s' is null. Remove and recreate via remove(\"renderer\", {emitter=\"%s\", index=%d}) then add(\"renderer\", {emitter=\"%s\", type=\"...\"})."), RIdx, *FEmName, *FEmName, RIdx, *FEmName)); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNRenderer", "Configure Niagara Renderer"));
				if (Storage.OwnerObject)
				{
					Storage.OwnerObject->Modify();
				}
				Renderer->Modify();

				sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties");
				if (Props.has_value())
				{
					FString ApplyErr;
					TArray<FString> Warnings;
					NeoLuaProperty::ApplyTable(Renderer, Props.value(), ApplyErr, &Warnings);
					for (const FString& W : Warnings)
						Session.Log(FString::Printf(TEXT("[WARN] configure(\"renderer\") %s"), *W));
				}

				// Flush attribute-binding caches — see comment in add("renderer").
				NSAINiagaraLCD::RefreshRendererBindings(Renderer);

				if (Storage.OwnerObject)
				{
					Storage.OwnerObject->PostEditChange();
				}
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] configure(\"renderer\", index=%d%s)"), RIdx, Storage.bStateless ? TEXT(", mode=stateless") : TEXT("")));
				return sol::make_object(Lua, true);
			}
			// ---- configure("module") ----
			//
			// NOTE: The old `configure("reflected_properties", {target="...", properties=...})`
			// verb was removed. `configure("system"|"emitter"|"renderer"|"simulation_stage", id,
			// {properties=...})` now goes through the full LCD reflection walker which covers
			// everything the deprecated verb did, plus arrays/structs/enums/object-asset paths.
			// Supports advanced parameter modes: {mode="dynamic_input", script="...", parameters={...}}
			// {mode="linked", parameter="Particles.Position"}, {mode="hlsl", code="..."},
			// {mode="data_interface", type="SkeletalMesh"}, {mode="reset"}, or plain values (static)
			else if (FType.Equals(TEXT("module"), ESearchCase::IgnoreCase))
			{
				FString ModName = Id.is<std::string>() ? NeoLuaStr::ToFString(Id.as<std::string>()) : TEXT("");
				if (ModName.IsEmpty()) { Session.Log(TEXT("[FAIL] configure(\"module\") -> module name required (second argument)")); return sol::lua_nil; }

				std::string EmitterName = Params.get_or<std::string>("emitter", "");
				std::string StageStr = Params.get_or<std::string>("stage", "particle_update");
				std::string ScratchPad = Params.get_or<std::string>("scratch_pad", "");
				// Default true: any structural mode (HLSL, dynamic_input, linked, data_interface)
				// triggers a sync compile so signature/HLSL errors surface as [WARN]/[ERR] on the
				// SAME call instead of silently waiting for an explicit compile() later. Pass
				// {validate=false} to skip when batching many writes in a row.
				const bool bValidate = Params.get_or("validate", true);
				WarnUnconsumedKeys(Session, Params, { TEXT("emitter"), TEXT("stage"), TEXT("scratch_pad"), TEXT("parameters"), TEXT("validate") }, TEXT("configure(\"module\")"));

				UNiagaraNodeOutput* OutputNode = nullptr;
				UNiagaraScript*     Script     = nullptr;
				ENiagaraScriptUsage Usage      = ENiagaraScriptUsage::Module;
				int32 EmIdx = INDEX_NONE;

				if (!ScratchPad.empty())
				{
					FScratchPadTarget Target = ResolveScratchPadTarget(System, Session,
						NeoLuaStr::ToFString(ScratchPad), TEXT("configure(\"module\")"));
					if (!Target.OutputNode) return sol::lua_nil;
					OutputNode = Target.OutputNode;
					Script     = Target.Script;
				}
				else
				{
					Usage = ParseUsage(NeoLuaStr::ToFString(StageStr));
					const bool bSys = (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript);
					if (!bSys)
					{
						EmIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(EmitterName));
						if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"module\") -> emitter '%s' not found"), UTF8_TO_TCHAR(EmitterName.c_str()))); return sol::lua_nil; }
					}

					OutputNode = FindOutputNode(System, EmIdx, Usage);
					if (!OutputNode) { Session.Log(TEXT("[FAIL] configure(\"module\") -> no output node")); return sol::lua_nil; }

					// Resolve stage-owning script for RI param operations
					if (EmIdx != INDEX_NONE)
					{
						FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
						if (ED)
						{
							TArray<UNiagaraScript*> Scripts;
							ED->GetScripts(Scripts, false);
							for (UNiagaraScript* Sc : Scripts)
							{
								if (Sc && Sc->GetUsage() == Usage) { Script = Sc; break; }
							}
						}
					}
					else
					{
						Script = (Usage == ENiagaraScriptUsage::SystemSpawnScript) ? System->GetSystemSpawnScript() : System->GetSystemUpdateScript();
					}
				}

				UNiagaraNodeFunctionCall* ModNode = FindModuleByName(OutputNode, ModName);
				if (!ModNode) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"module\") -> '%s' not found"), *ModName)); return sol::lua_nil; }

				sol::optional<sol::table> ParamsOpt = Params.get<sol::optional<sol::table>>("parameters");
				if (!ParamsOpt.has_value()) { Session.Log(TEXT("[FAIL] configure(\"module\") -> parameters table required")); return sol::lua_nil; }

				const FNiagaraEmitterHandle* EmitterHandlePtr = (EmIdx != INDEX_NONE) ? &System->GetEmitterHandle(EmIdx) : nullptr;
				FNiagaraEmitterHandle DummyHandle;
				if (!EmitterHandlePtr) EmitterHandlePtr = &DummyHandle;

				TArray<FNiagaraVariable> AvailableInputs = DiscoverModuleInputs(ModNode, Script, EmitterHandlePtr, Usage);

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNModule", "Configure Niagara Module"));
				int32 SetCount = 0;
				bool bGraphChanged = false; // Track if graph-structural changes happened (advanced modes)
				TArray<FString> Errors;
				// Track HLSL nodes we author so post-compile event filtering can pin warnings
				// to the writes that caused them (Niagara compile events carry NodeGuid/StackGuids).
				TArray<FGuid> WrittenHlslNodeGuids;
				bool bWroteHlsl = false;

				for (auto& kv : ParamsOpt.value())
				{
					if (!kv.first.is<std::string>()) continue;
					FString ParamName = NeoLuaStr::ToFString(kv.first.as<std::string>());

					// Find matching input
					FNiagaraVariable* MatchedInput = nullptr;
					for (FNiagaraVariable& Input : AvailableInputs)
					{
						FString InputName = Input.GetName().ToString();
						FString ShortName = InputName;
						if (ShortName.Contains(TEXT(".")))
							ShortName = ShortName.RightChop(ShortName.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);
						if (InputName.Equals(ParamName, ESearchCase::IgnoreCase) ||
							ShortName.Equals(ParamName, ESearchCase::IgnoreCase) ||
							InputName.Equals(TEXT("Module.") + ParamName, ESearchCase::IgnoreCase))
						{
							MatchedInput = &Input;
							break;
						}
					}

					if (!MatchedInput)
					{
						Errors.Add(FString::Printf(TEXT("'%s' not found"), *ParamName));
						continue;
					}

					// Normalise the matched input name to canonical "Module.<short>" form before
					// building the parameter handle. DiscoverModuleInputs returns inputs from four
					// sources (GetStackFunctionInputs, pin scan, function-script vars, RI store),
					// so MatchedInput->GetName() may be plain "SpawnRate" (from a pin) instead of
					// "Module.SpawnRate". Without this, CreateAliasedModuleParameterHandle produces
					// a non-canonical alias and SetParameterData(bAdd=true) silently creates an
					// orphan RI param at the wrong name — the original keeps its default value
					// and the configure call reports success while nothing actually changes.
					FName CanonicalInputName;
					{
						FString Raw = MatchedInput->GetName().ToString();
						FString Short = Raw;
						int32 LastDot = INDEX_NONE;
						if (Short.FindLastChar(TEXT('.'), LastDot))
							Short = Short.RightChop(LastDot + 1);
						CanonicalInputName = FName(*(FString(TEXT("Module.")) + Short));
					}

					// Check if value is a table with "mode" key (advanced mode)
					bool bAdvanced = false;
					if (kv.second.is<sol::table>())
					{
						sol::table ValTable = kv.second.as<sol::table>();
						sol::optional<std::string> ModeOpt = ValTable.get<sol::optional<std::string>>("mode");
						if (ModeOpt.has_value())
						{
							bAdvanced = true;
							bGraphChanged = true; // Advanced modes modify graph nodes
							FString Mode = NeoLuaStr::ToFStringOpt(ModeOpt).ToLower();

							FNiagaraTypeDefinition InputType = MatchedInput->GetType();
							FNiagaraParameterHandle InputHandle(CanonicalInputName);
							FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModNode);
							FNiagaraVariable AliasedVar(InputType, AliasedHandle.GetParameterHandleString());

							UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
								*ModNode, AliasedHandle, InputType, FGuid(), FGuid());

							if (!IsOverridePinOnParameterMapSet(OverridePin) && Mode != TEXT("static"))
							{
								sol::object StaticValue = ValTable.get<sol::object>("value");
								if (!StaticValue.valid() || StaticValue.is<sol::lua_nil_t>())
								{
									StaticValue = ValTable.get<sol::object>("Value");
								}
								if (!StaticValue.valid() || StaticValue.is<sol::lua_nil_t>())
								{
									FString DefaultHint = OverridePin.DefaultValue.IsEmpty() ? TEXT("(no default)") : OverridePin.DefaultValue;
									FString TypeHint = OverridePin.PinType.PinCategory.ToString();
									Errors.Add(FString::Printf(TEXT("'%s': parameter is a static switch (type=%s, current=%s) - use {value=\"...\"} instead of mode='%s'"), *ParamName, *TypeHint, *DefaultHint, *Mode));
									continue;
								}

								TArray<uint8> StaticBytes;
								FString StaticErr;
								if (!NSAINiagaraLCD::SerializeSolValueToTypedBytes(StaticValue, InputType, StaticBytes, StaticErr))
								{
									Errors.Add(FString::Printf(TEXT("'%s': %s"), *ParamName, *StaticErr));
									continue;
								}

								FString NewDefault;
								if (!TypedBytesToPinDefault(StaticBytes, InputType, Lua, NewDefault))
								{
									Errors.Add(FString::Printf(TEXT("'%s': could not convert value to static-switch pin default"), *ParamName));
									continue;
								}

								if (OverridePin.LinkedTo.Num() > 0) CleanupOverridePinNodes(OverridePin);
								if (Script) RemoveRapidIterationParam(System, *EmitterHandlePtr, Script, AliasedVar);
								OverridePin.DefaultValue = NewDefault;
								if (UEdGraphNode* OwningNode = OverridePin.GetOwningNode())
									OwningNode->PinDefaultValueChanged(&OverridePin);
								SetCount++;
								bGraphChanged = true;
								continue;
							}

							// Static switch pins live on the FunctionCall node, not the ParameterMapSet.
							// DI/dynamic/linked/curve modes require ParameterMapSet — skip with error if static switch.
							if (!IsOverridePinOnParameterMapSet(OverridePin) && Mode != TEXT("static"))
							{
								// For static switch inputs, set the pin default value directly
								if (!ValTable.get_or<std::string>("value", "").empty())
								{
									OverridePin.DefaultValue = NeoLuaStr::ToFString(ValTable.get<std::string>("value"));
									SetCount++;
								}
								else
								{
									FString DefaultHint = OverridePin.DefaultValue.IsEmpty() ? TEXT("(no default)") : OverridePin.DefaultValue;
								FString TypeHint = OverridePin.PinType.PinCategory.ToString();
								Errors.Add(FString::Printf(TEXT("'%s': parameter is a static switch (type=%s, current=%s) — use {value=\"...\"} instead of mode='%s'"), *ParamName, *TypeHint, *DefaultHint, *Mode));
								}
								continue;
							}

							if (Mode == TEXT("dynamic_input") || Mode == TEXT("dynamic"))
							{
								std::string ScriptPath = ValTable.get_or<std::string>("script", "");
								if (ScriptPath.empty()) { Errors.Add(FString::Printf(TEXT("'%s': dynamic_input needs 'script'"), *ParamName)); continue; }

								UNiagaraScript* DIScript = NeoLuaAsset::Resolve<UNiagaraScript>(NeoLuaStr::ToFString(ScriptPath));
								if (!DIScript || DIScript->GetUsage() != ENiagaraScriptUsage::DynamicInput)
								{
									Errors.Add(FString::Printf(TEXT("'%s': DI script not found or wrong usage"), *ParamName));
									continue;
								}

								if (OverridePin.LinkedTo.Num() > 0) CleanupOverridePinNodes(OverridePin);
								if (Script) RemoveRapidIterationParam(System, *EmitterHandlePtr, Script, AliasedVar);
								OverridePin.DefaultValue = FString();

								UNiagaraNodeFunctionCall* DINode = nullptr;
								FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(OverridePin, DIScript, DINode);
								if (DINode) SetCount++;
								else Errors.Add(FString::Printf(TEXT("'%s': SetDynamicInput failed"), *ParamName));
							}
							else if (Mode == TEXT("linked") || Mode == TEXT("link"))
							{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
								std::string LinkedParam = ValTable.get_or<std::string>("parameter", "");
								if (LinkedParam.empty()) { Errors.Add(FString::Printf(TEXT("'%s': linked needs 'parameter'"), *ParamName)); continue; }

								if (OverridePin.LinkedTo.Num() > 0) CleanupOverridePinNodes(OverridePin);
								if (Script) RemoveRapidIterationParam(System, *EmitterHandlePtr, Script, AliasedVar);
								OverridePin.DefaultValue = FString();

								FNiagaraVariable LinkedVar(InputType, FName(UTF8_TO_TCHAR(LinkedParam.c_str())));
								TArray<FNiagaraVariable> ExposedVars;
								System->GetExposedParameters().GetUserParameters(ExposedVars);
								TSet<FNiagaraVariableBase> KnownParameters;
								for (const FNiagaraVariable& V : ExposedVars) KnownParameters.Add(V);
								KnownParameters.Add(FNiagaraVariableBase(LinkedVar.GetType(), LinkedVar.GetName()));

								FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(
									OverridePin, LinkedVar, KnownParameters, ENiagaraDefaultMode::FailIfPreviouslyNotSet);

								// Engine parity: register parameter in graph's known parameter list
								UEdGraphNode* LinkedOverrideNode = OverridePin.GetOwningNode();
								if (LinkedOverrideNode)
								{
									UNiagaraGraph* LinkedGraph = Cast<UNiagaraGraph>(LinkedOverrideNode->GetGraph());
									if (LinkedGraph && !GraphHasVariable(LinkedGraph, LinkedVar))
									{
										LinkedGraph->Modify();
										GraphAddParameter(LinkedGraph, LinkedVar);
									}
								}
								SetCount++;
#else
								Errors.Add(FString::Printf(TEXT("'%s': linked mode requires UE 5.6+"), *ParamName));
#endif
							}
							else if (Mode == TEXT("hlsl") || Mode == TEXT("custom_hlsl") || Mode == TEXT("expression"))
							{
								std::string Code = ValTable.get_or<std::string>("code", "");
								if (Code.empty()) { Errors.Add(FString::Printf(TEXT("'%s': hlsl needs 'code'"), *ParamName)); continue; }

								// Niagara dynamic-input HLSL takes an EXPRESSION, not a statement —
								// the engine wraps user code as `Out_X = (Type)(USER_CODE);`
								// (NiagaraHlslTranslator.cpp:8920). A top-level `return` ends up
								// inside parentheses and the VM compiler errors with "unexpected
								// RETURN". Warn the user before they hit that confusing message.
								// Heuristic: trim leading whitespace, check if it starts with `return`
								// followed by a non-identifier character. Catches the common mistake
								// without false-positives on identifiers like `returnValue`.
								{
									FString Trimmed = NeoLuaStr::ToFString(Code);
									Trimmed.TrimStartInline();
									if (Trimmed.StartsWith(TEXT("return"), ESearchCase::CaseSensitive))
									{
										const TCHAR Next = Trimmed.Len() > 6 ? Trimmed[6] : TEXT(' ');
										const bool bIsIdentifierChar = FChar::IsAlnum(Next) || Next == TEXT('_');
										if (!bIsIdentifierChar)
										{
											Session.Log(FString::Printf(TEXT(
												"[WARN] configure(\"module\", \"%s\") '%s': HLSL begins with `return` — Niagara dynamic-input HLSL is an EXPRESSION, not a statement (engine wraps it as `Out = (Type)(YOUR_CODE);`). Drop the `return` keyword and trailing `;`. e.g. `1.0 + sin(EngineTime)` not `return 1.0 + sin(EngineTime);`"),
												*ModName, *ParamName));
											Errors.Add(FString::Printf(TEXT("'%s': HLSL dynamic input must be an expression, not a return statement"), *ParamName));
											continue;
										}
									}
								}

								{
									FString Trimmed = NeoLuaStr::ToFString(Code);
									Trimmed.TrimEndInline();
									if (!Trimmed.IsEmpty())
									{
										const TCHAR Last = Trimmed[Trimmed.Len() - 1];
										const bool bEndsWithOperator =
											Last == TEXT('+') || Last == TEXT('-') || Last == TEXT('*') ||
											Last == TEXT('/') || Last == TEXT('%') || Last == TEXT('&') ||
											Last == TEXT('|') || Last == TEXT('^') || Last == TEXT('?') ||
											Last == TEXT(':') || Last == TEXT(',');
										if (bEndsWithOperator)
										{
											Errors.Add(FString::Printf(TEXT("'%s': HLSL dynamic input expression is incomplete (ends with '%c')"), *ParamName, Last));
											continue;
										}
									}
								}

								if (OverridePin.LinkedTo.Num() > 0) CleanupOverridePinNodes(OverridePin);
								if (Script) RemoveRapidIterationParam(System, *EmitterHandlePtr, Script, AliasedVar);
								OverridePin.DefaultValue = FString();

								UEdGraphNode* OverrideNode = OverridePin.GetOwningNode();
								UEdGraph* Graph = OverrideNode ? OverrideNode->GetGraph() : nullptr;
								if (!Graph) { Errors.Add(FString::Printf(TEXT("'%s': no graph for HLSL node"), *ParamName)); continue; }
								Graph->Modify();

								// Engine parity: create custom HLSL dynamic input node
								FGraphNodeCreator<UNiagaraNodeCustomHlsl> NodeCreator(*Graph);
								UNiagaraNodeCustomHlsl* HlslNode = NodeCreator.CreateNode();
								InitCustomHlslAsDynamicInput(HlslNode, InputType);
								NodeCreator.Finalize();

								// Inherit enabled state from override node (engine: SetCustomExpressionForFunctionInput)
								HlslNode->SetEnabledState(OverrideNode->GetDesiredEnabledState(), OverrideNode->HasUserSetTheEnabledState());

								// Connect param map
								UEdGraphPin* HlslInputMap = GetParamMapInputPin(HlslNode);
								UEdGraphPin* OverrideNodeInputMap = GetParamMapInputPin(OverrideNode);
								if (HlslInputMap && OverrideNodeInputMap && OverrideNodeInputMap->LinkedTo.Num() > 0)
								{
									UEdGraphPin* PrevOut = OverrideNodeInputMap->LinkedTo[0];
									HlslInputMap->MakeLinkTo(PrevOut);
									HlslNode->PinConnectionListChanged(HlslInputMap);
									if (UEdGraphNode* PrevOwner = PrevOut->GetOwningNode()) PrevOwner->PinConnectionListChanged(PrevOut);
								}

								// Connect typed output to override pin
								FPinCollectorArray HlslOutputPins;
								HlslNode->GetOutputPins(HlslOutputPins);
								for (UEdGraphPin* OutPin : HlslOutputPins)
								{
									FNiagaraTypeDefinition PinType = GetDefault<UEdGraphSchema_Niagara>()->PinToTypeDefinition(OutPin);
									if (PinType != FNiagaraTypeDefinition::GetParameterMapDef())
									{
										OutPin->MakeLinkTo(&OverridePin);
										HlslNode->PinConnectionListChanged(OutPin);
										if (UEdGraphNode* OverrideOwner = OverridePin.GetOwningNode()) OverrideOwner->PinConnectionListChanged(&OverridePin);
										break;
									}
								}

								// Set HLSL code via reflection (SetCustomHlsl not exported from MinimalAPI class)
								SetCustomHlslText(HlslNode, NeoLuaStr::ToFString(Code));
								// Record the new node's GUID so we can correlate post-compile events to
								// THIS write — the Niagara compiler tags FNiagaraCompileEvent.NodeGuid /
								// StackGuids with the offending node, so a contains-check below filters
								// noise from unrelated emitters in the same system.
								WrittenHlslNodeGuids.Add(HlslNode->NodeGuid);
								bWroteHlsl = true;
								SetCount++;
							}
							else if (Mode == TEXT("data_interface") || Mode == TEXT("di"))
							{
								std::string DIType = ValTable.get_or<std::string>("type", "");
								if (DIType.empty()) { Errors.Add(FString::Printf(TEXT("'%s': data_interface needs 'type'"), *ParamName)); continue; }

								if (!InputType.IsDataInterface())
								{
									Errors.Add(FString::Printf(TEXT("'%s': not a data interface type"), *ParamName));
									continue;
								}

								FString FullClassName = FString::Printf(TEXT("NiagaraDataInterface%s"), UTF8_TO_TCHAR(DIType.c_str()));
								UClass* DIClass = FindFirstObject<UClass>(*FullClassName, EFindFirstObjectOptions::None);
								if (!DIClass) DIClass = FindFirstObject<UClass>(*NeoLuaStr::ToFString(DIType), EFindFirstObjectOptions::None);
								if (!DIClass || !DIClass->IsChildOf(UNiagaraDataInterface::StaticClass()))
								{
									Errors.Add(FString::Printf(TEXT("'%s': DI class '%s' not found"), *ParamName, UTF8_TO_TCHAR(DIType.c_str())));
									continue;
								}

								if (OverridePin.LinkedTo.Num() > 0) CleanupOverridePinNodes(OverridePin);
								OverridePin.DefaultValue = FString();
								if (Script) RemoveRapidIterationParam(System, *EmitterHandlePtr, Script, AliasedVar);

								UNiagaraDataInterface* NewDI = nullptr;
								FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(
									OverridePin, DIClass, AliasedHandle.GetParameterHandleString().ToString(), NewDI);
								if (NewDI)
								{
									// Apply DI properties via the full LCD walker (handles object
									// asset paths, nested structs, arrays — previously silent-failed).
									sol::optional<sol::table> DIProps = ValTable.get<sol::optional<sol::table>>("properties");
									if (DIProps.has_value())
									{
										FString ApplyErr;
										TArray<FString> Warnings;
										NeoLuaProperty::ApplyTable(NewDI, DIProps.value(), ApplyErr, &Warnings);
										for (const FString& W : Warnings)
											Session.Log(FString::Printf(TEXT("[WARN] DI '%s' %s"), *ParamName, *W));
									}
									SetCount++;
								}
								else
								{
									Errors.Add(FString::Printf(TEXT("'%s': SetDataInterface failed"), *ParamName));
								}
							}
							// ── Object-asset mode: assign a UObject asset (Texture, Mesh, Material, ...) ──
							// Engine helper SetObjectAssetValueForFunctionInput wires the pin to an input
							// node whose data value holds the loaded asset reference.
							else if (Mode == TEXT("object_asset") || Mode == TEXT("asset") || Mode == TEXT("object"))
							{
								std::string AssetPath = ValTable.get_or<std::string>("path", "");
								if (AssetPath.empty())
								{
									Errors.Add(FString::Printf(TEXT("'%s': object_asset mode needs 'path'"), *ParamName));
									continue;
								}

								UClass* ExpectedClass = InputType.GetClass();
								if (!ExpectedClass)
								{
									Errors.Add(FString::Printf(TEXT("'%s': input type '%s' is not a UObject-backed type"),
										*ParamName, *InputType.GetName()));
									continue;
								}

								const FString FAssetPath = NeoLuaStr::ToFString(AssetPath);
								UObject* Asset = NeoLuaAsset::Resolve<UObject>(FAssetPath);
								if (!Asset)
								{
									// Retry with class-qualified form ("Texture2D'/Game/T_Foo.T_Foo'")
									const FString Qualified = FString::Printf(TEXT("%s'%s'"), *ExpectedClass->GetName(), *FAssetPath);
									Asset = LoadObject<UObject>(nullptr, *Qualified);
								}
								if (!Asset)
								{
									Errors.Add(FString::Printf(TEXT("'%s': could not load '%s'"), *ParamName, *FAssetPath));
									continue;
								}
								if (!Asset->IsA(ExpectedClass))
								{
									Errors.Add(FString::Printf(TEXT("'%s': '%s' is a %s, not %s"),
										*ParamName, *FAssetPath, *Asset->GetClass()->GetName(), *ExpectedClass->GetName()));
									continue;
								}

								if (OverridePin.LinkedTo.Num() > 0) CleanupOverridePinNodes(OverridePin);
								if (Script) RemoveRapidIterationParam(System, *EmitterHandlePtr, Script, AliasedVar);
								OverridePin.DefaultValue = FString();

								FNiagaraStackGraphUtilities::SetObjectAssetValueForFunctionInput(
									OverridePin, ExpectedClass, AliasedHandle.GetParameterHandleString().ToString(), Asset);
								SetCount++;
							}
							// ── Unified curve DI modes ─────────────────────────────────────
							// LCD: one reflection-driven walker covers all 5 UNiagaraDataInterfaceCurveBase
							// subclasses. Field names on the DI drive the per-key value extraction:
							//   Curve                 ← keys[i].value
							//   X/Y/Z/W Curve         ← keys[i].{x,y,z,w}
							//   Red/Green/Blue/Alpha  ← keys[i].color.{r,g,b,a} or flat {r,g,b,a}
							// keys[i].interp = "linear"|"constant"|"cubic" is also honored.
							else if (Mode == TEXT("curve")          || Mode == TEXT("color_curve")     ||
									 Mode == TEXT("vector_curve")   || Mode == TEXT("vector2d_curve")  ||
									 Mode == TEXT("vector4_curve"))
							{
								sol::optional<sol::table> KeysOpt = ValTable.get<sol::optional<sol::table>>("keys");
								if (!KeysOpt.has_value())
								{
									Errors.Add(FString::Printf(TEXT("'%s': %s mode needs 'keys' array"), *ParamName, *Mode));
									continue;
								}

								UClass* DIClass = nullptr;
								if      (Mode == TEXT("curve"))          DIClass = UNiagaraDataInterfaceCurve::StaticClass();
								else if (Mode == TEXT("color_curve"))    DIClass = UNiagaraDataInterfaceColorCurve::StaticClass();
								else if (Mode == TEXT("vector_curve"))   DIClass = UNiagaraDataInterfaceVectorCurve::StaticClass();
								else if (Mode == TEXT("vector2d_curve")) DIClass = UNiagaraDataInterfaceVector2DCurve::StaticClass();
								else if (Mode == TEXT("vector4_curve")) DIClass = UNiagaraDataInterfaceVector4Curve::StaticClass();

								if (OverridePin.LinkedTo.Num() > 0) CleanupOverridePinNodes(OverridePin);
								if (Script) RemoveRapidIterationParam(System, *EmitterHandlePtr, Script, AliasedVar);
								OverridePin.DefaultValue = FString();

								UNiagaraDataInterface* NewDI = nullptr;
								FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(
									OverridePin, DIClass, AliasedHandle.GetParameterHandleString().ToString(), NewDI);
								if (!NewDI)
								{
									Errors.Add(FString::Printf(TEXT("'%s': SetDataInterface (%s) failed"), *ParamName, *Mode));
									continue;
								}

								FString CurveErr;
								if (!NSAINiagaraLCD::ApplyKeysToCurveDI(NewDI, KeysOpt.value(), CurveErr))
								{
									Errors.Add(FString::Printf(TEXT("'%s': %s"), *ParamName, *CurveErr));
									continue;
								}
								SetCount++;
							}
							else if (Mode == TEXT("reset") || Mode == TEXT("default"))
							{
								if (OverridePin.LinkedTo.Num() > 0) CleanupOverridePinNodes(OverridePin);
								if (Script) RemoveRapidIterationParam(System, *EmitterHandlePtr, Script, AliasedVar);
								OverridePin.DefaultValue = FString();
								SetCount++;
							}
							else
							{
								Errors.Add(FString::Printf(TEXT("'%s': unknown mode '%s'"), *ParamName, *Mode));
							}
						}
					}

					// Static value mode (plain number, string, bool, table).
					// LCD: serialize Lua value to typed bytes once; write to every affected RI store
					// via FNiagaraParameterStore::SetParameterData. Falls back to pin-default for
					// non-RI-capable types (raw strings for ImportText-only structs).
					if (!bAdvanced)
					{
						FNiagaraTypeDefinition InputType = MatchedInput->GetType();
						FNiagaraParameterHandle InputHandle(CanonicalInputName);
						FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModNode);
						FNiagaraVariable AliasedVar(InputType, AliasedHandle.GetParameterHandleString());

						if (UEdGraphPin* DirectPin = FindDirectModuleInputPin(ModNode, ParamName, CanonicalInputName))
						{
							TArray<uint8> PinBytes;
							FString PinErr;
							if (!NSAINiagaraLCD::SerializeSolValueToTypedBytes(kv.second, InputType, PinBytes, PinErr))
							{
								Errors.Add(FString::Printf(TEXT("'%s': %s"), *ParamName, *PinErr));
								continue;
							}

							FString NewDefault;
							if (!TypedBytesToPinDefault(PinBytes, InputType, Lua, NewDefault))
							{
								Errors.Add(FString::Printf(TEXT("'%s': could not convert value to pin default"), *ParamName));
								continue;
							}

							if (DirectPin->LinkedTo.Num() > 0) CleanupOverridePinNodes(*DirectPin);
							if (Script) RemoveRapidIterationParam(System, *EmitterHandlePtr, Script, AliasedVar);
							DirectPin->DefaultValue = NewDefault;
							if (UEdGraphNode* OwningNode = DirectPin->GetOwningNode())
								OwningNode->PinDefaultValueChanged(DirectPin);
							SetCount++;
							bGraphChanged = true;
							continue;
						}

						const TCHAR* EmNameForRI = nullptr;
						FString UniqueEmName;
						if (EmitterHandlePtr->IsValid() && EmitterHandlePtr->GetInstance().Emitter)
						{
							UniqueEmName = EmitterHandlePtr->GetInstance().Emitter->GetUniqueEmitterName();
							if (!UniqueEmName.IsEmpty()) EmNameForRI = *UniqueEmName;
						}

						TArray<uint8> Bytes;
						FString SerErr;
						const bool bSerialized = NSAINiagaraLCD::SerializeSolValueToTypedBytes(kv.second, InputType, Bytes, SerErr);

						bool bSet = false;
						if (bSerialized)
						{
							// Engine parity: write the RI parameter unconditionally when serialization
							// succeeded. The previous gate `&& System->ShouldUseRapidIterationParameters()`
							// was wrong — `ShouldUseRI` controls whether the COMPILED script *consumes*
							// the RI store at runtime, not whether we should write to it. With RI baked
							// out (the default for systems where bBakeOutRapidIterationOnCook=true and
							// bCompileForEdit=false), the engine's stack input handler still writes to
							// RI and then explicitly requests a recompile so the new value takes effect.
							// See NiagaraStackFunctionInput.cpp:2200-2235.
							//
							// bAdd=true so static-switch-gated inputs (e.g. Lifetime behind LifetimeMode)
							// get created automatically the first time they're written.
							TArray<UNiagaraScript*> AffectedScripts = FindAffectedScripts(System, *EmitterHandlePtr, Usage);
							for (UNiagaraScript* AS : AffectedScripts)
							{
								if (!AS) continue;
								FNiagaraVariable RIVar = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
									AliasedVar, EmNameForRI, AS->GetUsage());
								AS->Modify();
								AS->RapidIterationParameters.SetParameterData(Bytes.GetData(), RIVar, /*bAdd=*/true);
							}
							// If RI is baked out, the runtime ignores the RI store — request a recompile
							// so the new value gets baked into the next compiled script.
							if (!System->ShouldUseRapidIterationParameters())
							{
								System->RequestCompile(false);
							}
							bSet = true;
						}
						else if (InputType.IsEnum() || InputType == FNiagaraTypeDefinition::GetBoolDef())
						{
							Errors.Add(FString::Printf(TEXT("'%s': %s"), *ParamName, *SerErr));
						}
						else if (!kv.second.is<sol::table>())
						{
							// Fall back to pin default value for non-RI types (string/enum pins, etc.)
							UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
								*ModNode, AliasedHandle, InputType, FGuid(), FGuid());
							if (kv.second.is<double>())
								OverridePin.DefaultValue = FString::SanitizeFloat(kv.second.as<double>());
							else if (kv.second.is<std::string>())
								OverridePin.DefaultValue = NeoLuaStr::ToFString(kv.second.as<std::string>());
							else if (kv.second.is<bool>())
								OverridePin.DefaultValue = kv.second.as<bool>() ? TEXT("true") : TEXT("false");
							if (UEdGraphNode* OwningNode = OverridePin.GetOwningNode())
								OwningNode->PinDefaultValueChanged(&OverridePin);
							bSet = true;
						}

						if (bSet) SetCount++;
						else Errors.Add(FString::Printf(TEXT("'%s': %s"), *ParamName, *SerErr));
					}
				}

				// Only notify graph changed for structural modifications (advanced modes like
				// dynamic_input, linked, hlsl, data_interface, curve). RI param writes must NOT
				// trigger graph recompile — the engine rebuilds the RI store from defaults on
				// recompile, which would overwrite our changes. (Matches engine behavior in
				// NiagaraStackFunctionInput.cpp which only calls NotifyGraphNeedsRecompile for
				// static/switch inputs, not regular RI params.)
				if (bGraphChanged)
				{
					UNiagaraGraph* ModGraph = Cast<UNiagaraGraph>(OutputNode->GetGraph());
					if (ModGraph) ModGraph->NotifyGraphChanged();
				}

				// Scratch pad scripts are stand-alone compiled assets — if we set anything
				// (RI params or graph-structural modes), a recompile is required for the
				// change to take effect.
				const bool bScratchPadTarget = !ScratchPad.empty();
				if (bScratchPadTarget && Script && (SetCount > 0 || bGraphChanged))
				{
					Script->RequestCompile(FGuid());
				}

				// HLSL validation: any custom-expression write may carry a signature/return-type
				// mismatch (e.g. user `return float3(1,1,1)` for a Float input) that the engine
				// only surfaces during shader compilation. Without this hook, configure()
				// reports [OK] and the failure only emerges on an explicit compile() — agents
				// loop on broken systems for free. Pump the same compile + event-collection
				// path as the compile() verb, then surface anything tagged to a node we just
				// authored. Skip when {validate=false} or no HLSL writes happened.
				TArray<FString> HlslDiagnostics;
				if (bWroteHlsl && bValidate)
				{
					System->RequestCompile(false);
					System->WaitForCompilationComplete();

					TArray<UNiagaraScript*> AllScripts;
					AllScripts.Add(System->GetSystemSpawnScript());
					AllScripts.Add(System->GetSystemUpdateScript());
					for (int32 i = 0; i < System->GetNumEmitters(); ++i)
					{
						if (FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(i).GetEmitterData())
						{
							TArray<UNiagaraScript*> EmitterScripts;
							ED->GetScripts(EmitterScripts, false);
							AllScripts.Append(EmitterScripts);
						}
					}

					for (UNiagaraScript* Sc : AllScripts)
					{
						if (!Sc) continue;
						const TArray<FNiagaraCompileEvent>& Events = Sc->GetVMExecutableData().LastCompileEvents;
						for (const FNiagaraCompileEvent& Evt : Events)
						{
							if (Evt.Severity < FNiagaraCompileEventSeverity::Warning) continue;

							// Tie the event to one of our HLSL writes when the compiler reported a
							// node guid (or one of the surrounding stack guids matches). Errors we
							// surface unconditionally — a fresh shader-compile error on the same
							// turn as an HLSL write is almost always caused by it, and missing one
							// silently is the bug we're trying to kill.
							const bool bMatchesOurHlsl =
								WrittenHlslNodeGuids.Contains(Evt.NodeGuid)
								|| Evt.StackGuids.ContainsByPredicate([&WrittenHlslNodeGuids](const FGuid& G) {
									return WrittenHlslNodeGuids.Contains(G);
								});
							const bool bIsError = Evt.Severity == FNiagaraCompileEventSeverity::Error;
							if (!bIsError && !bMatchesOurHlsl) continue;

							const TCHAR* Tag = bIsError ? TEXT("ERR") : TEXT("WARN");
							const TCHAR* Pin = bMatchesOurHlsl ? TEXT(" [hlsl]") : TEXT("");
							HlslDiagnostics.Add(FString::Printf(TEXT("%s%s %s: %s"),
								Tag, Pin, *Sc->GetName(), *Evt.Message));
						}
					}
				}
				for (const FString& Diag : HlslDiagnostics)
					Session.Log(FString::Printf(TEXT("[WARN] configure(\"module\", \"%s\") %s"), *ModName, *Diag));

				System->MarkPackageDirty();
				if (Errors.Num() > 0)
				{
					Session.Log(FString::Printf(TEXT("[OK] configure(\"module\", \"%s\") -> %d set, errors: %s"), *ModName, SetCount, *FString::Join(Errors, TEXT("; "))));
				}
				else if (HlslDiagnostics.Num() > 0)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"module\", \"%s\") -> %d set, %d compile diagnostic(s) — see [WARN]s above; pass {validate=false} to skip post-write compile"), *ModName, SetCount, HlslDiagnostics.Num()));
				}
				else
				{
					Session.Log(FString::Printf(TEXT("[OK] configure(\"module\", \"%s\") -> %d set"), *ModName, SetCount));
				}
				return sol::make_object(Lua, SetCount > 0 && HlslDiagnostics.Num() == 0);
				}

			// ---- configure("spawn_info") ----
			// Apply LCD properties to an existing spawn info entry by {emitter, index}.
			else if (FType.Equals(TEXT("spawn_info"), ESearchCase::IgnoreCase))
			{
				FString EmName;
				int32 Index = -1;
				if (Id.is<sol::table>())
				{
					sol::table T = Id.as<sol::table>();
					EmName = NeoLuaStr::ToFString(T.get_or<std::string>("emitter", ""));
					Index = T.get_or("index", -1);
				}
				if (EmName.IsEmpty()) EmName = NeoLuaStr::ToFString(Params.get_or<std::string>("emitter", ""));
				if (Index < 0) Index = Params.get_or("index", -1);

				if (EmName.IsEmpty() || Index < 0) { Session.Log(TEXT("[FAIL] configure(\"spawn_info\") -> {emitter, index} required")); return sol::lua_nil; }

				int32 EmIdx = FindEmitterIndex(System, EmName);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"spawn_info\") -> emitter '%s' not found"), *EmName)); return sol::lua_nil; }

				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				if (Handle.GetEmitterMode() != ENiagaraEmitterMode::Stateless) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"spawn_info\") -> '%s' not stateless"), *EmName)); return sol::lua_nil; }

				UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
				if (!SLE) { Session.Log(TEXT("[FAIL] configure(\"spawn_info\") -> no stateless emitter")); return sol::lua_nil; }

				FArrayProperty* ArrProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("SpawnInfos")));
				if (!ArrProp) { Session.Log(TEXT("[FAIL] configure(\"spawn_info\") -> SpawnInfos UPROPERTY not found")); return sol::lua_nil; }
				FStructProperty* InnerProp = CastField<FStructProperty>(ArrProp->Inner);
				if (!InnerProp) { Session.Log(TEXT("[FAIL] configure(\"spawn_info\") -> inner not a struct")); return sol::lua_nil; }
				FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
				if (Index >= Helper.Num()) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"spawn_info\") -> index %d out of range (%d)"), Index, Helper.Num())); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNSpawnInfo", "Configure Stateless Spawn Info"));
				System->Modify();
				((UObject*)SLE)->Modify();

				sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties");
				if (Props.has_value())
				{
					FString ApplyErr;
					TArray<FString> Warnings;
					NeoLuaProperty::ApplyTableToStruct(InnerProp->Struct, Helper.GetRawPtr(Index),
						(UObject*)SLE, Props.value(), ApplyErr, &Warnings);
					for (const FString& W : Warnings)
						Session.Log(FString::Printf(TEXT("[WARN] configure(\"spawn_info\") %s"), *W));
				}

				((UObject*)SLE)->PostEditChange();
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] configure(\"spawn_info\", emitter=\"%s\", index=%d)"), *EmName, Index));
				return sol::make_object(Lua, true);
			}

			// ---- configure("stateless_emitter") ----
			// Applies LCD properties directly to the UNiagaraStatelessEmitter UObject.
			// Covers FixedBounds, RandomSeed, bDeterministic, bForceOutputAllAttributes,
			// AllowedFeatureMask (bitmask enum), and the EmitterState nested struct
			// (LoopDuration, LoopCount, bInfinite, InactiveBehavior, CompletionBehavior, etc.).
			else if (FType.Equals(TEXT("stateless_emitter"), ESearchCase::IgnoreCase))
			{
				FString EmName = Id.is<std::string>() ? NeoLuaStr::ToFString(Id.as<std::string>()) : TEXT("");
				if (EmName.IsEmpty()) EmName = NeoLuaStr::ToFString(Params.get_or<std::string>("emitter", ""));
				if (EmName.IsEmpty()) { Session.Log(TEXT("[FAIL] configure(\"stateless_emitter\") -> emitter name required")); return sol::lua_nil; }

				int32 EmIdx = FindEmitterIndex(System, EmName);
				if (EmIdx == INDEX_NONE)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"stateless_emitter\") -> '%s' not found"), *EmName));
					return sol::lua_nil;
				}

				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				if (Handle.GetEmitterMode() != ENiagaraEmitterMode::Stateless)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"stateless_emitter\") -> '%s' is not in stateless mode"), *EmName));
					return sol::lua_nil;
				}

				UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
				if (!SLE) { Session.Log(TEXT("[FAIL] configure(\"stateless_emitter\") -> no stateless emitter")); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNStatelessEmitter", "Configure Stateless Emitter"));
				System->Modify();

				sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties");
					if (Props.has_value())
					{
						FString ApplyErr;
						TArray<FString> Warnings;
						NeoLuaProperty::ApplyTable((UObject*)SLE, Props.value(), ApplyErr, &Warnings);
						for (const FString& W : Warnings)
							Session.Log(FString::Printf(TEXT("[WARN] configure(\"stateless_emitter\") %s"), *W));
					}

					((UObject*)SLE)->PostEditChange();
					System->MarkPackageDirty();
					Session.Log(FString::Printf(TEXT("[OK] configure(\"stateless_emitter\", \"%s\")"), *EmName));
					return sol::make_object(Lua, true);
			}

			// ---- configure("stateless_module") ----
			// Apply property updates to an existing stateless module by {emitter, index}.
			// All UPROPERTYs on the module class go through ApplyTable (distribution ranges,
			// enabled flag, binding values, etc.).
			else if (FType.Equals(TEXT("stateless_module"), ESearchCase::IgnoreCase))
			{
				FString EmName;
				int32 Index = -1;
				if (Id.is<sol::table>())
				{
					sol::table T = Id.as<sol::table>();
					EmName = NeoLuaStr::ToFString(T.get_or<std::string>("emitter", ""));
					Index = T.get_or("index", -1);
				}
				if (EmName.IsEmpty()) EmName = NeoLuaStr::ToFString(Params.get_or<std::string>("emitter", ""));
				if (Index < 0) Index = Params.get_or("index", -1);

				if (EmName.IsEmpty() || Index < 0)
				{
					Session.Log(TEXT("[FAIL] configure(\"stateless_module\") -> {emitter, index} required"));
					return sol::lua_nil;
				}

				int32 EmIdx = FindEmitterIndex(System, EmName);
				if (EmIdx == INDEX_NONE)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"stateless_module\") -> emitter '%s' not found"), *EmName));
					return sol::lua_nil;
				}

				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				if (Handle.GetEmitterMode() != ENiagaraEmitterMode::Stateless)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"stateless_module\") -> emitter '%s' is not in stateless mode"), *EmName));
					return sol::lua_nil;
				}

				UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
				if (!SLE) { Session.Log(TEXT("[FAIL] configure(\"stateless_module\") -> no stateless emitter")); return sol::lua_nil; }

				FArrayProperty* ModProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("Modules")));
				if (!ModProp) { Session.Log(TEXT("[FAIL] configure(\"stateless_module\") -> Modules UPROPERTY not found")); return sol::lua_nil; }
				FScriptArrayHelper Helper(ModProp, ModProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
				if (Index >= Helper.Num())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"stateless_module\") -> index %d out of range (%d)"), Index, Helper.Num()));
					return sol::lua_nil;
				}
				FObjectPropertyBase* InnerProp = CastField<FObjectPropertyBase>(ModProp->Inner);
				UObject* Mod = InnerProp ? InnerProp->GetObjectPropertyValue(Helper.GetRawPtr(Index)) : nullptr;
				if (!Mod) { Session.Log(TEXT("[FAIL] configure(\"stateless_module\") -> module instance is null")); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNStatelessMod", "Configure Stateless Module"));
				System->Modify();

				sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties");
					if (Props.has_value())
					{
						FString ApplyErr;
						TArray<FString> Warnings;
						NeoLuaProperty::ApplyTable(Mod, Props.value(), ApplyErr, &Warnings);
						for (const FString& W : Warnings)
							Session.Log(FString::Printf(TEXT("[WARN] configure(\"stateless_module\") %s"), *W));
					}

					Mod->PostEditChange();
					((UObject*)SLE)->PostEditChange();
					System->MarkPackageDirty();
					Session.Log(FString::Printf(TEXT("[OK] configure(\"stateless_module\", emitter=\"%s\", index=%d, class=\"%s\")"),
					*EmName, Index, *Mod->GetClass()->GetName()));
				return sol::make_object(Lua, true);
			}

			// ---- configure("emitter_mode") ----
			else if (FType.Equals(TEXT("emitter_mode"), ESearchCase::IgnoreCase))
			{
				FString EmName = Id.is<std::string>() ? NeoLuaStr::ToFString(Id.as<std::string>()) : TEXT("");
				if (EmName.IsEmpty()) { Session.Log(TEXT("[FAIL] configure(\"emitter_mode\") -> emitter name required")); return sol::lua_nil; }
				int32 EmIdx = FindEmitterIndex(System, EmName);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"emitter_mode\") -> emitter '%s' not found"), *EmName)); return sol::lua_nil; }

				std::string ModeStr = Params.get_or<std::string>("mode", "standard");
				FString FMode = NeoLuaStr::ToFString(ModeStr);

				ENiagaraEmitterMode TargetMode = ENiagaraEmitterMode::Standard;
				if (FMode.Contains(TEXT("stateless"), ESearchCase::IgnoreCase))
					TargetMode = ENiagaraEmitterMode::Stateless;

				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				if (Handle.GetEmitterMode() == TargetMode)
				{
					Session.Log(FString::Printf(TEXT("[OK] configure(\"emitter_mode\", \"%s\") -> already in '%s' mode"), *EmName, *FMode));
					return sol::make_object(Lua, true);
				}

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNEmitterMode", "Configure Niagara Emitter Mode"));
				System->Modify();
				Handle.SetEmitterMode(*System, TargetMode);
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] configure(\"emitter_mode\", \"%s\", mode=\"%s\")"), *EmName, *FMode));
				return sol::make_object(Lua, true);
			}

			Session.Log(FString::Printf(TEXT("[FAIL] configure(\"%s\") -> unknown type. Valid: module, emitter, emitter_mode, system, renderer, simulation_stage. For bulk UPROPERTY writes use configure(\"system\"|\"emitter\"|\"renderer\", id, {properties={...}})."), *FType));
			return sol::lua_nil;
		});

		// ==================================================================
		// enable_module({emitter, stage, module_name, enabled})
		// ==================================================================
		AssetObj.set_function("enable_module", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get_or<std::string>("emitter", "");
			std::string StageStr = Params.get_or<std::string>("stage", "particle_update");
			std::string ModName = Params.get_or<std::string>("module_name", "");
			bool bEnabled = Params.get_or("enabled", true);
			WarnUnconsumedKeys(Session, Params, { TEXT("emitter"), TEXT("stage"), TEXT("module_name"), TEXT("enabled") }, TEXT("enable_module"));

			if (ModName.empty()) { Session.Log(TEXT("[FAIL] enable_module -> module_name required")); return sol::lua_nil; }

			ENiagaraScriptUsage Usage = ParseUsage(NeoLuaStr::ToFString(StageStr));
			int32 EmIdx = INDEX_NONE;
			bool bSys = (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript);
			if (!bSys) EmIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(EmitterName));

			UNiagaraNodeOutput* OutputNode = FindOutputNode(System, EmIdx, Usage);
			if (!OutputNode) { Session.Log(TEXT("[FAIL] enable_module -> no output node")); return sol::lua_nil; }

			UNiagaraNodeFunctionCall* ModNode = FindModuleByName(OutputNode, NeoLuaStr::ToFString(ModName));
			if (!ModNode) { Session.Log(FString::Printf(TEXT("[FAIL] enable_module -> '%s' not found"), UTF8_TO_TCHAR(ModName.c_str()))); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "EnableNModule", "Enable Niagara Module"));
			// Engine-exported helper: handles Modify() + SetEnabledState + recompile dispatch.
			FNiagaraStackGraphUtilities::SetModuleIsEnabled(*ModNode, bEnabled);
			System->MarkPackageDirty();

			Session.Log(FString::Printf(TEXT("[OK] enable_module(\"%s\", %s)"), UTF8_TO_TCHAR(ModName.c_str()), bEnabled ? TEXT("true") : TEXT("false")));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// rename_emitter(name, new_name) or rename_emitter({old_name=, new_name=})
		// ==================================================================
		AssetObj.set_function("rename_emitter", [System, &Session](sol::table /*self*/,
			sol::object Arg1, sol::optional<std::string> Arg2, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FName, FNewName;
			if (Arg1.is<sol::table>())
			{
				sol::table T = Arg1.as<sol::table>();
				std::string OldNameStr = T.get_or<std::string>("old_name", "");
				if (OldNameStr.empty()) OldNameStr = T.get_or<std::string>("name", "");
				FName = NeoLuaStr::ToFString(OldNameStr);
				FNewName = NeoLuaStr::ToFString(T.get_or<std::string>("new_name", ""));
			}
			else if (Arg1.is<std::string>())
			{
				FName = NeoLuaStr::ToFString(Arg1.as<std::string>());
				FNewName = NeoLuaStr::ToFStringOpt(Arg2);
			}
			if (FName.IsEmpty() || FNewName.IsEmpty()) { Session.Log(TEXT("[FAIL] rename_emitter -> old_name and new_name required")); return sol::lua_nil; }
			int32 EmIdx = FindEmitterIndex(System, FName);
			if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] rename_emitter -> '%s' not found"), *FName)); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RenameNEmitter", "Rename Niagara Emitter"));
			System->Modify();
			System->GetEmitterHandle(EmIdx).SetName(::FName(*FNewName), *System);
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] rename_emitter(\"%s\", \"%s\")"), *FName, *FNewName));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// duplicate_emitter(name, new_name?) or duplicate_emitter({name=, new_name=})
		// ==================================================================
		AssetObj.set_function("duplicate_emitter", [System, &Session](sol::table /*self*/,
			sol::object Arg1, sol::optional<std::string> Arg2, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FName, FNewName;
			if (Arg1.is<sol::table>())
			{
				sol::table T = Arg1.as<sol::table>();
				FName = NeoLuaStr::ToFString(T.get_or<std::string>("name", ""));
				FNewName = NeoLuaStr::ToFString(T.get_or<std::string>("new_name", ""));
			}
			else if (Arg1.is<std::string>())
			{
				FName = NeoLuaStr::ToFString(Arg1.as<std::string>());
				if (Arg2.has_value()) FNewName = NeoLuaStr::ToFString(Arg2.value());
			}
			if (FName.IsEmpty()) { Session.Log(TEXT("[FAIL] duplicate_emitter -> name required")); return sol::lua_nil; }
			int32 EmIdx = FindEmitterIndex(System, FName);
			if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] duplicate_emitter -> '%s' not found"), *FName)); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "DupNEmitter", "Duplicate Niagara Emitter"));
			System->Modify();

			// Copy by value — DuplicateEmitterHandle emplaces into System->EmitterHandles
			// before reading back from the arg (NiagaraSystem.cpp:2853/2858/2862). If the
			// array reallocs on append, a reference to the source element would dangle.
			const FNiagaraEmitterHandle SrcHandle = System->GetEmitterHandle(EmIdx);
			UNiagaraEmitter* SrcEmitter = SrcHandle.GetInstance().Emitter;
			if (!SrcEmitter)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] duplicate_emitter -> emitter '%s' has no source emitter instance; pick a stateful emitter from list(\"emitters\")"), *FName));
				return sol::lua_nil;
			}

			// Generate a name if not provided — DuplicateEmitterHandle uses it for the emitter's unique name.
			// Passing FName() results in "None" which is not useful.
			if (FNewName.IsEmpty())
			{
				// Try "Fountain_Copy", then "Fountain_Copy_01", etc.
				FNewName = FName + TEXT("_Copy");
				int32 Suffix = 1;
				while (FindEmitterIndex(System, FNewName) != INDEX_NONE)
				{
					FNewName = FString::Printf(TEXT("%s_Copy_%02d"), *FName, Suffix++);
				}
			}
			FNiagaraEmitterHandle NewHandle = System->DuplicateEmitterHandle(SrcHandle, ::FName(*FNewName));
			// DuplicateEmitterHandle returns by value — find the actual handle in the system to set its name
			FGuid NewId = NewHandle.GetId();
			for (int32 i = 0; i < System->GetNumEmitters(); ++i)
			{
				if (System->GetEmitterHandle(i).GetId() == NewId)
				{
					System->GetEmitterHandle(i).SetName(::FName(*FNewName), *System);
					break;
				}
			}

			System->RequestCompile(false);
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] duplicate_emitter(\"%s\") -> %d emitters total"), *FName, System->GetNumEmitters()));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// reorder_emitter(name, new_index)
		// ==================================================================
		AssetObj.set_function("reorder_emitter", [System, &Session](sol::table /*self*/,
			const std::string& Name, int NewIndex, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FName = NeoLuaStr::ToFString(Name);
			int32 EmIdx = FindEmitterIndex(System, FName);
			if (EmIdx == INDEX_NONE)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] reorder_emitter -> '%s' not found"), *FName));
				return sol::lua_nil;
			}

			TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
			const int32 Count = Handles.Num();
			if (NewIndex < 0 || NewIndex >= Count)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] reorder_emitter -> new_index %d out of range (0-%d)"), NewIndex, Count - 1));
				return sol::lua_nil;
			}
			if (NewIndex == EmIdx)
			{
				Session.Log(FString::Printf(TEXT("[OK] reorder_emitter(\"%s\", %d) -> already at target position"), *FName, NewIndex));
				return sol::make_object(Lua, true);
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ReorderNEmitter", "Reorder Niagara Emitter"));
			System->Modify();
			FNiagaraEmitterHandle Moved = MoveTemp(Handles[EmIdx]);
			Handles.RemoveAt(EmIdx);
			// RemoveAt shifts subsequent entries down, so target index adjusts when moving forward.
			const int32 InsertIdx = (NewIndex > EmIdx) ? NewIndex - 1 : NewIndex;
			Handles.Insert(MoveTemp(Moved), InsertIdx);

			System->ComputeEmittersExecutionOrder();
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] reorder_emitter(\"%s\", %d -> %d)"), *FName, EmIdx, InsertIdx));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// validate()
		// ==================================================================
		AssetObj.set_function("validate", [System, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			sol::table Result = Lua.create_table();

			UNiagaraEffectType* EffectType = System->GetEffectType();
			int32 RuleCount = 0;
			if (EffectType)
			{
				RuleCount = EffectType->ValidationRules.Num();
			}

			Result["effect_type"] = EffectType ? TCHAR_TO_UTF8(*EffectType->GetName()) : "none";
			Result["rule_count"] = RuleCount;
			Result["emitters"] = static_cast<int>(System->GetEmitterHandles().Num());
			Session.Log(FString::Printf(TEXT("[OK] validate() -> %d rules, %d emitters"), RuleCount, System->GetEmitterHandles().Num()));
			return Result;
		});

		// ==================================================================
		// info()
		// ==================================================================
		AssetObj.set_function("info", [System, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			sol::table Result = Lua.create_table();

			// Convention used across the binding (CLAUDE.md note for new asset types):
			//   `<X>_count`  → integer count, always present
			//   `<X>`        → array of structured per-item info, iterable with ipairs
			// Older callers that read `result.emitters` as an int silently broke iteration;
			// they should switch to `result.emitter_count`. The array form is the new
			// canonical "structured summary" shape.

			static const ENiagaraScriptUsage EmitterStageUsages[] = {
				ENiagaraScriptUsage::EmitterSpawnScript,
				ENiagaraScriptUsage::EmitterUpdateScript,
				ENiagaraScriptUsage::ParticleSpawnScript,
				ENiagaraScriptUsage::ParticleUpdateScript,
			};
			static const TCHAR* EmitterStageNames[] = {
				TEXT("emitter_spawn"),
				TEXT("emitter_update"),
				TEXT("particle_spawn"),
				TEXT("particle_update"),
			};

			int32 TotalModules = 0;
			int32 TotalRenderers = 0;
			int32 TotalEventHandlers = 0;
			int32 TotalSimStages = 0;

			sol::table EmittersArray = Lua.create_table();
			for (int32 i = 0; i < System->GetNumEmitters(); ++i)
			{
				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(i);
				sol::table EInfo = Lua.create_table();
				EInfo["name"] = TCHAR_TO_UTF8(*Handle.GetName().ToString());
				EInfo["enabled"] = Handle.GetIsEnabled();
				EInfo["mode"] = Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless
					? "stateless" : "standard";

				int32 EmitterModules = 0;
				sol::table ModulesPerStage = Lua.create_table();
				for (int32 si = 0; si < UE_ARRAY_COUNT(EmitterStageUsages); ++si)
				{
					UNiagaraNodeOutput* Out = FindOutputNode(System, i, EmitterStageUsages[si], FGuid(), false);
					const int32 N = Out ? ListModulesInStack(Out).Num() : 0;
					ModulesPerStage[TCHAR_TO_UTF8(EmitterStageNames[si])] = N;
					EmitterModules += N;
				}
				EInfo["modules_per_stage"] = ModulesPerStage;
				EInfo["module_count"] = EmitterModules;

				int32 EmitterRenderers = 0;
				int32 EmitterSimStages = 0;
				int32 EmitterEvents = 0;
				if (Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless)
				{
					if (UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter())
					{
#if NSAI_NIAGARA_HAS_STATELESS_INTERNAL
						EmitterRenderers = SLE->GetRenderers().Num();
#else
						UObject* StatelessOwner = (UObject*)SLE;
						if (FArrayProperty* RendProp = CastField<FArrayProperty>(StatelessOwner->GetClass()->FindPropertyByName(TEXT("RendererProperties"))))
						{
							FScriptArrayHelper Helper(RendProp, RendProp->ContainerPtrToValuePtr<void>(StatelessOwner));
							EmitterRenderers = Helper.Num();
						}
#endif
					}
				}
				else if (FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData())
				{
					EmitterRenderers = ED->GetRenderers().Num();
					EmitterEvents = ED->GetEventHandlers().Num();
					EmitterSimStages = ED->GetSimulationStages().Num();
					EInfo["sim_target"] = ED->SimTarget == ENiagaraSimTarget::GPUComputeSim
						? "GPUComputeSim" : "CPUSim";
					EInfo["local_space"] = ED->bLocalSpace;
				}
				EInfo["renderer_count"] = EmitterRenderers;
				EInfo["simulation_stage_count"] = EmitterSimStages;
				EInfo["event_handler_count"] = EmitterEvents;

				TotalModules += EmitterModules;
				TotalRenderers += EmitterRenderers;
				TotalSimStages += EmitterSimStages;
				TotalEventHandlers += EmitterEvents;

				EmittersArray[i + 1] = EInfo;
			}

			// System-level modules (not associated with any emitter)
			UNiagaraNodeOutput* SysSpawn = FindOutputNode(System, INDEX_NONE, ENiagaraScriptUsage::SystemSpawnScript, FGuid(), false);
			UNiagaraNodeOutput* SysUpdate = FindOutputNode(System, INDEX_NONE, ENiagaraScriptUsage::SystemUpdateScript, FGuid(), false);
			const int32 SystemModules =
				(SysSpawn ? ListModulesInStack(SysSpawn).Num() : 0) +
				(SysUpdate ? ListModulesInStack(SysUpdate).Num() : 0);
			TotalModules += SystemModules;

			Result["emitter_count"] = System->GetNumEmitters();
			Result["emitters"] = EmittersArray;

			// User parameters: arrays of {name, type} so callers can iterate with ipairs.
			FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
			TArray<FNiagaraVariable> UserParams;
			Store.GetUserParameters(UserParams);
			sol::table UserParamsArray = Lua.create_table();
			for (int32 ui = 0; ui < UserParams.Num(); ++ui)
			{
				sol::table UP = Lua.create_table();
				UP["name"] = TCHAR_TO_UTF8(*UserParams[ui].GetName().ToString());
				UP["type"] = TCHAR_TO_UTF8(*UserParams[ui].GetType().GetName());
				UserParamsArray[ui + 1] = UP;
			}
			Result["user_parameter_count"] = UserParams.Num();
			Result["user_parameters"] = UserParamsArray;

			Result["system_module_count"] = SystemModules;
			Result["total_module_count"] = TotalModules;
			Result["total_renderer_count"] = TotalRenderers;
			Result["total_event_handler_count"] = TotalEventHandlers;
			Result["total_simulation_stage_count"] = TotalSimStages;

			// Stack-validator quick summary. Detailed per-issue list lives in get_stack_issues().
			{
				TSharedRef<FNiagaraSystemViewModel> StackVM = BuildSystemViewModelForStackInspection(System);
				FStackIssueCounts SC;
				CountStackIssues(CollectAllStackRoots(StackVM), SC);
				Result["stack_issue_count"]   = SC.Total;
				Result["stack_error_count"]   = SC.Errors;
				Result["stack_warning_count"] = SC.Warnings;
			}

			UNiagaraEffectType* ET = System->GetEffectType();
			if (ET) Result["effect_type"] = TCHAR_TO_UTF8(*ET->GetName());

			// Performance flags grouped under one sub-table so callers don't dot-walk
			// the top level for unrelated booleans.
			sol::table Perf = Lua.create_table();
			Perf["bake_out_rapid_iteration"] = !System->ShouldUseRapidIterationParameters();
			Perf["trim_attributes"] = System->ShouldTrimAttributes();
			Perf["compress_attributes"] = System->ShouldCompressAttributes();
			Result["performance"] = Perf;

			Session.Log(FString::Printf(TEXT("[OK] info() -> %d emitters, %d modules, %d renderers, %d user params"),
				System->GetNumEmitters(), TotalModules, TotalRenderers, UserParams.Num()));
			return Result;
		});

		// ==================================================================
		// move_module({emitter, stage, module_name, target_emitter?, target_stage?, target_index, copy?})
		// ==================================================================
		AssetObj.set_function("move_module", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get_or<std::string>("emitter", "");
			std::string StageStr = Params.get_or<std::string>("stage", "particle_update");
			std::string ModName = Params.get_or<std::string>("module_name", "");
			std::string TargetEmitter = Params.get_or<std::string>("target_emitter", EmitterName);
			std::string TargetStage = Params.get_or<std::string>("target_stage", StageStr);
			int32 TargetIndex = Params.get_or("target_index", -1);
			if (TargetIndex < 0) TargetIndex = Params.get_or("new_index", -1);
			bool bCopy = Params.get_or("copy", false);
			WarnUnconsumedKeys(Session, Params, { TEXT("emitter"), TEXT("stage"), TEXT("module_name"), TEXT("target_emitter"), TEXT("target_stage"), TEXT("target_index"), TEXT("new_index"), TEXT("copy") }, TEXT("move_module"));

			if (ModName.empty()) { Session.Log(TEXT("[FAIL] move_module -> module_name required")); return sol::lua_nil; }

			ENiagaraScriptUsage SrcUsage = ParseUsage(NeoLuaStr::ToFString(StageStr));
			ENiagaraScriptUsage DstUsage = ParseUsage(NeoLuaStr::ToFString(TargetStage));

			bool bSrcSys = (SrcUsage == ENiagaraScriptUsage::SystemSpawnScript || SrcUsage == ENiagaraScriptUsage::SystemUpdateScript);
			bool bDstSys = (DstUsage == ENiagaraScriptUsage::SystemSpawnScript || DstUsage == ENiagaraScriptUsage::SystemUpdateScript);

			int32 SrcEmIdx = INDEX_NONE, DstEmIdx = INDEX_NONE;
			if (!bSrcSys)
			{
				SrcEmIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(EmitterName));
				if (SrcEmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] move_module -> source emitter '%s' not found"), UTF8_TO_TCHAR(EmitterName.c_str()))); return sol::lua_nil; }
			}
			if (!bDstSys)
			{
				DstEmIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(TargetEmitter));
				if (DstEmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] move_module -> target emitter '%s' not found"), UTF8_TO_TCHAR(TargetEmitter.c_str()))); return sol::lua_nil; }
			}

			UNiagaraNodeOutput* SrcOutput = FindOutputNode(System, SrcEmIdx, SrcUsage);
			UNiagaraNodeOutput* DstOutput = FindOutputNode(System, DstEmIdx, DstUsage);
			if (!SrcOutput || !DstOutput) { Session.Log(TEXT("[FAIL] move_module -> could not resolve output nodes")); return sol::lua_nil; }

			FString FModName = NeoLuaStr::ToFString(ModName);
			UNiagaraNodeFunctionCall* ModNode = FindModuleByName(SrcOutput, FModName);
			if (!ModNode) { Session.Log(FString::Printf(TEXT("[FAIL] move_module -> '%s' not found"), *FModName)); return sol::lua_nil; }
			if (!ModNode->FunctionScript) { Session.Log(TEXT("[FAIL] move_module -> module has no function script")); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "MoveNModule", "Move Niagara Module"));

			// Save module info before potential removal
			UNiagaraScript* FuncScript = ModNode->FunctionScript;
			FString FuncName = ModNode->GetFunctionName();
			FGuid ScriptVersion = ModNode->SelectedScriptVersion;

			// For same-stack moves (not copy), remove the original FIRST to avoid name collision ("001" suffix)
			bool bSameStack = (SrcOutput == DstOutput) && !bCopy;
			if (bSameStack)
			{
				UEdGraph* Graph = ModNode->GetGraph();
				if (Graph) Graph->Modify();
				UEdGraphPin* ModMapIn = GetParamMapInputPin(ModNode);
				UEdGraphPin* ModMapOut = GetParamMapOutputPin(ModNode);
				UEdGraphPin* UpstreamOut = (ModMapIn && ModMapIn->LinkedTo.Num() > 0) ? ModMapIn->LinkedTo[0] : nullptr;
				TArray<UEdGraphPin*> DownstreamIns = ModMapOut ? ModMapOut->LinkedTo : TArray<UEdGraphPin*>();
				if (ModMapIn) ModMapIn->BreakAllPinLinks(true);
				if (ModMapOut) ModMapOut->BreakAllPinLinks(true);
				if (UpstreamOut)
				{
					for (UEdGraphPin* DownIn : DownstreamIns)
					{
						if (DownIn)
						{
							UpstreamOut->MakeLinkTo(DownIn);
							if (UEdGraphNode* UpOwner = UpstreamOut->GetOwningNode()) UpOwner->PinConnectionListChanged(UpstreamOut);
							if (UEdGraphNode* DownOwner = DownIn->GetOwningNode()) DownOwner->PinConnectionListChanged(DownIn);
						}
					}
				}
				TArray<UEdGraphNode*> NodesToRemove;
				TSet<UEdGraphNode*> Visited;
				CollectSubGraphNodes(ModNode, NodesToRemove, Visited);
				for (UEdGraphNode* N : NodesToRemove) { if (N && Graph) { Graph->RemoveNode(N); } }
				if (Graph) Graph->NotifyGraphChanged();
			}

			UNiagaraNodeFunctionCall* CopiedNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
				FuncScript, *DstOutput, TargetIndex, FuncName, ScriptVersion);
			if (!CopiedNode) { Session.Log(TEXT("[FAIL] move_module -> failed to add module at target")); return sol::lua_nil; }

			if (!bCopy && !bSameStack)
			{
				// Cross-stack move: remove original after successful add
				UEdGraph* Graph = ModNode->GetGraph();
				if (Graph) Graph->Modify();
				UEdGraphPin* ModMapIn = GetParamMapInputPin(ModNode);
				UEdGraphPin* ModMapOut = GetParamMapOutputPin(ModNode);
				UEdGraphPin* UpstreamOut = (ModMapIn && ModMapIn->LinkedTo.Num() > 0) ? ModMapIn->LinkedTo[0] : nullptr;
				TArray<UEdGraphPin*> DownstreamIns = ModMapOut ? ModMapOut->LinkedTo : TArray<UEdGraphPin*>();
				if (ModMapIn) ModMapIn->BreakAllPinLinks(true);
				if (ModMapOut) ModMapOut->BreakAllPinLinks(true);
				if (UpstreamOut)
				{
					for (UEdGraphPin* DownIn : DownstreamIns)
					{
						if (DownIn)
						{
							UpstreamOut->MakeLinkTo(DownIn);
							if (UEdGraphNode* UpOwner = UpstreamOut->GetOwningNode()) UpOwner->PinConnectionListChanged(UpstreamOut);
							if (UEdGraphNode* DownOwner = DownIn->GetOwningNode()) DownOwner->PinConnectionListChanged(DownIn);
						}
					}
				}
				TArray<UEdGraphNode*> NodesToRemove;
				TSet<UEdGraphNode*> Visited;
				CollectSubGraphNodes(ModNode, NodesToRemove, Visited);
				for (UEdGraphNode* N : NodesToRemove) { if (N && Graph) { Graph->RemoveNode(N); } }
				if (Graph) Graph->NotifyGraphChanged();
			}

			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] %s(\"%s\") -> %s/%s"),
				bCopy ? TEXT("copy_module") : TEXT("move_module"), *FModName,
				bDstSys ? TEXT("System") : UTF8_TO_TCHAR(TargetEmitter.c_str()),
				*UsageToStr(DstUsage)));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// create_scratch_pad_script({name, script_type="module"|"dynamic_input", duplicate_from?})
		// ==================================================================
		AssetObj.set_function("create_scratch_pad_script", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string Name = Params.get_or<std::string>("name", "");
			std::string ScriptType = Params.get_or<std::string>("script_type", "module");
			std::string DuplicateFrom = Params.get_or<std::string>("duplicate_from", "");

			if (Name.empty()) { Session.Log(TEXT("[FAIL] create_scratch_pad_script -> name required")); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "CreateNScratch", "Create Niagara Scratch Pad Script"));
			System->Modify();

			FString FName = NeoLuaStr::ToFString(Name);
			UNiagaraScript* NewScript = nullptr;

			if (!DuplicateFrom.empty())
			{
				UNiagaraScript* ExistingScript = NeoLuaAsset::Resolve<UNiagaraScript>(NeoLuaStr::ToFString(DuplicateFrom));
				if (!ExistingScript)
					ExistingScript = FindScratchPadScript(System, NeoLuaStr::ToFString(DuplicateFrom));
				if (!ExistingScript) { Session.Log(FString::Printf(TEXT("[FAIL] create_scratch_pad_script -> duplicate_from '%s' not found"), UTF8_TO_TCHAR(DuplicateFrom.c_str()))); return sol::lua_nil; }
				NewScript = Cast<UNiagaraScript>(StaticDuplicateObject(ExistingScript, System, ::FName(*FName)));
			}
			else
			{
				NewScript = NewObject<UNiagaraScript>(System, ::FName(*FName), RF_Transactional);
				if (!NewScript) { Session.Log(TEXT("[FAIL] create_scratch_pad_script -> allocation failed")); return sol::lua_nil; }

				FString FScriptType = NeoLuaStr::ToFString(ScriptType).ToLower();
				ENiagaraScriptUsage Usage = (FScriptType == TEXT("dynamic_input")) ? ENiagaraScriptUsage::DynamicInput : ENiagaraScriptUsage::Module;
				NewScript->SetUsage(Usage);

				UNiagaraScriptSource* Source = NewObject<UNiagaraScriptSource>(NewScript, NAME_None, RF_Transactional);
				UNiagaraGraph* Graph = Source ? NewObject<UNiagaraGraph>(Source, NAME_None, RF_Transactional) : nullptr;
				if (!Source || !Graph) { Session.Log(TEXT("[FAIL] create_scratch_pad_script -> graph init failed")); return sol::lua_nil; }
				Source->NodeGraph = Graph;

				const UEdGraphSchema_Niagara* Schema = Cast<UEdGraphSchema_Niagara>(Graph->GetSchema());
				if (!Schema) { Session.Log(TEXT("[FAIL] create_scratch_pad_script -> no schema")); return sol::lua_nil; }

				FGraphNodeCreator<UNiagaraNodeOutput> OutputCreator(*Graph);
				UNiagaraNodeOutput* OutputNode = OutputCreator.CreateNode();
				OutputNode->SetUsage(Usage);
				FGraphNodeCreator<UNiagaraNodeInput> InputCreator(*Graph);
				UNiagaraNodeInput* InputNode = InputCreator.CreateNode();
				InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
				InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn"));

				if (Usage == ENiagaraScriptUsage::DynamicInput)
					OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Output")));
				else
					OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Output")));

				OutputCreator.Finalize();
				InputCreator.Finalize();
				Schema->TryCreateConnection(InputNode->GetOutputPin(0), OutputNode->GetInputPin(0));

				NewScript->SetLatestSource(Source);
				NewScript->RequestCompile(FGuid());
			}

			if (!NewScript) { Session.Log(TEXT("[FAIL] create_scratch_pad_script -> creation failed")); return sol::lua_nil; }
			NewScript->ClearFlags(RF_Public | RF_Standalone);
			System->ScratchPadScripts.AddUnique(NewScript);
			System->MarkPackageDirty();

			Session.Log(FString::Printf(TEXT("[OK] create_scratch_pad_script(\"%s\", type=\"%s\")"), *FName, UTF8_TO_TCHAR(ScriptType.c_str())));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// delete_scratch_pad_script(name)
		// ==================================================================
		AssetObj.set_function("delete_scratch_pad_script", [System, &Session](sol::table /*self*/,
			const std::string& Name, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FName = NeoLuaStr::ToFString(Name);

			for (int32 i = 0; i < System->ScratchPadScripts.Num(); ++i)
			{
				UNiagaraScript* Script = System->ScratchPadScripts[i];
				if (Script && Script->GetName().Equals(FName, ESearchCase::IgnoreCase))
				{
						const FScopedTransaction Tx(NSLOCTEXT("AIK", "DelNScratch", "Delete Niagara Scratch Pad Script"));
						System->Modify();
						System->ScratchPadScripts.RemoveAt(i);
						System->PostEditChange();
						System->MarkPackageDirty();
					Session.Log(FString::Printf(TEXT("[OK] delete_scratch_pad_script(\"%s\")"), *FName));
					return sol::make_object(Lua, true);
				}
			}
			Session.Log(FString::Printf(TEXT("[FAIL] delete_scratch_pad_script -> '%s' not found"), *FName));
			return sol::lua_nil;
		});

		// ==================================================================
		// rename_scratch_pad_script(name, new_name)
		// ==================================================================
		AssetObj.set_function("rename_scratch_pad_script", [System, &Session](sol::table /*self*/,
			const std::string& Name, const std::string& NewName, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FName = NeoLuaStr::ToFString(Name);
			FString FNewName = NeoLuaStr::ToFString(NewName);

			for (UNiagaraScript* Script : System->ScratchPadScripts)
			{
				if (Script && Script->GetName().Equals(FName, ESearchCase::IgnoreCase))
				{
					const FScopedTransaction Tx(NSLOCTEXT("AIK", "RenNScratch", "Rename Niagara Scratch Pad Script"));
					System->Modify();
					Script->Rename(*FNewName, System, REN_DontCreateRedirectors);
					System->MarkPackageDirty();
					Session.Log(FString::Printf(TEXT("[OK] rename_scratch_pad_script(\"%s\", \"%s\")"), *FName, *FNewName));
					return sol::make_object(Lua, true);
				}
			}
			Session.Log(FString::Printf(TEXT("[FAIL] rename_scratch_pad_script -> '%s' not found"), *FName));
			return sol::lua_nil;
		});

		// ==================================================================
		// reorder_renderers({emitter, index, new_index})
		// Stateless path goes through UNiagaraStatelessEmitter::MoveRenderer;
		// stateful goes through UNiagaraEmitter.
		// ==================================================================
		AssetObj.set_function("reorder_renderers", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get<sol::optional<std::string>>("emitter").value_or("");
			int32 OldIdx = Params.get<sol::optional<int>>("index").value_or(-1);
			int32 NewIdx = Params.get<sol::optional<int>>("new_index").value_or(-1);
			if (OldIdx < 0 || NewIdx < 0) { Session.Log(TEXT("[FAIL] reorder_renderers -> index and new_index required (both must be >= 0)")); return sol::lua_nil; }

			FString FEmitter = NeoLuaStr::ToFString(EmitterName);
			int32 EmIdx = FindEmitterIndex(System, FEmitter);
			if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] reorder_renderers -> emitter '%s' not found. Use list(\"emitters\") to see available."), *FEmitter)); return sol::lua_nil; }

			FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
			const bool bStateless = (Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless);

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ReorderNRenderers", "Reorder Niagara Renderers"));
			System->Modify();

			if (bStateless)
			{
				UNiagaraStatelessEmitter* SLE = Handle.GetStatelessEmitter();
				if (!SLE) { Session.Log(FString::Printf(TEXT("[FAIL] reorder_renderers -> stateless emitter '%s' has no stateless instance"), *FEmitter)); return sol::lua_nil; }

#if NSAI_NIAGARA_HAS_STATELESS_INTERNAL
				const TArray<UNiagaraRendererProperties*>& Renderers = SLE->GetRenderers();
				const int32 Count = Renderers.Num();
				if (OldIdx >= Count || NewIdx >= Count) { Session.Log(FString::Printf(TEXT("[FAIL] reorder_renderers -> index out of range for stateless emitter '%s' (valid: 0-%d). Use list(\"renderers\", {emitter=\"%s\"}) first."), *FEmitter, Count - 1, *FEmitter)); return sol::lua_nil; }
				if (OldIdx == NewIdx) { System->MarkPackageDirty(); Session.Log(FString::Printf(TEXT("[OK] reorder_renderers(emitter=\"%s\", %d -> %d) -> already at target, mode=stateless"), *FEmitter, OldIdx, NewIdx)); return sol::make_object(Lua, true); }

				SLE->MoveRenderer(Renderers[OldIdx], NewIdx, Handle.GetInstance().Version);
#else
				FArrayProperty* ArrProp = CastField<FArrayProperty>(((UObject*)SLE)->GetClass()->FindPropertyByName(TEXT("RendererProperties")));
				if (!ArrProp) { Session.Log(TEXT("[FAIL] reorder_renderers -> RendererProperties UPROPERTY not found on stateless emitter")); return sol::lua_nil; }
				FObjectPropertyBase* InnerProp = CastField<FObjectPropertyBase>(ArrProp->Inner);
				if (!InnerProp) { Session.Log(TEXT("[FAIL] reorder_renderers -> RendererProperties inner is not an object")); return sol::lua_nil; }
				FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>((UObject*)SLE));
				const int32 Count = Helper.Num();
				if (OldIdx >= Count || NewIdx >= Count) { Session.Log(FString::Printf(TEXT("[FAIL] reorder_renderers -> index out of range for stateless emitter '%s' (valid: 0-%d). Use list(\"renderers\", {emitter=\"%s\"}) first."), *FEmitter, Count - 1, *FEmitter)); return sol::lua_nil; }
				if (OldIdx == NewIdx) { System->MarkPackageDirty(); Session.Log(FString::Printf(TEXT("[OK] reorder_renderers(emitter=\"%s\", %d -> %d) -> already at target, mode=stateless"), *FEmitter, OldIdx, NewIdx)); return sol::make_object(Lua, true); }

				((UObject*)SLE)->Modify();
				UObject* MovedObj = InnerProp->GetObjectPropertyValue(Helper.GetRawPtr(OldIdx));
				Helper.RemoveValues(OldIdx, 1);
				Helper.InsertValues(NewIdx, 1);
				InnerProp->SetObjectPropertyValue(Helper.GetRawPtr(NewIdx), MovedObj);
				((UObject*)SLE)->PostEditChange();
#endif
			}
			else
			{
				FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData();
				UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
				if (!ED || !Emitter) { Session.Log(TEXT("[FAIL] reorder_renderers -> no emitter data")); return sol::lua_nil; }

				const TArray<UNiagaraRendererProperties*>& Renderers = ED->GetRenderers();
				if (OldIdx >= Renderers.Num() || NewIdx >= Renderers.Num()) { Session.Log(FString::Printf(TEXT("[FAIL] reorder_renderers -> index out of range for emitter '%s' (valid: 0-%d). Use list(\"renderers\", {emitter=\"%s\"}) first."), *FEmitter, Renderers.Num() - 1, *FEmitter)); return sol::lua_nil; }

				Emitter->Modify();
				Emitter->MoveRenderer(Renderers[OldIdx], NewIdx, Handle.GetInstance().Version);
			}

			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] reorder_renderers(emitter=\"%s\", %d -> %d%s)"), *FEmitter, OldIdx, NewIdx, bStateless ? TEXT(", mode=stateless") : TEXT("")));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// reorder_simulation_stages({emitter, index, new_index})
		// ==================================================================
		AssetObj.set_function("reorder_simulation_stages", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get<sol::optional<std::string>>("emitter").value_or("");
			int32 OldIdx = Params.get<sol::optional<int>>("index").value_or(-1);
			int32 NewIdx = Params.get<sol::optional<int>>("new_index").value_or(-1);
			if (OldIdx < 0 || NewIdx < 0) { Session.Log(TEXT("[FAIL] reorder_simulation_stages -> index and new_index required")); return sol::lua_nil; }

			FString FEmitter = NeoLuaStr::ToFString(EmitterName);
			int32 EmIdx = FindEmitterIndex(System, FEmitter);
			if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] reorder_simulation_stages -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }

			FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
			FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData();
			UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
			if (!ED || !Emitter) { Session.Log(TEXT("[FAIL] reorder_simulation_stages -> no emitter data")); return sol::lua_nil; }

			const TArray<UNiagaraSimulationStageBase*>& Stages = ED->GetSimulationStages();
			if (OldIdx >= Stages.Num() || NewIdx >= Stages.Num()) { Session.Log(TEXT("[FAIL] reorder_simulation_stages -> index out of range")); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ReorderNSimStages", "Reorder Niagara Simulation Stages"));
			System->Modify();
			Emitter->Modify();
			Emitter->MoveSimulationStageToIndex(Stages[OldIdx], NewIdx, Handle.GetInstance().Version);
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] reorder_simulation_stages(emitter=\"%s\", %d -> %d)"), *FEmitter, OldIdx, NewIdx));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// rename_user_parameter({old_name, new_name})
		// Uses engine-exported UNiagaraSystem::HandleVariableRenamed which propagates
		// the rename through all scripts, emitter handles, and parameter map nodes.
		// ==================================================================
		AssetObj.set_function("rename_user_parameter", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string OldName = Params.get<sol::optional<std::string>>("old_name").value_or("");
			if (OldName.empty()) OldName = Params.get<sol::optional<std::string>>("name").value_or("");
			std::string NewName = Params.get<sol::optional<std::string>>("new_name").value_or("");
			if (OldName.empty() || NewName.empty())
			{
				Session.Log(TEXT("[FAIL] rename_user_parameter -> {old_name, new_name} required"));
				return sol::lua_nil;
			}

			const FString FOld = NeoLuaStr::ToFString(OldName);
			const FString FNew = NeoLuaStr::ToFString(NewName);
			const FString OldFull = FOld.StartsWith(TEXT("User.")) ? FOld : (TEXT("User.") + FOld);
			const FString NewFull = FNew.StartsWith(TEXT("User.")) ? FNew : (TEXT("User.") + FNew);

			FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
			TArray<FNiagaraVariable> AllParams;
			Store.GetUserParameters(AllParams);

			FNiagaraVariable Found;
			bool bFoundVar = false;
			for (const FNiagaraVariable& V : AllParams)
			{
				const FString VName = V.GetName().ToString();
				if (VName.Equals(FOld, ESearchCase::IgnoreCase) || VName.Equals(OldFull, ESearchCase::IgnoreCase))
				{
					Found = V;
					bFoundVar = true;
					break;
				}
			}
			if (!bFoundVar)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] rename_user_parameter -> '%s' not found"), *FOld));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RenNUserParam", "Rename Niagara User Parameter"));
			System->Modify();

			FNiagaraVariable NewVar(Found.GetType(), FName(*NewFull));
			// HandleVariableRenamed propagates through all scripts/emitters/parameter map nodes.
			System->HandleVariableRenamed(Found, NewVar, /*bUpdateContexts=*/true);

			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] rename_user_parameter(\"%s\" -> \"%s\")"), *FOld, *FNew));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// set_user_parameter({name, value})
		// LCD: reflection-driven byte writer covers every Niagara type uniformly.
		// ==================================================================
		AssetObj.set_function("set_user_parameter", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string ParamName = Params.get<sol::optional<std::string>>("name").value_or("");
			if (ParamName.empty()) { Session.Log(TEXT("[FAIL] set_user_parameter -> name required")); return sol::lua_nil; }

			FString FParamName = FString(NeoLuaStr::ToFString(ParamName));
			FString FullName = TEXT("User.") + FParamName;
			FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
			TArray<FNiagaraVariable> AllParams;
			Store.GetUserParameters(AllParams);

			// GetUserParameters() returns redirect-map KEYS (short names without "User." prefix).
			// Match against both the short name and the full "User." prefixed name.
			FNiagaraVariable* Found = nullptr;
			for (FNiagaraVariable& V : AllParams)
			{
				FString VName = V.GetName().ToString();
				if (VName.Equals(FParamName, ESearchCase::IgnoreCase) || VName.Equals(FullName, ESearchCase::IgnoreCase))
				{
					Found = &V;
					break;
				}
			}
			if (!Found) { Session.Log(FString::Printf(TEXT("[FAIL] set_user_parameter -> '%s' not found"), *FParamName)); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "SetNUserParam", "Set Niagara User Parameter"));
			System->Modify();

			sol::object Val = Params["value"];
			TArray<uint8> Bytes;
			FString SerErr;
			if (!NSAINiagaraLCD::SerializeSolValueToTypedBytes(Val, Found->GetType(), Bytes, SerErr))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_user_parameter(\"%s\") -> %s"), *FParamName, *SerErr));
				return sol::lua_nil;
			}

			Store.SetParameterData(Bytes.GetData(), *Found, /*bAdd=*/false);
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] set_user_parameter(\"%s\")"), *FParamName));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// configure_event_handler({emitter, index, ...props})
		// ==================================================================
		AssetObj.set_function("configure_event_handler", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get<sol::optional<std::string>>("emitter").value_or("");
			int32 Index = Params.get<sol::optional<int>>("index").value_or(0);

			FString FEmitter = NeoLuaStr::ToFString(EmitterName);
			int32 EmIdx = FindEmitterIndex(System, FEmitter);
			if (EmIdx == INDEX_NONE) { Session.Log(TEXT("[FAIL] configure_event_handler -> emitter not found")); return sol::lua_nil; }

			FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
			if (!ED) { Session.Log(TEXT("[FAIL] configure_event_handler -> no emitter data")); return sol::lua_nil; }

			const TArray<FNiagaraEventScriptProperties>& Events = ED->GetEventHandlers();
			if (Events.Num() == 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] configure_event_handler -> emitter '%s' has no event handlers. Use add(\"event_handler\", {emitter=\"%s\", source_event_name=\"...\"}) first."), *FEmitter, *FEmitter));
				return sol::lua_nil;
			}
			if (Index < 0 || Index >= Events.Num()) { Session.Log(FString::Printf(TEXT("[FAIL] configure_event_handler -> index %d out of range (0-%d)"), Index, Events.Num() - 1)); return sol::lua_nil; }

			// Engine traversal checks EventHandlerScriptProps[i].Script (NiagaraEmitter.cpp:1591);
			// a malformed asset can leave Script null, so guard before GetUsageId().
			UNiagaraScript* EvtScript = Events[Index].Script;
			if (!EvtScript)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] configure_event_handler -> event handler %d on emitter '%s' has no script; remove it with remove(\"event_handler\", {emitter=\"%s\", index=%d}) and re-add via add(\"event_handler\", {...})"), Index, *FEmitter, *FEmitter, Index));
				return sol::lua_nil;
			}
			// Use GetEventHandlerByIdUnsafe for mutable access
			FNiagaraEventScriptProperties* EvtPtr = ED->GetEventHandlerByIdUnsafe(EvtScript->GetUsageId());
			if (!EvtPtr) { Session.Log(TEXT("[FAIL] configure_event_handler -> could not get mutable event handler")); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNEvent", "Configure Niagara Event Handler"));
			System->Modify();

			// All fields on FNiagaraEventScriptProperties go through the LCD struct walker.
			// Pass the whole Params table — "emitter" / "index" log as unknown-property warnings
			// which we filter out of the user-facing log.
			sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties");
			const sol::table& ApplySource = Props.has_value() ? Props.value() : Params;
			UNiagaraEmitter* Emitter = System->GetEmitterHandle(EmIdx).GetInstance().Emitter;
			FString ApplyErr;
			TArray<FString> Warnings;
			NeoLuaProperty::ApplyTableToStruct(FNiagaraEventScriptProperties::StaticStruct(), EvtPtr,
				Emitter, ApplySource, ApplyErr, &Warnings);
			for (const FString& W : Warnings)
			{
				// Silence the two structural keys that aren't handler fields
				if (W.Contains(TEXT("'emitter'")) || W.Contains(TEXT("'index'"))) continue;
				Session.Log(FString::Printf(TEXT("[WARN] configure_event_handler %s"), *W));
			}

			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] configure_event_handler(emitter=\"%s\", index=%d)"), *FEmitter, Index));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// configure_simulation_stage({emitter, index, ...props})
		// ==================================================================
		AssetObj.set_function("configure_simulation_stage", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get<sol::optional<std::string>>("emitter").value_or("");
			int32 Index = Params.get<sol::optional<int>>("index").value_or(0);

			FString FEmitter = NeoLuaStr::ToFString(EmitterName);
			int32 EmIdx = FindEmitterIndex(System, FEmitter);
			if (EmIdx == INDEX_NONE) { Session.Log(TEXT("[FAIL] configure_simulation_stage -> emitter not found")); return sol::lua_nil; }

			FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
			if (!ED) { Session.Log(TEXT("[FAIL] configure_simulation_stage -> no emitter data")); return sol::lua_nil; }

			const TArray<UNiagaraSimulationStageBase*>& Stages = ED->GetSimulationStages();
			if (Index < 0 || Index >= Stages.Num()) { Session.Log(FString::Printf(TEXT("[FAIL] configure_simulation_stage -> index %d out of range for emitter '%s' (valid: 0-%d). Use list(\"simulation_stages\", {emitter=\"%s\"}) first."), Index, *FEmitter, Stages.Num() - 1, *FEmitter)); return sol::lua_nil; }

			UNiagaraSimulationStageBase* Stage = Stages[Index];
			if (!Stage) { Session.Log(FString::Printf(TEXT("[FAIL] configure_simulation_stage -> stage %d on emitter '%s' is null; remove and recreate it via remove(\"simulation_stage\", {emitter=\"%s\", index=%d}) and add(\"simulation_stage\", {emitter=\"%s\"})."), Index, *FEmitter, *FEmitter, Index, *FEmitter)); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNSimStage", "Configure Niagara Simulation Stage"));
			System->Modify();

			// All SimStage fields (SimulationStageName, bEnabled, IterationSource, NumIterations,
			// DirectDispatchType, ElementCountX/Y/Z, ParticleIterationState*, etc.) go through
			// the LCD walker. Accept either {properties={...}} or flat top-level keys.
			sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties");
			const sol::table& ApplySource = Props.has_value() ? Props.value() : Params;
			FString ApplyErr;
			TArray<FString> Warnings;
			NeoLuaProperty::ApplyTable(Stage, ApplySource, ApplyErr, &Warnings);
			for (const FString& W : Warnings)
			{
				if (W.Contains(TEXT("'emitter'")) || W.Contains(TEXT("'index'"))) continue;
				Session.Log(FString::Printf(TEXT("[WARN] configure_simulation_stage %s"), *W));
			}

			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] configure_simulation_stage(emitter=\"%s\", index=%d)"), *FEmitter, Index));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// Baker: settings + output CRUD
		// ==================================================================
		//
		// UNiagaraBakerSettings is a standalone UObject hanging off the system
		// (UNiagaraSystem::GetBakerSettings auto-creates on first access, NIAGARA_API).
		// All tunables are regular UPROPERTY, so configure_baker goes through the LCD
		// ApplyTable walker — adding a new baker field in a future engine release
		// requires no binding change.
		//
		// Outputs are UNiagaraBakerOutput subclass instances (Texture2D /
		// VolumeTexture / SparseVolumeTexture / StaticMesh / SimCache). Type dispatch
		// uses LuaDynamicType::FindDerivedClass with the NiagaraBakerOutput prefix.

		static const TArray<FString> BakerOutputPrefixes = { TEXT("NiagaraBakerOutput") };
		static const TArray<FString> BakerOutputSuffixes = { TEXT("") };

		AssetObj.set_function("configure_baker", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UNiagaraBakerSettings* Baker = System->GetBakerSettings();
			if (!Baker) { Session.Log(TEXT("[FAIL] configure_baker -> could not obtain baker settings")); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNBaker", "Configure Niagara Baker Settings"));
			System->Modify();
			Baker->Modify();

			sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties");
			const sol::table& ApplySource = Props.has_value() ? Props.value() : Params;
			FString ApplyErr;
			TArray<FString> Warnings;
			NeoLuaProperty::ApplyTable(Baker, ApplySource, ApplyErr, &Warnings);
			for (const FString& W : Warnings)
				Session.Log(FString::Printf(TEXT("[WARN] configure_baker %s"), *W));

			System->MarkPackageDirty();
			Session.Log(TEXT("[OK] configure_baker"));
			return sol::make_object(Lua, true);
		});

		AssetObj.set_function("get_baker_settings", [System](sol::table /*self*/,
			sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UNiagaraBakerSettings* Baker = System->GetBakerSettings();
			if (!Baker) return sol::make_object(Lua, sol::lua_nil);
			return sol::make_object(Lua, NeoLuaProperty::ReadAsTable(Baker, Lua));
		});

		// add_baker_output({type, properties?}) — spawns a new UNiagaraBakerOutput of
		// the requested subclass, applies LCD properties, and appends to Outputs.
		AssetObj.set_function("add_baker_output", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string TypeStr = Params.get_or<std::string>("type", "");
			if (TypeStr.empty()) { Session.Log(TEXT("[FAIL] add_baker_output -> type required")); return sol::lua_nil; }
			FString FTypeStr = NeoLuaStr::ToFString(TypeStr);

			UClass* OutputClass = LuaDynamicType::FindDerivedClass(
				UNiagaraBakerOutput::StaticClass(), FTypeStr, BakerOutputPrefixes, BakerOutputSuffixes);
			if (!OutputClass)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add_baker_output -> unknown type '%s'. Valid: %s"),
					*FTypeStr,
					*LuaDynamicType::FormatAvailableTypes(UNiagaraBakerOutput::StaticClass(),
						BakerOutputPrefixes, BakerOutputSuffixes)));
				return sol::lua_nil;
			}

			UNiagaraBakerSettings* Baker = System->GetBakerSettings();
			if (!Baker) { Session.Log(TEXT("[FAIL] add_baker_output -> no baker settings")); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNBakerOutput", "Add Niagara Baker Output"));
			Baker->Modify();

			UNiagaraBakerOutput* NewOutput = NewObject<UNiagaraBakerOutput>(Baker, OutputClass, NAME_None, RF_Transactional);
			if (!NewOutput) { Session.Log(TEXT("[FAIL] add_baker_output -> allocation failed")); return sol::lua_nil; }
			NewOutput->OutputName = NewOutput->MakeOutputName();

			if (sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties"))
			{
				FString Err;
				TArray<FString> Warnings;
				NeoLuaProperty::ApplyTable(NewOutput, Props.value(), Err, &Warnings);
				for (const FString& W : Warnings)
					Session.Log(FString::Printf(TEXT("[WARN] add_baker_output %s"), *W));
			}

			const int32 NewIdx = Baker->Outputs.Add(NewOutput);
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] add_baker_output(type=\"%s\") -> index %d"),
				*FTypeStr, NewIdx));
			return sol::make_object(Lua, NewIdx);
		});

		AssetObj.set_function("remove_baker_output", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			int32 Index = static_cast<int32>(Params.get_or("index", -1));
			UNiagaraBakerSettings* Baker = System->GetBakerSettings();
			if (!Baker) { Session.Log(TEXT("[FAIL] remove_baker_output -> no baker settings")); return sol::lua_nil; }
			if (Index < 0 || Index >= Baker->Outputs.Num())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] remove_baker_output -> index %d out of range (%d)"),
					Index, Baker->Outputs.Num()));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNBakerOutput", "Remove Niagara Baker Output"));
			Baker->Modify();
			Baker->Outputs.RemoveAt(Index);
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] remove_baker_output(index=%d)"), Index));
			return sol::make_object(Lua, true);
		});

		AssetObj.set_function("configure_baker_output", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			int32 Index = static_cast<int32>(Params.get_or("index", -1));
			UNiagaraBakerSettings* Baker = System->GetBakerSettings();
			if (!Baker) { Session.Log(TEXT("[FAIL] configure_baker_output -> no baker settings")); return sol::lua_nil; }
			if (Index < 0 || Index >= Baker->Outputs.Num())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] configure_baker_output -> index %d out of range (%d)"),
					Index, Baker->Outputs.Num()));
				return sol::lua_nil;
			}
			UNiagaraBakerOutput* Output = Baker->Outputs[Index];
			if (!Output) { Session.Log(TEXT("[FAIL] configure_baker_output -> null output")); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNBakerOutput", "Configure Niagara Baker Output"));
			Output->Modify();

			sol::optional<sol::table> Props = Params.get<sol::optional<sol::table>>("properties");
			const sol::table& ApplySource = Props.has_value() ? Props.value() : Params;
			FString Err;
			TArray<FString> Warnings;
			NeoLuaProperty::ApplyTable(Output, ApplySource, Err, &Warnings);
			for (const FString& W : Warnings)
			{
				if (W.Contains(TEXT("'index'"))) continue;
				Session.Log(FString::Printf(TEXT("[WARN] configure_baker_output %s"), *W));
			}

			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] configure_baker_output(index=%d)"), Index));
			return sol::make_object(Lua, true);
		});

		AssetObj.set_function("list_baker_outputs", [System](sol::table /*self*/,
			sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			sol::table Result = Lua.create_table();
			UNiagaraBakerSettings* Baker = System->GetBakerSettings();
			if (!Baker) return sol::make_object(Lua, Result);

			for (int32 i = 0; i < Baker->Outputs.Num(); ++i)
			{
				UNiagaraBakerOutput* Out = Baker->Outputs[i];
				if (!Out) continue;
				sol::table E = Lua.create_table();
				E["index"] = i;
				E["type"] = TCHAR_TO_UTF8(*LuaDynamicType::GetFriendlyTypeName(Out,
					BakerOutputPrefixes, BakerOutputSuffixes));
				E["output_name"] = TCHAR_TO_UTF8(*Out->OutputName);
				Result[i + 1] = E;
			}
			return sol::make_object(Lua, Result);
		});

		// ==================================================================
		// Emitter inheritance: reparent / remove parent / merge / sync check
		// ==================================================================
		//
		// All APIs are NIAGARA_API exported on UNiagaraEmitter / FVersionedNiagaraEmitterData:
		//   UNiagaraEmitter::SetParent(FVersionedNiagaraEmitter)
		//   UNiagaraEmitter::MergeChangesFromParent() -> TArray<FMergeEmitterResults>
		//   FVersionedNiagaraEmitterData::RemoveParent()
		//   FVersionedNiagaraEmitterData::Reparent(FVersionedNiagaraEmitter)
		//   FVersionedNiagaraEmitterData::GetParent() -> FVersionedNiagaraEmitter
		//   FVersionedNiagaraEmitterData::IsSynchronizedWithParent() -> bool

		AssetObj.set_function("reparent_emitter", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get_or<std::string>("emitter", "");
			std::string ParentPath  = Params.get_or<std::string>("parent_asset", "");
			std::string ParentVersion = Params.get_or<std::string>("parent_version", "");
			if (EmitterName.empty() || ParentPath.empty())
			{
				Session.Log(TEXT("[FAIL] reparent_emitter -> emitter and parent_asset required"));
				return sol::lua_nil;
			}

			FString FEmitter = NeoLuaStr::ToFString(EmitterName);
			int32 EmIdx = FindEmitterIndex(System, FEmitter);
			if (EmIdx == INDEX_NONE)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] reparent_emitter -> emitter '%s' not found"), *FEmitter));
				return sol::lua_nil;
			}

			UNiagaraEmitter* InstanceEmitter = System->GetEmitterHandle(EmIdx).GetInstance().Emitter;
			if (!InstanceEmitter) { Session.Log(TEXT("[FAIL] reparent_emitter -> no emitter object")); return sol::lua_nil; }

			UNiagaraEmitter* ParentAsset = NeoLuaAsset::Resolve<UNiagaraEmitter>(NeoLuaStr::ToFString(ParentPath));
			if (!ParentAsset)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] reparent_emitter -> parent asset '%s' not found"),
					UTF8_TO_TCHAR(ParentPath.c_str())));
				return sol::lua_nil;
			}

			FGuid VersionGuid;
			if (!ParentVersion.empty())
			{
				FGuid::Parse(NeoLuaStr::ToFString(ParentVersion), VersionGuid);
			}
			if (!VersionGuid.IsValid())
			{
				VersionGuid = ParentAsset->GetExposedVersion().VersionGuid;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ReparentNEmitter", "Reparent Niagara Emitter"));
			System->Modify();
			InstanceEmitter->Modify();

			InstanceEmitter->SetParent(FVersionedNiagaraEmitter(ParentAsset, VersionGuid));
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] reparent_emitter(\"%s\", parent=\"%s\")"),
				*FEmitter, UTF8_TO_TCHAR(ParentPath.c_str())));
			return sol::make_object(Lua, true);
		});

		AssetObj.set_function("remove_emitter_parent", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get_or<std::string>("emitter", "");
			if (EmitterName.empty()) { Session.Log(TEXT("[FAIL] remove_emitter_parent -> emitter required")); return sol::lua_nil; }

			FString FEmitter = NeoLuaStr::ToFString(EmitterName);
			int32 EmIdx = FindEmitterIndex(System, FEmitter);
			if (EmIdx == INDEX_NONE)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] remove_emitter_parent -> emitter '%s' not found"), *FEmitter));
				return sol::lua_nil;
			}

			FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
			if (!ED) { Session.Log(TEXT("[FAIL] remove_emitter_parent -> no emitter data")); return sol::lua_nil; }
			UNiagaraEmitter* InstanceEmitter = System->GetEmitterHandle(EmIdx).GetInstance().Emitter;

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNEmitterParent", "Remove Niagara Emitter Parent"));
			System->Modify();
			if (InstanceEmitter) InstanceEmitter->Modify();
			ED->RemoveParent();
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] remove_emitter_parent(\"%s\")"), *FEmitter));
			return sol::make_object(Lua, true);
		});

		AssetObj.set_function("merge_emitter_parent_changes", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get_or<std::string>("emitter", "");
			if (EmitterName.empty()) { Session.Log(TEXT("[FAIL] merge_emitter_parent_changes -> emitter required")); return sol::lua_nil; }

			FString FEmitter = NeoLuaStr::ToFString(EmitterName);
			int32 EmIdx = FindEmitterIndex(System, FEmitter);
			if (EmIdx == INDEX_NONE)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] merge_emitter_parent_changes -> emitter '%s' not found"), *FEmitter));
				return sol::lua_nil;
			}

			UNiagaraEmitter* InstanceEmitter = System->GetEmitterHandle(EmIdx).GetInstance().Emitter;
			if (!InstanceEmitter) { Session.Log(TEXT("[FAIL] merge_emitter_parent_changes -> no emitter object")); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "MergeNEmitter", "Merge Niagara Parent Changes"));
			System->Modify();
			InstanceEmitter->Modify();

			const TArray<INiagaraMergeManager::FMergeEmitterResults> Results = InstanceEmitter->MergeChangesFromParent();
			System->MarkPackageDirty();

			sol::table ResultTable = Lua.create_table();
			int32 Out = 1;
			int32 SuccessCount = 0;
			int32 FailureCount = 0;
			for (const INiagaraMergeManager::FMergeEmitterResults& R : Results)
			{
				sol::table E = Lua.create_table();
				const TCHAR* ResultName = TEXT("None");
				switch (R.MergeResult)
				{
				case INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied: ResultName = TEXT("SucceededDifferencesApplied"); ++SuccessCount; break;
				case INiagaraMergeManager::EMergeEmitterResult::SucceededNoDifferences:      ResultName = TEXT("SucceededNoDifferences");      ++SuccessCount; break;
				case INiagaraMergeManager::EMergeEmitterResult::FailedToDiff:                ResultName = TEXT("FailedToDiff");                ++FailureCount; break;
				case INiagaraMergeManager::EMergeEmitterResult::FailedToMerge:               ResultName = TEXT("FailedToMerge");               ++FailureCount; break;
				default: break;
				}
				E["result"] = TCHAR_TO_UTF8(ResultName);
				E["modified_graph"] = R.bModifiedGraph;
				if (R.ErrorMessages.Num() > 0)
					E["errors"] = TCHAR_TO_UTF8(*R.GetErrorMessagesString());
				ResultTable[Out++] = E;
			}

			Session.Log(FString::Printf(TEXT("[OK] merge_emitter_parent_changes(\"%s\") -> %d ok, %d fail"),
				*FEmitter, SuccessCount, FailureCount));
			return sol::make_object(Lua, ResultTable);
		});

		AssetObj.set_function("is_emitter_synchronized_with_parent", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get_or<std::string>("emitter", "");
			if (EmitterName.empty()) { Session.Log(TEXT("[FAIL] is_emitter_synchronized_with_parent -> emitter required")); return sol::lua_nil; }

			FString FEmitter = NeoLuaStr::ToFString(EmitterName);
			int32 EmIdx = FindEmitterIndex(System, FEmitter);
			if (EmIdx == INDEX_NONE) return sol::make_object(Lua, false);

			FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
			if (!ED) return sol::make_object(Lua, false);

			return sol::make_object(Lua, ED->IsSynchronizedWithParent());
		});

		AssetObj.set_function("get_emitter_parent", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string EmitterName = Params.get_or<std::string>("emitter", "");
			if (EmitterName.empty()) { Session.Log(TEXT("[FAIL] get_emitter_parent -> emitter required")); return sol::lua_nil; }

			FString FEmitter = NeoLuaStr::ToFString(EmitterName);
			int32 EmIdx = FindEmitterIndex(System, FEmitter);
			if (EmIdx == INDEX_NONE) return sol::make_object(Lua, sol::lua_nil);

			FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
			if (!ED) return sol::make_object(Lua, sol::lua_nil);

			const FVersionedNiagaraEmitter Parent = ED->GetParent();
			if (!Parent.Emitter) return sol::make_object(Lua, sol::lua_nil);

			sol::table Out = Lua.create_table();
			Out["asset_path"] = TCHAR_TO_UTF8(*Parent.Emitter->GetPathName());
			Out["version"]    = TCHAR_TO_UTF8(*Parent.Version.ToString());
			Out["synchronized"] = ED->IsSynchronizedWithParent();
			return sol::make_object(Lua, Out);
		});

		// ==================================================================
		// run_validation() — detailed validation with actual results
		// ==================================================================
		AssetObj.set_function("run_validation", [System, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			sol::table Result = Lua.create_table();

			UNiagaraEffectType* EffectType = System->GetEffectType();
			Result["effect_type"] = EffectType ? TCHAR_TO_UTF8(*EffectType->GetName()) : "none";
			Result["emitters"] = static_cast<int>(System->GetEmitterHandles().Num());

			// Create a temporary ViewModel for validation
			FNiagaraSystemViewModelOptions Options;
			Options.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;
			Options.bCanAutoCompile = false;
			Options.bCanSimulate = false;
			Options.bCanModifyEmittersFromTimeline = false;
			Options.bIsForDataProcessingOnly = true;
			// MessageLogGuid is required — NiagaraMessageManager asserts if not set
			Options.MessageLogGuid = System->GetAssetGuid();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			Options.bCompileForEdit = false;
#endif

			TSharedRef<FNiagaraSystemViewModel> SysViewModel = MakeShared<FNiagaraSystemViewModel>();
			SysViewModel->Initialize(*System, Options);

			sol::table Findings = Lua.create_table();
			int32 FindIdx = 1;
			NiagaraValidation::ValidateAllRulesInSystem(SysViewModel,
				[&Findings, &FindIdx, &Lua](const FNiagaraValidationResult& R)
				{
					sol::table F = Lua.create_table();
					F["severity"] = R.Severity == ENiagaraValidationSeverity::Error ? "error" :
						(R.Severity == ENiagaraValidationSeverity::Warning ? "warning" : "info");
					F["message"] = TCHAR_TO_UTF8(*R.SummaryText.ToString());
					if (!R.Description.IsEmpty())
						F["description"] = TCHAR_TO_UTF8(*R.Description.ToString());
					Findings[FindIdx++] = F;
				});

			Result["findings"] = Findings;
			Result["finding_count"] = FindIdx - 1;

			Session.Log(FString::Printf(TEXT("[OK] run_validation() -> %d findings"), FindIdx - 1));
			return Result;
		});

		// ==================================================================
		// get_stack_issues() — enumerate Niagara stack validator issues + fixes
		// Each issue has a stable id; each fix within an issue has a stable id.
		// Pair (issue_id, fix_id) is the input to apply_stack_fix.
		// ==================================================================
		AssetObj.set_function("get_stack_issues", [System, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
#if NSAI_NIAGARA_HAS_EXTERNAL_EDIT_UTILITIES
			FNiagaraExternalEditContext Context(System);
			FNiagaraExt_StackIssues StackIssues;
			UNiagaraExternalEditUtilities::GetStackIssues(System, StackIssues, Context);
			if (Context.HasErrors())
			{
				LogExternalEditErrors(Session, Context, TEXT("get_stack_issues"));
				return sol::lua_nil;
			}

			sol::table Result = Lua.create_table();
			sol::table IssuesArray = Lua.create_table();
			int32 Idx = 1;
			int32 FixableCount = 0;

			for (const FNiagaraExt_StackIssue& Issue : StackIssues.Issues)
			{
				sol::table I = Lua.create_table();
				I["id"] = TCHAR_TO_UTF8(*Issue.IssueId);
				I["severity"] = TCHAR_TO_UTF8(StackIssueSeverityToString(Issue.Severity));
				I["short_description"] = TCHAR_TO_UTF8(*Issue.ShortDescription);
				I["long_description"] = TCHAR_TO_UTF8(*Issue.LongDescription);
				I["owner_path"] = TCHAR_TO_UTF8(*Issue.StackDisplayPath);
				I["stack_display_path"] = TCHAR_TO_UTF8(*Issue.StackDisplayPath);
				I["location"] = BuildStackItemLocationTable(Lua, Issue.Location);
				I["can_be_dismissed"] = Issue.bCanBeDismissed;
				I["is_dismissed"] = Issue.bIsDismissed;

				sol::table FixesArray = Lua.create_table();
				int32 FixIdx = 1;
				for (const FNiagaraExt_StackIssueFix& Fix : Issue.Fixes)
				{
					sol::table F = Lua.create_table();
					F["id"] = TCHAR_TO_UTF8(*Fix.FixId);
					F["description"] = TCHAR_TO_UTF8(*Fix.Description);
					F["style"] = TCHAR_TO_UTF8(StackIssueFixStyleToString(Fix.Style));
					F["bound"] = Fix.Style == ENiagaraExt_StackIssueFixStyle::Fix;
					FixesArray[FixIdx++] = F;
				}
				I["fix_count"] = FixIdx - 1;
				I["fixes"] = FixesArray;
				if (FixIdx > 1) FixableCount++;

				IssuesArray[Idx++] = I;
			}

			Result["issues"] = IssuesArray;
			Result["issue_count"] = StackIssues.Issues.Num();
			Result["error_count"] = StackIssues.NumErrors;
			Result["warning_count"] = StackIssues.NumWarnings;
			Result["info_count"] = StackIssues.NumInfos;
			Result["fixable_count"] = FixableCount;

			Session.Log(FString::Printf(TEXT("[OK] get_stack_issues() -> %d issues (%d errors, %d warnings, %d info, %d fixable)"),
				StackIssues.Issues.Num(), StackIssues.NumErrors, StackIssues.NumWarnings, StackIssues.NumInfos, FixableCount));
			return Result;
#else
			TSharedRef<FNiagaraSystemViewModel> VM = BuildSystemViewModelForStackInspection(System);
			TArray<UNiagaraStackEntry*> Roots = CollectAllStackRoots(VM);

			sol::table Result = Lua.create_table();
			sol::table IssuesArray = Lua.create_table();
			int32 Idx = 1;
			FStackIssueCounts Counts;

			for (UNiagaraStackEntry* Root : Roots)
			{
				TraverseStackEntry(Root, [&](UNiagaraStackEntry* E)
				{
					for (const UNiagaraStackEntry::FStackIssue& Issue : E->GetIssues())
					{
						sol::table I = Lua.create_table();
						I["id"]                = TCHAR_TO_UTF8(*Issue.GetUniqueIdentifier());
						I["severity"]          = TCHAR_TO_UTF8(StackIssueSeverityToString(Issue.GetSeverity()));
						I["short_description"] = TCHAR_TO_UTF8(*Issue.GetShortDescription().ToString());
						I["long_description"]  = TCHAR_TO_UTF8(*Issue.GetLongDescription().ToString());
						I["owner_path"]        = TCHAR_TO_UTF8(*E->GetStackEditorDataKey());
						I["can_be_dismissed"]  = Issue.GetCanBeDismissed();

						sol::table FixesArray = Lua.create_table();
						int32 FixIdx = 1;
						for (const UNiagaraStackEntry::FStackIssueFix& Fix : Issue.GetFixes())
						{
							sol::table F = Lua.create_table();
							F["id"]          = TCHAR_TO_UTF8(*Fix.GetUniqueIdentifier());
							F["description"] = TCHAR_TO_UTF8(*Fix.GetDescription().ToString());
							F["style"]       = TCHAR_TO_UTF8(StackIssueFixStyleToString(Fix.GetStyle()));
							F["bound"]       = Fix.GetFixDelegate().IsBound();
							FixesArray[FixIdx++] = F;
						}
						I["fix_count"] = FixIdx - 1;
						I["fixes"]     = FixesArray;

						Counts.Total++;
						switch (Issue.GetSeverity())
						{
							case EStackIssueSeverity::Error:   Counts.Errors++;   break;
							case EStackIssueSeverity::Warning: Counts.Warnings++; break;
							case EStackIssueSeverity::Info:    Counts.Infos++;    break;
							default: break;
						}
						if (FixIdx - 1 > 0) Counts.Fixable++;

						IssuesArray[Idx++] = I;
					}
				});
			}

			Result["issues"]        = IssuesArray;
			Result["issue_count"]   = Counts.Total;
			Result["error_count"]   = Counts.Errors;
			Result["warning_count"] = Counts.Warnings;
			Result["info_count"]    = Counts.Infos;
			Result["fixable_count"] = Counts.Fixable;

			Session.Log(FString::Printf(TEXT("[OK] get_stack_issues() -> %d issues (%d errors, %d warnings, %d info, %d fixable)"),
				Counts.Total, Counts.Errors, Counts.Warnings, Counts.Infos, Counts.Fixable));
			return Result;
#endif
		});

		// ==================================================================
		// apply_stack_fix({issue_id, fix_id}) — fire a Niagara stack quick-fix
		// Stateless: rebuilds the system viewmodel internally and looks up the
		// (issue_id, fix_id) pair against the freshly-collected stack. IDs are
		// stable strings derived from StackEditorDataKey + descriptions, so they
		// match across viewmodel rebuilds (same approach as Epic's 5.8 wrapper).
		// ==================================================================
		AssetObj.set_function("apply_stack_fix", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string IssueIdStr = Params.get_or<std::string>("issue_id", "");
			std::string FixIdStr   = Params.get_or<std::string>("fix_id", "");
			bool bDryRun           = Params.get_or("dry_run", false);
			WarnUnconsumedKeys(Session, Params, { TEXT("issue_id"), TEXT("fix_id"), TEXT("dry_run") }, TEXT("apply_stack_fix"));

			if (IssueIdStr.empty()) { Session.Log(TEXT("[FAIL] apply_stack_fix -> issue_id required (call get_stack_issues() first)")); return sol::lua_nil; }
			if (FixIdStr.empty())   { Session.Log(TEXT("[FAIL] apply_stack_fix -> fix_id required (use one of issue.fixes[i].id)")); return sol::lua_nil; }

			FString IssueId = NeoLuaStr::ToFString(IssueIdStr);
			FString FixId   = NeoLuaStr::ToFString(FixIdStr);

#if NSAI_NIAGARA_HAS_EXTERNAL_EDIT_UTILITIES
			if (bDryRun)
			{
				FNiagaraExternalEditContext Context(System);
				FNiagaraExt_StackIssues StackIssues;
				UNiagaraExternalEditUtilities::GetStackIssues(System, StackIssues, Context);
				if (Context.HasErrors())
				{
					LogExternalEditErrors(Session, Context, TEXT("apply_stack_fix"));
					return sol::lua_nil;
				}

				bool bFound = false;
				bool bApplicable = false;
				FString FoundDesc;
				for (const FNiagaraExt_StackIssue& Issue : StackIssues.Issues)
				{
					if (!Issue.IssueId.Equals(IssueId)) continue;
					for (const FNiagaraExt_StackIssueFix& Fix : Issue.Fixes)
					{
						if (!Fix.FixId.Equals(FixId)) continue;
						bFound = true;
						bApplicable = Fix.Style == ENiagaraExt_StackIssueFixStyle::Fix;
						FoundDesc = Fix.Description;
						break;
					}
					if (bFound) break;
				}

				sol::table Out = Lua.create_table();
				Out["success"] = bFound && bApplicable;
				Out["dry_run"] = true;
				Out["issue_id"] = IssueIdStr;
				Out["fix_id"] = FixIdStr;
				Out["found"] = bFound;
				Out["bound"] = bApplicable;
				if (bFound) Out["description"] = TCHAR_TO_UTF8(*FoundDesc);
				Session.Log(FString::Printf(TEXT("[%s] apply_stack_fix(dry_run) -> issue='%s' fix='%s' found=%s applicable=%s"),
					(bFound && bApplicable) ? TEXT("OK") : TEXT("FAIL"),
					*IssueId, *FixId, bFound ? TEXT("true") : TEXT("false"), bApplicable ? TEXT("true") : TEXT("false")));
				return sol::make_object(Lua, Out);
			}

			FNiagaraExternalEditContext Context(System);
			FNiagaraExt_ApplyStackIssueFixResult ApplyResult;
			UNiagaraExternalEditUtilities::ApplyStackIssueFix(System, IssueId, FixId, ApplyResult, Context);
			if (Context.HasErrors())
			{
				LogExternalEditErrors(Session, Context, TEXT("apply_stack_fix"));
				return sol::lua_nil;
			}
			if (!ApplyResult.bApplied)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] apply_stack_fix -> issue='%s' fix='%s' was not applied"), *IssueId, *FixId));
				return sol::lua_nil;
			}

			System->MarkPackageDirty();
			sol::table Out = Lua.create_table();
			Out["success"] = true;
			Out["issue_id"] = IssueIdStr;
			Out["fix_id"] = FixIdStr;
			Out["description"] = TCHAR_TO_UTF8(*ApplyResult.AppliedFixDescription);
			Session.Log(FString::Printf(TEXT("[OK] apply_stack_fix(\"%s\", \"%s\") -> %s"), *IssueId, *FixId, *ApplyResult.AppliedFixDescription));
			return sol::make_object(Lua, Out);
#else
			TSharedRef<FNiagaraSystemViewModel> VM = BuildSystemViewModelForStackInspection(System);
			TArray<UNiagaraStackEntry*> Roots = CollectAllStackRoots(VM);

			if (bDryRun)
			{
				// Locate without firing: report whether the pair is bindable.
				bool bFound = false; bool bBound = false; FString FoundDesc;
				for (UNiagaraStackEntry* Root : Roots)
				{
					TraverseStackEntry(Root, [&](UNiagaraStackEntry* E)
					{
						if (bFound) return;
						for (const UNiagaraStackEntry::FStackIssue& Issue : E->GetIssues())
						{
							if (!Issue.GetUniqueIdentifier().Equals(IssueId)) continue;
							for (const UNiagaraStackEntry::FStackIssueFix& Fix : Issue.GetFixes())
							{
								if (!Fix.GetUniqueIdentifier().Equals(FixId)) continue;
								bFound = true;
								bBound = Fix.GetFixDelegate().IsBound();
								FoundDesc = Fix.GetDescription().ToString();
								return;
							}
						}
					});
					if (bFound) break;
				}
				sol::table Out = Lua.create_table();
				Out["success"]      = bFound && bBound;
				Out["dry_run"]      = true;
				Out["issue_id"]     = IssueIdStr;
				Out["fix_id"]       = FixIdStr;
				Out["found"]        = bFound;
				Out["bound"]        = bBound;
				if (bFound) Out["description"] = TCHAR_TO_UTF8(*FoundDesc);
				Session.Log(FString::Printf(TEXT("[%s] apply_stack_fix(dry_run) -> issue='%s' fix='%s' found=%s bound=%s"),
					(bFound && bBound) ? TEXT("OK") : TEXT("FAIL"),
					*IssueId, *FixId, bFound ? TEXT("true") : TEXT("false"), bBound ? TEXT("true") : TEXT("false")));
				return sol::make_object(Lua, Out);
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ApplyNStackFix", "Apply Niagara Stack Fix"));
			System->Modify();
			FString FiredDesc;
			EStackFixApplyResult R = FindAndFireStackFix(Roots, IssueId, FixId, FiredDesc);

			switch (R)
			{
				case EStackFixApplyResult::IssueNotFound:
					Session.Log(FString::Printf(TEXT("[FAIL] apply_stack_fix -> issue_id '%s' not found (re-run get_stack_issues to refresh ids)"), *IssueId));
					return sol::lua_nil;
				case EStackFixApplyResult::FixNotFound:
					Session.Log(FString::Printf(TEXT("[FAIL] apply_stack_fix -> fix_id '%s' not found on issue '%s'"), *FixId, *IssueId));
					return sol::lua_nil;
				case EStackFixApplyResult::FixNotBound:
					Session.Log(FString::Printf(TEXT("[FAIL] apply_stack_fix -> fix '%s' has no bound delegate (engine cannot auto-apply this fix)"), *FixId));
					return sol::lua_nil;
				case EStackFixApplyResult::Fired:
				{
					System->MarkPackageDirty();
					sol::table Out = Lua.create_table();
					Out["success"]     = true;
					Out["issue_id"]    = IssueIdStr;
					Out["fix_id"]      = FixIdStr;
					Out["description"] = TCHAR_TO_UTF8(*FiredDesc);
					Session.Log(FString::Printf(TEXT("[OK] apply_stack_fix(\"%s\", \"%s\") -> %s"), *IssueId, *FixId, *FiredDesc));
					return sol::make_object(Lua, Out);
				}
			}
			return sol::lua_nil;
#endif
		});

		// ==================================================================
		// subscribe_parameter_definitions({asset_path})
		// ==================================================================
		AssetObj.set_function("subscribe_parameter_definitions", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string AssetPath = Params.get<sol::optional<std::string>>("asset_path").value_or("");
			if (AssetPath.empty()) { Session.Log(TEXT("[FAIL] subscribe_parameter_definitions -> asset_path required")); return sol::lua_nil; }

			FString FPath = NeoLuaStr::ToFString(AssetPath);
			UNiagaraParameterDefinitionsBase* ParamDefs = NeoLuaAsset::Resolve<UNiagaraParameterDefinitionsBase>(FPath);
			if (!ParamDefs) { Session.Log(FString::Printf(TEXT("[FAIL] subscribe_parameter_definitions -> '%s' not found"), *FPath)); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "SubNParamDefs", "Subscribe Niagara Parameter Definitions"));
			System->Modify();
			System->SubscribeToParameterDefinitions(ParamDefs);
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] subscribe_parameter_definitions(\"%s\")"), *FPath));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// unsubscribe_parameter_definitions({asset_path})
		// ==================================================================
		AssetObj.set_function("unsubscribe_parameter_definitions", [System, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			std::string AssetPath = Params.get<sol::optional<std::string>>("asset_path").value_or("");
			if (AssetPath.empty()) { Session.Log(TEXT("[FAIL] unsubscribe_parameter_definitions -> asset_path required")); return sol::lua_nil; }

			FString FPath = NeoLuaStr::ToFString(AssetPath);
			UNiagaraParameterDefinitionsBase* ParamDefs = NeoLuaAsset::Resolve<UNiagaraParameterDefinitionsBase>(FPath);
			if (!ParamDefs) { Session.Log(FString::Printf(TEXT("[FAIL] unsubscribe_parameter_definitions -> '%s' not found"), *FPath)); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "UnsubNParamDefs", "Unsubscribe Niagara Parameter Definitions"));
			System->Modify();
			System->UnsubscribeFromParameterDefinitions(ParamDefs->GetDefinitionsUniqueId());
			System->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] unsubscribe_parameter_definitions(\"%s\")"), *FPath));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// synchronize_parameter_definitions()
		// ==================================================================
		AssetObj.set_function("synchronize_parameter_definitions", [System, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			const FScopedTransaction Tx(NSLOCTEXT("AIK", "SyncNParamDefs", "Synchronize Niagara Parameter Definitions"));
			System->Modify();
			System->SynchronizeWithParameterDefinitions();
			// Also synchronize each emitter
			for (int32 i = 0; i < System->GetNumEmitters(); ++i)
			{
				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(i);
				UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
				if (Emitter) Emitter->SynchronizeWithParameterDefinitions();
			}
			System->MarkPackageDirty();
			Session.Log(TEXT("[OK] synchronize_parameter_definitions()"));
			return sol::make_object(Lua, true);
		});

		// ==================================================================
		// list_parameter_definitions()
		// ==================================================================
		AssetObj.set_function("list_parameter_definitions", [System, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			sol::table Result = Lua.create_table();
			int32 Idx = 1;

			TArray<UNiagaraParameterDefinitionsBase*> Subscriptions = System->GetSubscribedParameterDefinitions();
			for (UNiagaraParameterDefinitionsBase* Def : Subscriptions)
			{
				if (!Def) continue;
				sol::table E = Lua.create_table();
				E["name"] = TCHAR_TO_UTF8(*Def->GetName());
				E["path"] = TCHAR_TO_UTF8(*Def->GetPathName());
				Result[Idx++] = E;
			}
			Session.Log(FString::Printf(TEXT("[OK] list_parameter_definitions() -> %d"), Idx - 1));
			return Result;
		});

		// ==================================================================
		// get(type, id, property?)
		// Read individual property values from system/emitter/renderer/module
		// ==================================================================
		AssetObj.set_function("get", [System, &Session](sol::table /*self*/,
			const std::string& Type, sol::optional<sol::object> IdOpt, sol::optional<std::string> PropertyOpt, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);

			// ---- get("system", _, "property"?) ----
			// LCD: property omitted → full editable-property table (arrays/structs/enums/objects).
			if (FType.Equals(TEXT("system"), ESearchCase::IgnoreCase))
			{
				if (!PropertyOpt.has_value())
				{
					Session.Log(TEXT("[OK] get(\"system\") -> full property table"));
					return sol::make_object(Lua, NeoLuaProperty::ReadAsTable(System, Lua));
				}

				FString PropName = NeoLuaStr::ToFString(PropertyOpt.value());
				FProperty* Prop = NeoLuaProperty::FindPropertyByName(System->GetClass(), PropName);
				if (!Prop) { Session.Log(FString::Printf(TEXT("[FAIL] get(\"system\", _, \"%s\") -> property not found"), *PropName)); return sol::lua_nil; }

				const void* Container = Prop->ContainerPtrToValuePtr<void>(System);
				return NeoLuaProperty::ReadPropertyAsSol(Prop, Container, Lua);
			}
			// ---- get("emitter", name, "property") ----
			else if (FType.Equals(TEXT("emitter"), ESearchCase::IgnoreCase))
			{
				if (!IdOpt.has_value() || !IdOpt.value().is<std::string>()) { Session.Log(TEXT("[FAIL] get(\"emitter\") -> emitter name required")); return sol::lua_nil; }
				FString EmName = NeoLuaStr::ToFString(IdOpt.value().as<std::string>());
				int32 EmIdx = FindEmitterIndex(System, EmName);
				if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] get(\"emitter\") -> '%s' not found"), *EmName)); return sol::lua_nil; }

				if (!PropertyOpt.has_value())
				{
					// Return summary table
					FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(EmIdx).GetEmitterData();
					if (!ED) return sol::lua_nil;
					sol::table R = Lua.create_table();
					R["name"] = TCHAR_TO_UTF8(*EmName);
					R["sim_target"] = (ED->SimTarget == ENiagaraSimTarget::GPUComputeSim) ? "gpu" : "cpu";
					R["local_space"] = ED->bLocalSpace;
					R["determinism"] = ED->bDeterminism;
					R["random_seed"] = ED->RandomSeed;
					R["requires_persistent_ids"] = (bool)ED->bRequiresPersistentIDs;
					R["allocation_mode"] = (ED->AllocationMode == EParticleAllocationMode::AutomaticEstimate) ? "automatic" : "manual";
					R["pre_allocation_count"] = ED->PreAllocationCount;
					R["max_gpu_particles_spawn_per_frame"] = ED->MaxGPUParticlesSpawnPerFrame;
					R["renderers"] = ED->GetRenderers().Num();
					R["event_handlers"] = ED->GetEventHandlers().Num();
					R["simulation_stages"] = ED->GetSimulationStages().Num();
					Session.Log(FString::Printf(TEXT("[OK] get(\"emitter\", \"%s\")"), *EmName));
					return R;
				}

				// Resolve property — first try FVersionedNiagaraEmitterData (where most per-emitter
				// data lives), then fall back to UNiagaraEmitter UObject. LCD read via ReadPropertyAsSol.
				FString PropName = NeoLuaStr::ToFString(PropertyOpt.value());
				FVersionedNiagaraEmitterData* PropED = System->GetEmitterHandle(EmIdx).GetEmitterData();
				UNiagaraEmitter* Emitter = System->GetEmitterHandle(EmIdx).GetInstance().Emitter;

				FProperty* Prop = NeoLuaProperty::FindPropertyByName(FVersionedNiagaraEmitterData::StaticStruct(), PropName);
				const void* Container = nullptr;
				if (Prop && PropED)
				{
					Container = Prop->ContainerPtrToValuePtr<void>(PropED);
				}
				else if (Emitter)
				{
					Prop = NeoLuaProperty::FindPropertyByName(Emitter->GetClass(), PropName);
					if (Prop) Container = Prop->ContainerPtrToValuePtr<void>(Emitter);
				}

				if (!Prop || !Container) { Session.Log(FString::Printf(TEXT("[FAIL] get(\"emitter\", \"%s\", \"%s\") -> property not found"), *EmName, *PropName)); return sol::lua_nil; }

				return NeoLuaProperty::ReadPropertyAsSol(Prop, Container, Lua);
			}
			// ---- get("renderer", {emitter, index}, "property") ----
			else if (FType.Equals(TEXT("renderer"), ESearchCase::IgnoreCase))
			{
				if (!IdOpt.has_value() || !IdOpt.value().is<sol::table>()) { Session.Log(TEXT("[FAIL] get(\"renderer\") -> {emitter, index} required")); return sol::lua_nil; }
				sol::table T = IdOpt.value().as<sol::table>();
				std::string EmName = T.get_or<std::string>("emitter", "");
				int32 RIdx = T.get_or("index", 0);

				FString FEmName = NeoLuaStr::ToFString(EmName);
				int32 EmIdx = FindEmitterIndex(System, FEmName);
				if (EmIdx == INDEX_NONE) { Session.Log(TEXT("[FAIL] get(\"renderer\") -> emitter not found")); return sol::lua_nil; }

				FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
				FNiagaraRendererStorage Storage;
				FString ResolveError;
				if (!ResolveRendererStorage(Handle, FEmName, TEXT("get(\"renderer\")"), Storage, ResolveError))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] %s"), *ResolveError));
					return sol::lua_nil;
				}
				if (RIdx < 0 || RIdx >= Storage.Renderers.Num()) { Session.Log(TEXT("[FAIL] get(\"renderer\") -> index out of range")); return sol::lua_nil; }

				UNiagaraRendererProperties* Renderer = Storage.Renderers[RIdx];
				if (!Renderer) return sol::lua_nil;

				if (!PropertyOpt.has_value())
				{
					sol::table R = BuildRendererSummary(Renderer, RIdx, Lua);
					Session.Log(FString::Printf(TEXT("[OK] get(\"renderer\", index=%d%s)"), RIdx, Storage.bStateless ? TEXT(", mode=stateless") : TEXT("")));
					return R;
				}

				FString PropName = NeoLuaStr::ToFString(PropertyOpt.value());
				FProperty* Prop = NeoLuaProperty::FindPropertyByName(Renderer->GetClass(), PropName);
				if (!Prop) { Session.Log(FString::Printf(TEXT("[FAIL] get(\"renderer\") -> property '%s' not found"), *PropName)); return sol::lua_nil; }

				return NeoLuaProperty::ReadPropertyAsSol(Prop, Prop->ContainerPtrToValuePtr<void>(Renderer), Lua);
			}
			// ---- get("user_parameter", name) ----
			// LCD: reflection-driven byte reader covers every Niagara type uniformly.
			else if (FType.Contains(TEXT("user_param"), ESearchCase::IgnoreCase) || FType.Contains(TEXT("parameter"), ESearchCase::IgnoreCase))
			{
				if (!IdOpt.has_value() || !IdOpt.value().is<std::string>()) { Session.Log(TEXT("[FAIL] get(\"user_parameter\") -> name required")); return sol::lua_nil; }
				FString ParamName = NeoLuaStr::ToFString(IdOpt.value().as<std::string>());
				FString FullName = TEXT("User.") + ParamName;

				FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
				TArray<FNiagaraVariable> AllParams;
				Store.GetUserParameters(AllParams);

				for (const FNiagaraVariable& V : AllParams)
				{
					FString VName = V.GetName().ToString();
					if (VName.Equals(ParamName, ESearchCase::IgnoreCase) || VName.Equals(FullName, ESearchCase::IgnoreCase))
					{
						const uint8* Bytes = Store.GetParameterData(V);
						if (!Bytes)
						{
							Session.Log(FString::Printf(TEXT("[FAIL] get(\"user_parameter\", \"%s\") -> data not allocated"), *ParamName));
							return sol::lua_nil;
						}
						return NSAINiagaraLCD::ReadTypedBytesAsSolValue(Bytes, V.GetType(), Lua);
					}
				}
				Session.Log(FString::Printf(TEXT("[FAIL] get(\"user_parameter\", \"%s\") -> not found"), *ParamName));
				return sol::lua_nil;
			}
			// ---- get("module", {emitter, stage, module_name}, "input_name") ----
			else if (FType.Equals(TEXT("module"), ESearchCase::IgnoreCase))
			{
				if (!IdOpt.has_value() || !IdOpt.value().is<sol::table>()) { Session.Log(TEXT("[FAIL] get(\"module\") -> {emitter, stage, module_name} required")); return sol::lua_nil; }
				sol::table T = IdOpt.value().as<sol::table>();
				std::string EmitterName = T.get_or<std::string>("emitter", "");
				std::string StageStr = T.get_or<std::string>("stage", "particle_update");
				std::string ModName = T.get_or<std::string>("module_name", "");
				WarnUnconsumedKeys(Session, T, { TEXT("emitter"), TEXT("stage"), TEXT("module_name") }, TEXT("get(\"module\")"));
				if (ModName.empty()) { Session.Log(TEXT("[FAIL] get(\"module\") -> module_name required")); return sol::lua_nil; }

				ENiagaraScriptUsage Usage = ParseUsage(NeoLuaStr::ToFString(StageStr));
				int32 EmIdx = INDEX_NONE;
				bool bSys = (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript);
				if (!bSys) EmIdx = FindEmitterIndex(System, NeoLuaStr::ToFString(EmitterName));

				UNiagaraNodeOutput* OutputNode = FindOutputNode(System, EmIdx, Usage, FGuid(), false);
				if (!OutputNode) { Session.Log(TEXT("[FAIL] get(\"module\") -> no output node")); return sol::lua_nil; }

				UNiagaraNodeFunctionCall* ModNode = FindModuleByName(OutputNode, NeoLuaStr::ToFString(ModName));
				if (!ModNode) { Session.Log(FString::Printf(TEXT("[FAIL] get(\"module\") -> '%s' not found"), UTF8_TO_TCHAR(ModName.c_str()))); return sol::lua_nil; }

				if (!PropertyOpt.has_value())
				{
					// Return all input pin default values
					sol::table R = Lua.create_table();
					R["name"] = TCHAR_TO_UTF8(*ModNode->GetFunctionName());
					R["enabled"] = ModNode->GetDesiredEnabledState() == ENodeEnabledState::Enabled;
					sol::table Inputs = Lua.create_table();
					int32 Idx2 = 1;
					for (UEdGraphPin* Pin : ModNode->GetAllPins())
					{
						if (Pin->Direction == EGPD_Input && !IsParameterMapPin(Pin))
						{
							sol::table PinInfo = Lua.create_table();
							PinInfo["name"] = TCHAR_TO_UTF8(*Pin->GetName());
							FNiagaraTypeDefinition PinType = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
							PinInfo["type"] = TCHAR_TO_UTF8(*PinType.GetName());
							if (!Pin->DefaultValue.IsEmpty())
							{
								PinInfo["default_value"] = TCHAR_TO_UTF8(*Pin->DefaultValue);
								PinInfo["value"] = PinDefaultToSolValue(Pin->DefaultValue, PinType, Lua);
							}
							PinInfo["linked"] = Pin->LinkedTo.Num() > 0;
							if (Pin->bHidden)
								PinInfo["hidden"] = true;
							Inputs[Idx2++] = PinInfo;
						}
					}
					R["inputs"] = Inputs;
					Session.Log(FString::Printf(TEXT("[OK] get(\"module\", \"%s\")"), UTF8_TO_TCHAR(ModName.c_str())));
					return R;
				}

				// Resolve owning script for RI param lookups
				UNiagaraScript* GetModScript = nullptr;
				const FNiagaraEmitterHandle* GetModEmHandle = (EmIdx != INDEX_NONE) ? &System->GetEmitterHandle(EmIdx) : nullptr;
				if (EmIdx != INDEX_NONE)
				{
					FVersionedNiagaraEmitterData* GetModED = System->GetEmitterHandle(EmIdx).GetEmitterData();
					if (GetModED)
					{
						TArray<UNiagaraScript*> GetModScripts;
						GetModED->GetScripts(GetModScripts, false);
						for (UNiagaraScript* Sc : GetModScripts) { if (Sc && Sc->GetUsage() == Usage) { GetModScript = Sc; break; } }
					}
				}
				else
				{
					GetModScript = (Usage == ENiagaraScriptUsage::SystemSpawnScript) ? System->GetSystemSpawnScript() : System->GetSystemUpdateScript();
				}

				// Find specific input pin (include hidden pins for static-switch-gated inputs)
				FString InputName = NeoLuaStr::ToFString(PropertyOpt.value());
				for (UEdGraphPin* Pin : ModNode->GetAllPins())
				{
					if (Pin->Direction != EGPD_Input || IsParameterMapPin(Pin)) continue;
					FString PinName = Pin->GetName();
					FString ShortName = PinName;
					if (ShortName.Contains(TEXT(".")))
						ShortName = ShortName.RightChop(ShortName.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);

					if (PinName.Equals(InputName, ESearchCase::IgnoreCase) ||
						ShortName.Equals(InputName, ESearchCase::IgnoreCase))
					{
						sol::table R = Lua.create_table();
						R["name"] = TCHAR_TO_UTF8(*PinName);
						FNiagaraTypeDefinition PinType = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
						R["type"] = TCHAR_TO_UTF8(*PinType.GetName());
						R["linked"] = Pin->LinkedTo.Num() > 0;
						if (!Pin->DefaultValue.IsEmpty())
						{
							R["default_value"] = TCHAR_TO_UTF8(*Pin->DefaultValue);
							R["value"] = PinDefaultToSolValue(Pin->DefaultValue, PinType, Lua);
						}
						if (Pin->LinkedTo.Num() > 0)
						{
							UEdGraphNode* LinkedNode = Pin->LinkedTo[0]->GetOwningNode();
							if (UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(LinkedNode))
								R["linked_to"] = TCHAR_TO_UTF8(*FC->GetFunctionName());
							else if (UNiagaraNodeCustomHlsl* Hlsl = Cast<UNiagaraNodeCustomHlsl>(LinkedNode))
								R["hlsl_code"] = TCHAR_TO_UTF8(*GetCustomHlslText(Hlsl));
						}

						// Also check RI params for the current value. Read unconditionally —
						// `ShouldUseRI` only controls whether the *compiled* script consumes
						// the RI store at runtime; the edit-time RI store still holds the
						// authored value. Gating the read on it caused get("module", input)
						// to return pin info without `value`/`value_mode` when RI was baked
						// out, even though list("module_inputs") (which doesn't gate) showed
						// it correctly. Same class of bug as the configure-write gate fixed
						// at line 3262.
						if (GetModScript)
						{
							FNiagaraVariable PinVar(PinType, Pin->GetFName());
							FNiagaraParameterHandle PinHandle(PinVar.GetName());
							FNiagaraParameterHandle AliasedPinHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(PinHandle, ModNode);
							FNiagaraVariable AliasedPinVar(PinType, AliasedPinHandle.GetParameterHandleString());

							FNiagaraEmitterHandle DummyHandle2;
							const FNiagaraEmitterHandle& RIEmHandle = GetModEmHandle ? *GetModEmHandle : DummyHandle2;
							const TCHAR* RIEmName = nullptr;
							FString RIEmUniqueName;
							if (RIEmHandle.IsValid() && RIEmHandle.GetInstance().Emitter)
							{
								RIEmUniqueName = RIEmHandle.GetInstance().Emitter->GetUniqueEmitterName();
								if (!RIEmUniqueName.IsEmpty()) RIEmName = *RIEmUniqueName;
							}
							FNiagaraVariable RIVar = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(AliasedPinVar, RIEmName, GetModScript->GetUsage());
							const int32* RIOffset = GetModScript->RapidIterationParameters.FindParameterOffset(RIVar);
							if (RIOffset)
							{
								R["value_mode"] = "rapid_iteration";
								if (PinType == FNiagaraTypeDefinition::GetFloatDef())
									R["value"] = GetModScript->RapidIterationParameters.GetParameterValue<float>(RIVar);
								else if (PinType == FNiagaraTypeDefinition::GetIntDef())
									R["value"] = GetModScript->RapidIterationParameters.GetParameterValue<int32>(RIVar);
								else if (PinType == FNiagaraTypeDefinition::GetBoolDef())
									R["value"] = GetModScript->RapidIterationParameters.GetParameterValue<FNiagaraBool>(RIVar).GetValue();
							}
						}

						Session.Log(FString::Printf(TEXT("[OK] get(\"module\", \"%s\", \"%s\")"), UTF8_TO_TCHAR(ModName.c_str()), *InputName));
						return R;
					}
				}
				// Pin search failed — try RI store directly by scanning all RI params for this module
				// Also check ALL emitter scripts, not just the one matching Usage, since RI params
				// may be stored on spawn/update scripts regardless of which stage the module is visually in
				{
					TArray<UNiagaraScript*> ScriptsToSearch;
					if (GetModScript) ScriptsToSearch.Add(GetModScript);
					if (EmIdx != INDEX_NONE)
					{
						FVersionedNiagaraEmitterData* SearchED = System->GetEmitterHandle(EmIdx).GetEmitterData();
						if (SearchED)
						{
							TArray<UNiagaraScript*> AllScripts;
							SearchED->GetScripts(AllScripts, false);
							for (UNiagaraScript* Sc : AllScripts) ScriptsToSearch.AddUnique(Sc);
						}
					}

					FString ModFuncName = ModNode->GetFunctionName();
					for (UNiagaraScript* SearchScript : ScriptsToSearch)
					{
						if (!SearchScript) continue;
						for (const FNiagaraVariableWithOffset& RIVar : SearchScript->RapidIterationParameters.ReadParameterVariables())
						{
							FString RIName = RIVar.GetName().ToString();
							if (!RIName.Contains(ModFuncName)) continue;

							FString ShortName = RIName;
							int32 LastDot = INDEX_NONE;
							if (ShortName.FindLastChar(TEXT('.'), LastDot))
								ShortName = ShortName.RightChop(LastDot + 1);

							if (!ShortName.Equals(InputName, ESearchCase::IgnoreCase)) continue;

							sol::table R = Lua.create_table();
							R["name"] = TCHAR_TO_UTF8(*ShortName);
							R["full_name"] = TCHAR_TO_UTF8(*RIName);
							R["type"] = TCHAR_TO_UTF8(*RIVar.GetType().GetName());
							R["value_mode"] = "rapid_iteration";

							const int32* Off = SearchScript->RapidIterationParameters.FindParameterOffset(RIVar);
							if (Off)
							{
								if (RIVar.GetType() == FNiagaraTypeDefinition::GetFloatDef())
									R["value"] = SearchScript->RapidIterationParameters.GetParameterValue<float>(RIVar);
								else if (RIVar.GetType() == FNiagaraTypeDefinition::GetIntDef())
									R["value"] = SearchScript->RapidIterationParameters.GetParameterValue<int32>(RIVar);
								else if (RIVar.GetType() == FNiagaraTypeDefinition::GetBoolDef())
									R["value"] = SearchScript->RapidIterationParameters.GetParameterValue<FNiagaraBool>(RIVar).GetValue();
							}
							Session.Log(FString::Printf(TEXT("[OK] get(\"module\", \"%s\", \"%s\")"), UTF8_TO_TCHAR(ModName.c_str()), *InputName));
							return R;
						}
					}
				}

				Session.Log(FString::Printf(TEXT("[FAIL] get(\"module\", \"%s\", \"%s\") -> input not found"), UTF8_TO_TCHAR(ModName.c_str()), *InputName));
				return sol::lua_nil;
			}

			Session.Log(FString::Printf(TEXT("[FAIL] get(\"%s\") -> unknown type. Valid: system, emitter, renderer, user_parameter, module"), *FType));
			return sol::lua_nil;
		});

		// ==================================================================
		// version(verb, params?) — emitter versioning
		// ==================================================================
		AssetObj.set_function("version", [System, &Session](sol::table /*self*/,
			const std::string& Verb, sol::optional<sol::table> ParamsOpt, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FVerb = NeoLuaStr::ToFString(Verb);

			if (!ParamsOpt.has_value()) { Session.Log(TEXT("[FAIL] version() -> params table required")); return sol::lua_nil; }
			sol::table P = ParamsOpt.value();
			std::string EmitterName = P.get_or<std::string>("emitter", "");

			if (EmitterName.empty()) { Session.Log(TEXT("[FAIL] version() -> emitter name required")); return sol::lua_nil; }
			FString FEmitter = NeoLuaStr::ToFString(EmitterName);
			int32 EmIdx = FindEmitterIndex(System, FEmitter);
			if (EmIdx == INDEX_NONE) { Session.Log(FString::Printf(TEXT("[FAIL] version() -> emitter '%s' not found"), *FEmitter)); return sol::lua_nil; }

			FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(EmIdx);
			UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
			if (!Emitter) { Session.Log(TEXT("[FAIL] version() -> no emitter object")); return sol::lua_nil; }

			// ---- version("list") ----
			if (FVerb.Equals(TEXT("list"), ESearchCase::IgnoreCase))
			{
				TArray<FNiagaraAssetVersion> Versions = Emitter->GetAllAvailableVersions();
				FNiagaraAssetVersion ExposedVer = Emitter->GetExposedVersion();

				sol::table Result = Lua.create_table();
				for (int32 i = 0; i < Versions.Num(); ++i)
				{
					sol::table V = Lua.create_table();
					V["major"] = Versions[i].MajorVersion;
					V["minor"] = Versions[i].MinorVersion;
					V["version_guid"] = TCHAR_TO_UTF8(*Versions[i].VersionGuid.ToString());
					V["is_exposed"] = (Versions[i].VersionGuid == ExposedVer.VersionGuid);
					V["visible_in_selector"] = Versions[i].bIsVisibleInVersionSelector;
					Result[i + 1] = V;
				}
				Session.Log(FString::Printf(TEXT("[OK] version(\"list\", emitter=\"%s\") -> %d versions"), *FEmitter, Versions.Num()));
				return Result;
			}
			// ---- version("add") ----
			else if (FVerb.Equals(TEXT("add"), ESearchCase::IgnoreCase))
			{
				int32 Major = static_cast<int32>(P.get_or("major", 1));
				int32 Minor = static_cast<int32>(P.get_or("minor", 0));

				if (Major <= 1 && Minor <= 0) { Session.Log(TEXT("[FAIL] version(\"add\") -> version must be > 1.0")); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNVersion", "Add Niagara Emitter Version"));
				Emitter->Modify();

				if (!Emitter->IsVersioningEnabled())
					Emitter->EnableVersioning();

				FGuid NewGuid = Emitter->AddNewVersion(Major, Minor);
				if (!NewGuid.IsValid())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] version(\"add\") -> failed to add version %d.%d (may already exist)"), Major, Minor));
					return sol::lua_nil;
				}

				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] version(\"add\", emitter=\"%s\", %d.%d) -> %s"), *FEmitter, Major, Minor, *NewGuid.ToString()));
				sol::table Result = Lua.create_table();
				Result["version_guid"] = TCHAR_TO_UTF8(*NewGuid.ToString());
				Result["major"] = Major;
				Result["minor"] = Minor;
				return Result;
			}
			// ---- version("expose") ----
			else if (FVerb.Equals(TEXT("expose"), ESearchCase::IgnoreCase))
			{
				std::string GuidStr = P.get_or<std::string>("version_guid", "");
				if (GuidStr.empty()) { Session.Log(TEXT("[FAIL] version(\"expose\") -> version_guid required")); return sol::lua_nil; }

				FGuid VersionGuid;
				FGuid::Parse(NeoLuaStr::ToFString(GuidStr), VersionGuid);
				if (!VersionGuid.IsValid()) { Session.Log(TEXT("[FAIL] version(\"expose\") -> invalid guid")); return sol::lua_nil; }

				const FNiagaraAssetVersion* FoundVersion = Emitter->FindVersionData(VersionGuid);
				if (!FoundVersion) { Session.Log(FString::Printf(TEXT("[FAIL] version(\"expose\") -> version '%s' not found"), UTF8_TO_TCHAR(GuidStr.c_str()))); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "ExposeNVersion", "Expose Niagara Emitter Version"));
				Emitter->Modify();
				Emitter->ExposeVersion(VersionGuid);
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] version(\"expose\", emitter=\"%s\", guid=\"%s\")"), *FEmitter, UTF8_TO_TCHAR(GuidStr.c_str())));
				return sol::make_object(Lua, true);
			}
			// ---- version("delete") ----
			else if (FVerb.Equals(TEXT("delete"), ESearchCase::IgnoreCase))
			{
				std::string GuidStr = P.get_or<std::string>("version_guid", "");
				if (GuidStr.empty()) { Session.Log(TEXT("[FAIL] version(\"delete\") -> version_guid required")); return sol::lua_nil; }

				FGuid VersionGuid;
				FGuid::Parse(NeoLuaStr::ToFString(GuidStr), VersionGuid);
				if (!VersionGuid.IsValid()) { Session.Log(TEXT("[FAIL] version(\"delete\") -> invalid guid")); return sol::lua_nil; }

				// Cannot delete the exposed version
				FNiagaraAssetVersion ExposedVer = Emitter->GetExposedVersion();
				if (VersionGuid == ExposedVer.VersionGuid)
				{
					Session.Log(TEXT("[FAIL] version(\"delete\") -> cannot delete the currently exposed version"));
					return sol::lua_nil;
				}

				const FNiagaraAssetVersion* FoundVersion = Emitter->FindVersionData(VersionGuid);
				if (!FoundVersion) { Session.Log(FString::Printf(TEXT("[FAIL] version(\"delete\") -> version '%s' not found"), UTF8_TO_TCHAR(GuidStr.c_str()))); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "DelNVersion", "Delete Niagara Emitter Version"));
				Emitter->Modify();
				Emitter->DeleteVersion(VersionGuid);
				System->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] version(\"delete\", emitter=\"%s\", guid=\"%s\")"), *FEmitter, UTF8_TO_TCHAR(GuidStr.c_str())));
				return sol::make_object(Lua, true);
			}

			Session.Log(FString::Printf(TEXT("[FAIL] version(\"%s\") -> unknown verb. Valid: list, add, expose, delete"), *FVerb));
			return sol::lua_nil;
		});

		// ==================================================================
		// compile() — request compile and report status
		// ==================================================================
		AssetObj.set_function("compile", [System, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			System->RequestCompile(true);
			System->WaitForCompilationComplete(true);

			sol::table Result = Lua.create_table();
			sol::table Errors = Lua.create_table();
			int32 ErrorIdx = 1;

			TArray<UNiagaraScript*> AllScripts;
			AllScripts.Add(System->GetSystemSpawnScript());
			AllScripts.Add(System->GetSystemUpdateScript());
			for (int32 i = 0; i < System->GetNumEmitters(); ++i)
			{
				FVersionedNiagaraEmitterData* ED = System->GetEmitterHandle(i).GetEmitterData();
				if (ED)
				{
					TArray<UNiagaraScript*> EmitterScripts;
					ED->GetScripts(EmitterScripts, false);
					AllScripts.Append(EmitterScripts);
				}
			}

			for (UNiagaraScript* Script : AllScripts)
			{
				if (!Script) continue;
				const TArray<FNiagaraCompileEvent>& CompileEvents = Script->GetVMExecutableData().LastCompileEvents;
				for (const FNiagaraCompileEvent& Evt : CompileEvents)
				{
					if (Evt.Severity >= FNiagaraCompileEventSeverity::Warning)
					{
						sol::table E = Lua.create_table();
						E["severity"] = (Evt.Severity == FNiagaraCompileEventSeverity::Error) ? "error" : "warning";
						E["message"] = TCHAR_TO_UTF8(*Evt.Message);
						if (!Evt.ShortDescription.IsEmpty())
							E["short_description"] = TCHAR_TO_UTF8(*Evt.ShortDescription);
						if (Evt.NodeGuid.IsValid())
							E["node_guid"] = TCHAR_TO_UTF8(*Evt.NodeGuid.ToString());
						if (Evt.PinGuid.IsValid())
							E["pin_guid"] = TCHAR_TO_UTF8(*Evt.PinGuid.ToString());
						if (Evt.StackGuids.Num() > 0)
						{
							sol::table StackGuids = Lua.create_table();
							for (int32 gi = 0; gi < Evt.StackGuids.Num(); ++gi)
								StackGuids[gi + 1] = TCHAR_TO_UTF8(*Evt.StackGuids[gi].ToString());
							E["stack_guids"] = StackGuids;
						}
						E["script"] = TCHAR_TO_UTF8(*Script->GetName());
						Errors[ErrorIdx++] = E;
					}
				}
			}

			Result["errors"] = Errors;
			Result["error_count"] = ErrorIdx - 1;
			Result["success"] = (ErrorIdx == 1);
			// Also add errors as integer keys on top level so ipairs(result) works
			for (int32 i = 1; i < ErrorIdx; i++)
				Result[i] = Errors[i];
			Session.Log(FString::Printf(TEXT("[OK] compile() -> %d errors/warnings"), ErrorIdx - 1));
			return Result;
		});
	});

	// ==========================================================================
	// niagara_module_schema(script_asset_path) — describe a Niagara Module
	// script asset's inputs without instantiating it. Mirrors Epic's 5.8
	// GetModuleSchemaFromAsset, built on 5.7-public APIs:
	//   • UNiagaraScript->GetVMExecutableData().Parameters.Parameters
	//     (filtered by "Module." prefix — same path the system enrichment uses
	//     when reading function-call inputs)
	//   • UNiagaraGraph::GetMetaData(FNiagaraVariable) for tooltip/category
	//
	// Returns: {asset_path, usage, name, inputs=[{name, type, tooltip, category,
	// description, has_default, advanced}]} or nil if the asset isn't a script
	// or its usage doesn't match the expected kind. Empty inputs[] with a
	// [WARN] log if the script hasn't been compiled yet.
	// ==========================================================================
	auto BuildScriptSchema = [&Session](UNiagaraScript* Script, const TCHAR* VerbName,
		ENiagaraScriptUsage ExpectedUsage, sol::state_view L) -> sol::object
	{
		if (Script->GetUsage() != ExpectedUsage)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] %s -> '%s' has usage '%s', expected '%s'"),
				VerbName, *Script->GetPathName(),
				*UEnum::GetValueAsString(Script->GetUsage()),
				*UEnum::GetValueAsString(ExpectedUsage)));
			return sol::lua_nil;
		}

#if NSAI_NIAGARA_HAS_EXTERNAL_EDIT_UTILITIES
		auto BuildExternalSchemaResult = [&Session, VerbName, Script, L](const FNiagaraExt_ModuleSchema& Schema) mutable -> sol::object
		{
			sol::table Result = L.create_table();
			Result["asset_path"] = TCHAR_TO_UTF8(*Script->GetPathName());
			Result["name"] = TCHAR_TO_UTF8(*Script->GetName());
			Result["usage"] = TCHAR_TO_UTF8(*UEnum::GetValueAsString(Script->GetUsage()));

			sol::table InputsArray = L.create_table();
			int32 InputIdx = 1;
			for (const FNiagaraExt_StackInputSchema& Input : Schema.Inputs)
			{
				sol::table Entry = L.create_table();
				Entry["name"] = TCHAR_TO_UTF8(*Input.Name.ToString());
				Entry["type"] = TCHAR_TO_UTF8(*Input.Type.GetName());
				Entry["category"] = TCHAR_TO_UTF8(*Input.Category.ToString());
				Entry["supports_expressions"] = Input.bSupportsExpressions;
				Entry["tooltip"] = TCHAR_TO_UTF8(*Input.MetaData.Description.ToString());
				Entry["description"] = TCHAR_TO_UTF8(*Input.MetaData.Description.ToString());
				Entry["advanced"] = Input.MetaData.bAdvancedDisplay;
				Entry["sort_priority"] = Input.MetaData.InlineParameterSortPriority;
				InputsArray[InputIdx++] = Entry;
			}

			sol::table OutputsArray = L.create_table();
			int32 OutputIdx = 1;
			for (const FNiagaraExt_Variable& Output : Schema.Outputs)
			{
				sol::table Entry = L.create_table();
				Entry["name"] = TCHAR_TO_UTF8(*Output.Name.ToString());
				Entry["type"] = TCHAR_TO_UTF8(*Output.Type.GetName());
				OutputsArray[OutputIdx++] = Entry;
			}

			Result["inputs"] = InputsArray;
			Result["input_count"] = InputIdx - 1;
			Result["outputs"] = OutputsArray;
			Result["output_count"] = OutputIdx - 1;

			Session.Log(FString::Printf(TEXT("[OK] %s -> '%s' (%d inputs, %d outputs)"),
				VerbName, *Script->GetName(), InputIdx - 1, OutputIdx - 1));
			return sol::make_object(L, Result);
		};

		FNiagaraExternalEditContext Context;
		if (ExpectedUsage == ENiagaraScriptUsage::Module)
		{
			FNiagaraExt_ModuleSchema Schema;
			UNiagaraExternalEditUtilities::GetModuleSchema(Script, Schema, Context);
			if (Context.HasErrors())
			{
				LogExternalEditErrors(Session, Context, VerbName);
				return sol::lua_nil;
			}
			return BuildExternalSchemaResult(Schema);
		}
		if (ExpectedUsage == ENiagaraScriptUsage::DynamicInput)
		{
			FNiagaraExt_DynamicInputSchema Schema;
			UNiagaraExternalEditUtilities::GetDynamicInputSchema(Script, Schema, Context);
			if (Context.HasErrors())
			{
				LogExternalEditErrors(Session, Context, VerbName);
				return sol::lua_nil;
			}
			return BuildExternalSchemaResult(Schema);
		}
#endif

		sol::table Result = L.create_table();
		Result["asset_path"] = TCHAR_TO_UTF8(*Script->GetPathName());
		Result["name"]       = TCHAR_TO_UTF8(*Script->GetName());
		Result["usage"]      = TCHAR_TO_UTF8(*UEnum::GetValueAsString(Script->GetUsage()));

		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		UNiagaraGraph* Graph = Source ? Source->NodeGraph : nullptr;

		sol::table InputsArray = L.create_table();
		int32 Idx = 1;
		const TArray<FNiagaraVariable>& Params = Script->GetVMExecutableData().Parameters.Parameters;
		for (const FNiagaraVariable& Var : Params)
		{
			FString VarName = Var.GetName().ToString();
			if (!VarName.StartsWith(TEXT("Module."))) continue;
			FString ShortName = VarName.RightChop(7); // strip "Module."

			sol::table Entry = L.create_table();
			Entry["name"] = TCHAR_TO_UTF8(*ShortName);
			Entry["type"] = TCHAR_TO_UTF8(*Var.GetType().GetName());
			Entry["has_default"] = Var.IsDataAllocated();

			if (Graph)
			{
				TOptional<FNiagaraVariableMetaData> Meta = Graph->GetMetaData(Var);
				if (Meta.IsSet())
				{
					Entry["tooltip"]     = TCHAR_TO_UTF8(*Meta->Description.ToString());
					Entry["description"] = TCHAR_TO_UTF8(*Meta->Description.ToString());
					Entry["advanced"]    = Meta->bAdvancedDisplay;
					Entry["sort_priority"] = Meta->InlineParameterSortPriority;
					// CategoryName is deprecated on 5.7+ — Epic 5.8 commented it out too.
					// Fall back to the freeform PropertyMetaData map if a Category was set.
					if (const FString* Cat = Meta->PropertyMetaData.Find(TEXT("Category")))
					{
						Entry["category"] = TCHAR_TO_UTF8(**Cat);
					}
				}
			}
			InputsArray[Idx++] = Entry;
		}

		Result["inputs"] = InputsArray;
		Result["input_count"] = Idx - 1;

		if (Idx == 1 && Params.Num() == 0)
		{
			Session.Log(FString::Printf(TEXT("[WARN] %s -> '%s' has no compiled parameters (compile the script first to populate the schema)"),
				VerbName, *Script->GetPathName()));
		}
		else
		{
			Session.Log(FString::Printf(TEXT("[OK] %s -> '%s' (%d inputs)"),
				VerbName, *Script->GetName(), Idx - 1));
		}
		return sol::make_object(L, Result);
	};

	Lua.set_function("niagara_module_schema",
		[&Session, BuildScriptSchema](const std::string& AssetPath, sol::this_state S) -> sol::object
		{
			sol::state_view L(S);
			FString FPath = NeoLuaStr::ToFString(AssetPath);
			UNiagaraScript* Script = NeoLuaAsset::Resolve<UNiagaraScript>(FPath);
			if (!Script)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] niagara_module_schema(\"%s\") -> not a UNiagaraScript"), *FPath));
				return sol::lua_nil;
			}
			return BuildScriptSchema(Script, TEXT("niagara_module_schema"), ENiagaraScriptUsage::Module, L);
		});

	Lua.set_function("niagara_dynamic_input_schema",
		[&Session, BuildScriptSchema](const std::string& AssetPath, sol::this_state S) -> sol::object
		{
			sol::state_view L(S);
			FString FPath = NeoLuaStr::ToFString(AssetPath);
			UNiagaraScript* Script = NeoLuaAsset::Resolve<UNiagaraScript>(FPath);
			if (!Script)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] niagara_dynamic_input_schema(\"%s\") -> not a UNiagaraScript"), *FPath));
				return sol::lua_nil;
			}
			return BuildScriptSchema(Script, TEXT("niagara_dynamic_input_schema"), ENiagaraScriptUsage::DynamicInput, L);
		});
}

static void BindNiagaraGraph(sol::state& Lua, FLuaSessionData& Session)
{
	Lua.set_function("_niagara_add_node", [&Session](sol::lightuserdata_value GraphUD,
		sol::object, double, double, sol::this_state S) -> sol::object
	{
		UNiagaraGraph* Graph = Cast<UNiagaraGraph>(reinterpret_cast<UEdGraph*>(GraphUD.value));
		if (!Graph)
		{
			Session.Log(TEXT("[FAIL] _niagara_add_node -> invalid Niagara graph"));
			return sol::lua_nil;
		}

		Session.Log(TEXT("[FAIL] add_node on Niagara graph is not supported. Use open_asset(...).add(\"module\", ...), add(\"emitter\", ...), add(\"renderer\", ...) or configure(\"module\", ...) instead."));
		return sol::lua_nil;
	});

	Lua.set_function("_niagara_find_nodes", [&Session](sol::lightuserdata_value GraphUD,
		const std::string&, int32, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UNiagaraGraph* Graph = Cast<UNiagaraGraph>(reinterpret_cast<UEdGraph*>(GraphUD.value));
		if (!Graph)
		{
			Session.Log(TEXT("[FAIL] _niagara_find_nodes -> invalid Niagara graph"));
			return LuaView.create_table();
		}

		Session.Log(TEXT("[FAIL] find_nodes on Niagara graph is not supported. Use open_asset(...).list(\"available_modules\"), list(\"modules\"), add(\"module\", ...) or configure(\"module\", ...) instead."));
		return LuaView.create_table();
	});
}

// ============================================================================
// UNiagaraParameterDefinitions binding (asset-level parameter library CRUD)
// ============================================================================
//
// UNiagaraParameterDefinitions lives in the NiagaraEditor module as UCLASS(MinimalAPI),
// meaning the class is exported but its mutator methods (AddParameter / RemoveParameter /
// RenameParameter / SubscribeBindingNameToExternalParameterDefinitions / etc.) are NOT
// NIAGARAEDITOR_API-exported. We replicate their behaviour via UPROPERTY reflection on
// the private `ScriptVariables` TArray plus UNiagaraScriptVariable's public inline setters
// (Init / SetIsStaticSwitch / SetIsSubscribedToParameterDefinitions are exported).
//
// This matches how we handle other MinimalAPI types elsewhere in the binding
// (e.g. UNiagaraStatelessEmitter) and keeps the linker happy across UE versions.

namespace
{

// Returns the TArray<TObjectPtr<UNiagaraScriptVariable>> FArrayProperty on the definitions
// class, cached across calls. UNiagaraParameterDefinitions::ScriptVariables is the only
// UPROPERTY TArray of its inner type on this class, so cache is safe.
FArrayProperty* GetDefinitionsScriptVarsProperty(UNiagaraParameterDefinitions* Defs)
{
	static FArrayProperty* Cached = nullptr;
	if (Cached) return Cached;
	if (!Defs) return nullptr;
	Cached = CastField<FArrayProperty>(Defs->GetClass()->FindPropertyByName(TEXT("ScriptVariables")));
	return Cached;
}

// Walk ScriptVariables via reflection — returns the (TObjectPtr-unwrapped) pointers.
TArray<UNiagaraScriptVariable*> GetDefinitionsScriptVars(UNiagaraParameterDefinitions* Defs)
{
	TArray<UNiagaraScriptVariable*> Out;
	FArrayProperty* ArrProp = GetDefinitionsScriptVarsProperty(Defs);
	if (!Defs || !ArrProp) return Out;
	FObjectPropertyBase* InnerObjProp = CastField<FObjectPropertyBase>(ArrProp->Inner);
	if (!InnerObjProp) return Out;

	FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(Defs));
	Out.Reserve(Helper.Num());
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		UObject* Obj = InnerObjProp->GetObjectPropertyValue(Helper.GetRawPtr(i));
		Out.Add(Cast<UNiagaraScriptVariable>(Obj));
	}
	return Out;
}

UNiagaraScriptVariable* FindScriptVarByName(UNiagaraParameterDefinitions* Defs, const FName& VarName)
{
	for (UNiagaraScriptVariable* SV : GetDefinitionsScriptVars(Defs))
	{
		if (SV && SV->Variable.GetName() == VarName) return SV;
	}
	return nullptr;
}

// Serialize the default value portion of a script var — accepts a Lua value
// matching the variable's type and writes into UNiagaraScriptVariable's
// DefaultValueVariant via the inline SetDefaultValueData helper.
bool ApplyDefaultValueToScriptVar(UNiagaraScriptVariable* SV, const sol::object& Value, FString& OutError)
{
	if (!SV || !SV->Variable.GetType().IsValid())
	{
		OutError = TEXT("script variable has no valid type");
		return false;
	}
	TArray<uint8> Bytes;
	if (!NSAINiagaraLCD::SerializeSolValueToTypedBytes(Value, SV->Variable.GetType(), Bytes, OutError))
	{
		return false;
	}
	SV->SetDefaultValueData(Bytes.GetData());
	SV->UpdateChangeId();
	return true;
}

// Apply a metadata table to FNiagaraVariableMetaData. Supported keys:
//   description, category, editor_advanced_display, widget_custom_sort, widget_segmented
// (Field names follow the USTRUCT layout; unknown keys are ignored silently — the
// LCD walker would warn about them, but we don't want to spam for optional metadata.)
void ApplyMetadataTable(UNiagaraScriptVariable* SV, const sol::table& MetaTable)
{
	UScriptStruct* MetaStruct = FNiagaraVariableMetaData::StaticStruct();
	FString Err;
	TArray<FString> Warnings;
	NeoLuaProperty::ApplyTableToStruct(MetaStruct, &SV->Metadata, SV,
		MetaTable, Err, &Warnings);
}

sol::table ReadScriptVarAsTable(UNiagaraScriptVariable* SV, sol::state_view Lua)
{
	sol::table Out = Lua.create_table();
	if (!SV) return Out;
	Out["name"] = TCHAR_TO_UTF8(*SV->Variable.GetName().ToString());
	Out["type"] = TCHAR_TO_UTF8(*SV->Variable.GetType().GetName());
	Out["guid"] = TCHAR_TO_UTF8(*SV->Metadata.GetVariableGuid().ToString());

	// Default value round-trip (may be nil if the variant hasn't been populated yet).
	if (const uint8* DefBytes = SV->GetDefaultValueData())
	{
		Out["default"] = NSAINiagaraLCD::ReadTypedBytesAsSolValue(DefBytes, SV->Variable.GetType(), Lua);
	}
	Out["is_static_switch"] = SV->GetIsStaticSwitch();
	Out["is_subscribed"]    = SV->GetIsSubscribedToParameterDefinitions();

	// Metadata nested table — use the LCD reader so the full USTRUCT is surfaced.
	Out["metadata"] = NeoLuaProperty::ReadStructAsTable(
		FNiagaraVariableMetaData::StaticStruct(), &SV->Metadata, Lua);
	return Out;
}

} // namespace

static void BindNiagaraParamDefs(sol::state& Lua, FLuaSessionData& Session)
{
	Lua.set_function("_enrich_niagara_param_defs", [&Session](sol::table AssetObj, sol::this_state S)
	{
		sol::state_view LuaView(S);
		std::string PathStr = AssetObj.get<std::string>("path");
		FString FPath = NeoLuaStr::ToFString(PathStr);
		UNiagaraParameterDefinitions* Defs = NeoLuaAsset::Resolve<UNiagaraParameterDefinitions>(FPath);
		if (!Defs) return;

		AssetObj["_help_text"] =
			"Element types for add/remove/list/configure:\n"
			"  parameter, subscriber\n"
			"\n"
			"add(\"parameter\", {name=\"MyFloat\", type=\"Float\", default=1.0, metadata={...}})\n"
			"remove(\"parameter\", {name=\"MyFloat\"})\n"
			"configure(\"parameter\", \"MyFloat\", {default=2.0, metadata={...}})\n"
			"configure(\"definition\", {promote_to_top=true, menu_sort_order=10})\n"
			"rename_parameter(old_name, new_name)\n"
			"list(\"parameters\") -> [{name,type,guid,default,metadata}]\n";

		// --------------------------------------------------------------
		// add(type, params)
		// --------------------------------------------------------------
		AssetObj.set_function("add", [Defs, &Session](sol::table /*self*/,
			const std::string& Type, sol::optional<sol::table> Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);
			if (!Params.has_value()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> params required"), *FType)); return sol::lua_nil; }
			sol::table P = Params.value();

			if (FType.Equals(TEXT("parameter"), ESearchCase::IgnoreCase))
			{
				std::string Name = P.get_or<std::string>("name", "");
				std::string TypeName = P.get_or<std::string>("type", "");
				if (Name.empty() || TypeName.empty())
				{
					Session.Log(TEXT("[FAIL] add(\"parameter\") -> name and type required"));
					return sol::lua_nil;
				}
				FString FName = NeoLuaStr::ToFString(Name);
				FString FTypeName = NeoLuaStr::ToFString(TypeName);

				FNiagaraTypeDefinition TypeDef = NSAINiagaraLCD::ResolveTypeDefinition(FTypeName);
				if (!TypeDef.IsValid())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"parameter\") -> unknown type '%s'"), *FTypeName));
					return sol::lua_nil;
				}

				FNiagaraVariable NewVar(TypeDef, ::FName(*FName));

				// Existence check (non-exported HasParameter is reimplemented via reflection).
				if (FindScriptVarByName(Defs, NewVar.GetName()))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"parameter\") -> '%s' already exists"), *FName));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddNiagaraDefsParam", "Add Parameter Definition"));
				Defs->Modify();

				FArrayProperty* ArrProp = GetDefinitionsScriptVarsProperty(Defs);
				FObjectPropertyBase* InnerObjProp = ArrProp ? CastField<FObjectPropertyBase>(ArrProp->Inner) : nullptr;
				if (!ArrProp || !InnerObjProp)
				{
					Session.Log(TEXT("[FAIL] add(\"parameter\") -> ScriptVariables UPROPERTY not found"));
					return sol::lua_nil;
				}

				UNiagaraScriptVariable* NewSV = NewObject<UNiagaraScriptVariable>(Defs, ::FName(), RF_Transactional);
				NewSV->Init(NewVar, FNiagaraVariableMetaData());
				NewSV->SetIsStaticSwitch(false);
				NewSV->SetIsSubscribedToParameterDefinitions(true);

				FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(Defs));
				const int32 NewIdx = Helper.AddValue();
				InnerObjProp->SetObjectPropertyValue(Helper.GetRawPtr(NewIdx), NewSV);

				// Optional default value.
				sol::object DefObj = P["default"];
				if (!DefObj.valid() || DefObj.is<sol::lua_nil_t>()) DefObj = P["default_value"];
				if (DefObj.valid() && !DefObj.is<sol::lua_nil_t>())
				{
					FString DErr;
					if (!ApplyDefaultValueToScriptVar(NewSV, DefObj, DErr))
					{
						Session.Log(FString::Printf(TEXT("[WARN] add(\"parameter\") default: %s"), *DErr));
					}
				}

				// Optional metadata.
				if (sol::optional<sol::table> MetaOpt = P.get<sol::optional<sol::table>>("metadata"))
				{
					ApplyMetadataTable(NewSV, MetaOpt.value());
				}

				Defs->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"parameter\", \"%s\", type=\"%s\")"), *FName, *FTypeName));
				return sol::make_object(Lua, true);
			}

			Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> unknown type. Valid: parameter"), *FType));
			return sol::lua_nil;
		});

		// --------------------------------------------------------------
		// remove(type, id)
		// --------------------------------------------------------------
		AssetObj.set_function("remove", [Defs, &Session](sol::table /*self*/,
			const std::string& Type, sol::object Id, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);

			if (FType.Equals(TEXT("parameter"), ESearchCase::IgnoreCase))
			{
				std::string Name;
				if (Id.is<sol::table>()) Name = Id.as<sol::table>().get_or<std::string>("name", "");
				else if (Id.is<std::string>()) Name = Id.as<std::string>();
				if (Name.empty()) { Session.Log(TEXT("[FAIL] remove(\"parameter\") -> name required")); return sol::lua_nil; }

				FString FName = NeoLuaStr::ToFString(Name);
				FArrayProperty* ArrProp = GetDefinitionsScriptVarsProperty(Defs);
				FObjectPropertyBase* InnerObjProp = ArrProp ? CastField<FObjectPropertyBase>(ArrProp->Inner) : nullptr;
				if (!ArrProp || !InnerObjProp) { Session.Log(TEXT("[FAIL] remove(\"parameter\") -> reflection missing")); return sol::lua_nil; }

				FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(Defs));
				for (int32 i = 0; i < Helper.Num(); ++i)
				{
					UObject* Obj = InnerObjProp->GetObjectPropertyValue(Helper.GetRawPtr(i));
					UNiagaraScriptVariable* SV = Cast<UNiagaraScriptVariable>(Obj);
					if (SV && SV->Variable.GetName().ToString().Equals(FName, ESearchCase::IgnoreCase))
					{
						const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemNiagaraDefsParam", "Remove Parameter Definition"));
						Defs->Modify();
						Helper.RemoveValues(i, 1);
						Defs->MarkPackageDirty();
						Session.Log(FString::Printf(TEXT("[OK] remove(\"parameter\", \"%s\")"), *FName));
						return sol::make_object(Lua, true);
					}
				}
				Session.Log(FString::Printf(TEXT("[FAIL] remove(\"parameter\") -> '%s' not found"), *FName));
				return sol::lua_nil;
			}

			Session.Log(FString::Printf(TEXT("[FAIL] remove(\"%s\") -> unknown type. Valid: parameter"), *FType));
			return sol::lua_nil;
		});

		// --------------------------------------------------------------
		// configure(type, id, params)
		// --------------------------------------------------------------
		AssetObj.set_function("configure", [Defs, &Session](sol::table /*self*/,
			const std::string& Type, sol::object Id, sol::optional<sol::table> ParamsOpt, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);

			if (FType.Equals(TEXT("parameter"), ESearchCase::IgnoreCase))
			{
				std::string Name = Id.is<std::string>() ? Id.as<std::string>() : "";
				if (Name.empty()) { Session.Log(TEXT("[FAIL] configure(\"parameter\") -> name required (2nd arg)")); return sol::lua_nil; }
				if (!ParamsOpt.has_value()) { Session.Log(TEXT("[FAIL] configure(\"parameter\") -> params required")); return sol::lua_nil; }

				UNiagaraScriptVariable* SV = FindScriptVarByName(Defs, ::FName(UTF8_TO_TCHAR(Name.c_str())));
				if (!SV) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"parameter\") -> '%s' not found"), UTF8_TO_TCHAR(Name.c_str()))); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNiagaraDefsParam", "Configure Parameter Definition"));
				Defs->Modify();
				SV->Modify();

				sol::table P = ParamsOpt.value();
				bool bChanged = false;

				sol::object DefObj = P["default"];
				if (!DefObj.valid() || DefObj.is<sol::lua_nil_t>()) DefObj = P["default_value"];
				if (DefObj.valid() && !DefObj.is<sol::lua_nil_t>())
				{
					FString DErr;
					if (ApplyDefaultValueToScriptVar(SV, DefObj, DErr)) bChanged = true;
					else Session.Log(FString::Printf(TEXT("[WARN] configure(\"parameter\") default: %s"), *DErr));
				}

				if (sol::optional<sol::table> MetaOpt = P.get<sol::optional<sol::table>>("metadata"))
				{
					ApplyMetadataTable(SV, MetaOpt.value());
					bChanged = true;
				}

				if (sol::optional<bool> StaticSwitch = P.get<sol::optional<bool>>("is_static_switch"))
				{
					SV->SetIsStaticSwitch(StaticSwitch.value());
					bChanged = true;
				}

				if (bChanged) SV->UpdateChangeId();
				Defs->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] configure(\"parameter\", \"%s\")"), UTF8_TO_TCHAR(Name.c_str())));
				return sol::make_object(Lua, bChanged);
			}

			if (FType.Equals(TEXT("definition"), ESearchCase::IgnoreCase))
			{
				if (!ParamsOpt.has_value()) { Session.Log(TEXT("[FAIL] configure(\"definition\") -> params required")); return sol::lua_nil; }
				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgNiagaraDefs", "Configure Parameter Definitions"));
				Defs->Modify();

				FString Err;
				TArray<FString> Warnings;
				NeoLuaProperty::ApplyTable(Defs, ParamsOpt.value(), Err, &Warnings);
				for (const FString& W : Warnings)
					Session.Log(FString::Printf(TEXT("[WARN] configure(\"definition\") %s"), *W));

				Defs->MarkPackageDirty();
				Session.Log(TEXT("[OK] configure(\"definition\")"));
				return sol::make_object(Lua, true);
			}

			Session.Log(FString::Printf(TEXT("[FAIL] configure(\"%s\") -> unknown type. Valid: parameter, definition"), *FType));
			return sol::lua_nil;
		});

		// --------------------------------------------------------------
		// rename_parameter(old_name, new_name)
		// --------------------------------------------------------------
		AssetObj.set_function("rename_parameter", [Defs, &Session](sol::table /*self*/,
			const std::string& OldName, const std::string& NewName, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			if (OldName.empty() || NewName.empty())
			{
				Session.Log(TEXT("[FAIL] rename_parameter -> both names required"));
				return sol::lua_nil;
			}
			UNiagaraScriptVariable* SV = FindScriptVarByName(Defs, ::FName(UTF8_TO_TCHAR(OldName.c_str())));
			if (!SV)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] rename_parameter -> '%s' not found"), UTF8_TO_TCHAR(OldName.c_str())));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RenNiagaraDefsParam", "Rename Parameter Definition"));
			Defs->Modify();
			SV->Modify();
			SV->Variable.SetName(::FName(UTF8_TO_TCHAR(NewName.c_str())));
			SV->UpdateChangeId();
			Defs->MarkPackageDirty();

			Session.Log(FString::Printf(TEXT("[OK] rename_parameter(\"%s\", \"%s\")"),
				UTF8_TO_TCHAR(OldName.c_str()), UTF8_TO_TCHAR(NewName.c_str())));
			return sol::make_object(Lua, true);
		});

		// --------------------------------------------------------------
		// list(type)
		// --------------------------------------------------------------
		AssetObj.set_function("list", [Defs, &Session](sol::table /*self*/,
			sol::optional<std::string> Type, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = Type.has_value()
				? NeoLuaStr::ToFString(Type.value())
				: TEXT("parameters");

			if (FType.Contains(TEXT("parameter"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lua.create_table();
				int32 Idx = 1;
				for (UNiagaraScriptVariable* SV : GetDefinitionsScriptVars(Defs))
				{
					if (!SV) continue;
					Result[Idx++] = ReadScriptVarAsTable(SV, Lua);
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"parameters\") -> %d"), Idx - 1));
				return sol::make_object(Lua, Result);
			}

			Session.Log(FString::Printf(TEXT("[FAIL] list(\"%s\") -> unknown type. Valid: parameters"), *FType));
			return sol::lua_nil;
		});

		// --------------------------------------------------------------
		// get(type, id) — single-entry read, mirrors list().
		// --------------------------------------------------------------
		AssetObj.set_function("get", [Defs, &Session](sol::table /*self*/,
			const std::string& Type, sol::object Id, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);

			if (FType.Equals(TEXT("parameter"), ESearchCase::IgnoreCase))
			{
				std::string Name;
				if (Id.is<std::string>()) Name = Id.as<std::string>();
				else if (Id.is<sol::table>()) Name = Id.as<sol::table>().get_or<std::string>("name", "");
				if (Name.empty()) { Session.Log(TEXT("[FAIL] get(\"parameter\") -> name required")); return sol::lua_nil; }

				UNiagaraScriptVariable* SV = FindScriptVarByName(Defs, ::FName(UTF8_TO_TCHAR(Name.c_str())));
				if (!SV) { Session.Log(FString::Printf(TEXT("[FAIL] get(\"parameter\") -> '%s' not found"), UTF8_TO_TCHAR(Name.c_str()))); return sol::lua_nil; }
				return sol::make_object(Lua, ReadScriptVarAsTable(SV, Lua));
			}

			Session.Log(FString::Printf(TEXT("[FAIL] get(\"%s\") -> unknown type. Valid: parameter"), *FType));
			return sol::lua_nil;
		});
	});
}

namespace NSAINiagaraExtension
{
	const TArray<FLuaFunctionDoc>& GetNiagaraLuaDocs()
	{
		return NiagaraDocs;
	}

	void BindNiagaraLua(sol::state& Lua, FLuaSessionData& Session)
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("Niagara")))
		{
			Session.Log(TEXT("[WARN] Niagara plugin is not loaded. Enable it in Edit > Plugins to use this feature."));
			return;
		}
		BindNiagara(Lua, Session);
		BindNiagaraGraph(Lua, Session);
		BindNiagaraParamDefs(Lua, Session);
	}
}
