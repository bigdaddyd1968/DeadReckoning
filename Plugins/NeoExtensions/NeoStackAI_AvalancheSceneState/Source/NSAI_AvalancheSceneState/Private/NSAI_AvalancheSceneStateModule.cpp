#include "Modules/ModuleManager.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "Lua/LuaBindingRegistry.h"
#include "Lua/LuaPropertyTable.h"

#include "AvaSceneStateActor.h"
#include "AvaSceneStateBlueprint.h"
#include "AvaSceneStateComponent.h"
#include "AvaSceneStatePlayer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"
#include "StructUtils/InstancedStruct.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/PackageName.h"
#include "Nodes/SceneStateMachineConduitNode.h"
#include "Nodes/SceneStateMachineEntryNode.h"
#include "Nodes/SceneStateMachineExitNode.h"
#include "Nodes/SceneStateMachineNode.h"
#include "Nodes/SceneStateMachineStateNode.h"
#include "Nodes/SceneStateMachineTaskNode.h"
#include "Nodes/SceneStateMachineTransitionNode.h"
#include "PropertyBindingDataView.h"
#include "PropertyBindingPath.h"
#include "SceneStateActor.h"
#include "SceneStateBinding.h"
#include "SceneStateBindingCollection.h"
#include "SceneStateBindingFunction.h"
#include "SceneStateBlueprint.h"
#include "SceneStateBlueprintFactory.h"
#include "SceneStateComponent.h"
#include "SceneStateDelayTask.h"
#include "SceneStateEventHandler.h"
#include "SceneStateEventSchema.h"
#include "SceneStateEventSchemaCollection.h"
#include "SceneStateEventSchemaHandle.h"
#include "SceneStateExecutionContext.h"
#include "SceneStateMachine.h"
#include "SceneStatePrintStringTask.h"
#include "SceneStateSpawnActorTask.h"
#include "SceneStateTaskBlueprint.h"
#include "SceneStateComponentPlayer.h"
#include "SceneStateGeneratedClass.h"
#include "SceneStateMachineGraph.h"
#include "SceneStateMachineGraphSchema.h"
#include "SceneStateObject.h"
#include "SceneStateTemplateData.h"
#include "SceneStateTransitionGraph.h"
#include "Nodes/SceneStateTransitionResultNode.h"
#include "ScopedTransaction.h"
#include "StructUtils/PropertyBag.h"
#include "Tasks/SceneStateBlueprintableTask.h"
#include "Tasks/SceneStateBlueprintableTaskWrapper.h"
#include "Tasks/SceneStateTaskGeneratedClass.h"
#include "Transition/SceneStateTransitionResult.h"
#include "UObject/Package.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

#include <sol/sol.hpp>

namespace
{
	const TCHAR* AvalancheSceneStateExtensionId = TEXT("neostack.avalanche_scene_state");

	FNeoStackExtensionDescriptor BuildAvalancheSceneStateDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = AvalancheSceneStateExtensionId;
		Descriptor.DisplayName = TEXT("Avalanche Scene State");
		Descriptor.ModuleName = TEXT("NSAI_AvalancheSceneState");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("motion-design");
		Descriptor.Description = TEXT("Author Motion Design Scene State actors from Lua.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("motion-design"), TEXT("scene-state"), TEXT("beta") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}

	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	bool IsUsableSceneStateActor(AAvaSceneStateActor* Actor)
	{
		return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && !Actor->IsPendingKillPending();
	}

	AAvaSceneStateActor* FindSceneStateActor(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		for (AAvaSceneStateActor* Actor : TActorRange<AAvaSceneStateActor>(World))
		{
			if (IsUsableSceneStateActor(Actor))
			{
				return Actor;
			}
		}
		return nullptr;
	}

	UObject* GetSceneStateBlueprintObject(AAvaSceneStateActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), TEXT("SceneStateBlueprint")))
		{
			return Property->GetObjectPropertyValue(Property->ContainerPtrToValuePtr<void>(Actor));
		}
		return nullptr;
	}

	void SetSceneStateBlueprintObject(AAvaSceneStateActor* Actor, USceneStateBlueprint* Blueprint)
	{
		if (!Actor)
		{
			return;
		}

		if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Actor->GetClass(), TEXT("SceneStateBlueprint")))
		{
			Property->SetObjectPropertyValue(Property->ContainerPtrToValuePtr<void>(Actor), Blueprint);
		}
	}

	USceneStateComponent* GetSceneStateComponent(AAvaSceneStateActor* Actor)
	{
		return Actor ? Actor->GetSceneStateComponent() : nullptr;
	}

	USceneStateBlueprint* GetSceneStateBlueprint(AAvaSceneStateActor* Actor)
	{
		return Cast<USceneStateBlueprint>(GetSceneStateBlueprintObject(Actor));
	}

	USceneStateMachineGraph* GetRootStateMachineGraph(USceneStateBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		for (UEdGraph* Graph : Blueprint->StateMachineGraphs)
		{
			if (USceneStateMachineGraph* StateMachineGraph = Cast<USceneStateMachineGraph>(Graph))
			{
				return StateMachineGraph;
			}
		}
		return nullptr;
	}

	USceneStateMachineNode* FindStateMachineNodeByName(USceneStateMachineGraph* Graph, const FString& NodeName)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			USceneStateMachineNode* Node = Cast<USceneStateMachineNode>(GraphNode);
			if (!Node)
			{
				continue;
			}

			if (Node->GetNodeName().ToString() == NodeName)
			{
				return Node;
			}
		}
		return nullptr;
	}

	USceneStateMachineTransitionNode* FindTransition(USceneStateMachineNode* SourceNode, USceneStateMachineNode* TargetNode)
	{
		if (!SourceNode || !TargetNode)
		{
			return nullptr;
		}

		for (USceneStateMachineTransitionNode* TransitionNode : SourceNode->GatherTransitions())
		{
			if (TransitionNode && TransitionNode->GetTargetNode() == TargetNode)
			{
				return TransitionNode;
			}
		}
		return nullptr;
	}

	FFloatProperty* FindFloatProperty(const UScriptStruct* Struct, const TCHAR* Name)
	{
		return Struct ? FindFProperty<FFloatProperty>(Struct, Name) : nullptr;
	}

	FBoolProperty* FindBoolProperty(const UScriptStruct* Struct, const TCHAR* Name)
	{
		return Struct ? FindFProperty<FBoolProperty>(Struct, Name) : nullptr;
	}

	const TCHAR* RunModeToString(ESceneStateMachineRunMode RunMode)
	{
		switch (RunMode)
		{
		case ESceneStateMachineRunMode::Auto:
			return TEXT("Auto");
		case ESceneStateMachineRunMode::Manual:
			return TEXT("Manual");
		default:
			return TEXT("Unknown");
		}
	}

	bool TryParseRunMode(const std::string& Value, ESceneStateMachineRunMode& OutRunMode)
	{
		const FString RunMode = UTF8_TO_TCHAR(Value.c_str());
		if (RunMode.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
		{
			OutRunMode = ESceneStateMachineRunMode::Auto;
			return true;
		}
		if (RunMode.Equals(TEXT("Manual"), ESearchCase::IgnoreCase))
		{
			OutRunMode = ESceneStateMachineRunMode::Manual;
			return true;
		}
		return false;
	}

	sol::table BuildParameterInfo(sol::state_view Lua, const FInstancedPropertyBag& Parameters)
	{
		sol::table ParameterInfos = Lua.create_table();
		const UPropertyBag* BagStruct = Parameters.GetPropertyBagStruct();
		if (!BagStruct)
		{
			return ParameterInfos;
		}

		int32 LuaIndex = 0;
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			sol::table ParamInfo = Lua.create_table();
			ParamInfo["name"] = TCHAR_TO_UTF8(*Desc.Name.ToString());
			ParamInfo["value"] = NeoLuaProperty::ReadPropertyBagValue(Parameters, Desc.Name, Lua);
			if (const FProperty* Property = BagStruct->FindPropertyByName(Desc.Name))
			{
				ParamInfo["type"] = TCHAR_TO_UTF8(*Property->GetCPPType());
			}
			ParameterInfos[++LuaIndex] = ParamInfo;
		}
		return ParameterInfos;
	}

	void EnsureTaskNodeId(USceneStateMachineTaskNode* Node)
	{
		if (!Node || Node->GetTaskId().IsValid())
		{
			return;
		}

		FStructProperty* TaskIdProperty = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("TaskId"));
		if (!TaskIdProperty || TaskIdProperty->Struct != TBaseStructure<FGuid>::Get())
		{
			return;
		}

		Node->Modify();
		if (FGuid* TaskId = TaskIdProperty->ContainerPtrToValuePtr<FGuid>(Node))
		{
			*TaskId = FGuid::NewGuid();
		}
	}

	UScriptStruct* ResolveTaskStruct(const std::string& StructName)
	{
		if (StructName.empty())
		{
			return nullptr;
		}

		const FString Name = UTF8_TO_TCHAR(StructName.c_str());
		if (Name.StartsWith(TEXT("/Script/")))
		{
			return LoadObject<UScriptStruct>(nullptr, *Name);
		}
		if (UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/SceneStateTasks.%s"), *Name)))
		{
			return Struct;
		}
		return FindFirstObject<UScriptStruct>(*Name, EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("Resolve Scene State task struct"));
	}

	FString ToAssetObjectPath(const FString& PackageOrObjectPath)
	{
		if (PackageOrObjectPath.Contains(TEXT(".")))
		{
			return PackageOrObjectPath;
		}
		return FString::Printf(TEXT("%s.%s"), *PackageOrObjectPath, *FPackageName::GetLongPackageAssetName(PackageOrObjectPath));
	}

	USceneStateTaskBlueprint* LoadSceneStateTaskBlueprint(const FString& PackageOrObjectPath)
	{
		return LoadObject<USceneStateTaskBlueprint>(nullptr, *ToAssetObjectPath(PackageOrObjectPath));
	}

	USceneStateEventSchemaCollection* LoadEventSchemaCollection(const FString& PackageOrObjectPath)
	{
		return LoadObject<USceneStateEventSchemaCollection>(nullptr, *ToAssetObjectPath(PackageOrObjectPath));
	}

	FString SceneStatePinTypeToName(const FEdGraphPinType& PinType)
	{
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return TEXT("Bool");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
		{
			return PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float ? TEXT("Float") : TEXT("Double");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_String)
		{
			return TEXT("String");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			return TEXT("Name");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
		{
			return TEXT("Text");
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			return TEXT("Integer");
		}
		return PinType.PinCategory.ToString();
	}

	bool ResolveSceneStateEventFieldType(const std::string& TypeString, FEdGraphPinType& OutPinType)
	{
		const FString Type = UTF8_TO_TCHAR(TypeString.c_str());
		if (Type.IsEmpty() || Type.Equals(TEXT("Bool"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			return true;
		}
		if (Type.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			return true;
		}
		if (Type.Equals(TEXT("Double"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			return true;
		}
		if (Type.Equals(TEXT("String"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
			return true;
		}
		if (Type.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			return true;
		}
		if (Type.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
			return true;
		}
		if (Type.Equals(TEXT("Integer"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("Int"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			return true;
		}
		return false;
	}

	UUserDefinedStruct* CreateSceneStateEventStruct(UObject* Outer)
	{
		if (!Outer)
		{
			return nullptr;
		}

		UUserDefinedStruct* Struct = NewObject<UUserDefinedStruct>(Outer, NAME_None, RF_Transactional | RF_Public);
		if (!Struct)
		{
			return nullptr;
		}

		Struct->EditorData = NewObject<UUserDefinedStructEditorData>(Struct, NAME_None, RF_Transactional);
		if (!Struct->EditorData)
		{
			return nullptr;
		}

		Struct->Guid = FGuid::NewGuid();
		Struct->SetMetaData(TEXT("BlueprintType"), TEXT("true"));
		Struct->Bind();
		Struct->StaticLink(true);
		Struct->Status = UDSS_Error;
		return Struct;
	}

	bool AddEventSchemaField(USceneStateEventSchemaObject* Schema, const std::string& FieldName, const std::string& TypeName)
	{
		if (!Schema || FieldName.empty())
		{
			return false;
		}

		if (!Schema->Struct)
		{
			Schema->Struct = CreateSceneStateEventStruct(Schema);
		}
		if (!Schema->Struct)
		{
			return false;
		}

		FEdGraphPinType PinType;
		if (!ResolveSceneStateEventFieldType(TypeName, PinType))
		{
			return false;
		}

		if (!FStructureEditorUtils::AddVariable(Schema->Struct, PinType))
		{
			return false;
		}

		TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(Schema->Struct);
		if (VarDescs.IsEmpty())
		{
			return false;
		}

		const FGuid FieldGuid = VarDescs.Last().VarGuid;
		FStructureEditorUtils::RenameVariable(Schema->Struct, FieldGuid, UTF8_TO_TCHAR(FieldName.c_str()));
		return true;
	}

	sol::table BuildEventSchemaCollectionInfo(sol::state_view Lua, USceneStateEventSchemaCollection* Collection)
	{
		sol::table Info = Lua.create_table();
		Info["valid"] = Collection != nullptr;
		if (!Collection)
		{
			return Info;
		}

		Info["path"] = TCHAR_TO_UTF8(*Collection->GetOutermost()->GetName());
		Info["class"] = TCHAR_TO_UTF8(*Collection->GetClass()->GetName());
		Info["schema_count"] = Collection->GetEventSchemas().Num();
		sol::table Schemas = Lua.create_table();
		int32 SchemaIndex = 0;
		for (USceneStateEventSchemaObject* Schema : Collection->GetEventSchemas())
		{
			sol::table SchemaInfo = Lua.create_table();
			SchemaInfo["name"] = Schema ? TCHAR_TO_UTF8(*Schema->Name.ToString()) : "";
			SchemaInfo["object_name"] = Schema ? TCHAR_TO_UTF8(*Schema->GetName()) : "";
			SchemaInfo["path"] = Schema ? TCHAR_TO_UTF8(*Schema->GetPathName()) : "";
			SchemaInfo["id"] = Schema ? TCHAR_TO_UTF8(*Schema->Id.ToString(EGuidFormats::DigitsWithHyphens)) : "";
			SchemaInfo["id_valid"] = Schema ? Schema->Id.IsValid() : false;
			SchemaInfo["struct_name"] = Schema && Schema->Struct ? TCHAR_TO_UTF8(*Schema->Struct->GetName()) : "";
			SchemaInfo["struct_path"] = Schema && Schema->Struct ? TCHAR_TO_UTF8(*Schema->Struct->GetPathName()) : "";

			sol::table Fields = Lua.create_table();
			int32 FieldIndex = 0;
			if (Schema && Schema->Struct)
			{
				for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Schema->Struct))
				{
					sol::table FieldInfo = Lua.create_table();
					FieldInfo["name"] = TCHAR_TO_UTF8(*Desc.FriendlyName);
					FieldInfo["id"] = TCHAR_TO_UTF8(*Desc.VarGuid.ToString(EGuidFormats::DigitsWithHyphens));
					FieldInfo["type"] = TCHAR_TO_UTF8(*SceneStatePinTypeToName(Desc.ToPinType()));
					Fields[++FieldIndex] = FieldInfo;
				}
			}
			SchemaInfo["field_count"] = FieldIndex;
			SchemaInfo["fields"] = Fields;
			Schemas[++SchemaIndex] = SchemaInfo;
		}
		Info["schemas"] = Schemas;
		return Info;
	}

	USceneStateEventSchemaObject* FindEventSchemaByName(USceneStateEventSchemaCollection* Collection, const FString& SchemaName)
	{
		if (!Collection)
		{
			return nullptr;
		}

		for (USceneStateEventSchemaObject* Schema : Collection->GetEventSchemas())
		{
			if (Schema && Schema->Name.ToString().Equals(SchemaName, ESearchCase::IgnoreCase))
			{
				return Schema;
			}
		}
		return nullptr;
	}

	bool AddSchemaToCollection(USceneStateEventSchemaCollection* Collection, USceneStateEventSchemaObject* Schema)
	{
		if (!Collection || !Schema)
		{
			return false;
		}

		FArrayProperty* EventSchemasProperty = FindFProperty<FArrayProperty>(Collection->GetClass(), USceneStateEventSchemaCollection::GetEventSchemasName());
		if (!EventSchemasProperty || !CastField<FObjectPropertyBase>(EventSchemasProperty->Inner))
		{
			return false;
		}

		FScriptArrayHelper Helper(EventSchemasProperty, EventSchemasProperty->ContainerPtrToValuePtr<void>(Collection));
		const int32 NewIndex = Helper.AddValue();
		CastFieldChecked<FObjectPropertyBase>(EventSchemasProperty->Inner)->SetObjectPropertyValue(Helper.GetRawPtr(NewIndex), Schema);
		return true;
	}

	bool SetSoftObjectReference(void* StructMemory, UStruct* StructType, const FName PropertyName, UObject* Object)
	{
		if (!StructMemory || !StructType || !Object)
		{
			return false;
		}

		FProperty* Property = StructType->FindPropertyByName(PropertyName);
		if (!Property || !Property->IsA<FSoftObjectProperty>())
		{
			return false;
		}

		const FString ImportValue = Object->GetPathName();
		return Property->ImportText_Direct(*ImportValue, Property->ContainerPtrToValuePtr<void>(StructMemory), nullptr, PPF_None) != nullptr;
	}

	bool SetEventSchemaHandle(FSceneStateEventHandler& Handler, USceneStateEventSchemaObject* Schema)
	{
		if (!Schema)
		{
			return false;
		}

		FStructProperty* SchemaHandleProperty = FindFProperty<FStructProperty>(
			FSceneStateEventHandler::StaticStruct(),
			FSceneStateEventHandler::GetSchemaHandlePropertyName());
		if (!SchemaHandleProperty || SchemaHandleProperty->Struct != FSceneStateEventSchemaHandle::StaticStruct())
		{
			return false;
		}

		void* SchemaHandleMemory = SchemaHandleProperty->ContainerPtrToValuePtr<void>(&Handler);
		return SetSoftObjectReference(SchemaHandleMemory, SchemaHandleProperty->Struct, FSceneStateEventSchemaHandle::GetEventSchemaPropertyName(), Schema)
			&& (!Schema->Struct || SetSoftObjectReference(SchemaHandleMemory, SchemaHandleProperty->Struct, FSceneStateEventSchemaHandle::GetEventStructPropertyName(), Schema->Struct));
	}

	USceneStateTransitionResultNode* GetTransitionResultNode(USceneStateMachineNode* Node)
	{
		USceneStateTransitionGraph* TransitionGraph = Node ? Cast<USceneStateTransitionGraph>(Node->GetBoundGraph()) : nullptr;
		return TransitionGraph ? TransitionGraph->ResultNode : nullptr;
	}

	UEdGraphPin* GetCanTransitionPin(USceneStateTransitionResultNode* ResultNode)
	{
		return ResultNode ? ResultNode->FindPin(GET_MEMBER_NAME_CHECKED(FSceneStateTransitionResult, bCanTransition)) : nullptr;
	}

	bool ReadTransitionConditionDefault(USceneStateMachineNode* Node, bool& OutValue)
	{
		UEdGraphPin* Pin = GetCanTransitionPin(GetTransitionResultNode(Node));
		if (!Pin || !Pin->LinkedTo.IsEmpty())
		{
			return false;
		}

		LexFromString(OutValue, *Pin->DefaultValue);
		return true;
	}

	void AddTransitionConditionInfo(sol::table& NodeInfo, USceneStateMachineNode* Node)
	{
		USceneStateTransitionResultNode* ResultNode = GetTransitionResultNode(Node);
		UEdGraphPin* CanTransitionPin = GetCanTransitionPin(ResultNode);
		NodeInfo["condition_graph_class"] = Node && Node->GetBoundGraph() ? TCHAR_TO_UTF8(*Node->GetBoundGraph()->GetClass()->GetName()) : "";
		NodeInfo["condition_result_node"] = ResultNode ? TCHAR_TO_UTF8(*ResultNode->GetName()) : "";
		NodeInfo["condition_has_result_node"] = ResultNode != nullptr;
		NodeInfo["condition_can_transition_link_count"] = CanTransitionPin ? CanTransitionPin->LinkedTo.Num() : 0;
		if (CanTransitionPin && CanTransitionPin->LinkedTo.IsEmpty())
		{
			bool bCanTransition = true;
			LexFromString(bCanTransition, *CanTransitionPin->DefaultValue);
			NodeInfo["condition_can_transition_default"] = bCanTransition;
			NodeInfo["condition_can_transition_default_string"] = TCHAR_TO_UTF8(*CanTransitionPin->DefaultValue);
		}
	}

	sol::table BuildTaskBlueprintInfo(sol::state_view Lua, USceneStateTaskBlueprint* Blueprint)
	{
		sol::table Info = Lua.create_table();
		Info["valid"] = Blueprint != nullptr;
		if (!Blueprint)
		{
			return Info;
		}

		Info["path"] = TCHAR_TO_UTF8(*Blueprint->GetOutermost()->GetName());
		Info["class"] = TCHAR_TO_UTF8(*Blueprint->GetClass()->GetName());
		Info["parent_class"] = Blueprint->ParentClass ? TCHAR_TO_UTF8(*Blueprint->ParentClass->GetName()) : "";
		Info["generated_class"] = Blueprint->GeneratedClass ? TCHAR_TO_UTF8(*Blueprint->GeneratedClass->GetName()) : "";
		Info["generated_class_path"] = Blueprint->GeneratedClass ? TCHAR_TO_UTF8(*Blueprint->GeneratedClass->GetPathName()) : "";
		Info["is_scene_state_task_blueprint"] = true;
		Info["generated_is_scene_state_task"] = Blueprint->GeneratedClass
			? Blueprint->GeneratedClass->IsChildOf(USceneStateBlueprintableTask::StaticClass())
			: false;
		Info["blueprint_class_type"] = Blueprint->GeneratedClass
			? TCHAR_TO_UTF8(*Blueprint->GeneratedClass->GetClass()->GetName())
			: "";
		return Info;
	}

	sol::table BuildBindingInfo(sol::state_view Lua, const FSceneStateBindingCollection& BindingCollection)
	{
		sol::table Bindings = Lua.create_table();
		int32 LuaIndex = 0;
		for (const FSceneStateBinding& Binding : BindingCollection.GetBindings())
		{
			sol::table BindingInfo = Lua.create_table();
			const FPropertyBindingPath& SourcePath = Binding.GetSourcePath();
			const FPropertyBindingPath& TargetPath = Binding.GetTargetPath();
			BindingInfo["source_id"] = TCHAR_TO_UTF8(*SourcePath.GetStructID().ToString(EGuidFormats::DigitsWithHyphens));
			BindingInfo["source_path"] = TCHAR_TO_UTF8(*SourcePath.ToString());
			BindingInfo["source_segment_count"] = SourcePath.NumSegments();
			BindingInfo["target_id"] = TCHAR_TO_UTF8(*TargetPath.GetStructID().ToString(EGuidFormats::DigitsWithHyphens));
			BindingInfo["target_path"] = TCHAR_TO_UTF8(*TargetPath.ToString());
			BindingInfo["target_segment_count"] = TargetPath.NumSegments();
			BindingInfo["has_property_function"] = Binding.GetPropertyFunctionNode().IsValid();
			if (const FSceneStateBindingFunction* BindingFunction = Binding.GetPropertyFunctionNode().GetPtr<const FSceneStateBindingFunction>())
			{
				const UScriptStruct* FunctionStruct = BindingFunction->Function.GetScriptStruct();
				const UScriptStruct* FunctionInstanceStruct = BindingFunction->FunctionInstance.GetScriptStruct();
				BindingInfo["function_id"] = TCHAR_TO_UTF8(*BindingFunction->FunctionId.ToString(EGuidFormats::DigitsWithHyphens));
				BindingInfo["function_id_valid"] = BindingFunction->FunctionId.IsValid();
				BindingInfo["function_struct"] = FunctionStruct ? TCHAR_TO_UTF8(*FunctionStruct->GetName()) : "";
				BindingInfo["function_struct_path"] = FunctionStruct ? TCHAR_TO_UTF8(*FunctionStruct->GetStructPathName().ToString()) : "";
				BindingInfo["function_display_name"] = FunctionStruct ? TCHAR_TO_UTF8(*FunctionStruct->GetDisplayNameText().ToString()) : "";
				BindingInfo["function_instance_struct"] = FunctionInstanceStruct ? TCHAR_TO_UTF8(*FunctionInstanceStruct->GetName()) : "";
			}
			Bindings[++LuaIndex] = BindingInfo;
		}
		return Bindings;
	}

	bool TryBuildBindingPathFromView(
		const FPropertyBindingDataView& DataView,
		const FGuid& StructId,
		const std::string& PathString,
		FPropertyBindingPath& OutPath)
	{
		if (!DataView.GetStruct() || !StructId.IsValid() || PathString.empty())
		{
			return false;
		}

		FPropertyBindingPath Path(StructId);
		if (!Path.FromString(UTF8_TO_TCHAR(PathString.c_str())) || Path.IsPathEmpty())
		{
			return false;
		}

		FString ResolveError;
		if (!Path.UpdateSegments(DataView.GetStruct(), &ResolveError))
		{
			return false;
		}

		OutPath = MoveTemp(Path);
		return true;
	}

	bool TryBuildBindingPath(
		const USceneStateBlueprint* Blueprint,
		const std::string& StructIdString,
		const std::string& PathString,
		FPropertyBindingPath& OutPath)
	{
		if (!Blueprint || StructIdString.empty() || PathString.empty())
		{
			return false;
		}

		FGuid StructId;
		if (!FGuid::Parse(UTF8_TO_TCHAR(StructIdString.c_str()), StructId) || !StructId.IsValid())
		{
			return false;
		}

		FPropertyBindingPath Path(StructId);
		if (!Path.FromString(UTF8_TO_TCHAR(PathString.c_str())) || Path.IsPathEmpty())
		{
			return false;
		}

		FPropertyBindingDataView DataView;
		if (!Blueprint->GetBindingDataViewByID(StructId, DataView) || !DataView.IsValid())
		{
			return false;
		}

		FString ResolveError;
		if (!Path.UpdateSegmentsFromValue(DataView, &ResolveError))
		{
			return false;
		}

		OutPath = MoveTemp(Path);
		return true;
	}

	sol::object CreateTaskBlueprintAsset(sol::this_state S, const std::string& AssetPath)
	{
		sol::state_view Lua(S);
		const FString PackageName = UTF8_TO_TCHAR(AssetPath.c_str());
		FText Reason;
		if (!FPackageName::IsValidLongPackageName(PackageName, false, &Reason))
		{
			return sol::lua_nil;
		}

		if (USceneStateTaskBlueprint* ExistingBlueprint = LoadSceneStateTaskBlueprint(PackageName))
		{
			return sol::make_object(S, BuildTaskBlueprintInfo(Lua, ExistingBlueprint));
		}

		const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return sol::lua_nil;
		}

		USceneStateTaskBlueprint* Blueprint = Cast<USceneStateTaskBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			USceneStateBlueprintableTask::StaticClass(),
			Package,
			FName(*AssetName),
			BPTYPE_Normal,
			USceneStateTaskBlueprint::StaticClass(),
			USceneStateTaskGeneratedClass::StaticClass()));
		if (!Blueprint)
		{
			return sol::lua_nil;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		FAssetRegistryModule::AssetCreated(Blueprint);
		Package->MarkPackageDirty();
		return sol::make_object(S, BuildTaskBlueprintInfo(Lua, Blueprint));
	}

	void SetTaskInstanceValue(TStructView<FSceneStateTaskInstance> TaskInstance, const sol::table& Options)
	{
		if (!TaskInstance.GetScriptStruct() || !TaskInstance.GetMemory())
		{
			return;
		}

		sol::object ValueObject = Options["value"];
		if (ValueObject == sol::lua_nil)
		{
			return;
		}

		FProperty* ValueProperty = FindFProperty<FProperty>(TaskInstance.GetScriptStruct(), TEXT("Value"));
		if (!ValueProperty)
		{
			return;
		}

		void* InstanceMemory = TaskInstance.GetMemory();
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(ValueProperty))
		{
			BoolProperty->SetPropertyValue_InContainer(InstanceMemory, ValueObject.as<bool>());
		}
		else if (FIntProperty* IntProperty = CastField<FIntProperty>(ValueProperty))
		{
			IntProperty->SetPropertyValue_InContainer(InstanceMemory, ValueObject.as<int32>());
		}
		else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(ValueProperty))
		{
			FloatProperty->SetPropertyValue_InContainer(InstanceMemory, static_cast<float>(ValueObject.as<double>()));
		}
		else if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(ValueProperty))
		{
			DoubleProperty->SetPropertyValue_InContainer(InstanceMemory, ValueObject.as<double>());
		}
		else if (FStrProperty* StringProperty = CastField<FStrProperty>(ValueProperty))
		{
			StringProperty->SetPropertyValue_InContainer(InstanceMemory, FString(UTF8_TO_TCHAR(ValueObject.as<std::string>().c_str())));
		}
		else if (FTextProperty* TextProperty = CastField<FTextProperty>(ValueProperty))
		{
			TextProperty->SetPropertyValue_InContainer(InstanceMemory, FText::FromString(UTF8_TO_TCHAR(ValueObject.as<std::string>().c_str())));
		}
	}

	void AddValueFieldInfo(sol::table& NodeInfo, TStructView<FSceneStateTaskInstance> TaskInstance)
	{
		if (!TaskInstance.GetScriptStruct() || !TaskInstance.GetMemory())
		{
			return;
		}

		FProperty* ValueProperty = FindFProperty<FProperty>(TaskInstance.GetScriptStruct(), TEXT("Value"));
		if (!ValueProperty)
		{
			return;
		}

		void* InstanceMemory = TaskInstance.GetMemory();
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(ValueProperty))
		{
			NodeInfo["value"] = BoolProperty->GetPropertyValue_InContainer(InstanceMemory);
		}
		else if (FIntProperty* IntProperty = CastField<FIntProperty>(ValueProperty))
		{
			NodeInfo["value"] = IntProperty->GetPropertyValue_InContainer(InstanceMemory);
		}
		else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(ValueProperty))
		{
			NodeInfo["value"] = FloatProperty->GetPropertyValue_InContainer(InstanceMemory);
		}
		else if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(ValueProperty))
		{
			NodeInfo["value"] = DoubleProperty->GetPropertyValue_InContainer(InstanceMemory);
		}
		else if (FStrProperty* StringProperty = CastField<FStrProperty>(ValueProperty))
		{
			NodeInfo["value"] = TCHAR_TO_UTF8(*StringProperty->GetPropertyValue_InContainer(InstanceMemory));
		}
		else if (FTextProperty* TextProperty = CastField<FTextProperty>(ValueProperty))
		{
			NodeInfo["value"] = TCHAR_TO_UTF8(*TextProperty->GetPropertyValue_InContainer(InstanceMemory).ToString());
		}
	}

	void AddBlueprintTaskInfo(sol::table& NodeInfo, TConstStructView<FSceneStateTask> Task, TStructView<FSceneStateTaskInstance> TaskInstance)
	{
		if (Task.GetScriptStruct() == FSceneStateBlueprintableTaskWrapper::StaticStruct() && Task.GetMemory())
		{
			const FSceneStateBlueprintableTaskWrapper& Wrapper = Task.Get<FSceneStateBlueprintableTaskWrapper>();
			UClass* TaskClass = Wrapper.GetTaskClass();
			NodeInfo["blueprint_task_class"] = TaskClass ? TCHAR_TO_UTF8(*TaskClass->GetName()) : "";
			NodeInfo["blueprint_task_class_path"] = TaskClass ? TCHAR_TO_UTF8(*TaskClass->GetPathName()) : "";
			NodeInfo["blueprint_task_parent_class"] = TaskClass && TaskClass->GetSuperClass()
				? TCHAR_TO_UTF8(*TaskClass->GetSuperClass()->GetName())
				: "";
		}

		if (TaskInstance.GetScriptStruct() != FSceneStateBlueprintableTaskInstance::StaticStruct() || !TaskInstance.GetMemory())
		{
			return;
		}

		if (FObjectProperty* TaskObjectProperty = FindFProperty<FObjectProperty>(TaskInstance.GetScriptStruct(), TEXT("Task")))
		{
			USceneStateBlueprintableTask* TaskObject = Cast<USceneStateBlueprintableTask>(
				TaskObjectProperty->GetObjectPropertyValue_InContainer(TaskInstance.GetMemory()));
			NodeInfo["blueprint_task_instance_class"] = TaskObject && TaskObject->GetClass()
				? TCHAR_TO_UTF8(*TaskObject->GetClass()->GetName())
				: "";
			NodeInfo["blueprint_task_instance_outer"] = TaskObject && TaskObject->GetOuter()
				? TCHAR_TO_UTF8(*TaskObject->GetOuter()->GetName())
				: "";
			NodeInfo["blueprint_task_instance_instanced"] = TaskObject != nullptr && TaskObject->GetOuter() != nullptr;
		}
	}

	FInstancedStruct* GetMutableTaskStruct(USceneStateMachineTaskNode* Node)
	{
		FStructProperty* TaskProperty = Node
			? FindFProperty<FStructProperty>(Node->GetClass(), USceneStateMachineTaskNode::GetTaskPropertyName())
			: nullptr;
		return TaskProperty ? TaskProperty->ContainerPtrToValuePtr<FInstancedStruct>(Node) : nullptr;
	}

	bool SetSpawnActorTaskClass(USceneStateMachineTaskNode* Node, UClass* ActorClass)
	{
		FInstancedStruct* Task = GetMutableTaskStruct(Node);
		if (!Task || Task->GetScriptStruct() != FSceneStateSpawnActorTask::StaticStruct() || !Task->GetMutableMemory())
		{
			return false;
		}

		FClassProperty* ActorClassProperty = FindFProperty<FClassProperty>(Task->GetScriptStruct(), TEXT("ActorClass"));
		if (!ActorClassProperty || !ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
		{
			return false;
		}

		ActorClassProperty->SetPropertyValue_InContainer(Task->GetMutableMemory(), ActorClass);
		return true;
	}

	AActor* CreateSpawnActorTemplate(USceneStateMachineTaskNode* Node, UClass* ActorClass)
	{
		UWorld* World = GetEditorWorld();
		if (!Node || !World || !ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags = RF_Transient | RF_Transactional;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* SpawnedActor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, SpawnParams);
		if (!SpawnedActor)
		{
			return nullptr;
		}

		AActor* TemplateActor = Cast<AActor>(StaticDuplicateObject(SpawnedActor, Node, NAME_None, RF_AllFlags & ~RF_Transient));
		if (TemplateActor)
		{
			TemplateActor->DetachFromActor(FDetachmentTransformRules::KeepRelativeTransform);
			TemplateActor->bIsEditorPreviewActor = false;
			TemplateActor->SetFlags(RF_ArchetypeObject | RF_Transactional);
		}

		World->DestroyActor(SpawnedActor, false, false);
		return TemplateActor;
	}

	bool ConfigureSpawnActorInstance(USceneStateMachineTaskNode* Node, UClass* ActorClass, const sol::table& Options)
	{
		TStructView<FSceneStateTaskInstance> TaskInstance = Node ? Node->GetTaskInstance() : TStructView<FSceneStateTaskInstance>();
		if (!TaskInstance.GetScriptStruct() || TaskInstance.GetScriptStruct() != FSceneStateSpawnActorTaskInstance::StaticStruct() || !TaskInstance.GetMemory())
		{
			return false;
		}

		void* InstanceMemory = TaskInstance.GetMemory();
		if (FStructProperty* TemplateStructProperty = FindFProperty<FStructProperty>(TaskInstance.GetScriptStruct(), TEXT("ActorTemplate")))
		{
			void* TemplateStructMemory = TemplateStructProperty->ContainerPtrToValuePtr<void>(InstanceMemory);
			if (TemplateStructMemory && TemplateStructProperty->Struct)
			{
				if (FObjectProperty* TemplateProperty = FindFProperty<FObjectProperty>(TemplateStructProperty->Struct, TEXT("Template")))
				{
					AActor* TemplateActor = CreateSpawnActorTemplate(Node, ActorClass);
					if (TemplateActor)
					{
						TemplateProperty->SetObjectPropertyValue_InContainer(TemplateStructMemory, TemplateActor);
					}
				}
			}
		}

		if (FStructProperty* TransformProperty = FindFProperty<FStructProperty>(TaskInstance.GetScriptStruct(), TEXT("SpawnTransform")))
		{
			if (TransformProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				if (FTransform* Transform = TransformProperty->ContainerPtrToValuePtr<FTransform>(InstanceMemory))
				{
					Transform->SetLocation(FVector(
						Options.get_or("spawn_x", 0.0),
						Options.get_or("spawn_y", 0.0),
						Options.get_or("spawn_z", 0.0)));
				}
			}
		}

		if (FByteProperty* CollisionProperty = FindFProperty<FByteProperty>(TaskInstance.GetScriptStruct(), TEXT("SpawnCollisionHandling")))
		{
			CollisionProperty->SetPropertyValue_InContainer(InstanceMemory, static_cast<uint8>(ESpawnActorCollisionHandlingMethod::AlwaysSpawn));
		}
		else if (FEnumProperty* CollisionEnumProperty = FindFProperty<FEnumProperty>(TaskInstance.GetScriptStruct(), TEXT("SpawnCollisionHandling")))
		{
			CollisionEnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(
				CollisionEnumProperty->ContainerPtrToValuePtr<void>(InstanceMemory),
				static_cast<int64>(ESpawnActorCollisionHandlingMethod::AlwaysSpawn));
		}
		return true;
	}

	void AddSpawnedActorReferenceInfo(sol::table& NodeInfo, TStructView<FSceneStateTaskInstance> TaskInstance)
	{
		if (TaskInstance.GetScriptStruct() != FSceneStateSpawnActorTaskInstance::StaticStruct() || !TaskInstance.GetMemory())
		{
			return;
		}

		const FStructProperty* SpawnedActorProperty = FindFProperty<FStructProperty>(TaskInstance.GetScriptStruct(), TEXT("SpawnedActor"));
		const FSceneStatePropertyReference* SpawnedActorReference = SpawnedActorProperty && SpawnedActorProperty->Struct == FSceneStatePropertyReference::StaticStruct()
			? SpawnedActorProperty->ContainerPtrToValuePtr<FSceneStatePropertyReference>(TaskInstance.GetMemory())
			: nullptr;
		NodeInfo["spawned_actor_reference_valid"] = SpawnedActorReference && SpawnedActorReference->IsValidIndex();
		NodeInfo["spawned_actor_reference_index"] = SpawnedActorReference && SpawnedActorReference->IsValidIndex()
			? static_cast<int32>(SpawnedActorReference->ReferenceIndex)
			: -1;
	}

	void AddSpawnActorInfo(sol::table& NodeInfo, TConstStructView<FSceneStateTask> Task, TStructView<FSceneStateTaskInstance> TaskInstance)
	{
		if (Task.GetScriptStruct() == FSceneStateSpawnActorTask::StaticStruct() && Task.GetMemory())
		{
			if (FClassProperty* ActorClassProperty = FindFProperty<FClassProperty>(Task.GetScriptStruct(), TEXT("ActorClass")))
			{
				UClass* ActorClass = Cast<UClass>(ActorClassProperty->GetObjectPropertyValue_InContainer(Task.GetMemory()));
				NodeInfo["actor_class"] = ActorClass ? TCHAR_TO_UTF8(*ActorClass->GetName()) : "";
				NodeInfo["actor_class_path"] = ActorClass ? TCHAR_TO_UTF8(*ActorClass->GetPathName()) : "";
			}
		}

		if (TaskInstance.GetScriptStruct() != FSceneStateSpawnActorTaskInstance::StaticStruct() || !TaskInstance.GetMemory())
		{
			return;
		}

		void* InstanceMemory = TaskInstance.GetMemory();
		if (FStructProperty* TemplateStructProperty = FindFProperty<FStructProperty>(TaskInstance.GetScriptStruct(), TEXT("ActorTemplate")))
		{
			void* TemplateStructMemory = TemplateStructProperty->ContainerPtrToValuePtr<void>(InstanceMemory);
			if (TemplateStructMemory && TemplateStructProperty->Struct)
			{
				if (FObjectProperty* TemplateProperty = FindFProperty<FObjectProperty>(TemplateStructProperty->Struct, TEXT("Template")))
				{
					AActor* TemplateActor = Cast<AActor>(TemplateProperty->GetObjectPropertyValue_InContainer(TemplateStructMemory));
					NodeInfo["actor_template_class"] = TemplateActor && TemplateActor->GetClass() ? TCHAR_TO_UTF8(*TemplateActor->GetClass()->GetName()) : "";
					NodeInfo["actor_template_archetype"] = TemplateActor ? TemplateActor->HasAnyFlags(RF_ArchetypeObject) : false;
				}
			}
		}
		if (FStructProperty* TransformProperty = FindFProperty<FStructProperty>(TaskInstance.GetScriptStruct(), TEXT("SpawnTransform")))
		{
			if (TransformProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				const FTransform* Transform = TransformProperty->ContainerPtrToValuePtr<FTransform>(InstanceMemory);
				const FVector Location = Transform ? Transform->GetLocation() : FVector::ZeroVector;
				NodeInfo["spawn_x"] = Location.X;
				NodeInfo["spawn_y"] = Location.Y;
				NodeInfo["spawn_z"] = Location.Z;
			}
		}
		if (FByteProperty* CollisionProperty = FindFProperty<FByteProperty>(TaskInstance.GetScriptStruct(), TEXT("SpawnCollisionHandling")))
		{
			const uint8 CollisionValue = CollisionProperty->GetPropertyValue_InContainer(InstanceMemory);
			if (CollisionProperty->Enum)
			{
				NodeInfo["spawn_collision_handling"] = TCHAR_TO_UTF8(*CollisionProperty->Enum->GetNameStringByValue(CollisionValue));
			}
			else
			{
				NodeInfo["spawn_collision_handling"] = static_cast<int32>(CollisionValue);
			}
		}
		else if (FEnumProperty* CollisionEnumProperty = FindFProperty<FEnumProperty>(TaskInstance.GetScriptStruct(), TEXT("SpawnCollisionHandling")))
		{
			const int64 CollisionValue = CollisionEnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(
				CollisionEnumProperty->ContainerPtrToValuePtr<void>(InstanceMemory));
			if (CollisionEnumProperty->GetEnum())
			{
				NodeInfo["spawn_collision_handling"] = TCHAR_TO_UTF8(*CollisionEnumProperty->GetEnum()->GetNameStringByValue(CollisionValue));
			}
			else
			{
				NodeInfo["spawn_collision_handling"] = static_cast<int32>(CollisionValue);
			}
		}
		AddSpawnedActorReferenceInfo(NodeInfo, TaskInstance);
	}

	void AddPrintSettingsInfo(sol::table& NodeInfo, TStructView<FSceneStateTaskInstance> TaskInstance)
	{
		const UScriptStruct* InstanceStruct = TaskInstance.GetScriptStruct();
		void* InstanceMemory = TaskInstance.GetMemory();
		if (!InstanceStruct || !InstanceMemory)
		{
			return;
		}

		if (FStrProperty* MessageProperty = FindFProperty<FStrProperty>(InstanceStruct, TEXT("Message")))
		{
			NodeInfo["message"] = TCHAR_TO_UTF8(*MessageProperty->GetPropertyValue_InContainer(InstanceMemory));
		}

		FStructProperty* PrintSettingsProperty = FindFProperty<FStructProperty>(InstanceStruct, TEXT("PrintSettings"));
		if (!PrintSettingsProperty || !PrintSettingsProperty->Struct)
		{
			return;
		}

		void* SettingsMemory = PrintSettingsProperty->ContainerPtrToValuePtr<void>(InstanceMemory);
		if (FBoolProperty* PrintToLogProperty = FindBoolProperty(PrintSettingsProperty->Struct, TEXT("bPrintToLog")))
		{
			NodeInfo["print_to_log"] = PrintToLogProperty->GetPropertyValue_InContainer(SettingsMemory);
		}
		if (FBoolProperty* PrintToScreenProperty = FindBoolProperty(PrintSettingsProperty->Struct, TEXT("bPrintToScreen")))
		{
			NodeInfo["print_to_screen"] = PrintToScreenProperty->GetPropertyValue_InContainer(SettingsMemory);
		}
		if (FFloatProperty* DurationProperty = FindFloatProperty(PrintSettingsProperty->Struct, TEXT("Duration")))
		{
			NodeInfo["duration"] = DurationProperty->GetPropertyValue_InContainer(SettingsMemory);
		}
		if (FNameProperty* KeyProperty = FindFProperty<FNameProperty>(PrintSettingsProperty->Struct, TEXT("Key")))
		{
			NodeInfo["key"] = TCHAR_TO_UTF8(*KeyProperty->GetPropertyValue_InContainer(SettingsMemory).ToString());
		}
		if (FStructProperty* TextColorProperty = FindFProperty<FStructProperty>(PrintSettingsProperty->Struct, TEXT("TextColor")))
		{
			if (TextColorProperty->Struct == TBaseStructure<FLinearColor>::Get())
			{
				const FLinearColor* Color = TextColorProperty->ContainerPtrToValuePtr<FLinearColor>(SettingsMemory);
				if (Color)
				{
					NodeInfo["text_color_r"] = Color->R;
					NodeInfo["text_color_g"] = Color->G;
					NodeInfo["text_color_b"] = Color->B;
					NodeInfo["text_color_a"] = Color->A;
				}
			}
		}
	}

	USceneStateBlueprint* CreateEmbeddedBlueprint(AAvaSceneStateActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		USceneStateBlueprintFactory* Factory = NewObject<USceneStateBlueprintFactory>(GetTransientPackage());
		if (!Factory)
		{
			return nullptr;
		}

		USceneStateBlueprint* Blueprint = Cast<USceneStateBlueprint>(Factory->FactoryCreateNew(
			UAvaSceneStateBlueprint::StaticClass(),
			Actor,
			TEXT("SceneStateBlueprint"),
			RF_Transactional,
			nullptr,
			GWarn));
		if (!Blueprint)
		{
			return nullptr;
		}

		Blueprint->ClearFlags(RF_Standalone);
		SetSceneStateBlueprintObject(Actor, Blueprint);
		UClass* GeneratedSceneStateClass = Blueprint->GeneratedClass.Get();
		Actor->SetSceneStateClass(TSubclassOf<USceneStateObject>(GeneratedSceneStateClass));
		if (USceneStateComponent* Component = GetSceneStateComponent(Actor))
		{
			Component->SetSceneStateClass(TSubclassOf<USceneStateObject>(GeneratedSceneStateClass));
		}
		return Blueprint;
	}

	sol::table BuildInfo(sol::state_view Lua, AAvaSceneStateActor* Actor)
	{
		sol::table Info = Lua.create_table();
		Info["valid"] = Actor != nullptr;
		if (!Actor)
		{
			return Info;
		}

		USceneStateComponent* Component = GetSceneStateComponent(Actor);
		UObject* BlueprintObject = GetSceneStateBlueprintObject(Actor);
		USceneStateBlueprint* Blueprint = Cast<USceneStateBlueprint>(BlueprintObject);
		TSubclassOf<USceneStateObject> SceneStateClass = Component ? Component->GetSceneStateClass() : nullptr;

		Info["actor_class"] = TCHAR_TO_UTF8(*Actor->GetClass()->GetName());
		Info["actor_name"] = TCHAR_TO_UTF8(*Actor->GetName());
		Info["label"] = TCHAR_TO_UTF8(*Actor->GetActorLabel());
		Info["is_user_managed"] = false;
		Info["component_class"] = Component ? TCHAR_TO_UTF8(*Component->GetClass()->GetName()) : "";
		Info["player_class"] = Component && Component->GetSceneStatePlayer() ? TCHAR_TO_UTF8(*Component->GetSceneStatePlayer()->GetClass()->GetName()) : "";
		Info["has_blueprint"] = Blueprint != nullptr;
		Info["blueprint_class"] = Blueprint ? TCHAR_TO_UTF8(*Blueprint->GetClass()->GetName()) : "";
		Info["blueprint_outer"] = Blueprint ? TCHAR_TO_UTF8(*GetNameSafe(Blueprint->GetOuter())) : "";
		Info["blueprint_name"] = Blueprint ? TCHAR_TO_UTF8(*Blueprint->GetName()) : "";
		Info["blueprint_standalone"] = Blueprint ? Blueprint->HasAnyFlags(RF_Standalone) : false;
		Info["generated_class"] = Blueprint && Blueprint->GeneratedClass ? TCHAR_TO_UTF8(*Blueprint->GeneratedClass->GetName()) : "";
		Info["component_scene_state_class"] = SceneStateClass ? TCHAR_TO_UTF8(*SceneStateClass->GetName()) : "";
		Info["scene_state_class"] = Info["component_scene_state_class"];
		if (USceneStateGeneratedClass* GeneratedSceneStateClass = Blueprint ? Cast<USceneStateGeneratedClass>(Blueprint->GeneratedClass) : nullptr)
		{
			const USceneStateTemplateData* TemplateData = GeneratedSceneStateClass->GetTemplateData();
			Info["compiled_transition_count"] = TemplateData ? TemplateData->Transitions.Num() : 0;
			Info["compiled_transition_link_count"] = TemplateData ? TemplateData->TransitionLinks.Num() : 0;
			Info["compiled_conduit_count"] = TemplateData ? TemplateData->Conduits.Num() : 0;
		}
		else
		{
			Info["compiled_transition_count"] = 0;
			Info["compiled_transition_link_count"] = 0;
			Info["compiled_conduit_count"] = 0;
		}
		Info["graph_count"] = Blueprint ? Blueprint->StateMachineGraphs.Num() : 0;
		Info["binding_count"] = Blueprint ? Blueprint->GetBindingCollection().GetNumBindings() : 0;
		Info["bindings"] = Blueprint ? BuildBindingInfo(Lua, Blueprint->GetBindingCollection()) : Lua.create_table();

		sol::table Graphs = Lua.create_table();
		if (Blueprint)
		{
			int32 LuaIndex = 0;
			for (UEdGraph* Graph : Blueprint->StateMachineGraphs)
			{
				sol::table GraphInfo = Lua.create_table();
				GraphInfo["index"] = ++LuaIndex;
				GraphInfo["name"] = Graph ? TCHAR_TO_UTF8(*Graph->GetName()) : "";
				GraphInfo["node_count"] = Graph ? Graph->Nodes.Num() : 0;
				if (USceneStateMachineGraph* StateMachineGraph = Cast<USceneStateMachineGraph>(Graph))
				{
					GraphInfo["category"] = TCHAR_TO_UTF8(*StateMachineGraph->Category.ToString());
					GraphInfo["parameters_id"] = TCHAR_TO_UTF8(*StateMachineGraph->ParametersId.ToString(EGuidFormats::DigitsWithHyphens));
					GraphInfo["parameters_id_valid"] = StateMachineGraph->ParametersId.IsValid();
					GraphInfo["run_mode"] = TCHAR_TO_UTF8(RunModeToString(StateMachineGraph->RunMode));
					GraphInfo["parameters"] = BuildParameterInfo(Lua, StateMachineGraph->Parameters);
					GraphInfo["parameter_count"] = StateMachineGraph->Parameters.GetPropertyBagStruct()
						? StateMachineGraph->Parameters.GetPropertyBagStruct()->GetPropertyDescs().Num()
						: 0;
				}

				sol::table Nodes = Lua.create_table();
				int32 NodeLuaIndex = 0;
				if (Graph)
				{
					for (UEdGraphNode* GraphNode : Graph->Nodes)
					{
						USceneStateMachineNode* StateMachineNode = Cast<USceneStateMachineNode>(GraphNode);
						if (!StateMachineNode)
						{
							continue;
						}

						sol::table NodeInfo = Lua.create_table();
						NodeInfo["index"] = ++NodeLuaIndex;
						NodeInfo["class"] = TCHAR_TO_UTF8(*StateMachineNode->GetClass()->GetName());
						NodeInfo["object_name"] = TCHAR_TO_UTF8(*StateMachineNode->GetName());
						NodeInfo["name"] = TCHAR_TO_UTF8(*StateMachineNode->GetNodeName().ToString());
						NodeInfo["position_x"] = StateMachineNode->NodePosX;
						NodeInfo["position_y"] = StateMachineNode->NodePosY;
						NodeInfo["input_links"] = StateMachineNode->GetInputPin() ? StateMachineNode->GetInputPin()->LinkedTo.Num() : 0;
						NodeInfo["output_links"] = StateMachineNode->GetOutputPin() ? StateMachineNode->GetOutputPin()->LinkedTo.Num() : 0;
						NodeInfo["bound_graph_count"] = StateMachineNode->GetBoundGraphs().Num();
						if (USceneStateMachineStateNode* StateNode = Cast<USceneStateMachineStateNode>(StateMachineNode))
						{
							sol::table EventHandlers = Lua.create_table();
							int32 HandlerIndex = 0;
							for (const FSceneStateEventHandler& EventHandler : StateNode->GetEventHandlers())
							{
								sol::table HandlerInfo = Lua.create_table();
								const FSceneStateEventSchemaHandle& SchemaHandle = EventHandler.GetEventSchemaHandle();
								USceneStateEventSchemaObject* EventSchema = SchemaHandle.GetEventSchema();
								FGuid ResolvedHandlerId;
								const bool bProviderResolves = StateNode->FindEventHandlerId(SchemaHandle, ResolvedHandlerId);
								HandlerInfo["handler_id"] = TCHAR_TO_UTF8(*EventHandler.GetHandlerId().ToString(EGuidFormats::DigitsWithHyphens));
								HandlerInfo["handler_id_valid"] = EventHandler.GetHandlerId().IsValid();
								HandlerInfo["schema_name"] = EventSchema ? TCHAR_TO_UTF8(*EventSchema->Name.ToString()) : "";
								HandlerInfo["schema_path"] = EventSchema ? TCHAR_TO_UTF8(*EventSchema->GetPathName()) : "";
								HandlerInfo["schema_id"] = EventSchema ? TCHAR_TO_UTF8(*EventSchema->Id.ToString(EGuidFormats::DigitsWithHyphens)) : "";
								HandlerInfo["schema_id_valid"] = EventSchema ? EventSchema->Id.IsValid() : false;
								HandlerInfo["event_struct_name"] = SchemaHandle.GetEventStruct() ? TCHAR_TO_UTF8(*SchemaHandle.GetEventStruct()->GetName()) : "";
								HandlerInfo["provider_resolves_handler"] = bProviderResolves && ResolvedHandlerId == EventHandler.GetHandlerId();
								EventHandlers[++HandlerIndex] = HandlerInfo;
							}
							NodeInfo["event_handler_count"] = HandlerIndex;
							NodeInfo["event_handlers"] = EventHandlers;
						}
						if (USceneStateMachineTransitionNode* TransitionNode = Cast<USceneStateMachineTransitionNode>(StateMachineNode))
						{
								NodeInfo["source"] = TransitionNode->GetSourceNode() ? TCHAR_TO_UTF8(*TransitionNode->GetSourceNode()->GetNodeName().ToString()) : "";
								NodeInfo["target"] = TransitionNode->GetTargetNode() ? TCHAR_TO_UTF8(*TransitionNode->GetTargetNode()->GetNodeName().ToString()) : "";
								NodeInfo["priority"] = TransitionNode->GetPriority();
								NodeInfo["wait_for_tasks"] = TransitionNode->ShouldWaitForTasksToFinish();
								AddTransitionConditionInfo(NodeInfo, TransitionNode);
							}
							if (USceneStateMachineConduitNode* ConduitNode = Cast<USceneStateMachineConduitNode>(StateMachineNode))
							{
								NodeInfo["wait_for_tasks"] = ConduitNode->ShouldWaitForTasksToFinish();
								AddTransitionConditionInfo(NodeInfo, ConduitNode);
							}
							if (USceneStateMachineTaskNode* TaskNode = Cast<USceneStateMachineTaskNode>(StateMachineNode))
							{
								TConstStructView<FSceneStateTask> Task = TaskNode->GetTask();
								TStructView<FSceneStateTaskInstance> TaskInstance = TaskNode->GetTaskInstance();
									NodeInfo["task_struct"] = Task.GetScriptStruct() ? TCHAR_TO_UTF8(*Task.GetScriptStruct()->GetName()) : "";
									NodeInfo["task_instance_struct"] = TaskInstance.GetScriptStruct() ? TCHAR_TO_UTF8(*TaskInstance.GetScriptStruct()->GetName()) : "";
									NodeInfo["task_id"] = TCHAR_TO_UTF8(*TaskNode->GetTaskId().ToString(EGuidFormats::DigitsWithHyphens));
									NodeInfo["task_id_valid"] = TaskNode->GetTaskId().IsValid();
									if (TaskInstance.GetScriptStruct())
									{
										if (FFloatProperty* DelayProperty = FindFProperty<FFloatProperty>(TaskInstance.GetScriptStruct(), TEXT("Delay")))
										{
											NodeInfo["delay"] = DelayProperty->GetPropertyValue_InContainer(TaskInstance.GetMemory());
										}
										AddValueFieldInfo(NodeInfo, TaskInstance);
										AddBlueprintTaskInfo(NodeInfo, Task, TaskInstance);
										AddSpawnActorInfo(NodeInfo, Task, TaskInstance);
										AddPrintSettingsInfo(NodeInfo, TaskInstance);
									}
							}

							Nodes[NodeLuaIndex] = NodeInfo;
						}
				}
				GraphInfo["nodes"] = Nodes;
				Graphs[LuaIndex] = GraphInfo;
			}
		}
		Info["graphs"] = Graphs;
		return Info;
	}

	sol::object AddState(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& NodeName, int32 X, int32 Y)
	{
		sol::state_view Lua(S);
		if (!IsUsableSceneStateActor(Actor))
		{
			Actor = FindSceneStateActor(GetEditorWorld());
		}
		if (!Actor || NodeName.empty())
		{
			return sol::lua_nil;
		}

		USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
		USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
		if (!Blueprint || !Graph || FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(NodeName.c_str())))
		{
			return sol::lua_nil;
		}

		const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddSceneStateNode", "NeoStack: Add Scene State Node"));
		Graph->Modify();
		USceneStateMachineStateNode* Node = NewObject<USceneStateMachineStateNode>(Graph);
		if (!Node)
		{
			return sol::lua_nil;
		}

		Node->SetFlags(RF_Transactional);
		Graph->AddNode(Node, true, false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Node->OnRenameNode(UTF8_TO_TCHAR(NodeName.c_str()));
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return sol::make_object(S, BuildInfo(Lua, Actor));
		}

		sol::object AddConduit(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& NodeName, bool bWaitForTasks, int32 X, int32 Y)
		{
			sol::state_view Lua(S);
			if (!IsUsableSceneStateActor(Actor))
			{
				Actor = FindSceneStateActor(GetEditorWorld());
			}
			if (!Actor || NodeName.empty())
			{
				return sol::lua_nil;
			}

			USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
			USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
			if (!Blueprint || !Graph || FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(NodeName.c_str())))
			{
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddSceneStateConduit", "NeoStack: Add Scene State Conduit"));
			Graph->Modify();
			USceneStateMachineConduitNode* Node = NewObject<USceneStateMachineConduitNode>(Graph);
			if (!Node)
			{
				return sol::lua_nil;
			}

			Node->SetFlags(RF_Transactional);
			Graph->AddNode(Node, true, false);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
			Node->NodePosX = X;
			Node->NodePosY = Y;
			Node->OnRenameNode(UTF8_TO_TCHAR(NodeName.c_str()));
			if (FBoolProperty* WaitProperty = FindFProperty<FBoolProperty>(Node->GetClass(), TEXT("bWaitForTasksToFinish")))
			{
				WaitProperty->SetPropertyValue_InContainer(Node, bWaitForTasks);
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return sol::make_object(S, BuildInfo(Lua, Actor));
		}

		sol::object AddExit(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& NodeName, int32 X, int32 Y)
		{
			sol::state_view Lua(S);
			if (!IsUsableSceneStateActor(Actor))
			{
				Actor = FindSceneStateActor(GetEditorWorld());
			}
			if (!Actor || NodeName.empty())
			{
				return sol::lua_nil;
			}

			USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
			USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
			if (!Blueprint || !Graph || FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(NodeName.c_str())))
			{
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddSceneStateExit", "NeoStack: Add Scene State Exit"));
			Graph->Modify();
			USceneStateMachineExitNode* Node = NewObject<USceneStateMachineExitNode>(Graph);
			if (!Node)
			{
				return sol::lua_nil;
			}

			Node->SetFlags(RF_Transactional);
			Graph->AddNode(Node, true, false);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
			Node->NodePosX = X;
			Node->NodePosY = Y;
			Node->OnRenameNode(UTF8_TO_TCHAR(NodeName.c_str()));
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return sol::make_object(S, BuildInfo(Lua, Actor));
		}

		sol::object AddDelayTask(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& NodeName, float Delay, int32 X, int32 Y)
		{
			sol::state_view Lua(S);
			if (!IsUsableSceneStateActor(Actor))
			{
				Actor = FindSceneStateActor(GetEditorWorld());
			}
			if (!Actor || NodeName.empty())
			{
				return sol::lua_nil;
			}

			USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
			USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
			if (!Blueprint || !Graph || FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(NodeName.c_str())))
			{
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddSceneStateDelayTask", "NeoStack: Add Scene State Delay Task"));
			Graph->Modify();
			USceneStateMachineTaskNode* Node = NewObject<USceneStateMachineTaskNode>(Graph);
			if (!Node)
			{
				return sol::lua_nil;
			}

			Node->SetFlags(RF_Transactional);
			Graph->AddNode(Node, true, false);
			Node->CreateNewGuid();
				Node->PostPlacedNewNode();
				Node->AllocateDefaultPins();
				Node->SetTaskStruct(FSceneStateDelayTask::StaticStruct());
				EnsureTaskNodeId(Node);
				Node->NodePosX = X;
				Node->NodePosY = Y;
			Node->OnRenameNode(UTF8_TO_TCHAR(NodeName.c_str()));

			TStructView<FSceneStateTaskInstance> TaskInstance = Node->GetTaskInstance();
			if (TaskInstance.GetScriptStruct())
			{
				if (FFloatProperty* DelayProperty = FindFProperty<FFloatProperty>(TaskInstance.GetScriptStruct(), TEXT("Delay")))
				{
					DelayProperty->SetPropertyValue_InContainer(TaskInstance.GetMemory(), FMath::Max(0.0f, Delay));
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return sol::make_object(S, BuildInfo(Lua, Actor));
		}

		sol::object AddPrintStringTask(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& NodeName, sol::optional<sol::table> Options)
		{
			sol::state_view Lua(S);
			if (!IsUsableSceneStateActor(Actor))
			{
				Actor = FindSceneStateActor(GetEditorWorld());
			}
			if (!Actor || NodeName.empty())
			{
				return sol::lua_nil;
			}

			USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
			USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
			if (!Blueprint || !Graph || FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(NodeName.c_str())))
			{
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddSceneStatePrintStringTask", "NeoStack: Add Scene State Print String Task"));
			Graph->Modify();
			USceneStateMachineTaskNode* Node = NewObject<USceneStateMachineTaskNode>(Graph);
			if (!Node)
			{
				return sol::lua_nil;
			}

			Node->SetFlags(RF_Transactional);
			Graph->AddNode(Node, true, false);
			Node->CreateNewGuid();
				Node->PostPlacedNewNode();
				Node->AllocateDefaultPins();
				Node->SetTaskStruct(FSceneStatePrintStringTask::StaticStruct());
				EnsureTaskNodeId(Node);
				Node->OnRenameNode(UTF8_TO_TCHAR(NodeName.c_str()));

			Node->NodePosX = 0;
			Node->NodePosY = 0;
			if (Options)
			{
				sol::table Opts = *Options;
				Node->NodePosX = Opts.get_or("x", 0);
				Node->NodePosY = Opts.get_or("y", 0);
			}

			TStructView<FSceneStateTaskInstance> TaskInstance = Node->GetTaskInstance();
			const UScriptStruct* InstanceStruct = TaskInstance.GetScriptStruct();
			void* InstanceMemory = TaskInstance.GetMemory();
			if (InstanceStruct && InstanceMemory)
			{
				if (FStrProperty* MessageProperty = FindFProperty<FStrProperty>(InstanceStruct, TEXT("Message")))
				{
					std::string Message;
					if (Options)
					{
						sol::table Opts = *Options;
						Message = Opts.get_or<std::string>("message", "");
					}
					MessageProperty->SetPropertyValue_InContainer(InstanceMemory, UTF8_TO_TCHAR(Message.c_str()));
				}

				if (FStructProperty* PrintSettingsProperty = FindFProperty<FStructProperty>(InstanceStruct, TEXT("PrintSettings")))
				{
					void* SettingsMemory = PrintSettingsProperty->ContainerPtrToValuePtr<void>(InstanceMemory);
					if (SettingsMemory && PrintSettingsProperty->Struct)
					{
						if (FBoolProperty* PrintToLogProperty = FindBoolProperty(PrintSettingsProperty->Struct, TEXT("bPrintToLog")))
						{
							bool bPrintToLog = true;
							if (Options)
							{
								sol::table Opts = *Options;
								bPrintToLog = Opts.get_or("print_to_log", true);
							}
							PrintToLogProperty->SetPropertyValue_InContainer(SettingsMemory, bPrintToLog);
						}
						if (FBoolProperty* PrintToScreenProperty = FindBoolProperty(PrintSettingsProperty->Struct, TEXT("bPrintToScreen")))
						{
							bool bPrintToScreen = true;
							if (Options)
							{
								sol::table Opts = *Options;
								bPrintToScreen = Opts.get_or("print_to_screen", true);
							}
							PrintToScreenProperty->SetPropertyValue_InContainer(SettingsMemory, bPrintToScreen);
						}
						if (FFloatProperty* DurationProperty = FindFloatProperty(PrintSettingsProperty->Struct, TEXT("Duration")))
						{
							float Duration = 2.0f;
							if (Options)
							{
								sol::table Opts = *Options;
								Duration = static_cast<float>(Opts.get_or("duration", 2.0));
							}
							DurationProperty->SetPropertyValue_InContainer(SettingsMemory, Duration);
						}
						if (FNameProperty* KeyProperty = FindFProperty<FNameProperty>(PrintSettingsProperty->Struct, TEXT("Key")))
						{
							std::string Key;
							if (Options)
							{
								sol::table Opts = *Options;
								Key = Opts.get_or<std::string>("key", "");
							}
							KeyProperty->SetPropertyValue_InContainer(SettingsMemory, FName(UTF8_TO_TCHAR(Key.c_str())));
						}
						if (FStructProperty* TextColorProperty = FindFProperty<FStructProperty>(PrintSettingsProperty->Struct, TEXT("TextColor")))
						{
							if (TextColorProperty->Struct == TBaseStructure<FLinearColor>::Get())
							{
								FLinearColor* Color = TextColorProperty->ContainerPtrToValuePtr<FLinearColor>(SettingsMemory);
								if (Color && Options)
								{
									sol::table Opts = *Options;
									Color->R = static_cast<float>(Opts.get_or("text_color_r", static_cast<double>(Color->R)));
									Color->G = static_cast<float>(Opts.get_or("text_color_g", static_cast<double>(Color->G)));
									Color->B = static_cast<float>(Opts.get_or("text_color_b", static_cast<double>(Color->B)));
									Color->A = static_cast<float>(Opts.get_or("text_color_a", static_cast<double>(Color->A)));
								}
							}
						}
					}
				}
			}

				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				return sol::make_object(S, BuildInfo(Lua, Actor));
			}

			sol::object AddNativeTask(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& NodeName, const std::string& TaskStructName, sol::optional<sol::table> Options)
			{
				sol::state_view Lua(S);
				if (!IsUsableSceneStateActor(Actor))
				{
					Actor = FindSceneStateActor(GetEditorWorld());
				}
				if (!Actor || NodeName.empty())
				{
					return sol::lua_nil;
				}

				UScriptStruct* TaskStruct = ResolveTaskStruct(TaskStructName);
				if (!TaskStruct || !TaskStruct->IsChildOf(FSceneStateTask::StaticStruct()))
				{
					return sol::lua_nil;
				}

				USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
				USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
				if (!Blueprint || !Graph || FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(NodeName.c_str())))
				{
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddNativeSceneStateTask", "NeoStack: Add Native Scene State Task"));
				Graph->Modify();
				USceneStateMachineTaskNode* Node = NewObject<USceneStateMachineTaskNode>(Graph);
				if (!Node)
				{
					return sol::lua_nil;
				}

				Node->SetFlags(RF_Transactional);
				Graph->AddNode(Node, true, false);
				Node->CreateNewGuid();
				Node->PostPlacedNewNode();
				Node->AllocateDefaultPins();
				if (!Node->SetTaskStruct(TaskStruct))
				{
					Node->DestroyNode();
					return sol::lua_nil;
				}
				EnsureTaskNodeId(Node);
				Node->OnRenameNode(UTF8_TO_TCHAR(NodeName.c_str()));

				Node->NodePosX = 0;
				Node->NodePosY = 0;
				if (Options)
				{
					sol::table Opts = *Options;
					Node->NodePosX = Opts.get_or("x", 0);
					Node->NodePosY = Opts.get_or("y", 0);
					SetTaskInstanceValue(Node->GetTaskInstance(), Opts);
				}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return sol::make_object(S, BuildInfo(Lua, Actor));
		}

		sol::object AddBlueprintTask(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& NodeName, const std::string& TaskBlueprintPath, sol::optional<sol::table> Options)
		{
			sol::state_view Lua(S);
			if (!IsUsableSceneStateActor(Actor))
			{
				Actor = FindSceneStateActor(GetEditorWorld());
			}
			if (!Actor || NodeName.empty() || TaskBlueprintPath.empty())
			{
				return sol::lua_nil;
			}

			USceneStateTaskBlueprint* TaskBlueprint = LoadSceneStateTaskBlueprint(UTF8_TO_TCHAR(TaskBlueprintPath.c_str()));
			if (!TaskBlueprint || !TaskBlueprint->GeneratedClass || !TaskBlueprint->GeneratedClass->IsChildOf(USceneStateBlueprintableTask::StaticClass()))
			{
				return sol::lua_nil;
			}

			USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
			USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
			if (!Blueprint || !Graph || FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(NodeName.c_str())))
			{
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddBlueprintSceneStateTask", "NeoStack: Add Blueprint Scene State Task"));
			Graph->Modify();
			USceneStateMachineTaskNode* Node = NewObject<USceneStateMachineTaskNode>(Graph);
			if (!Node)
			{
				return sol::lua_nil;
			}

			Node->SetFlags(RF_Transactional);
			Graph->AddNode(Node, true, false);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
			Node->SetTaskBlueprintClass(Cast<UClass>(TaskBlueprint->GeneratedClass.Get()));
			EnsureTaskNodeId(Node);
			Node->OnRenameNode(UTF8_TO_TCHAR(NodeName.c_str()));

			Node->NodePosX = 0;
			Node->NodePosY = 0;
			if (Options)
			{
				sol::table Opts = *Options;
				Node->NodePosX = Opts.get_or("x", 0);
				Node->NodePosY = Opts.get_or("y", 0);
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return sol::make_object(S, BuildInfo(Lua, Actor));
		}

		sol::object AddBinding(sol::this_state S, AAvaSceneStateActor*& Actor, sol::optional<sol::table> Options)
		{
			sol::state_view Lua(S);
			if (!IsUsableSceneStateActor(Actor))
			{
				Actor = FindSceneStateActor(GetEditorWorld());
			}
			if (!Actor || !Options)
			{
				return sol::lua_nil;
			}

			USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
			if (!Blueprint)
			{
				return sol::lua_nil;
			}

			sol::table Opts = *Options;
			const std::string SourceId = Opts.get_or<std::string>("source_id", "");
			const std::string SourcePathString = Opts.get_or<std::string>("source_path", "");
			const std::string TargetId = Opts.get_or<std::string>("target_id", "");
			const std::string TargetPathString = Opts.get_or<std::string>("target_path", "");

			FPropertyBindingPath SourcePath;
			FPropertyBindingPath TargetPath;
			if (!TryBuildBindingPath(Blueprint, SourceId, SourcePathString, SourcePath)
				|| !TryBuildBindingPath(Blueprint, TargetId, TargetPathString, TargetPath))
			{
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddSceneStateBinding", "NeoStack: Add Scene State Binding"));
			Actor->Modify();
			Blueprint->Modify();
			Blueprint->GetBindingCollection().AddBinding(SourcePath, TargetPath);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return sol::make_object(S, BuildInfo(Lua, Actor));
		}

		sol::object AddFunctionBinding(sol::this_state S, AAvaSceneStateActor*& Actor, sol::optional<sol::table> Options)
		{
			sol::state_view Lua(S);
			if (!IsUsableSceneStateActor(Actor))
			{
				Actor = FindSceneStateActor(GetEditorWorld());
			}
			if (!Actor || !Options)
			{
				return sol::lua_nil;
			}

			USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
			if (!Blueprint)
			{
				return sol::lua_nil;
			}

			sol::table Opts = *Options;
			const std::string FunctionName = Opts.get_or<std::string>("function", "");
			const std::string OutputPathString = Opts.get_or<std::string>("output_path", "Output");
			const std::string TargetId = Opts.get_or<std::string>("target_id", "");
			const std::string TargetPathString = Opts.get_or<std::string>("target_path", "");
			if (FunctionName.empty())
			{
				return sol::lua_nil;
			}

			FPropertyBindingPath TargetPath;
			if (!TryBuildBindingPath(Blueprint, TargetId, TargetPathString, TargetPath))
			{
				return sol::lua_nil;
			}

			UE::SceneState::FBindingFunctionInfo FunctionInfo;
			bool bFoundFunction = false;
			Blueprint->ForEachBindableFunction(
				[&FunctionName, &FunctionInfo, &bFoundFunction](const FSceneStateBindingDesc& BindingDesc, const UE::SceneState::FBindingFunctionInfo& InFunctionInfo)->bool
				{
					const UScriptStruct* FunctionStruct = InFunctionInfo.FunctionTemplate.GetScriptStruct();
					if (!FunctionStruct)
					{
						return true;
					}

					const FString Query = UTF8_TO_TCHAR(FunctionName.c_str());
					const bool bMatches = FunctionStruct->GetName() == Query
						|| FunctionStruct->GetStructPathName().ToString() == Query
						|| FunctionStruct->GetDisplayNameText().ToString() == Query
						|| BindingDesc.Name.ToString() == Query;
					if (bMatches)
					{
						FunctionInfo = InFunctionInfo;
						bFoundFunction = true;
						return false;
					}
					return true;
				});

			const UScriptStruct* FunctionInstanceStruct = FunctionInfo.InstanceTemplate.GetScriptStruct();
			if (!bFoundFunction || !FunctionInstanceStruct)
			{
				return sol::lua_nil;
			}

			FPropertyBindingPath OutputPath;
			if (!TryBuildBindingPathFromView(
				FPropertyBindingDataView(FunctionInstanceStruct, nullptr),
				FGuid::NewGuid(),
				OutputPathString,
				OutputPath))
			{
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddSceneStateFunctionBinding", "NeoStack: Add Scene State Function Binding"));
			Actor->Modify();
			Blueprint->Modify();
			Blueprint->GetBindingCollection().AddBindingFunction(FunctionInfo, OutputPath.GetSegments(), TargetPath);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return sol::make_object(S, BuildInfo(Lua, Actor));
		}

		sol::object RemoveBinding(sol::this_state S, AAvaSceneStateActor*& Actor, sol::optional<sol::table> Options)
		{
			sol::state_view Lua(S);
			if (!IsUsableSceneStateActor(Actor))
			{
				Actor = FindSceneStateActor(GetEditorWorld());
			}
			if (!Actor || !Options)
			{
				return sol::lua_nil;
			}

			USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
			if (!Blueprint)
			{
				return sol::lua_nil;
			}

			sol::table Opts = *Options;
			const std::string TargetId = Opts.get_or<std::string>("target_id", "");
			const std::string TargetPathString = Opts.get_or<std::string>("target_path", "");

			FPropertyBindingPath TargetPath;
			if (!TryBuildBindingPath(Blueprint, TargetId, TargetPathString, TargetPath))
			{
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "RemoveSceneStateBinding", "NeoStack: Remove Scene State Binding"));
			Actor->Modify();
			Blueprint->Modify();
			Blueprint->GetBindingCollection().RemoveBindings(TargetPath, FPropertyBindingBindingCollection::ESearchMode::Exact);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return sol::make_object(S, BuildInfo(Lua, Actor));
		}

		AActor* ReadActorParameter(const FSceneStateMachineInstance* StateMachineInstance, FName ParameterName)
		{
			if (!StateMachineInstance || ParameterName.IsNone())
			{
				return nullptr;
			}

			const UPropertyBag* BagStruct = StateMachineInstance->Parameters.GetPropertyBagStruct();
			const uint8* Memory = StateMachineInstance->Parameters.GetValue().GetMemory();
			if (!BagStruct || !Memory)
			{
				return nullptr;
			}

			const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(BagStruct->FindPropertyByName(ParameterName));
			if (!ObjectProperty || !ObjectProperty->PropertyClass || !ObjectProperty->PropertyClass->IsChildOf(AActor::StaticClass()))
			{
				return nullptr;
			}

			const void* ValuePtr = ObjectProperty->ContainerPtrToValuePtr<void>(Memory);
			return Cast<AActor>(ObjectProperty->GetObjectPropertyValue(ValuePtr));
		}

		const FSceneStateMachineInstance* FindRuntimeStateMachineInstanceByIndex(FSceneStateExecutionContext* ExecutionContext, uint16 StateMachineIndex)
		{
			if (!ExecutionContext)
			{
				return nullptr;
			}

			const FMapProperty* InstancesProperty = FindFProperty<FMapProperty>(FSceneStateExecutionContext::StaticStruct(), TEXT("StateMachineInstances"));
			const FNumericProperty* KeyProperty = InstancesProperty ? CastField<FNumericProperty>(InstancesProperty->KeyProp) : nullptr;
			const FStructProperty* ValueProperty = InstancesProperty ? CastField<FStructProperty>(InstancesProperty->ValueProp) : nullptr;
			if (!InstancesProperty || !KeyProperty || !ValueProperty || ValueProperty->Struct != FSceneStateMachineInstance::StaticStruct())
			{
				return nullptr;
			}

			const void* MapMemory = InstancesProperty->ContainerPtrToValuePtr<void>(ExecutionContext);
			FScriptMapHelper Helper(InstancesProperty, MapMemory);
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}

				const uint64 KeyValue = KeyProperty->GetUnsignedIntPropertyValue(Helper.GetKeyPtr(Index));
				if (KeyValue == StateMachineIndex)
				{
					return reinterpret_cast<const FSceneStateMachineInstance*>(Helper.GetValuePtr(Index));
				}
			}
			return nullptr;
		}

		sol::table ReadRuntimeParameters(sol::state_view Lua, const FSceneStateMachineInstance* StateMachineInstance, sol::object ParameterNamesObject)
		{
			sol::table Parameters = Lua.create_table();
			if (!StateMachineInstance || ParameterNamesObject == sol::lua_nil || !ParameterNamesObject.is<sol::table>())
			{
				return Parameters;
			}

			int32 LuaIndex = 0;
			sol::table ParameterNames = ParameterNamesObject.as<sol::table>();
			for (const auto& Entry : ParameterNames)
			{
				if (!Entry.second.is<std::string>())
				{
					continue;
				}

				const std::string ParameterNameString = Entry.second.as<std::string>();
				const FName ParameterName(UTF8_TO_TCHAR(ParameterNameString.c_str()));
				sol::table Parameter = Lua.create_table();
				Parameter["name"] = ParameterNameString;
				Parameter["value"] = NeoLuaProperty::ReadPropertyBagValue(StateMachineInstance->Parameters, ParameterName, Lua);
				Parameters[++LuaIndex] = Parameter;
			}
			return Parameters;
		}

		sol::object RunSceneState(sol::this_state S, AAvaSceneStateActor*& Actor, sol::optional<sol::table> Options)
		{
			sol::state_view Lua(S);
			sol::table Result = Lua.create_table();
			Result["ok"] = false;

			if (!IsUsableSceneStateActor(Actor))
			{
				Actor = FindSceneStateActor(GetEditorWorld());
			}
			UWorld* World = GetEditorWorld();
			USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
			USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
			USceneStateComponent* Component = GetSceneStateComponent(Actor);
			USceneStateComponentPlayer* Player = Component ? Component->GetSceneStatePlayer() : nullptr;
			if (!Actor || !World || !Blueprint || !Graph || !Component || !Player)
			{
				Result["error"] = "missing scene state actor, blueprint, graph, component, or player";
				return sol::make_object(S, Result);
			}

			sol::table Opts = Options ? *Options : Lua.create_table();
			const std::string ActorClassPath = Opts.get_or<std::string>("actor_class", "");
			UClass* ActorClass = ActorClassPath.empty() ? AActor::StaticClass() : LoadClass<AActor>(nullptr, UTF8_TO_TCHAR(ActorClassPath.c_str()));
			if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
			{
				Result["error"] = "invalid actor_class";
				return sol::make_object(S, Result);
			}

			const std::string ParameterNameString = Opts.get_or<std::string>("spawned_actor_parameter", "");
			const FName ParameterName(UTF8_TO_TCHAR(ParameterNameString.c_str()));
			const float DeltaSeconds = static_cast<float>(Opts.get_or("delta_seconds", 0.016));
			const int32 TickCount = FMath::Max(0, Opts.get_or("ticks", 1));
			const bool bCleanupSpawnedActor = Opts.get_or("cleanup_spawned_actor", true);

			int32 SpawnedClassCountBefore = 0;
			for (TActorIterator<AActor> It(World, ActorClass); It; ++It)
			{
				if (*It)
				{
					++SpawnedClassCountBefore;
				}
			}

			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			if (!Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(USceneStateObject::StaticClass()))
			{
				Result["error"] = "compile did not produce a Scene State generated class";
				return sol::make_object(S, Result);
			}

			Component->SetSceneStateClass(TSubclassOf<USceneStateObject>(Blueprint->GeneratedClass.Get()));
			Player = Component->GetSceneStatePlayer();
			if (!Player)
			{
				Result["error"] = "component has no Scene State player after compile";
				return sol::make_object(S, Result);
			}

			Player->End();
			Player->Setup();
			Player->Begin();
			for (int32 TickIndex = 0; TickIndex < TickCount; ++TickIndex)
			{
				Player->Tick(DeltaSeconds);
			}

			USceneStateObject* RootState = Player->GetSceneState();
			const USceneStateTemplateData* TemplateData = RootState ? RootState->GetTemplateData() : nullptr;
			const uint16* StateMachineIndex = TemplateData ? TemplateData->StateMachineIdToIndex.Find(Graph->ParametersId) : nullptr;
			const FSceneStateMachineInstance* StateMachineInstance = nullptr;
			if (RootState && TemplateData && StateMachineIndex && TemplateData->StateMachines.IsValidIndex(*StateMachineIndex))
			{
				const FStructProperty* ContextProperty = FindFProperty<FStructProperty>(USceneStateObject::StaticClass(), TEXT("RootExecutionContext"));
				FSceneStateExecutionContext* ExecutionContext = ContextProperty && ContextProperty->Struct == FSceneStateExecutionContext::StaticStruct()
					? ContextProperty->ContainerPtrToValuePtr<FSceneStateExecutionContext>(RootState)
					: nullptr;
				StateMachineInstance = ExecutionContext
					? FindRuntimeStateMachineInstanceByIndex(ExecutionContext, *StateMachineIndex)
					: nullptr;
			}

			AActor* ReferencedActor = ReadActorParameter(StateMachineInstance, ParameterName);
			AActor* SpawnedActor = ReferencedActor;
			if (!SpawnedActor)
			{
				for (TActorIterator<AActor> It(World, ActorClass); It; ++It)
				{
					if (*It && *It != Actor)
					{
						SpawnedActor = *It;
						break;
					}
				}
			}

			int32 SpawnedClassCountAfter = 0;
			for (TActorIterator<AActor> It(World, ActorClass); It; ++It)
			{
				if (*It)
				{
					++SpawnedClassCountAfter;
				}
			}

			Result["ok"] = true;
			Result["root_active"] = RootState ? RootState->IsActive() : false;
			Result["compiled_state_machine_found"] = StateMachineIndex != nullptr;
			Result["runtime_state_machine_instance_found"] = StateMachineInstance != nullptr;
			Result["spawned_class_count_before"] = SpawnedClassCountBefore;
			Result["spawned_class_count_after"] = SpawnedClassCountAfter;
			Result["spawned_actor_parameter"] = ParameterNameString;
			Result["spawned_actor_parameter_resolved"] = ReferencedActor != nullptr;
			Result["spawned_actor_exists"] = SpawnedActor != nullptr;
			Result["spawned_actor_is_parameter_value"] = SpawnedActor != nullptr && SpawnedActor == ReferencedActor;
			Result["spawned_actor_class"] = SpawnedActor && SpawnedActor->GetClass() ? TCHAR_TO_UTF8(*SpawnedActor->GetClass()->GetName()) : "";
			Result["spawned_actor_class_path"] = SpawnedActor && SpawnedActor->GetClass() ? TCHAR_TO_UTF8(*SpawnedActor->GetClass()->GetPathName()) : "";
			Result["spawned_actor_name"] = SpawnedActor ? TCHAR_TO_UTF8(*SpawnedActor->GetName()) : "";
			Result["spawned_actor_path"] = SpawnedActor ? TCHAR_TO_UTF8(*SpawnedActor->GetPathName()) : "";
			const FVector SpawnedLocation = SpawnedActor ? SpawnedActor->GetActorLocation() : FVector::ZeroVector;
			Result["spawned_x"] = SpawnedLocation.X;
			Result["spawned_y"] = SpawnedLocation.Y;
			Result["spawned_z"] = SpawnedLocation.Z;
			Result["runtime_parameters"] = ReadRuntimeParameters(Lua, StateMachineInstance, Opts["parameters"]);

			if (bCleanupSpawnedActor && SpawnedActor)
			{
				Result["cleanup_destroyed_actor"] = World->DestroyActor(SpawnedActor, false, false);
			}
			else
			{
				Result["cleanup_destroyed_actor"] = false;
			}

			Player->End();
			return sol::make_object(S, Result);
		}

		sol::object CreateEventSchema(sol::this_state S, const std::string& CollectionPath, const std::string& SchemaName, sol::optional<sol::table> Options)
		{
			sol::state_view Lua(S);
			const FString PackageName = UTF8_TO_TCHAR(CollectionPath.c_str());
			const FString SchemaNameString = UTF8_TO_TCHAR(SchemaName.c_str());
			FText Reason;
			if (!FPackageName::IsValidLongPackageName(PackageName, false, &Reason) || SchemaNameString.IsEmpty())
			{
				return sol::lua_nil;
			}

			USceneStateEventSchemaCollection* Collection = LoadEventSchemaCollection(PackageName);
			if (!Collection)
			{
				const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
				UPackage* Package = CreatePackage(*PackageName);
				if (!Package)
				{
					return sol::lua_nil;
				}
				Collection = NewObject<USceneStateEventSchemaCollection>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
				if (!Collection)
				{
					return sol::lua_nil;
				}
				FAssetRegistryModule::AssetCreated(Collection);
			}

			Collection->Modify();
			USceneStateEventSchemaObject* Schema = FindEventSchemaByName(Collection, SchemaNameString);
			if (!Schema)
			{
				const EObjectFlags SchemaFlags = (Collection->GetMaskedFlags(RF_PropagateToSubObjects) | RF_Public | RF_Transactional) & ~RF_Standalone;
				Schema = NewObject<USceneStateEventSchemaObject>(Collection, NAME_None, SchemaFlags);
				if (!Schema)
				{
					return sol::lua_nil;
				}
				Schema->Name = FName(*SchemaNameString);
				if (!AddSchemaToCollection(Collection, Schema))
				{
					return sol::lua_nil;
				}
			}

			if (Options)
			{
				sol::table Opts = *Options;
				if (sol::object FieldsObject = Opts["fields"]; FieldsObject != sol::lua_nil)
				{
					sol::table Fields = FieldsObject.as<sol::table>();
					for (const auto& Entry : Fields)
					{
						if (!Entry.second.is<sol::table>())
						{
							return sol::lua_nil;
						}

						sol::table Field = Entry.second.as<sol::table>();
						const std::string FieldName = Field.get_or<std::string>("name", "");
						const std::string FieldType = Field.get_or<std::string>("type", "Bool");
						if (!AddEventSchemaField(Schema, FieldName, FieldType))
						{
							return sol::lua_nil;
						}
					}
				}
			}

			Collection->GetOutermost()->MarkPackageDirty();
			return sol::make_object(S, BuildEventSchemaCollectionInfo(Lua, Collection));
		}

		sol::object AddEventHandler(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& StateName, const std::string& CollectionPath, const std::string& SchemaName)
		{
			sol::state_view Lua(S);
			if (!IsUsableSceneStateActor(Actor))
			{
				Actor = FindSceneStateActor(GetEditorWorld());
			}
			if (!Actor || StateName.empty() || CollectionPath.empty() || SchemaName.empty())
			{
				return sol::lua_nil;
			}

			USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
			USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
			USceneStateMachineStateNode* StateNode = Cast<USceneStateMachineStateNode>(FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(StateName.c_str())));
			USceneStateEventSchemaCollection* Collection = LoadEventSchemaCollection(UTF8_TO_TCHAR(CollectionPath.c_str()));
			USceneStateEventSchemaObject* Schema = FindEventSchemaByName(Collection, UTF8_TO_TCHAR(SchemaName.c_str()));
			if (!Blueprint || !Graph || !StateNode || !Schema)
			{
				return sol::lua_nil;
			}

			FSceneStateEventHandler NewHandler(EForceInit::ForceInit);
			if (!SetEventSchemaHandle(NewHandler, Schema))
			{
				return sol::lua_nil;
			}

			const FSceneStateEventSchemaHandle& NewSchemaHandle = NewHandler.GetEventSchemaHandle();
			for (const FSceneStateEventHandler& ExistingHandler : StateNode->GetEventHandlers())
			{
				if (ExistingHandler.GetEventSchemaHandle() == NewSchemaHandle)
				{
					return sol::make_object(S, BuildInfo(Lua, Actor));
				}
			}

			const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddSceneStateEventHandler", "NeoStack: Add Scene State Event Handler"));
			Actor->Modify();
			Blueprint->Modify();
			StateNode->Modify();
			StateNode->EventHandlers.Add(NewHandler);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return sol::make_object(S, BuildInfo(Lua, Actor));
		}

		sol::object AddSpawnActorTask(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& NodeName, const std::string& ActorClassPath, sol::optional<sol::table> Options)
		{
			sol::state_view Lua(S);
			if (!IsUsableSceneStateActor(Actor))
				{
					Actor = FindSceneStateActor(GetEditorWorld());
				}
				if (!Actor || NodeName.empty() || ActorClassPath.empty())
				{
					return sol::lua_nil;
				}

				UClass* ActorClass = LoadClass<AActor>(nullptr, UTF8_TO_TCHAR(ActorClassPath.c_str()));
				if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
				{
					return sol::lua_nil;
				}

				USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
				USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
				if (!Blueprint || !Graph || FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(NodeName.c_str())))
				{
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "AddSpawnActorSceneStateTask", "NeoStack: Add Scene State Spawn Actor Task"));
				Graph->Modify();
				USceneStateMachineTaskNode* Node = NewObject<USceneStateMachineTaskNode>(Graph);
				if (!Node)
				{
					return sol::lua_nil;
				}

				Node->SetFlags(RF_Transactional);
				Graph->AddNode(Node, true, false);
				Node->CreateNewGuid();
				Node->PostPlacedNewNode();
				Node->AllocateDefaultPins();
				if (!Node->SetTaskStruct(FSceneStateSpawnActorTask::StaticStruct()) || !SetSpawnActorTaskClass(Node, ActorClass))
				{
					Node->DestroyNode();
					return sol::lua_nil;
				}
				EnsureTaskNodeId(Node);
				Node->OnRenameNode(UTF8_TO_TCHAR(NodeName.c_str()));

				Node->NodePosX = 0;
				Node->NodePosY = 0;
				if (Options)
				{
					sol::table Opts = *Options;
					Node->NodePosX = Opts.get_or("x", 0);
					Node->NodePosY = Opts.get_or("y", 0);
					ConfigureSpawnActorInstance(Node, ActorClass, Opts);
				}
				else
				{
					sol::table EmptyOptions = Lua.create_table();
					ConfigureSpawnActorInstance(Node, ActorClass, EmptyOptions);
				}

				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				return sol::make_object(S, BuildInfo(Lua, Actor));
			}

			sol::object ConfigureGraph(sol::this_state S, AAvaSceneStateActor*& Actor, sol::optional<sol::table> Options)
			{
				sol::state_view Lua(S);
				if (!IsUsableSceneStateActor(Actor))
				{
					Actor = FindSceneStateActor(GetEditorWorld());
				}
				if (!Actor || !Options)
				{
					return sol::lua_nil;
				}

				USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
				USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
				if (!Blueprint || !Graph)
				{
					return sol::lua_nil;
				}

				sol::table Opts = *Options;
				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "ConfigureSceneStateGraph", "NeoStack: Configure Scene State Graph"));
				Graph->Modify();

				if (sol::object CategoryObject = Opts["category"]; CategoryObject != sol::lua_nil)
				{
					Graph->Category = FText::FromString(UTF8_TO_TCHAR(CategoryObject.as<std::string>().c_str()));
				}

				if (sol::object RunModeObject = Opts["run_mode"]; RunModeObject != sol::lua_nil)
				{
					ESceneStateMachineRunMode RunMode;
					if (!TryParseRunMode(RunModeObject.as<std::string>(), RunMode))
					{
						return sol::lua_nil;
					}
					Graph->RunMode = RunMode;
				}

				bool bParametersChanged = false;
				if (sol::object ParametersObject = Opts["parameters"]; ParametersObject != sol::lua_nil)
				{
					sol::table ParametersTable = ParametersObject.as<sol::table>();
					for (const auto& Entry : ParametersTable)
					{
						if (!Entry.second.is<sol::table>())
						{
							return sol::lua_nil;
						}

						sol::table Param = Entry.second.as<sol::table>();
						const std::string ParamNameString = Param.get_or<std::string>("name", "");
						if (ParamNameString.empty())
						{
							return sol::lua_nil;
						}

						const FName ParamName(UTF8_TO_TCHAR(ParamNameString.c_str()));
						const bool bHasProperty = Graph->Parameters.FindPropertyDescByName(ParamName) != nullptr;
						if (!bHasProperty)
						{
							sol::object TypeSpec = Param["type"];
							if (TypeSpec == sol::lua_nil)
							{
								return sol::lua_nil;
							}

							FString AddError;
							if (!NeoLuaProperty::AddPropertyBagProperty(Graph->Parameters, ParamName, TypeSpec, AddError))
							{
								return sol::lua_nil;
							}
							bParametersChanged = true;
						}

						sol::object Value = Param["value"];
						if (Value != sol::lua_nil)
						{
							FString SetError;
							if (!NeoLuaProperty::SetPropertyBagValue(Graph->Parameters, ParamName, Value, SetError))
							{
								return sol::lua_nil;
							}
							bParametersChanged = true;
						}
					}
				}

				if (bParametersChanged)
				{
					Graph->NotifyParametersChanged();
				}
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				return sol::make_object(S, BuildInfo(Lua, Actor));
			}

			sol::object ConnectNodes(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& SourceName, const std::string& TargetName, sol::optional<sol::table> Options)
			{
		sol::state_view Lua(S);
		if (!IsUsableSceneStateActor(Actor))
		{
			Actor = FindSceneStateActor(GetEditorWorld());
		}
		if (!Actor || SourceName.empty() || TargetName.empty())
		{
			return sol::lua_nil;
		}

		USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
		USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
		USceneStateMachineNode* SourceNode = FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(SourceName.c_str()));
		USceneStateMachineNode* TargetNode = FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(TargetName.c_str()));
		if (!Blueprint || !Graph || !SourceNode || !TargetNode || !SourceNode->GetOutputPin() || !TargetNode->GetInputPin())
		{
			return sol::lua_nil;
		}

		const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "ConnectSceneStateNodes", "NeoStack: Connect Scene State Nodes"));
		Graph->Modify();
		const UEdGraphSchema* Schema = Graph->GetSchema();
		const bool bConnected = Schema && Schema->TryCreateConnection(SourceNode->GetOutputPin(), TargetNode->GetInputPin());
		if (!bConnected)
		{
			return sol::lua_nil;
		}

		if (USceneStateMachineTransitionNode* TransitionNode = FindTransition(SourceNode, TargetNode))
		{
			if (Options)
			{
				TransitionNode->Modify();
				sol::table Opts = *Options;
				if (FIntProperty* PriorityProperty = FindFProperty<FIntProperty>(TransitionNode->GetClass(), TEXT("Priority")))
				{
					const int32 Priority = Opts.get_or("priority", TransitionNode->GetPriority());
					PriorityProperty->SetPropertyValue_InContainer(TransitionNode, Priority);
				}
				if (FBoolProperty* WaitProperty = FindFProperty<FBoolProperty>(TransitionNode->GetClass(), TEXT("bWaitForTasksToFinish")))
				{
					const bool bWaitForTasks = Opts.get_or("wait_for_tasks", TransitionNode->ShouldWaitForTasksToFinish());
					WaitProperty->SetPropertyValue_InContainer(TransitionNode, bWaitForTasks);
				}
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return sol::make_object(S, BuildInfo(Lua, Actor));
	}

	sol::object SetTransitionConditionOnNode(sol::this_state S, AAvaSceneStateActor* Actor, USceneStateBlueprint* Blueprint, USceneStateMachineGraph* Graph, USceneStateMachineNode* Node, bool bCanTransition)
	{
		sol::state_view Lua(S);
		if (!Actor || !Blueprint || !Graph || !Node || (!Node->IsA<USceneStateMachineTransitionNode>() && !Node->IsA<USceneStateMachineConduitNode>()))
		{
			return sol::lua_nil;
		}

		if (!Node->GetBoundGraph())
		{
			return sol::lua_nil;
		}

		USceneStateTransitionResultNode* ResultNode = GetTransitionResultNode(Node);
		UEdGraphPin* CanTransitionPin = GetCanTransitionPin(ResultNode);
		if (!ResultNode || !CanTransitionPin || !CanTransitionPin->LinkedTo.IsEmpty())
		{
			return sol::lua_nil;
		}

		const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "SetSceneStateTransitionCondition", "NeoStack: Set Scene State Transition Condition"));
		Graph->Modify();
		Node->Modify();
		Node->GetBoundGraph()->Modify();
		ResultNode->Modify();
		CanTransitionPin->Modify();
		const UEdGraphSchema* Schema = Node->GetBoundGraph()->GetSchema();
		const FString NewValue = bCanTransition ? TEXT("true") : TEXT("false");
		if (Schema)
		{
			Schema->TrySetDefaultValue(*CanTransitionPin, NewValue);
		}
		else
		{
			CanTransitionPin->DefaultValue = NewValue;
		}

		bool bAuthoredValue = false;
		if (!ReadTransitionConditionDefault(Node, bAuthoredValue) || bAuthoredValue != bCanTransition)
		{
			return sol::lua_nil;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return sol::make_object(S, BuildInfo(Lua, Actor));
	}

	sol::object SetTransitionCondition(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& NodeName, bool bCanTransition)
	{
		if (!IsUsableSceneStateActor(Actor))
		{
			Actor = FindSceneStateActor(GetEditorWorld());
		}
		if (!Actor || NodeName.empty())
		{
			return sol::lua_nil;
		}

		USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
		USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
		USceneStateMachineNode* Node = FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(NodeName.c_str()));
		return SetTransitionConditionOnNode(S, Actor, Blueprint, Graph, Node, bCanTransition);
	}

	sol::object SetTransitionEdgeCondition(sol::this_state S, AAvaSceneStateActor*& Actor, const std::string& SourceName, const std::string& TargetName, bool bCanTransition)
	{
		sol::state_view Lua(S);
		if (!IsUsableSceneStateActor(Actor))
		{
			Actor = FindSceneStateActor(GetEditorWorld());
		}
		if (!Actor || SourceName.empty() || TargetName.empty())
		{
			return sol::lua_nil;
		}

		USceneStateBlueprint* Blueprint = GetSceneStateBlueprint(Actor);
		USceneStateMachineGraph* Graph = GetRootStateMachineGraph(Blueprint);
		USceneStateMachineNode* SourceNode = FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(SourceName.c_str()));
		USceneStateMachineNode* TargetNode = FindStateMachineNodeByName(Graph, UTF8_TO_TCHAR(TargetName.c_str()));
		USceneStateMachineTransitionNode* TransitionNode = FindTransition(SourceNode, TargetNode);
		return SetTransitionConditionOnNode(S, Actor, Blueprint, Graph, TransitionNode, bCanTransition);
	}

	bool DeleteActor(AAvaSceneStateActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		Actor->Modify();
		if (USceneStateBlueprint* Blueprint = Cast<USceneStateBlueprint>(GetSceneStateBlueprintObject(Actor)))
		{
			const FName ObjectName = MakeUniqueObjectName(GetTransientPackage(), Blueprint->GetClass(), FName(TEXT("SceneStateBlueprint_Trashed")));
			Blueprint->Rename(*ObjectName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		}
		SetSceneStateBlueprintObject(Actor, nullptr);
		Actor->SetSceneStateClass(nullptr);
		UWorld* World = Actor->GetWorld();
		return World ? World->EditorDestroyActor(Actor, true) : Actor->Destroy(false, false);
	}

	bool DeleteAllSceneStateActors(UWorld* World)
	{
		if (!World)
		{
			return false;
		}

		TArray<AAvaSceneStateActor*> Actors;
		for (AAvaSceneStateActor* Actor : TActorRange<AAvaSceneStateActor>(World))
		{
			if (IsUsableSceneStateActor(Actor))
			{
				Actors.Add(Actor);
			}
		}

		bool bDeletedAny = false;
		for (AAvaSceneStateActor* Actor : Actors)
		{
			bDeletedAny |= DeleteActor(Actor);
		}
		return bDeletedAny;
	}

	void BindAvalancheSceneStateLua(sol::state& Lua, FLuaSessionData&)
	{
		Lua.set_function("ava_scene_state", [](sol::this_state S) -> sol::object
		{
			sol::state_view State(S);
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return sol::lua_nil;
			}

			AAvaSceneStateActor* Actor = FindSceneStateActor(World);
			sol::table Handle = State.create_table();
			Handle["exists"] = Actor != nullptr;

			Handle.set_function("info", [Actor](sol::this_state InnerS) mutable -> sol::object
			{
				if (!IsUsableSceneStateActor(Actor))
				{
					Actor = FindSceneStateActor(GetEditorWorld());
				}
				if (!Actor)
				{
					return sol::lua_nil;
				}
				return sol::make_object(InnerS, BuildInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("ensure", [Actor](sol::this_state InnerS) mutable -> sol::object
			{
				UWorld* InnerWorld = GetEditorWorld();
				if (!InnerWorld)
				{
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "EnsureSceneState", "NeoStack: Ensure Motion Design Scene State"));
				if (!IsUsableSceneStateActor(Actor))
				{
					Actor = FindSceneStateActor(InnerWorld);
				}
				if (!Actor)
				{
					FActorSpawnParameters Params;
					Params.ObjectFlags = RF_Transactional;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					Actor = InnerWorld->SpawnActor<AAvaSceneStateActor>(Params);
				}
				if (!Actor)
				{
					return sol::lua_nil;
				}

				Actor->Modify();
				if (!Cast<USceneStateBlueprint>(GetSceneStateBlueprintObject(Actor)))
				{
					if (!CreateEmbeddedBlueprint(Actor))
					{
						return sol::lua_nil;
					}
				}
				return sol::make_object(InnerS, BuildInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("delete", [Actor](sol::this_state InnerS) mutable -> sol::object
			{
				UWorld* World = GetEditorWorld();
				if (!World)
				{
					return sol::make_object(InnerS, false);
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSceneState", "DeleteSceneState", "NeoStack: Delete Motion Design Scene State"));
				const bool bDeleted = DeleteAllSceneStateActors(World);
				Actor = nullptr;
				return sol::make_object(InnerS, bDeleted);
			});

			Handle.set_function("add_state", [Actor](sol::this_state InnerS, sol::table, const std::string& NodeName, sol::optional<sol::table> Options) mutable -> sol::object
			{
				int32 X = 0;
				int32 Y = 0;
				if (Options)
				{
					sol::table Opts = *Options;
					X = Opts.get_or("x", 0);
					Y = Opts.get_or("y", 0);
				}
				return AddState(InnerS, Actor, NodeName, X, Y);
			});

			Handle.set_function("connect", [Actor](sol::this_state InnerS, sol::table, const std::string& SourceName, const std::string& TargetName, sol::optional<sol::table> Options) mutable -> sol::object
			{
				return ConnectNodes(InnerS, Actor, SourceName, TargetName, Options);
			});

			Handle.set_function("add_delay_task", [Actor](sol::this_state InnerS, sol::table, const std::string& NodeName, sol::optional<sol::table> Options) mutable -> sol::object
			{
				float Delay = 0.5f;
				int32 X = 0;
				int32 Y = 0;
				if (Options)
				{
					sol::table Opts = *Options;
					Delay = static_cast<float>(Opts.get_or("delay", 0.5));
					X = Opts.get_or("x", 0);
					Y = Opts.get_or("y", 0);
				}
				return AddDelayTask(InnerS, Actor, NodeName, Delay, X, Y);
			});

				Handle.set_function("add_print_string_task", [Actor](sol::this_state InnerS, sol::table, const std::string& NodeName, sol::optional<sol::table> Options) mutable -> sol::object
				{
					return AddPrintStringTask(InnerS, Actor, NodeName, Options);
				});

				Handle.set_function("add_native_task", [Actor](sol::this_state InnerS, sol::table, const std::string& NodeName, const std::string& TaskStructName, sol::optional<sol::table> Options) mutable -> sol::object
				{
					return AddNativeTask(InnerS, Actor, NodeName, TaskStructName, Options);
				});

				Handle.set_function("create_task_blueprint", [](sol::this_state InnerS, sol::table, const std::string& AssetPath) -> sol::object
				{
					return CreateTaskBlueprintAsset(InnerS, AssetPath);
				});

				Handle.set_function("add_blueprint_task", [Actor](sol::this_state InnerS, sol::table, const std::string& NodeName, const std::string& TaskBlueprintPath, sol::optional<sol::table> Options) mutable -> sol::object
				{
					return AddBlueprintTask(InnerS, Actor, NodeName, TaskBlueprintPath, Options);
				});

				Handle.set_function("add_spawn_actor_task", [Actor](sol::this_state InnerS, sol::table, const std::string& NodeName, const std::string& ActorClassPath, sol::optional<sol::table> Options) mutable -> sol::object
				{
					return AddSpawnActorTask(InnerS, Actor, NodeName, ActorClassPath, Options);
				});

				Handle.set_function("configure_graph", [Actor](sol::this_state InnerS, sol::table, sol::optional<sol::table> Options) mutable -> sol::object
				{
					return ConfigureGraph(InnerS, Actor, Options);
				});

				Handle.set_function("add_binding", [Actor](sol::this_state InnerS, sol::table, sol::optional<sol::table> Options) mutable -> sol::object
				{
					return AddBinding(InnerS, Actor, Options);
				});

				Handle.set_function("add_function_binding", [Actor](sol::this_state InnerS, sol::table, sol::optional<sol::table> Options) mutable -> sol::object
				{
					return AddFunctionBinding(InnerS, Actor, Options);
				});

				Handle.set_function("remove_binding", [Actor](sol::this_state InnerS, sol::table, sol::optional<sol::table> Options) mutable -> sol::object
				{
					return RemoveBinding(InnerS, Actor, Options);
				});

				Handle.set_function("run", [Actor](sol::this_state InnerS, sol::table, sol::optional<sol::table> Options) mutable -> sol::object
				{
					return RunSceneState(InnerS, Actor, Options);
				});

				Handle.set_function("create_event_schema", [](sol::this_state InnerS, sol::table, const std::string& CollectionPath, const std::string& SchemaName, sol::optional<sol::table> Options) -> sol::object
				{
					return CreateEventSchema(InnerS, CollectionPath, SchemaName, Options);
				});

				Handle.set_function("add_event_handler", [Actor](sol::this_state InnerS, sol::table, const std::string& StateName, const std::string& CollectionPath, const std::string& SchemaName) mutable -> sol::object
				{
					return AddEventHandler(InnerS, Actor, StateName, CollectionPath, SchemaName);
				});

				Handle.set_function("set_condition", [Actor](sol::this_state InnerS, sol::table, const std::string& NodeName, bool bCanTransition) mutable -> sol::object
				{
					return SetTransitionCondition(InnerS, Actor, NodeName, bCanTransition);
				});

				Handle.set_function("set_transition_condition", [Actor](sol::this_state InnerS, sol::table, const std::string& SourceName, const std::string& TargetName, bool bCanTransition) mutable -> sol::object
				{
					return SetTransitionEdgeCondition(InnerS, Actor, SourceName, TargetName, bCanTransition);
				});

				Handle.set_function("add_conduit", [Actor](sol::this_state InnerS, sol::table, const std::string& NodeName, sol::optional<sol::table> Options) mutable -> sol::object
				{
				bool bWaitForTasks = true;
				int32 X = 0;
				int32 Y = 0;
				if (Options)
				{
					sol::table Opts = *Options;
					bWaitForTasks = Opts.get_or("wait_for_tasks", true);
					X = Opts.get_or("x", 0);
					Y = Opts.get_or("y", 0);
				}
				return AddConduit(InnerS, Actor, NodeName, bWaitForTasks, X, Y);
			});

			Handle.set_function("add_exit", [Actor](sol::this_state InnerS, sol::table, const std::string& NodeName, sol::optional<sol::table> Options) mutable -> sol::object
			{
				int32 X = 0;
				int32 Y = 0;
				if (Options)
				{
					sol::table Opts = *Options;
					X = Opts.get_or("x", 0);
					Y = Opts.get_or("y", 0);
				}
				return AddExit(InnerS, Actor, NodeName, X, Y);
			});

			return sol::make_object(S, Handle);
		});
	}
}

class FNSAI_AvalancheSceneStateModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildAvalancheSceneStateDescriptor());
		Registrar.RegisterLuaBinding(
			AvalancheSceneStateExtensionId,
			TEXT("AvalancheSceneState"),
			{
				{ TEXT("ava_scene_state()"), TEXT("Open Motion Design Scene State handle for the editor world"), TEXT("handle or nil") },
				{ TEXT("handle:ensure()"), TEXT("Find/spawn the hidden Scene State actor and create its embedded blueprint"), TEXT("info table or nil") },
					{ TEXT("handle:info()"), TEXT("Inspect actor, embedded blueprint, component, player, generated class, and graphs"), TEXT("info table or nil") },
					{ TEXT("handle:configure_graph(opts)"), TEXT("Set root state-machine category, run mode, and parameter bag entries"), TEXT("info table or nil") },
					{ TEXT("handle:add_state(name, opts?)"), TEXT("Add a Scene State state node to the root state-machine graph"), TEXT("info table or nil") },
					{ TEXT("handle:connect(source, target, opts?)"), TEXT("Connect Scene State graph nodes using the graph schema transition path"), TEXT("info table or nil") },
						{ TEXT("handle:add_delay_task(name, opts?)"), TEXT("Add and configure a native Delay task node in the root Scene State graph"), TEXT("info table or nil") },
						{ TEXT("handle:add_print_string_task(name, opts?)"), TEXT("Add and configure a native Print String task node in the root Scene State graph"), TEXT("info table or nil") },
						{ TEXT("handle:add_native_task(name, task_struct, opts?)"), TEXT("Add and configure a native Scene State task by UScriptStruct name or path"), TEXT("info table or nil") },
						{ TEXT("handle:create_task_blueprint(path)"), TEXT("Create a Motion Design Scene State Task Blueprint asset for Blueprint task authoring"), TEXT("info table or nil") },
						{ TEXT("handle:add_blueprint_task(name, task_blueprint_path, opts?)"), TEXT("Add a Blueprint task node using the task Blueprint generated class"), TEXT("info table or nil") },
						{ TEXT("handle:add_spawn_actor_task(name, actor_class, opts?)"), TEXT("Add and configure a native Spawn Actor task node with an actor template"), TEXT("info table or nil") },
						{ TEXT("handle:add_binding(opts)"), TEXT("Bind a Scene State source struct property path to a target struct property path"), TEXT("info table or nil") },
						{ TEXT("handle:add_function_binding(opts)"), TEXT("Bind a Scene State function output to a target property using the editor binding-function API"), TEXT("info table or nil") },
						{ TEXT("handle:remove_binding(opts)"), TEXT("Remove Scene State bindings for a target struct property path"), TEXT("info table or nil") },
						{ TEXT("handle:run(opts?)"), TEXT("Compile and run the Scene State player headlessly, returning runtime spawn and output-reference state"), TEXT("result table") },
							{ TEXT("handle:create_event_schema(collection_path, schema_name, opts?)"), TEXT("Create/configure a Scene State Event Schema Collection asset and schema fields"), TEXT("info table or nil") },
							{ TEXT("handle:add_event_handler(state_name, collection_path, schema_name)"), TEXT("Add an event handler schema reference to a state node"), TEXT("info table or nil") },
							{ TEXT("handle:set_condition(node_name, can_transition)"), TEXT("Set a transition or conduit result graph's bCanTransition default and compile the blueprint"), TEXT("info table or nil") },
							{ TEXT("handle:set_transition_condition(source, target, can_transition)"), TEXT("Set a source-target transition rule graph's bCanTransition default and compile the blueprint"), TEXT("info table or nil") },
							{ TEXT("handle:add_conduit(name, opts?)"), TEXT("Add and configure a Scene State conduit node in the root graph"), TEXT("info table or nil") },
					{ TEXT("handle:add_exit(name, opts?)"), TEXT("Add a Scene State exit node in the root graph"), TEXT("info table or nil") },
					{ TEXT("handle:delete()"), TEXT("Delete the Scene State actor and clean up the embedded blueprint reference"), TEXT("boolean") },
				},
			FLuaBindingFunc::CreateStatic(&BindAvalancheSceneStateLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(AvalancheSceneStateExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_AvalancheSceneStateModule, NSAI_AvalancheSceneState)
