#include "Modules/ModuleManager.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "Lua/LuaActorResolver.h"
#include "Lua/LuaBindingRegistry.h"
#include "Lua/LuaPropertyTable.h"

#include "AvaDefs.h"
#include "AvaAttributeContainer.h"
#include "AvaMediaDefines.h"
#include "AvaMediaSettings.h"
#include "AvaNameAttribute.h"
#include "AvaRCLibrary.h"
#include "AvaRemoteControlRebind.h"
#include "AvaScene.h"
#include "AvaSceneItem.h"
#include "AvaSceneSettings.h"
#include "AvaSceneRigSubsystem.h"
#include "AvaSequence.h"
#include "AvaSequencePlaybackObject.h"
#include "AvaSequencePlayer.h"
#include "AvaCameraSubsystem.h"
#include "AvaShapeActor.h"
#include "AvaShapePrimitiveFunctions.h"
#include "AvaTextActor.h"
#include "AvaTagCollection.h"
#include "AvaTagHandle.h"
#include "AvaTagHandleContainer.h"
#include "AvaTagId.h"
#include "AvaTransitionSubsystem.h"
#include "AvaTransitionTree.h"
#include "AvaViewportDataSubsystem.h"
#include "AvaViewportGuideInfo.h"
#include "AvaViewportGuidePresetProvider.h"
#include "AvaViewportSettings.h"
#include "Containers/Ticker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Action/RCAction.h"
#include "Broadcast/AvaBroadcast.h"
#include "Components/DynamicMeshComponent.h"
#include "Controller/RCController.h"
#include "DynamicMeshes/AvaShape2DArrowDynMesh.h"
#include "DynamicMeshes/AvaShapeChevronDynMesh.h"
#include "DynamicMeshes/AvaShapeConeDynMesh.h"
#include "DynamicMeshes/AvaShapeCubeDynMesh.h"
#include "DynamicMeshes/AvaShapeDynMeshBase.h"
#include "DynamicMeshes/AvaShapeEllipseDynMesh.h"
#include "DynamicMeshes/AvaShapeIrregularPolygonDynMesh.h"
#include "DynamicMeshes/AvaShapeLineDynMesh.h"
#include "DynamicMeshes/AvaShapeNGonDynMesh.h"
#include "DynamicMeshes/AvaShapeRectangleDynMesh.h"
#include "DynamicMeshes/AvaShapeRingDynMesh.h"
#include "DynamicMeshes/AvaShapeRoundedPolygonDynMesh.h"
#include "DynamicMeshes/AvaShapeSphereDynMesh.h"
#include "DynamicMeshes/AvaShapeStarDynMesh.h"
#include "DynamicMeshes/AvaShapeTorusDynMesh.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Level.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/LevelStreaming.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Commands/InputChord.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Behavior/IAvaTransitionBehavior.h"
#include "Behaviour/Builtin/Bind/RCBehaviourBind.h"
#include "Behaviour/Builtin/Bind/RCBehaviourBindNode.h"
#include "IAvalancheComponentVisualizersModule.h"
#include "IAvaSceneRigEditorModule.h"
#include "GeometryMaskCanvas.h"
#include "GeometryMaskReadComponent.h"
#include "GeometryMaskTypes.h"
#include "GeometryMaskWriteComponent.h"
#include "GeometryMaskWorldSubsystem.h"
#include "Characters/Text3DCharacterBase.h"
#include "Extensions/Text3DDefaultMaterialExtension.h"
#include "Extensions/Text3DDefaultRenderingExtension.h"
#include "Extensions/Text3DDefaultStyleExtension.h"
#include "Extensions/Text3DDefaultTokenExtension.h"
#include "Extensions/Text3DGeometryExtensionBase.h"
#include "Extensions/Text3DLayoutExtensionBase.h"
#include "Extensions/Text3DLayoutTransformEffect.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "MediaOutput.h"
#include "Mask2D/AvaMask2DBaseModifier.h"
#include "Mask2D/AvaMask2DReadModifier.h"
#include "Mask2D/AvaMask2DWriteModifier.h"
#include "Modifiers/ActorModifierCoreBase.h"
#include "Modifiers/ActorModifierCoreStack.h"
#include "Modifiers/Utilities/ActorModifierCoreLibrary.h"
#include "Playable/AvaPlayable.h"
#include "Playable/AvaPlayableGroup.h"
#include "Playback/AvaPlaybackGraph.h"
#include "Playback/Nodes/AvaPlaybackNode.h"
#include "Playback/Nodes/AvaPlaybackNodePlayer.h"
#include "Playback/Nodes/AvaPlaybackNodeRoot.h"
#include "Playback/Nodes/Events/Actions/AvaPlaybackAnimations.h"
#include "RemoteControlField.h"
#include "RemoteControlFieldPath.h"
#include "RemoteControlBinding.h"
#include "RemoteControlPreset.h"
#include "ScopedTransaction.h"
#include "Settings/Text3DProjectSettings.h"
#include "Text3DTypes.h"
#include "Rundown/AvaRundown.h"
#include "Rundown/AvaRundownMacroCollection.h"
#include "Rundown/AvaRundownPage.h"
#include "Rundown/AvaRundownPageCommand.h"
#include "Rundown/AvaRundownPagePlayer.h"
#include "Settings/AvaSequencePreset.h"
#include "Settings/AvaSequencerDisplayRate.h"
#include "Settings/AvaSequencerSettings.h"
#include "Tags/AvaTagAttributeBase.h"
#include "Tags/AvaTagContainerAttribute.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "Styles/Text3DStyleBase.h"
#include "Text3DActor.h"
#include "Text3DComponent.h"
#include "Tokens/Text3DTokenBase.h"
#include "Misc/PackageName.h"
#include "Types/SlateEnums.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UnrealEdGlobals.h"

#include <sol/sol.hpp>

namespace
{
	const TCHAR* AvalancheExtensionId = TEXT("neostack.avalanche");

	sol::table BuildLinearColor(sol::state_view Lua, const FLinearColor& Value);
	FLinearColor TableToLinearColor(const sol::table& Table, const FLinearColor& DefaultValue);

	FNeoStackExtensionDescriptor BuildAvalancheDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = AvalancheExtensionId;
		Descriptor.DisplayName = TEXT("Avalanche");
		Descriptor.ModuleName = TEXT("NSAI_Avalanche");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("motion-design");
		Descriptor.Description = TEXT("Author core Motion Design actors and parametric shapes from Lua.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("motion-design"), TEXT("shapes") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}

	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	UWorld* GetPIEWorld()
	{
		if (!GUnrealEd)
		{
			return nullptr;
		}

		if (FWorldContext* PIEContext = GUnrealEd->GetPIEWorldContext())
		{
			return PIEContext->World();
		}

		return nullptr;
	}

	FVector TableToVector(const sol::table& Table, const FVector& DefaultValue = FVector::ZeroVector)
	{
		return FVector(
			Table.get_or("x", Table.get_or("X", DefaultValue.X)),
			Table.get_or("y", Table.get_or("Y", DefaultValue.Y)),
			Table.get_or("z", Table.get_or("Z", DefaultValue.Z)));
	}

	FRotator TableToRotator(const sol::table& Table, const FRotator& DefaultValue = FRotator::ZeroRotator)
	{
		return FRotator(
			Table.get_or("pitch", Table.get_or("Pitch", DefaultValue.Pitch)),
			Table.get_or("yaw", Table.get_or("Yaw", DefaultValue.Yaw)),
			Table.get_or("roll", Table.get_or("Roll", DefaultValue.Roll)));
	}

	FVector2D TableToVector2D(const sol::table& Table, const FVector2D& DefaultValue = FVector2D(50.0, 50.0))
	{
		return FVector2D(
			Table.get_or("x", Table.get_or("X", DefaultValue.X)),
			Table.get_or("y", Table.get_or("Y", DefaultValue.Y)));
	}

	bool InvokeBoolSetter(UObject* Object, const FName FunctionName, bool bValue)
	{
		if (!Object)
		{
			return false;
		}

		UFunction* Function = Object->FindFunction(FunctionName);
		if (!Function || Function->NumParms != 1)
		{
			return false;
		}

		struct FBoolSetterParams
		{
			bool bValue = false;
		};
		FBoolSetterParams Params;
		Params.bValue = bValue;
		Object->ProcessEvent(Function, &Params);
		return true;
	}

	AAvaShapeActor* FindShapeActor(const std::string& Label)
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return nullptr;
		}

		const FString ActorId = UTF8_TO_TCHAR(Label.c_str());
		return Cast<AAvaShapeActor>(NeoLuaActor::FindByNameOrLabel(World, ActorId));
	}

		AText3DActor* FindTextActor(const std::string& Label)
		{
			UWorld* World = GetEditorWorld();
			if (!World)
		{
			return nullptr;
		}

			const FString ActorId = UTF8_TO_TCHAR(Label.c_str());
			return Cast<AText3DActor>(NeoLuaActor::FindByNameOrLabel(World, ActorId));
		}

		AAvaTextActor* FindLegacyTextActor(const std::string& Label)
		{
			UWorld* World = GetEditorWorld();
			if (!World || Label.empty())
			{
				return nullptr;
			}

			const FString ActorId = UTF8_TO_TCHAR(Label.c_str());
			return Cast<AAvaTextActor>(NeoLuaActor::FindByNameOrLabel(World, ActorId));
		}

		AAvaScene* GetMotionDesignScene(bool bCreate)
		{
			UWorld* World = GetEditorWorld();
			ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
			return Level ? AAvaScene::GetScene(Level, bCreate) : nullptr;
		}

		sol::table BuildRemoteControlControlledActorsProof(sol::state_view Lua, const std::string& ActorId, const std::string& ControllerName)
		{
			sol::table Result = Lua.create_table();
			Result["valid"] = false;
			Result["actor"] = "";
			Result["controller"] = "";
			Result["controller_count"] = 0;
			Result["behavior_count"] = 0;
			Result["action_count"] = 0;
			Result["controlled_actor_count"] = 0;
			Result["controlled_actors"] = Lua.create_table();
			Result["exposed_label"] = "";
			Result["exposed_id"] = "";
			Result["component_class"] = "";
			Result["property_path"] = "bVisible";
			Result["error"] = "";

			UWorld* World = GetEditorWorld();
			AActor* Actor = World ? NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(ActorId.c_str())) : nullptr;
			AAvaScene* Scene = GetMotionDesignScene(true);
			URemoteControlPreset* Preset = Scene ? Scene->GetRemoteControlPreset() : nullptr;
			USceneComponent* RootComponent = Actor ? Actor->GetRootComponent() : nullptr;
			if (!World || !Actor || !Preset || !RootComponent || ControllerName.empty())
			{
				Result["error"] = !World ? "no editor world"
					: !Actor ? "actor not found"
					: !Preset ? "motion design scene has no remote control preset"
					: !RootComponent ? "actor has no root component"
					: "controller name is empty";
				return Result;
			}

			const FName ControllerDisplayName(*FString(UTF8_TO_TCHAR(ControllerName.c_str())));
			if (URCVirtualPropertyBase* ExistingController = Preset->GetControllerByDisplayName(ControllerDisplayName))
			{
				Preset->RemoveController(ExistingController->GetPropertyName());
			}

			const FString ExposedLabel = FString::Printf(TEXT("%s_bVisible"), *FString(UTF8_TO_TCHAR(ControllerName.c_str())));
			if (const FGuid ExistingEntityId = Preset->GetExposedEntityId(FName(*ExposedLabel)); ExistingEntityId.IsValid())
			{
				Preset->Unexpose(ExistingEntityId);
			}

			FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AuthorRemoteControlControlledActorsProof", "NeoStack: Author Motion Design Remote Control Controlled Actors Proof"));
			URCController* Controller = Cast<URCController>(Preset->AddController(URCController::StaticClass(), EPropertyBagPropertyType::Bool, nullptr, ControllerDisplayName));
			if (!Controller)
			{
				Result["error"] = "failed to create bool controller";
				return Result;
			}
			Preset->SetControllerDisplayName(Controller->Id, ControllerDisplayName);

			FRemoteControlPresetExposeArgs ExposeArgs;
			ExposeArgs.Label = ExposedLabel;
			TSharedPtr<FRemoteControlProperty> ExposedProperty = Preset->ExposeProperty(RootComponent, FRCFieldPathInfo(TEXT("bVisible")), MoveTemp(ExposeArgs)).Pin();
			if (!ExposedProperty.IsValid())
			{
				Result["error"] = "failed to expose root component bVisible property";
				return Result;
			}

			URCBehaviour* Behavior = Controller->AddBehaviour(URCBehaviourBindNode::StaticClass());
			if (!Behavior)
			{
				Result["error"] = "failed to create bind behavior";
				return Result;
			}

			URCAction* Action = Behavior->AddAction(StaticCastSharedRef<const FRemoteControlField>(ExposedProperty.ToSharedRef()));
			if (!Action)
			{
				Result["error"] = "failed to create action for exposed property";
				return Result;
			}

			TArray<AActor*> ControlledActors = UAvaRCLibrary::GetControlledActors(World, Controller);
			sol::table ControlledActorRows = Lua.create_table();
			int32 ControlledActorCount = 0;
			for (AActor* ControlledActor : ControlledActors)
			{
				if (!ControlledActor)
				{
					continue;
				}
				sol::table Row = Lua.create_table();
				Row["label"] = TCHAR_TO_UTF8(*ControlledActor->GetActorLabel());
				Row["class"] = TCHAR_TO_UTF8(*ControlledActor->GetClass()->GetName());
				Row["path"] = TCHAR_TO_UTF8(*ControlledActor->GetPathName());
				ControlledActorRows[++ControlledActorCount] = Row;
			}

			Result["valid"] = ControlledActorCount > 0;
			Result["actor"] = TCHAR_TO_UTF8(*Actor->GetActorLabel());
			Result["controller"] = TCHAR_TO_UTF8(*Controller->DisplayName.ToString());
			Result["controller_count"] = Preset->GetNumControllers();
			Result["behavior_count"] = Controller->GetBehaviors().Num();
			Result["action_count"] = Behavior->GetNumActions();
			Result["controlled_actor_count"] = ControlledActorCount;
			Result["controlled_actors"] = ControlledActorRows;
			Result["exposed_label"] = TCHAR_TO_UTF8(*ExposedProperty->GetLabel().ToString());
			Result["exposed_id"] = TCHAR_TO_UTF8(*ExposedProperty->GetId().ToString(EGuidFormats::DigitsWithHyphens));
			Result["component_class"] = TCHAR_TO_UTF8(*RootComponent->GetClass()->GetName());
			Result["action_exposed_id"] = Action ? TCHAR_TO_UTF8(*Action->ExposedFieldId.ToString(EGuidFormats::DigitsWithHyphens)) : "";
			Result["error"] = "";
			return Result;
		}

		sol::table BuildRemoteControlRebindProof(sol::state_view Lua, const std::string& TargetActorId, const std::string& DecoyActorId, const std::string& ExposedLabelSuffix)
		{
			sol::table Result = Lua.create_table();
			Result["valid"] = false;
			Result["target_actor"] = "";
			Result["decoy_actor"] = "";
			Result["property_path"] = "bVisible";
			Result["binding_class"] = "";
			Result["exposed_label"] = "";
			Result["exposed_id"] = "";
			Result["initial_bound_owner"] = "";
			Result["initial_bound_object"] = "";
			Result["disturbed_bound_owner"] = "";
			Result["disturbed_bound_object"] = "";
			Result["rebound_owner"] = "";
			Result["rebound_object"] = "";
			Result["target_actor_name"] = "";
			Result["decoy_actor_name"] = "";
			Result["binding_context_owner_name"] = "";
			Result["binding_context_component_name"] = "";
			Result["binding_context_subobject_path"] = "";
			Result["binding_context_owner_class"] = "";
			Result["binding_context_supported_class"] = "";
			Result["field_path_resolved_before"] = false;
			Result["field_path_resolved_after"] = false;
			Result["error"] = "";

			UWorld* World = GetEditorWorld();
			ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
			AActor* TargetActor = World ? NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(TargetActorId.c_str())) : nullptr;
			AActor* DecoyActor = World ? NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(DecoyActorId.c_str())) : nullptr;
			USceneComponent* TargetRoot = TargetActor ? TargetActor->GetRootComponent() : nullptr;
			USceneComponent* DecoyRoot = DecoyActor ? DecoyActor->GetRootComponent() : nullptr;
			AAvaScene* Scene = GetMotionDesignScene(true);
			URemoteControlPreset* Preset = Scene ? Scene->GetRemoteControlPreset() : nullptr;
			if (!World || !Level || !TargetActor || !DecoyActor || !TargetRoot || !DecoyRoot || !Preset)
			{
				Result["error"] = !World ? "no editor world"
					: !Level ? "no current level"
					: !TargetActor ? "target actor not found"
					: !DecoyActor ? "decoy actor not found"
					: !TargetRoot ? "target actor has no root component"
					: !DecoyRoot ? "decoy actor has no root component"
					: "motion design scene has no remote control preset";
				return Result;
			}

			const FString LabelSuffix = ExposedLabelSuffix.empty()
				? FString::Printf(TEXT("%s_RebindVisible"), *TargetActor->GetActorLabel())
				: FString(UTF8_TO_TCHAR(ExposedLabelSuffix.c_str()));
			const FString ExposedLabel = FString::Printf(TEXT("NSAI_Rebind_%s"), *LabelSuffix);

			FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AuthorRemoteControlRebindProof", "NeoStack: Author Motion Design Remote Control Rebind Proof"));
			FRemoteControlPresetExposeArgs ExposeArgs;
			ExposeArgs.Label = ExposedLabel;
			TSharedPtr<FRemoteControlProperty> ExposedProperty = Preset->ExposeProperty(TargetRoot, FRCFieldPathInfo(TEXT("bVisible")), MoveTemp(ExposeArgs)).Pin();
			if (!ExposedProperty.IsValid())
			{
				Result["error"] = "failed to expose target root component bVisible property";
				return Result;
			}

			const TArray<TWeakObjectPtr<URemoteControlBinding>>& Bindings = ExposedProperty->GetBindings();
			URemoteControlLevelDependantBinding* LevelBinding = Bindings.Num() > 0 ? Cast<URemoteControlLevelDependantBinding>(Bindings[0].Get()) : nullptr;
			if (!LevelBinding)
			{
				Result["error"] = "exposed property did not create a level-dependent binding";
				return Result;
			}

			FString BindingContextOwnerName;
			FString BindingContextComponentName;
			FString BindingContextSubObjectPath;
			FString BindingContextOwnerClass;
			FString BindingContextSupportedClass;
			const FStructProperty* BindingContextProperty = FindFProperty<FStructProperty>(LevelBinding->GetClass(), TEXT("BindingContext"));
			if (BindingContextProperty && BindingContextProperty->Struct->IsChildOf(FRemoteControlInitialBindingContext::StaticStruct()))
			{
				const FRemoteControlInitialBindingContext* BindingContext = BindingContextProperty->ContainerPtrToValuePtr<FRemoteControlInitialBindingContext>(LevelBinding);
				if (BindingContext)
				{
					BindingContextOwnerName = BindingContext->OwnerActorName.ToString();
					BindingContextComponentName = BindingContext->ComponentName.ToString();
					BindingContextSubObjectPath = BindingContext->SubObjectPath;
					BindingContextOwnerClass = BindingContext->OwnerActorClass.ToString();
					BindingContextSupportedClass = BindingContext->SupportedClass.ToString();
				}
			}

			auto DescribeBoundObject = [](UObject* Object, FString& OutOwner, FString& OutObject)
			{
				OutOwner.Reset();
				OutObject.Reset();
				if (!Object)
				{
					return;
				}
				OutObject = Object->GetName();
				if (const UActorComponent* Component = Cast<UActorComponent>(Object))
				{
					OutOwner = Component->GetOwner() ? Component->GetOwner()->GetActorLabel() : FString();
				}
				else if (const AActor* Actor = Cast<AActor>(Object))
				{
					OutOwner = Actor->GetActorLabel();
				}
				else if (const AActor* OuterActor = Object->GetTypedOuter<AActor>())
				{
					OutOwner = OuterActor->GetActorLabel();
				}
			};

			UObject* InitialBoundObject = LevelBinding->Resolve();
			FString InitialOwner;
			FString InitialObject;
			DescribeBoundObject(InitialBoundObject, InitialOwner, InitialObject);

			LevelBinding->SetBoundObject(TSoftObjectPtr<ULevel>(Level), TSoftObjectPtr<UObject>(DecoyRoot));
			UObject* DisturbedBoundObject = LevelBinding->Resolve();
			FString DisturbedOwner;
			FString DisturbedObject;
			DescribeBoundObject(DisturbedBoundObject, DisturbedOwner, DisturbedObject);

			const bool bFieldPathResolvedBefore = ExposedProperty->FieldPathInfo.IsResolved();
			FAvaRemoteControlRebind::RebindUnboundEntities(Preset, Level);
			const bool bFieldPathResolvedAfter = ExposedProperty->FieldPathInfo.IsResolved();

			UObject* ReboundObject = LevelBinding->Resolve();
			FString ReboundOwner;
			FString ReboundObjectName;
			DescribeBoundObject(ReboundObject, ReboundOwner, ReboundObjectName);

			Result["valid"] = ReboundObject == TargetRoot
				&& InitialBoundObject == TargetRoot
				&& DisturbedBoundObject == DecoyRoot
				&& bFieldPathResolvedAfter;
			Result["target_actor"] = TCHAR_TO_UTF8(*TargetActor->GetActorLabel());
			Result["decoy_actor"] = TCHAR_TO_UTF8(*DecoyActor->GetActorLabel());
			Result["binding_class"] = TCHAR_TO_UTF8(*LevelBinding->GetClass()->GetName());
			Result["exposed_label"] = TCHAR_TO_UTF8(*ExposedProperty->GetLabel().ToString());
			Result["exposed_id"] = TCHAR_TO_UTF8(*ExposedProperty->GetId().ToString(EGuidFormats::DigitsWithHyphens));
			Result["initial_bound_owner"] = TCHAR_TO_UTF8(*InitialOwner);
			Result["initial_bound_object"] = TCHAR_TO_UTF8(*InitialObject);
			Result["disturbed_bound_owner"] = TCHAR_TO_UTF8(*DisturbedOwner);
			Result["disturbed_bound_object"] = TCHAR_TO_UTF8(*DisturbedObject);
			Result["rebound_owner"] = TCHAR_TO_UTF8(*ReboundOwner);
			Result["rebound_object"] = TCHAR_TO_UTF8(*ReboundObjectName);
			Result["target_actor_name"] = TCHAR_TO_UTF8(*TargetActor->GetFName().ToString());
			Result["decoy_actor_name"] = TCHAR_TO_UTF8(*DecoyActor->GetFName().ToString());
			Result["binding_context_owner_name"] = TCHAR_TO_UTF8(*BindingContextOwnerName);
			Result["binding_context_component_name"] = TCHAR_TO_UTF8(*BindingContextComponentName);
			Result["binding_context_subobject_path"] = TCHAR_TO_UTF8(*BindingContextSubObjectPath);
			Result["binding_context_owner_class"] = TCHAR_TO_UTF8(*BindingContextOwnerClass);
			Result["binding_context_supported_class"] = TCHAR_TO_UTF8(*BindingContextSupportedClass);
			Result["field_path_resolved_before"] = bFieldPathResolvedBefore;
			Result["field_path_resolved_after"] = bFieldPathResolvedAfter;
			Result["error"] = "";
			return Result;
		}

		UAvaTagCollection* LoadTagCollection(const std::string& Path)
		{
			if (Path.empty())
			{
				return nullptr;
			}
			return Cast<UAvaTagCollection>(FSoftObjectPath(UTF8_TO_TCHAR(Path.c_str())).TryLoad());
		}

		FAvaTagHandle FindTagHandle(UAvaTagCollection* Collection, const FName TagName)
		{
			if (!Collection || TagName.IsNone())
			{
				return FAvaTagHandle();
			}

			for (const FAvaTagId& TagId : Collection->GetTagIds(true))
			{
				if (TagId.IsValid() && Collection->GetTagName(TagId) == TagName)
				{
					return FAvaTagHandle(Collection, TagId);
				}
			}
			return FAvaTagHandle();
		}

		TArray<FAvaTagHandle> ResolveTagHandles(UAvaTagCollection* Collection, const sol::table& Options)
		{
			TArray<FAvaTagHandle> Handles;
			if (!Collection)
			{
				return Handles;
			}

			if (sol::optional<std::string> TagNameValue = Options.get<sol::optional<std::string>>("tag"))
			{
				const FAvaTagHandle Handle = FindTagHandle(Collection, FName(UTF8_TO_TCHAR(TagNameValue->c_str())));
				if (Handle.IsValid())
				{
					Handles.Add(Handle);
				}
			}

			if (sol::optional<sol::table> TagsTable = Options.get<sol::optional<sol::table>>("tags"))
			{
				for (const auto& Entry : *TagsTable)
				{
					const sol::optional<std::string> TagNameValue = Entry.second.as<sol::optional<std::string>>();
					if (!TagNameValue)
					{
						continue;
					}

					const FAvaTagHandle Handle = FindTagHandle(Collection, FName(UTF8_TO_TCHAR(TagNameValue->c_str())));
					if (Handle.IsValid())
					{
						const bool bAlreadyAdded = Handles.ContainsByPredicate(
							[&Handle](const FAvaTagHandle& ExistingHandle)
							{
								return ExistingHandle.MatchesExact(Handle);
							});
						if (!bAlreadyAdded)
						{
							Handles.Add(Handle);
						}
					}
				}
			}

			return Handles;
		}

		EAvaTransitionInstancingMode ParseAvalancheInstancingMode(const std::string& Value, EAvaTransitionInstancingMode DefaultValue)
		{
			const FString Mode = UTF8_TO_TCHAR(Value.c_str());
			if (Mode.Equals(TEXT("Reuse"), ESearchCase::IgnoreCase) ||
				Mode.Equals(TEXT("EAvaTransitionInstancingMode::Reuse"), ESearchCase::IgnoreCase))
			{
				return EAvaTransitionInstancingMode::Reuse;
			}
			if (Mode.Equals(TEXT("New"), ESearchCase::IgnoreCase) ||
				Mode.Equals(TEXT("EAvaTransitionInstancingMode::New"), ESearchCase::IgnoreCase))
			{
				return EAvaTransitionInstancingMode::New;
			}
			return DefaultValue;
		}

		const TCHAR* AvalancheInstancingModeToString(EAvaTransitionInstancingMode Mode)
		{
			switch (Mode)
			{
			case EAvaTransitionInstancingMode::New:
				return TEXT("New");
			case EAvaTransitionInstancingMode::Reuse:
				return TEXT("Reuse");
			default:
				return TEXT("Unknown");
			}
		}

		sol::table BuildAvaTransitionLayerInfo(sol::state_view State, const FAvaTagHandle& Layer)
		{
			sol::table Result = State.create_table();
			Result["valid"] = Layer.IsValid();
			Result["name"] = std::string(TCHAR_TO_UTF8(*Layer.ToName().ToString()));
			Result["value"] = std::string(TCHAR_TO_UTF8(*Layer.ToString()));
			Result["debug"] = std::string(TCHAR_TO_UTF8(*Layer.ToDebugString()));
			Result["source"] = Layer.Source
				? std::string(TCHAR_TO_UTF8(*FSoftObjectPath(Layer.Source).ToString()))
				: std::string();
			return Result;
		}

		sol::table BuildSceneTagContainsInfo(sol::state_view State, UAvaAttributeContainer* AttributeContainer, const TArray<FAvaTagHandle>& Handles)
		{
			sol::table Result = State.create_table();
			int32 LuaIndex = 0;
			int32 ContainsCount = 0;
			for (const FAvaTagHandle& Handle : Handles)
			{
				const bool bContains = AttributeContainer && AttributeContainer->ContainsTagAttribute(Handle);
				if (bContains)
				{
					++ContainsCount;
				}

				sol::table Row = State.create_table();
				Row["name"] = std::string(TCHAR_TO_UTF8(*Handle.ToName().ToString()));
				Row["valid"] = Handle.IsValid();
				Row["contains"] = bContains;
				Result[++LuaIndex] = Row;
			}
			Result["count"] = Handles.Num();
			Result["contains_count"] = ContainsCount;
			Result["contains_all"] = Handles.Num() > 0 && ContainsCount == Handles.Num();
			return Result;
		}

		bool AddObjectArrayAttribute(UObject* Owner, const FName PropertyName, UAvaAttribute* Attribute)
		{
			if (!Owner || PropertyName.IsNone() || !Attribute)
			{
				return false;
			}

			FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(Owner->GetClass(), PropertyName);
			FObjectPropertyBase* ObjectProperty = ArrayProperty ? CastField<FObjectPropertyBase>(ArrayProperty->Inner) : nullptr;
			if (!ArrayProperty || !ObjectProperty)
			{
				return false;
			}

			FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Owner));
			const int32 Index = Helper.AddValue();
			ObjectProperty->SetObjectPropertyValue(Helper.GetRawPtr(Index), Attribute);
			return true;
		}

		int32 RemoveObjectArrayTagContainerAttributes(UObject* Owner, const FName PropertyName, const TArray<FAvaTagHandle>& Handles)
		{
			if (!Owner || PropertyName.IsNone() || Handles.IsEmpty())
			{
				return 0;
			}

			FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(Owner->GetClass(), PropertyName);
			FObjectPropertyBase* ObjectProperty = ArrayProperty ? CastField<FObjectPropertyBase>(ArrayProperty->Inner) : nullptr;
			if (!ArrayProperty || !ObjectProperty)
			{
				return 0;
			}

			FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Owner));
			int32 RemovedCount = 0;
			for (int32 Index = Helper.Num() - 1; Index >= 0; --Index)
			{
				UAvaTagContainerAttribute* ContainerAttribute = Cast<UAvaTagContainerAttribute>(ObjectProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
				if (!ContainerAttribute)
				{
					continue;
				}

				bool bMatchesAll = true;
				for (const FAvaTagHandle& Handle : Handles)
				{
					if (!ContainerAttribute->ContainsTag(Handle))
					{
						bMatchesAll = false;
						break;
					}
				}

				if (bMatchesAll)
				{
					Helper.RemoveValues(Index);
					++RemovedCount;
				}
			}
			return RemovedCount;
		}

		sol::object BuildSceneInfo(sol::state_view State, bool bCreate)
		{
			AAvaScene* Scene = GetMotionDesignScene(bCreate);
			if (!Scene)
			{
				return sol::lua_nil;
			}

			UAvaSceneSettings* Settings = Scene->GetSceneSettings();
			UAvaAttributeContainer* AttributeContainer = Scene->GetAttributeContainer();

			sol::table Result = State.create_table();
			Result["actor_name"] = std::string(TCHAR_TO_UTF8(*Scene->GetName()));
			Result["actor_class"] = std::string(TCHAR_TO_UTF8(*Scene->GetClass()->GetName()));
			Result["has_scene_settings"] = Settings != nullptr;
			Result["has_attribute_container"] = AttributeContainer != nullptr;
			Result["has_remote_control_preset"] = Scene->GetRemoteControlPreset() != nullptr;
			Result["settings_scene_attribute_count"] = Settings ? Settings->GetSceneAttributes().Num() : 0;
			Result["scene_rig"] = Settings ? std::string(TCHAR_TO_UTF8(*Settings->GetSceneRig().ToString())) : std::string();

			sol::table Names = State.create_table();
			sol::table TagAttributes = State.create_table();
			int32 AttributeCount = 0;
			int32 NameAttributeCount = 0;
			int32 TagAttributeCount = 0;
			int32 TagContainerAttributeCount = 0;
			if (AttributeContainer)
			{
				if (FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(AttributeContainer->GetClass(), TEXT("SceneAttributes")))
				{
					FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(AttributeContainer));
					AttributeCount = Helper.Num();
					if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner))
					{
						for (int32 Index = 0; Index < Helper.Num(); ++Index)
						{
							if (UAvaNameAttribute* NameAttribute = Cast<UAvaNameAttribute>(ObjectProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index))))
							{
								++NameAttributeCount;
								Names[NameAttributeCount] = std::string(TCHAR_TO_UTF8(*NameAttribute->Name.ToString()));
							}
							if (UAvaTagAttributeBase* TagAttribute = Cast<UAvaTagAttributeBase>(ObjectProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index))))
							{
								++TagAttributeCount;
								if (Cast<UAvaTagContainerAttribute>(TagAttribute))
								{
									++TagContainerAttributeCount;
								}

								sol::table Row = State.create_table();
								Row["class"] = std::string(TCHAR_TO_UTF8(*TagAttribute->GetClass()->GetName()));
								Row["display_name"] = std::string(TCHAR_TO_UTF8(*TagAttribute->GetDisplayName().ToString()));
								Row["has_valid_tag_handle"] = TagAttribute->HasValidTagHandle();
								TagAttributes[TagAttributeCount] = Row;
							}
						}
					}
				}
			}
			Result["attribute_count"] = AttributeCount;
			Result["name_attribute_count"] = NameAttributeCount;
			Result["name_attributes"] = Names;
			Result["tag_attribute_count"] = TagAttributeCount;
			Result["tag_container_attribute_count"] = TagContainerAttributeCount;
			Result["tag_attributes"] = TagAttributes;

			int32 SettingsTagAttributeCount = 0;
			int32 SettingsTagContainerAttributeCount = 0;
			if (Settings)
			{
				for (UAvaAttribute* Attribute : Settings->GetSceneAttributes())
				{
					if (UAvaTagAttributeBase* TagAttribute = Cast<UAvaTagAttributeBase>(Attribute))
					{
						++SettingsTagAttributeCount;
						if (Cast<UAvaTagContainerAttribute>(TagAttribute))
						{
							++SettingsTagContainerAttributeCount;
						}
					}
				}
			}
			Result["settings_tag_attribute_count"] = SettingsTagAttributeCount;
			Result["settings_tag_container_attribute_count"] = SettingsTagContainerAttributeCount;
			return sol::make_object(State, Result);
		}

		sol::table BuildSceneTreeActorInfo(sol::state_view State, AActor* Actor, const FAvaSceneTree& SceneTree, UWorld* World)
		{
			sol::table Row = State.create_table();
			if (!IsValid(Actor) || !World)
			{
				Row["valid"] = false;
				return Row;
			}

			Row["valid"] = true;
			Row["name"] = std::string(TCHAR_TO_UTF8(*Actor->GetName()));
#if WITH_EDITOR
			Row["label"] = std::string(TCHAR_TO_UTF8(*Actor->GetActorLabel()));
#else
			Row["label"] = std::string(TCHAR_TO_UTF8(*Actor->GetName()));
#endif
			Row["class"] = std::string(TCHAR_TO_UTF8(*Actor->GetClass()->GetName()));

			const FAvaSceneTreeNode* Node = SceneTree.FindTreeNode(FAvaSceneItem(Actor, World));
			Row["has_tree_node"] = Node != nullptr;
			if (Node)
			{
				Row["local_index"] = Node->GetLocalIndex();
				Row["global_index"] = Node->GetGlobalIndex();
				Row["parent_index"] = Node->GetParentIndex();
				Row["child_count"] = Node->GetChildrenIndices().Num();
			}
			else
			{
				Row["local_index"] = INDEX_NONE;
				Row["global_index"] = INDEX_NONE;
				Row["parent_index"] = INDEX_NONE;
				Row["child_count"] = 0;
			}
			return Row;
		}

		sol::table BuildSceneTreeInfo(sol::state_view State, sol::optional<std::string> ActorId = sol::nullopt)
		{
			sol::table Result = State.create_table();
			UWorld* World = GetEditorWorld();
			AAvaScene* Scene = GetMotionDesignScene(false);
			Result["has_world"] = World != nullptr;
			Result["has_scene"] = Scene != nullptr;
			if (!World || !Scene)
			{
				Result["root_count"] = 0;
				Result["roots"] = State.create_table();
				return Result;
			}

			const FAvaSceneTree& SceneTree = Scene->GetSceneTree();
			const TArray<AActor*> RootActors = SceneTree.GetRootActors(Scene->GetLevel());
			sol::table Roots = State.create_table();
			int32 RootIndex = 1;
			for (AActor* RootActor : RootActors)
			{
				if (IsValid(RootActor))
				{
					Roots[RootIndex++] = BuildSceneTreeActorInfo(State, RootActor, SceneTree, World);
				}
			}
			Result["root_count"] = RootIndex - 1;
			Result["roots"] = Roots;

			if (ActorId.has_value())
			{
				AActor* Actor = NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(ActorId->c_str()));
				sol::table ActorInfo = BuildSceneTreeActorInfo(State, Actor, SceneTree, World);
				sol::table Children = State.create_table();
				int32 ChildIndex = 1;
				if (IsValid(Actor))
				{
					for (AActor* ChildActor : SceneTree.GetChildActors(Actor))
					{
						if (IsValid(ChildActor))
						{
							Children[ChildIndex++] = BuildSceneTreeActorInfo(State, ChildActor, SceneTree, World);
						}
					}
				}
				ActorInfo["children"] = Children;
				ActorInfo["child_count"] = ChildIndex - 1;
				Result["actor"] = ActorInfo;
			}
			return Result;
		}

		sol::object AddSceneTreeActor(sol::state_view State, const std::string& ActorId, sol::optional<std::string> ParentActorId)
		{
			UWorld* World = GetEditorWorld();
			AAvaScene* Scene = GetMotionDesignScene(true);
			if (!World || !Scene || ActorId.empty())
			{
				return sol::lua_nil;
			}

			AActor* Actor = NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(ActorId.c_str()));
			if (!IsValid(Actor))
			{
				return sol::lua_nil;
			}

			AActor* ParentActor = nullptr;
			if (ParentActorId.has_value() && !ParentActorId->empty())
			{
				ParentActor = NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(ParentActorId->c_str()));
				if (!IsValid(ParentActor))
				{
					return sol::lua_nil;
				}
			}

			FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddSceneTreeActor", "NeoStack: Add Motion Design Scene Tree Actor"));
			Scene->Modify();
			FAvaSceneTree& SceneTree = Scene->GetSceneTree();
			FAvaSceneItem ParentItem;
			if (ParentActor)
			{
				ParentItem = FAvaSceneItem(ParentActor, World);
			}
			SceneTree.GetOrAddTreeNode(FAvaSceneItem(Actor, World), ParentItem);
			Scene->MarkPackageDirty();

			return sol::make_object(State, BuildSceneTreeInfo(State, ActorId));
		}

		AActor* FindActorInEditorOrActiveSceneRig(const std::string& ActorId)
		{
			UWorld* World = GetEditorWorld();
			AActor* Actor = World ? NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(ActorId.c_str())) : nullptr;
			if (Actor)
			{
				return Actor;
			}

			const UAvaSceneRigSubsystem* Subsystem = UAvaSceneRigSubsystem::ForWorld(World);
			if (!Subsystem)
			{
				return nullptr;
			}

			const FString Wanted = UTF8_TO_TCHAR(ActorId.c_str());
			AActor* Found = nullptr;
			Subsystem->ForEachActiveSceneRigActor([&Found, &Wanted](AActor* const Candidate)
			{
				if (Found || !IsValid(Candidate))
				{
					return;
				}
				if (Candidate->GetName().Equals(Wanted, ESearchCase::IgnoreCase)
#if WITH_EDITOR
					|| Candidate->GetActorLabel().Equals(Wanted, ESearchCase::IgnoreCase)
#endif
					)
				{
					Found = Candidate;
				}
			});
			return Found;
		}

		sol::table BuildSceneRigActorInfo(sol::state_view State, AActor* Actor)
		{
			sol::table Row = State.create_table();
			if (!IsValid(Actor))
			{
				Row["valid"] = false;
				return Row;
			}

			UWorld* World = GetEditorWorld();
			const UAvaSceneRigSubsystem* Subsystem = UAvaSceneRigSubsystem::ForWorld(World);
			Row["valid"] = true;
			Row["name"] = std::string(TCHAR_TO_UTF8(*Actor->GetName()));
#if WITH_EDITOR
			Row["label"] = std::string(TCHAR_TO_UTF8(*Actor->GetActorLabel()));
#else
			Row["label"] = std::string(TCHAR_TO_UTF8(*Actor->GetName()));
#endif
			Row["class"] = std::string(TCHAR_TO_UTF8(*Actor->GetClass()->GetName()));
			Row["is_supported_class"] = UAvaSceneRigSubsystem::IsSupportedActorClass(Actor->GetClass());
			Row["in_active_scene_rig"] = Subsystem ? Subsystem->IsActiveSceneRigActor(Actor) : false;
			if (const ULevel* Level = Actor->GetLevel())
			{
				Row["level_name"] = std::string(TCHAR_TO_UTF8(*Level->GetOuter()->GetName()));
			}
			return Row;
		}

		sol::table BuildSceneRigInfo(sol::state_view State, sol::optional<std::string> ActorId = sol::nullopt)
		{
			sol::table Result = State.create_table();
			UWorld* World = GetEditorWorld();
			AAvaScene* Scene = GetMotionDesignScene(false);
			UAvaSceneSettings* Settings = Scene ? Scene->GetSceneSettings() : nullptr;
			UAvaSceneRigSubsystem* Subsystem = UAvaSceneRigSubsystem::ForWorld(World);

			Result["has_world"] = World != nullptr;
			Result["has_scene"] = Scene != nullptr;
			Result["has_scene_settings"] = Settings != nullptr;
			Result["has_scene_rig_subsystem"] = Subsystem != nullptr;
			Result["active_scene_rig"] = Settings ? std::string(TCHAR_TO_UTF8(*Settings->GetSceneRig().ToString())) : std::string();

			sol::table SupportedClasses = State.create_table();
			int32 SupportedIndex = 1;
			for (const TSubclassOf<AActor>& Class : UAvaSceneRigSubsystem::GetSupportedActorClasses())
			{
				if (*Class)
				{
					SupportedClasses[SupportedIndex++] = std::string(TCHAR_TO_UTF8(*Class->GetName()));
				}
			}
			Result["supported_class_count"] = SupportedIndex - 1;
			Result["supported_classes"] = SupportedClasses;

			sol::table StreamingLevels = State.create_table();
			int32 StreamingIndex = 1;
			ULevelStreaming* FirstStreamingLevel = nullptr;
			if (Subsystem)
			{
				for (ULevelStreaming* StreamingLevel : Subsystem->FindAllSceneRigs())
				{
					if (!IsValid(StreamingLevel))
					{
						continue;
					}
					if (!FirstStreamingLevel)
					{
						FirstStreamingLevel = StreamingLevel;
					}
					sol::table Row = State.create_table();
					Row["package"] = std::string(TCHAR_TO_UTF8(*StreamingLevel->GetWorldAssetPackageFName().ToString()));
					Row["world_asset"] = std::string(TCHAR_TO_UTF8(*StreamingLevel->GetWorldAsset().ToSoftObjectPath().ToString()));
					Row["loaded"] = StreamingLevel->GetLoadedLevel() != nullptr;
					Row["level_color"] = BuildLinearColor(State, StreamingLevel->LevelColor);
					StreamingLevels[StreamingIndex++] = Row;
				}
			}
			Result["streaming_level_count"] = StreamingIndex - 1;
			Result["streaming_levels"] = StreamingLevels;
			Result["has_active_streaming_level"] = FirstStreamingLevel != nullptr;

			sol::table Actors = State.create_table();
			int32 ActorIndex = 1;
			if (Subsystem)
			{
				Subsystem->ForEachActiveSceneRigActor([&](AActor* const Actor)
				{
					Actors[ActorIndex++] = BuildSceneRigActorInfo(State, Actor);
				});
			}
			Result["actor_count"] = ActorIndex - 1;
			Result["actors"] = Actors;

			if (ActorId.has_value())
			{
				Result["actor"] = BuildSceneRigActorInfo(State, FindActorInEditorOrActiveSceneRig(*ActorId));
			}
			return Result;
		}

		const TCHAR* ShapeOverlayTypeToString(EAvaShapeEditorOverlayType Type)
		{
			switch (Type)
			{
			case EAvaShapeEditorOverlayType::FullDetails:
				return TEXT("FullDetails");
			case EAvaShapeEditorOverlayType::ComponentVisualizerOnly:
			default:
				return TEXT("ComponentVisualizerOnly");
			}
		}

		EAvaShapeEditorOverlayType ParseShapeOverlayType(const std::string& Value, EAvaShapeEditorOverlayType DefaultValue)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("FullDetails"), ESearchCase::IgnoreCase))
			{
				return EAvaShapeEditorOverlayType::FullDetails;
			}
			if (Text.Equals(TEXT("ComponentVisualizerOnly"), ESearchCase::IgnoreCase))
			{
				return EAvaShapeEditorOverlayType::ComponentVisualizerOnly;
			}
			return DefaultValue;
		}

		UObject* GetOutlinerSettings()
		{
			UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/AvalancheOutliner.AvaOutlinerSettings"));
			if (!SettingsClass)
			{
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (It->GetName() == TEXT("AvaOutlinerSettings"))
					{
						SettingsClass = *It;
						break;
					}
				}
			}
			return SettingsClass ? SettingsClass->GetDefaultObject() : nullptr;
		}

		int64 ParseOutlinerFilterMode(const std::string& Value)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("ContainerOfType"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("Container"), ESearchCase::IgnoreCase))
			{
				return 2;
			}
			if (Text.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				return 0;
			}
			return 1;
		}

		sol::table BuildOutlinerFilterInfo(sol::state_view State, const UScriptStruct* Struct, const void* ValuePtr)
		{
			sol::table Row = State.create_table();
			if (!Struct || !ValuePtr)
			{
				return Row;
			}

			if (const FArrayProperty* ClassesProperty = FindFProperty<FArrayProperty>(Struct, TEXT("FilterClasses")))
			{
				sol::table Classes = State.create_table();
				int32 ClassIndex = 1;
				FScriptArrayHelper Helper(ClassesProperty, ClassesProperty->ContainerPtrToValuePtr<void>(ValuePtr));
				if (const FClassProperty* ClassProperty = CastField<FClassProperty>(ClassesProperty->Inner))
				{
					for (int32 Index = 0; Index < Helper.Num(); ++Index)
					{
						if (UClass* ClassValue = Cast<UClass>(ClassProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index))))
						{
							Classes[ClassIndex++] = std::string(TCHAR_TO_UTF8(*ClassValue->GetName()));
						}
					}
				}
				Row["class_count"] = ClassIndex - 1;
				Row["classes"] = Classes;
			}

			if (const FEnumProperty* ModeProperty = FindFProperty<FEnumProperty>(Struct, TEXT("FilterMode")))
			{
				const int64 Value = ModeProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ModeProperty->ContainerPtrToValuePtr<void>(ValuePtr));
				Row["mode_value"] = static_cast<int32>(Value);
				Row["mode"] = std::string(TCHAR_TO_UTF8(*ModeProperty->GetEnum()->GetAuthoredNameStringByValue(Value)));
			}

			if (const FTextProperty* TextProperty = FindFProperty<FTextProperty>(Struct, TEXT("FilterText")))
			{
				Row["filter_text"] = std::string(TCHAR_TO_UTF8(*TextProperty->GetPropertyValue(TextProperty->ContainerPtrToValuePtr<void>(ValuePtr)).ToString()));
			}
			if (const FTextProperty* TooltipProperty = FindFProperty<FTextProperty>(Struct, TEXT("TooltipText")))
			{
				Row["tooltip_text"] = std::string(TCHAR_TO_UTF8(*TooltipProperty->GetPropertyValue(TooltipProperty->ContainerPtrToValuePtr<void>(ValuePtr)).ToString()));
			}
			if (const FBoolProperty* UseIconProperty = FindFProperty<FBoolProperty>(Struct, TEXT("bUseOverrideIcon")))
			{
				Row["use_override_icon"] = UseIconProperty->GetPropertyValue(UseIconProperty->ContainerPtrToValuePtr<void>(ValuePtr));
			}
			return Row;
		}

		sol::object BuildOutlinerSettingsInfo(sol::state_view State)
		{
			UObject* Settings = GetOutlinerSettings();
			if (!Settings)
			{
				return sol::lua_nil;
			}

			sol::table Result = State.create_table();
			Result["class"] = std::string(TCHAR_TO_UTF8(*Settings->GetClass()->GetName()));

			auto ReadBool = [Settings](const TCHAR* Name) -> bool
			{
				if (const FBoolProperty* Property = FindFProperty<FBoolProperty>(Settings->GetClass(), Name))
				{
					return Property->GetPropertyValue_InContainer(Settings);
				}
				return false;
			};
			auto ReadInt = [Settings](const TCHAR* Name) -> int32
			{
				if (const FIntProperty* Property = FindFProperty<FIntProperty>(Settings->GetClass(), Name))
				{
					return Property->GetPropertyValue_InContainer(Settings);
				}
				return 0;
			};

			Result["always_show_visibility_state"] = ReadBool(TEXT("bAlwaysShowVisibilityState"));
			Result["always_show_lock_state"] = ReadBool(TEXT("bAlwaysShowLockState"));
			Result["use_muted_hierarchy"] = ReadBool(TEXT("bUseMutedHierarchy"));
			Result["auto_expand_to_selection"] = ReadBool(TEXT("bAutoExpandToSelection"));
			Result["item_default_view_mode"] = ReadInt(TEXT("ItemDefaultViewMode"));
			Result["item_proxy_view_mode"] = ReadInt(TEXT("ItemProxyViewMode"));

			sol::table Colors = State.create_table();
			int32 ColorIndex = 1;
			if (const FMapProperty* MapProperty = FindFProperty<FMapProperty>(Settings->GetClass(), TEXT("ItemColorMap")))
			{
				FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Settings));
				if (const FNameProperty* KeyProperty = CastField<FNameProperty>(MapProperty->KeyProp))
				{
					for (FScriptMapHelper::FIterator It(Helper); It; ++It)
					{
						sol::table Row = State.create_table();
						Row["key"] = std::string(TCHAR_TO_UTF8(*KeyProperty->GetPropertyValue(Helper.GetKeyPtr(It)).ToString()));
						if (const FStructProperty* ColorProperty = CastField<FStructProperty>(MapProperty->ValueProp))
						{
							if (ColorProperty->Struct == TBaseStructure<FLinearColor>::Get())
							{
								Row["color"] = BuildLinearColor(State, *reinterpret_cast<const FLinearColor*>(Helper.GetValuePtr(It)));
							}
						}
						Colors[ColorIndex++] = Row;
					}
				}
			}
			Result["color_count"] = ColorIndex - 1;
			Result["colors"] = Colors;

			sol::table Filters = State.create_table();
			int32 FilterIndex = 1;
			if (const FMapProperty* MapProperty = FindFProperty<FMapProperty>(Settings->GetClass(), TEXT("CustomItemTypeFilters")))
			{
				FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Settings));
				if (const FNameProperty* KeyProperty = CastField<FNameProperty>(MapProperty->KeyProp))
				{
					const FStructProperty* StructProperty = CastField<FStructProperty>(MapProperty->ValueProp);
					for (FScriptMapHelper::FIterator It(Helper); It; ++It)
					{
						sol::table Row = StructProperty ? BuildOutlinerFilterInfo(State, StructProperty->Struct, Helper.GetValuePtr(It)) : State.create_table();
						Row["key"] = std::string(TCHAR_TO_UTF8(*KeyProperty->GetPropertyValue(Helper.GetKeyPtr(It)).ToString()));
						Filters[FilterIndex++] = Row;
					}
				}
			}
			Result["custom_filter_count"] = FilterIndex - 1;
			Result["custom_filters"] = Filters;
			return sol::make_object(State, Result);
		}

		void SetOutlinerBool(UObject* Settings, const TCHAR* Name, bool bValue)
		{
			if (FBoolProperty* Property = FindFProperty<FBoolProperty>(Settings->GetClass(), Name))
			{
				Property->SetPropertyValue_InContainer(Settings, bValue);
			}
		}

		void SetOutlinerInt(UObject* Settings, const TCHAR* Name, int32 Value)
		{
			if (FIntProperty* Property = FindFProperty<FIntProperty>(Settings->GetClass(), Name))
			{
				Property->SetPropertyValue_InContainer(Settings, Value);
			}
		}

		void ApplyOutlinerColorMap(UObject* Settings, sol::table Rows)
		{
			FMapProperty* MapProperty = FindFProperty<FMapProperty>(Settings->GetClass(), TEXT("ItemColorMap"));
			if (!MapProperty)
			{
				return;
			}
			FNameProperty* KeyProperty = CastField<FNameProperty>(MapProperty->KeyProp);
			FStructProperty* ValueProperty = CastField<FStructProperty>(MapProperty->ValueProp);
			if (!KeyProperty || !ValueProperty || ValueProperty->Struct != TBaseStructure<FLinearColor>::Get())
			{
				return;
			}

			FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Settings));
			Helper.EmptyValues();
			for (int32 LuaIndex = 1;; ++LuaIndex)
			{
				sol::object EntryObj = Rows[LuaIndex];
				if (!EntryObj.valid() || EntryObj.get_type() == sol::type::lua_nil)
				{
					break;
				}
				if (!EntryObj.is<sol::table>())
				{
					continue;
				}
				sol::table Entry = EntryObj.as<sol::table>();
				const std::string Key = Entry.get_or<std::string>("key", "");
				if (Key.empty())
				{
					continue;
				}
				const int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
				KeyProperty->SetPropertyValue(Helper.GetKeyPtr(NewIndex), FName(UTF8_TO_TCHAR(Key.c_str())));
				FLinearColor Color = FLinearColor::White;
				if (sol::optional<sol::table> ColorTable = Entry.get<sol::optional<sol::table>>("color"))
				{
					Color = TableToLinearColor(*ColorTable, FLinearColor::White);
				}
				Color.A = 1.0f;
				ValueProperty->CopyCompleteValue(Helper.GetValuePtr(NewIndex), &Color);
			}
			Helper.Rehash();
		}

		void ApplyOutlinerFilterMap(UObject* Settings, sol::table Rows)
		{
			FMapProperty* MapProperty = FindFProperty<FMapProperty>(Settings->GetClass(), TEXT("CustomItemTypeFilters"));
			if (!MapProperty)
			{
				return;
			}
			FNameProperty* KeyProperty = CastField<FNameProperty>(MapProperty->KeyProp);
			FStructProperty* ValueProperty = CastField<FStructProperty>(MapProperty->ValueProp);
			if (!KeyProperty || !ValueProperty || !ValueProperty->Struct)
			{
				return;
			}

			FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Settings));
			Helper.EmptyValues();
			for (int32 LuaIndex = 1;; ++LuaIndex)
			{
				sol::object EntryObj = Rows[LuaIndex];
				if (!EntryObj.valid() || EntryObj.get_type() == sol::type::lua_nil)
				{
					break;
				}
				if (!EntryObj.is<sol::table>())
				{
					continue;
				}
				sol::table Entry = EntryObj.as<sol::table>();
				const std::string Key = Entry.get_or<std::string>("key", "");
				if (Key.empty())
				{
					continue;
				}

				const int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
				KeyProperty->SetPropertyValue(Helper.GetKeyPtr(NewIndex), FName(UTF8_TO_TCHAR(Key.c_str())));
				void* ValuePtr = Helper.GetValuePtr(NewIndex);
				ValueProperty->InitializeValue(ValuePtr);

				if (FArrayProperty* ClassesProperty = FindFProperty<FArrayProperty>(ValueProperty->Struct, TEXT("FilterClasses")))
				{
					if (FClassProperty* ClassProperty = CastField<FClassProperty>(ClassesProperty->Inner))
					{
						FScriptArrayHelper ClassesHelper(ClassesProperty, ClassesProperty->ContainerPtrToValuePtr<void>(ValuePtr));
						ClassesHelper.EmptyValues();
						if (sol::optional<sol::table> Classes = Entry.get<sol::optional<sol::table>>("classes"))
						{
							for (int32 ClassLuaIndex = 1;; ++ClassLuaIndex)
							{
								sol::object ClassObj = (*Classes)[ClassLuaIndex];
								if (!ClassObj.valid() || ClassObj.get_type() == sol::type::lua_nil)
								{
									break;
								}
								const FString ClassPath = UTF8_TO_TCHAR(ClassObj.as<std::string>().c_str());
								UClass* Class = FindObject<UClass>(nullptr, *ClassPath);
								if (!Class)
								{
									Class = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
								}
								if (!Class)
								{
									for (TObjectIterator<UClass> It; It; ++It)
									{
										if (It->GetName() == ClassPath)
										{
											Class = *It;
											break;
										}
									}
								}
								if (Class)
								{
									const int32 ClassIndex = ClassesHelper.AddValue();
									ClassProperty->SetObjectPropertyValue(ClassesHelper.GetRawPtr(ClassIndex), Class);
								}
							}
						}
					}
				}
				if (FEnumProperty* ModeProperty = FindFProperty<FEnumProperty>(ValueProperty->Struct, TEXT("FilterMode")))
				{
					const std::string Mode = Entry.get_or<std::string>("mode", "MatchesType");
					ModeProperty->GetUnderlyingProperty()->SetIntPropertyValue(ModeProperty->ContainerPtrToValuePtr<void>(ValuePtr), ParseOutlinerFilterMode(Mode));
				}
				if (FTextProperty* TextProperty = FindFProperty<FTextProperty>(ValueProperty->Struct, TEXT("FilterText")))
				{
					const std::string Text = Entry.get_or<std::string>("filter_text", "");
					TextProperty->SetPropertyValue(TextProperty->ContainerPtrToValuePtr<void>(ValuePtr), FText::FromString(UTF8_TO_TCHAR(Text.c_str())));
				}
				if (FTextProperty* TooltipProperty = FindFProperty<FTextProperty>(ValueProperty->Struct, TEXT("TooltipText")))
				{
					const std::string Text = Entry.get_or<std::string>("tooltip_text", "");
					TooltipProperty->SetPropertyValue(TooltipProperty->ContainerPtrToValuePtr<void>(ValuePtr), FText::FromString(UTF8_TO_TCHAR(Text.c_str())));
				}
				if (FBoolProperty* UseIconProperty = FindFProperty<FBoolProperty>(ValueProperty->Struct, TEXT("bUseOverrideIcon")))
				{
					UseIconProperty->SetPropertyValue(UseIconProperty->ContainerPtrToValuePtr<void>(ValuePtr), Entry.get_or("use_override_icon", false));
				}
			}
			Helper.Rehash();
		}

		const TCHAR* MaskWriteOperationToString(EGeometryMaskCompositeOperation Operation)
		{
			switch (Operation)
			{
			case EGeometryMaskCompositeOperation::Subtract:
				return TEXT("Subtract");
			case EGeometryMaskCompositeOperation::Intersect:
				return TEXT("Intersect");
			case EGeometryMaskCompositeOperation::Add:
			default:
				return TEXT("Add");
			}
		}

		EGeometryMaskCompositeOperation ParseMaskWriteOperation(const sol::object& Value, EGeometryMaskCompositeOperation DefaultValue)
		{
			if (Value.is<int32>())
			{
				const int32 Raw = Value.as<int32>();
				if (Raw == static_cast<int32>(EGeometryMaskCompositeOperation::Subtract))
				{
					return EGeometryMaskCompositeOperation::Subtract;
				}
				if (Raw == static_cast<int32>(EGeometryMaskCompositeOperation::Intersect))
				{
					return EGeometryMaskCompositeOperation::Intersect;
				}
				return EGeometryMaskCompositeOperation::Add;
			}
			if (!Value.is<std::string>())
			{
				return DefaultValue;
			}

			const FString Text = UTF8_TO_TCHAR(Value.as<std::string>().c_str());
			if (Text.Equals(TEXT("Subtract"), ESearchCase::IgnoreCase))
			{
				return EGeometryMaskCompositeOperation::Subtract;
			}
			if (Text.Equals(TEXT("Intersect"), ESearchCase::IgnoreCase))
			{
				return EGeometryMaskCompositeOperation::Intersect;
			}
			if (Text.Equals(TEXT("Add"), ESearchCase::IgnoreCase))
			{
				return EGeometryMaskCompositeOperation::Add;
			}
			return DefaultValue;
		}

		UAvaMask2DBaseModifier* FindMaskModifier(const std::string& ActorId, int32 OneBasedIndex)
		{
			UWorld* World = GetEditorWorld();
			AActor* Actor = World ? NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(ActorId.c_str())) : nullptr;
			UActorModifierCoreStack* Stack = nullptr;
			if (!Actor || !UActorModifierCoreLibrary::FindModifierStack(Actor, Stack, false) || !Stack)
			{
				return nullptr;
			}

			TArray<UActorModifierCoreBase*> Modifiers;
			if (!UActorModifierCoreLibrary::GetStackModifiers(Stack, Modifiers))
			{
				return nullptr;
			}
			const int32 ZeroBasedIndex = OneBasedIndex - 1;
			return Modifiers.IsValidIndex(ZeroBasedIndex) ? Cast<UAvaMask2DBaseModifier>(Modifiers[ZeroBasedIndex]) : nullptr;
		}

		const TCHAR* GeometryMaskColorChannelToString(EGeometryMaskColorChannel Channel)
		{
			switch (Channel)
			{
			case EGeometryMaskColorChannel::Red:
				return TEXT("Red");
			case EGeometryMaskColorChannel::Green:
				return TEXT("Green");
			case EGeometryMaskColorChannel::Blue:
				return TEXT("Blue");
			case EGeometryMaskColorChannel::Alpha:
				return TEXT("Alpha");
			case EGeometryMaskColorChannel::None:
				return TEXT("None");
			case EGeometryMaskColorChannel::Num:
				return TEXT("Num");
			default:
				return TEXT("Unknown");
			}
		}

		sol::table BuildGeometryMaskComponentInfo(sol::state_view State, const UAvaMask2DBaseModifier* Modifier)
		{
			sol::table ComponentsTable = State.create_table();
			if (!Modifier)
			{
				return ComponentsTable;
			}

			const AActor* Actor = Modifier->GetModifiedActor();
			if (!Actor)
			{
				return ComponentsTable;
			}

			int32 Index = 1;
			TArray<UGeometryMaskReadComponent*> ReadComponents;
			Actor->GetComponents<UGeometryMaskReadComponent>(ReadComponents);
			for (const UGeometryMaskReadComponent* ReadComponent : ReadComponents)
			{
				if (!ReadComponent)
				{
					continue;
				}

				const FGeometryMaskReadParameters& Parameters = ReadComponent->GetParameters();
				sol::table Row = State.create_table();
				Row["name"] = std::string(TCHAR_TO_UTF8(*ReadComponent->GetName()));
				Row["class"] = std::string(TCHAR_TO_UTF8(*ReadComponent->GetClass()->GetName()));
				Row["mode"] = std::string("Read");
				Row["canvas_name"] = std::string(TCHAR_TO_UTF8(*Parameters.CanvasName.ToString()));
				Row["color_channel"] = std::string(TCHAR_TO_UTF8(GeometryMaskColorChannelToString(Parameters.ColorChannel)));
				Row["invert"] = Parameters.bInvert;
				ComponentsTable[Index++] = Row;
			}

			TArray<UGeometryMaskWriteMeshComponent*> WriteComponents;
			Actor->GetComponents<UGeometryMaskWriteMeshComponent>(WriteComponents);
			for (const UGeometryMaskWriteMeshComponent* WriteComponent : WriteComponents)
			{
				if (!WriteComponent)
				{
					continue;
				}

				const FGeometryMaskWriteParameters& Parameters = WriteComponent->GetParameters();
				sol::table Row = State.create_table();
				Row["name"] = std::string(TCHAR_TO_UTF8(*WriteComponent->GetName()));
				Row["class"] = std::string(TCHAR_TO_UTF8(*WriteComponent->GetClass()->GetName()));
				Row["mode"] = std::string("Write");
				Row["canvas_name"] = std::string(TCHAR_TO_UTF8(*Parameters.CanvasName.ToString()));
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
				Row["color_channel"] = std::string(TCHAR_TO_UTF8(GeometryMaskColorChannelToString(Parameters.ColorChannel)));
#else
				Row["color_channel"] = std::string("Deprecated");
#endif
				Row["operation"] = std::string(TCHAR_TO_UTF8(MaskWriteOperationToString(Parameters.OperationType)));
				Row["invert"] = Parameters.bInvert;
				Row["priority"] = Parameters.Priority;
				Row["outer_radius"] = Parameters.OuterRadius;
				Row["inner_radius"] = Parameters.InnerRadius;
				ComponentsTable[Index++] = Row;
			}

			return ComponentsTable;
		}

		sol::table BuildGeometryMaskCanvasInfo(sol::state_view State, const UAvaMask2DBaseModifier* Modifier)
		{
			sol::table Result = State.create_table();
			Result["valid"] = false;
			if (!Modifier)
			{
				return Result;
			}

			const AActor* Actor = Modifier->GetModifiedActor();
			const ULevel* Level = Actor ? Actor->GetLevel() : nullptr;
			UWorld* World = Actor ? Actor->GetWorld() : nullptr;
			UGeometryMaskWorldSubsystem* Subsystem = World ? World->GetSubsystem<UGeometryMaskWorldSubsystem>() : nullptr;
			UGeometryMaskCanvas* Canvas = (Subsystem && Level) ? Subsystem->GetNamedCanvas(Level, Modifier->GetChannel()) : nullptr;
			if (!Canvas)
			{
				return Result;
			}

			const FGeometryMaskCanvasId& CanvasId = Canvas->GetCanvasId();
			Result["valid"] = true;
			Result["name"] = std::string(TCHAR_TO_UTF8(*CanvasId.Name.ToString()));
			Result["id"] = std::string(TCHAR_TO_UTF8(*CanvasId.ToString()));
			Result["scene_view_index"] = static_cast<int32>(CanvasId.SceneViewIndex);
			Result["is_default"] = Canvas->IsDefaultCanvas();
			Result["writer_count"] = Canvas->GetNumWriters();
			Result["color_channel"] = std::string(TCHAR_TO_UTF8(GeometryMaskColorChannelToString(Canvas->GetColorChannel())));
			Result["apply_blur"] = Canvas->IsBlurApplied();
			Result["blur_strength"] = Canvas->GetBlurStrength();
			Result["apply_feather"] = Canvas->IsFeatherApplied();
			Result["outer_feather_radius"] = Canvas->GetOuterFeatherRadius();
			Result["inner_feather_radius"] = Canvas->GetInnerFeatherRadius();

			if (UCanvasRenderTarget2D* Texture = Canvas->GetTexture())
			{
				Result["has_texture"] = true;
				Result["texture_class"] = std::string(TCHAR_TO_UTF8(*Texture->GetClass()->GetName()));
				Result["texture_name"] = std::string(TCHAR_TO_UTF8(*Texture->GetName()));
				Result["texture_size_x"] = Texture->SizeX;
				Result["texture_size_y"] = Texture->SizeY;
			}
			else
			{
				Result["has_texture"] = false;
				Result["texture_class"] = std::string();
				Result["texture_name"] = std::string();
				Result["texture_size_x"] = 0;
				Result["texture_size_y"] = 0;
			}
			return Result;
		}

		sol::table BuildMaskModifierInfo(sol::state_view State, UAvaMask2DBaseModifier* Modifier)
		{
			sol::table Result = State.create_table();
			Result["valid"] = Modifier != nullptr;
			if (!Modifier)
			{
				return Result;
			}

			Result["class"] = std::string(TCHAR_TO_UTF8(*Modifier->GetClass()->GetName()));
			Result["channel"] = std::string(TCHAR_TO_UTF8(*Modifier->GetChannel().ToString()));
			Result["use_parent_channel"] = Modifier->UseParentChannel();
			Result["inverted"] = Modifier->IsInverted();
			Result["use_blur"] = Modifier->UseBlur();
			Result["blur_strength"] = Modifier->GetBlurStrength();
			Result["use_feathering"] = Modifier->UseFeathering();
			Result["outer_feather_radius"] = Modifier->GetOuterFeatherRadius();
			Result["inner_feather_radius"] = Modifier->GetInnerFeatherRadius();
			sol::table GeometryMaskComponents = BuildGeometryMaskComponentInfo(State, Modifier);
			Result["geometry_mask_components"] = GeometryMaskComponents;
			Result["geometry_mask_component_count"] = GeometryMaskComponents.size();
			Result["canvas"] = BuildGeometryMaskCanvasInfo(State, Modifier);

			if (const UAvaMask2DReadModifier* ReadModifier = Cast<UAvaMask2DReadModifier>(Modifier))
			{
				Result["mode"] = std::string("Read");
				Result["base_opacity"] = ReadModifier->GetBaseOpacity();
			}
			else if (const UAvaMask2DWriteModifier* WriteModifier = Cast<UAvaMask2DWriteModifier>(Modifier))
			{
				Result["mode"] = std::string("Write");
				Result["write_operation"] = std::string(TCHAR_TO_UTF8(MaskWriteOperationToString(WriteModifier->GetWriteOperation())));
			}
			return Result;
		}

		void ApplyMaskModifierOptions(UAvaMask2DBaseModifier* Modifier, sol::table Options)
		{
			if (!Modifier)
			{
				return;
			}
			if (sol::optional<bool> Value = Options.get<sol::optional<bool>>("use_parent_channel"))
			{
				Modifier->SetUseParentChannel(*Value);
			}
			if (sol::optional<std::string> Value = Options.get<sol::optional<std::string>>("channel"))
			{
				Modifier->SetChannel(FName(UTF8_TO_TCHAR(Value->c_str())));
			}
			if (sol::optional<bool> Value = Options.get<sol::optional<bool>>("inverted"))
			{
				Modifier->SetIsInverted(*Value);
			}
			if (sol::optional<bool> Value = Options.get<sol::optional<bool>>("use_blur"))
			{
				Modifier->UseBlur(*Value);
			}
			if (sol::optional<double> Value = Options.get<sol::optional<double>>("blur_strength"))
			{
				Modifier->SetBlurStrength(static_cast<float>(*Value));
			}
			if (sol::optional<bool> Value = Options.get<sol::optional<bool>>("use_feathering"))
			{
				Modifier->UseFeathering(*Value);
			}
			if (sol::optional<int32> Value = Options.get<sol::optional<int32>>("outer_feather_radius"))
			{
				Modifier->SetOuterFeatherRadius(*Value);
			}
			if (sol::optional<int32> Value = Options.get<sol::optional<int32>>("inner_feather_radius"))
			{
				Modifier->SetInnerFeatherRadius(*Value);
			}
			if (UAvaMask2DReadModifier* ReadModifier = Cast<UAvaMask2DReadModifier>(Modifier))
			{
				if (sol::optional<double> Value = Options.get<sol::optional<double>>("base_opacity"))
				{
					ReadModifier->SetBaseOpacity(static_cast<float>(*Value));
				}
			}
			if (UAvaMask2DWriteModifier* WriteModifier = Cast<UAvaMask2DWriteModifier>(Modifier))
			{
				sol::object Value = Options["write_operation"];
				if (Value.valid() && Value.get_type() != sol::type::lua_nil)
				{
					WriteModifier->SetWriteOperation(ParseMaskWriteOperation(Value, WriteModifier->GetWriteOperation()));
				}
			}
			Modifier->MarkModifierDirty();
		}

		UObject* GetComponentVisualizerSettingsObject()
		{
			IAvaComponentVisualizersSettings* SettingsInterface = IAvalancheComponentVisualizersModule::Get().GetSettings();
			UObject* SettingsObject = nullptr;
			if (UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/AvalancheComponentVisualizers.AvaComponentVisualizersSettings")))
			{
				SettingsObject = SettingsClass->GetDefaultObject();
			}
			if (!SettingsObject)
			{
				return nullptr;
			}
			return SettingsInterface ? SettingsObject : nullptr;
		}

		sol::table BuildComponentVisualizerSettingsInfo(sol::state_view State)
		{
			sol::table Result = State.create_table();
			IAvaComponentVisualizersSettings* SettingsInterface = IAvalancheComponentVisualizersModule::Get().GetSettings();
			UObject* SettingsObject = GetComponentVisualizerSettingsObject();
			if (!SettingsInterface || !SettingsObject)
			{
				return Result;
			}

			Result["sprite_size"] = SettingsInterface->GetSpriteSize();
			sol::table Sprites = State.create_table();
			int32 SpriteIndex = 1;
			if (const FMapProperty* MapProperty = FindFProperty<FMapProperty>(SettingsObject->GetClass(), TEXT("VisualizerSprites")))
			{
				const FNameProperty* KeyProperty = CastField<FNameProperty>(MapProperty->KeyProp);
				const FSoftObjectProperty* ValueProperty = CastField<FSoftObjectProperty>(MapProperty->ValueProp);
				FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(SettingsObject));
				if (KeyProperty && ValueProperty)
				{
					for (FScriptMapHelper::FIterator It(Helper); It; ++It)
					{
						sol::table Row = State.create_table();
						const FName Key = KeyProperty->GetPropertyValue(Helper.GetKeyPtr(It));
						const FSoftObjectPtr Value = ValueProperty->GetPropertyValue(Helper.GetValuePtr(It));
						Row["name"] = std::string(TCHAR_TO_UTF8(*Key.ToString()));
						Row["texture"] = std::string(TCHAR_TO_UTF8(*Value.ToSoftObjectPath().ToString()));
						Sprites[SpriteIndex++] = Row;
					}
				}
			}
			Result["sprite_count"] = SpriteIndex - 1;
			Result["sprites"] = Sprites;
			return Result;
		}

		bool RemoveComponentVisualizerSprite(UObject* SettingsObject, FName Name)
		{
			if (!SettingsObject || Name.IsNone())
			{
				return false;
			}
			FMapProperty* MapProperty = FindFProperty<FMapProperty>(SettingsObject->GetClass(), TEXT("VisualizerSprites"));
			if (!MapProperty)
			{
				return false;
			}
			FNameProperty* KeyProperty = CastField<FNameProperty>(MapProperty->KeyProp);
			if (!KeyProperty)
			{
				return false;
			}

			FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(SettingsObject));
			for (FScriptMapHelper::FIterator It(Helper); It; ++It)
			{
				if (KeyProperty->GetPropertyValue(Helper.GetKeyPtr(It)) == Name)
				{
					Helper.RemoveAt(It.GetInternalIndex());
					Helper.Rehash();
					return true;
				}
			}
			return false;
		}

		const TCHAR* SnapStateToString(EAvaViewportSnapState State)
		{
			switch (State)
			{
			case EAvaViewportSnapState::Global:
				return TEXT("Global");
			case EAvaViewportSnapState::Screen:
				return TEXT("Screen");
			case EAvaViewportSnapState::Grid:
				return TEXT("Grid");
			case EAvaViewportSnapState::Actor:
				return TEXT("Actor");
			case EAvaViewportSnapState::All:
				return TEXT("All");
			case EAvaViewportSnapState::Off:
			default:
				return TEXT("Off");
			}
		}

		int32 ParseSnapStateObject(const sol::object& Value, int32 DefaultValue)
		{
			if (Value.is<int32>())
			{
				return Value.as<int32>();
			}
			if (!Value.is<std::string>())
			{
				return DefaultValue;
			}

			const FString Text = UTF8_TO_TCHAR(Value.as<std::string>().c_str());
			if (Text.Equals(TEXT("Off"), ESearchCase::IgnoreCase))
			{
				return static_cast<int32>(EAvaViewportSnapState::Off);
			}
			if (Text.Equals(TEXT("Global"), ESearchCase::IgnoreCase))
			{
				return static_cast<int32>(EAvaViewportSnapState::Global);
			}
			if (Text.Equals(TEXT("Screen"), ESearchCase::IgnoreCase))
			{
				return static_cast<int32>(EAvaViewportSnapState::Screen);
			}
			if (Text.Equals(TEXT("Grid"), ESearchCase::IgnoreCase))
			{
				return static_cast<int32>(EAvaViewportSnapState::Grid);
			}
			if (Text.Equals(TEXT("Actor"), ESearchCase::IgnoreCase))
			{
				return static_cast<int32>(EAvaViewportSnapState::Actor);
			}
			if (Text.Equals(TEXT("All"), ESearchCase::IgnoreCase))
			{
				return static_cast<int32>(EAvaViewportSnapState::All);
			}
			return DefaultValue;
		}

		const TCHAR* OrientationToString(EOrientation Orientation)
		{
			return Orientation == EOrientation::Orient_Vertical ? TEXT("Vertical") : TEXT("Horizontal");
		}

		EOrientation ParseOrientation(const std::string& Value, EOrientation DefaultValue)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("Vertical"), ESearchCase::IgnoreCase))
			{
				return EOrientation::Orient_Vertical;
			}
			if (Text.Equals(TEXT("Horizontal"), ESearchCase::IgnoreCase))
			{
				return EOrientation::Orient_Horizontal;
			}
			return DefaultValue;
		}

		const TCHAR* GuideStateToString(EAvaViewportGuideState State)
		{
			switch (State)
			{
			case EAvaViewportGuideState::Enabled:
				return TEXT("Enabled");
			case EAvaViewportGuideState::SnappedTo:
				return TEXT("SnappedTo");
			case EAvaViewportGuideState::Disabled:
			default:
				return TEXT("Disabled");
			}
		}

		EAvaViewportGuideState ParseGuideState(const std::string& Value, EAvaViewportGuideState DefaultValue)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("Enabled"), ESearchCase::IgnoreCase))
			{
				return EAvaViewportGuideState::Enabled;
			}
			if (Text.Equals(TEXT("SnappedTo"), ESearchCase::IgnoreCase))
			{
				return EAvaViewportGuideState::SnappedTo;
			}
			if (Text.Equals(TEXT("Disabled"), ESearchCase::IgnoreCase))
			{
				return EAvaViewportGuideState::Disabled;
			}
			return DefaultValue;
		}

		sol::table BuildSafeFrameInfo(sol::state_view State, const FAvaLevelViewportSafeFrame& SafeFrame)
		{
			sol::table Result = State.create_table();
			Result["screen_percentage"] = SafeFrame.ScreenPercentage;
			Result["color"] = BuildLinearColor(State, SafeFrame.Color);
			Result["thickness"] = SafeFrame.Thickness;
			return Result;
		}

		sol::table BuildGuideInfo(sol::state_view State, const FAvaViewportGuideInfo& Guide)
		{
			sol::table Result = State.create_table();
			Result["orientation"] = std::string(TCHAR_TO_UTF8(OrientationToString(Guide.Orientation)));
			Result["offset_fraction"] = Guide.OffsetFraction;
			Result["state"] = std::string(TCHAR_TO_UTF8(GuideStateToString(Guide.State)));
			Result["locked"] = Guide.bLocked;
			Result["enabled"] = Guide.IsEnabled();
			return Result;
		}

		sol::table BuildViewportSettingsInfo(sol::state_view State)
		{
			const UAvaViewportSettings* Settings = GetDefault<UAvaViewportSettings>();
			sol::table Result = State.create_table();
			if (!Settings)
			{
				return Result;
			}

			Result["enable_viewport_overlay"] = Settings->bEnableViewportOverlay;
			Result["enable_bounding_boxes"] = Settings->bEnableBoundingBoxes;
			Result["background_material"] = std::string(TCHAR_TO_UTF8(*Settings->ViewportBackgroundMaterial.ToSoftObjectPath().ToString()));
			Result["checkerboard_material"] = std::string(TCHAR_TO_UTF8(*Settings->ViewportCheckerboardMaterial.ToSoftObjectPath().ToString()));
			Result["checkerboard_color0"] = BuildLinearColor(State, Settings->ViewportCheckerboardColor0);
			Result["checkerboard_color1"] = BuildLinearColor(State, Settings->ViewportCheckerboardColor1);
			Result["checkerboard_size"] = Settings->ViewportCheckerboardSize;
			Result["enable_shapes_editor_overlay"] = Settings->bEnableShapesEditorOverlay;
			Result["shape_editor_overlay_type"] = std::string(TCHAR_TO_UTF8(ShapeOverlayTypeToString(Settings->ShapeEditorOverlayType)));
			Result["grid_enabled"] = Settings->bGridEnabled;
			Result["grid_always_visible"] = Settings->bGridAlwaysVisible;
			Result["grid_size"] = Settings->GridSize;
			Result["grid_color"] = BuildLinearColor(State, Settings->GridColor);
			Result["grid_thickness"] = Settings->GridThickness;
			Result["pixel_grid_enabled"] = Settings->bPixelGridEnabled;
			Result["pixel_grid_color"] = BuildLinearColor(State, Settings->PixelGridColor);
			Result["snap_state"] = Settings->SnapState;
			Result["snap_state_name"] = std::string(TCHAR_TO_UTF8(SnapStateToString(Settings->GetSnapState())));
			Result["snap_indicators_enabled"] = Settings->bSnapIndicatorsEnabled;
			Result["snap_indicator_color"] = BuildLinearColor(State, Settings->SnapIndicatorColor);
			Result["snap_indicator_thickness"] = Settings->SnapIndicatorThickness;
			Result["guides_enabled"] = Settings->bGuidesEnabled;
			Result["guide_config_path"] = std::string(TCHAR_TO_UTF8(*Settings->GuideConfigPath));
			Result["enabled_guide_color"] = BuildLinearColor(State, Settings->EnabledGuideColor);
			Result["enabled_locked_guide_color"] = BuildLinearColor(State, Settings->EnabledLockedGuideColor);
			Result["disabled_guide_color"] = BuildLinearColor(State, Settings->DisabledGuideColor);
			Result["disabled_locked_guide_color"] = BuildLinearColor(State, Settings->DisabledLockedGuideColor);
			Result["dragged_guide_color"] = BuildLinearColor(State, Settings->DraggedGuideColor);
			Result["guide_thickness"] = Settings->GuideThickness;
			Result["safe_frames_enabled"] = Settings->bSafeFramesEnabled;
			sol::table SafeFrames = State.create_table();
			for (int32 Index = 0; Index < Settings->SafeFrames.Num(); ++Index)
			{
				SafeFrames[Index + 1] = BuildSafeFrameInfo(State, Settings->SafeFrames[Index]);
			}
			Result["safe_frame_count"] = Settings->SafeFrames.Num();
			Result["safe_frames"] = SafeFrames;
			Result["camera_bounds_shade_color"] = BuildLinearColor(State, Settings->CameraBoundsShadeColor);
			Result["enable_texture_overlay"] = Settings->bEnableTextureOverlay;
			Result["texture_overlay_texture"] = std::string(TCHAR_TO_UTF8(*Settings->TextureOverlayTexture.ToSoftObjectPath().ToString()));
			Result["texture_overlay_opacity"] = Settings->TextureOverlayOpacity;
			Result["texture_overlay_stretch"] = Settings->bTextureOverlayStretch;
			return Result;
		}

		void ApplyOptionalBool(sol::table Options, const char* Key, bool& Field)
		{
			if (sol::optional<bool> Value = Options.get<sol::optional<bool>>(Key))
			{
				Field = *Value;
			}
		}

		void ApplyOptionalFloat(sol::table Options, const char* Key, float& Field, float MinValue, float MaxValue)
		{
			if (sol::optional<double> Value = Options.get<sol::optional<double>>(Key))
			{
				Field = FMath::Clamp(static_cast<float>(*Value), MinValue, MaxValue);
			}
		}

		void ApplyOptionalColor(sol::table Options, const char* Key, FLinearColor& Field)
		{
			if (sol::optional<sol::table> Value = Options.get<sol::optional<sol::table>>(Key))
			{
				Field = TableToLinearColor(*Value, Field);
			}
		}

		template <typename ObjectType>
		void ApplyOptionalSoftPath(sol::table Options, const char* Key, TSoftObjectPtr<ObjectType>& Field)
		{
			if (sol::optional<std::string> Value = Options.get<sol::optional<std::string>>(Key))
			{
				Field = TSoftObjectPtr<ObjectType>(FSoftObjectPath(UTF8_TO_TCHAR(Value->c_str())));
			}
		}

		void ApplyViewportSettingsOptions(UAvaViewportSettings* Settings, sol::table Options)
		{
			ApplyOptionalBool(Options, "enable_viewport_overlay", Settings->bEnableViewportOverlay);
			ApplyOptionalBool(Options, "enable_bounding_boxes", Settings->bEnableBoundingBoxes);
			ApplyOptionalSoftPath(Options, "background_material", Settings->ViewportBackgroundMaterial);
			ApplyOptionalSoftPath(Options, "checkerboard_material", Settings->ViewportCheckerboardMaterial);
			ApplyOptionalColor(Options, "checkerboard_color0", Settings->ViewportCheckerboardColor0);
			ApplyOptionalColor(Options, "checkerboard_color1", Settings->ViewportCheckerboardColor1);
			ApplyOptionalFloat(Options, "checkerboard_size", Settings->ViewportCheckerboardSize, 0.0f, TNumericLimits<float>::Max());
			ApplyOptionalBool(Options, "enable_shapes_editor_overlay", Settings->bEnableShapesEditorOverlay);
			if (sol::optional<std::string> ShapeOverlayType = Options.get<sol::optional<std::string>>("shape_editor_overlay_type"))
			{
				Settings->ShapeEditorOverlayType = ParseShapeOverlayType(*ShapeOverlayType, Settings->ShapeEditorOverlayType);
			}
			ApplyOptionalBool(Options, "grid_enabled", Settings->bGridEnabled);
			ApplyOptionalBool(Options, "grid_always_visible", Settings->bGridAlwaysVisible);
			if (sol::optional<int32> GridSize = Options.get<sol::optional<int32>>("grid_size"))
			{
				Settings->GridSize = FMath::Clamp(*GridSize, 1, 256);
			}
			ApplyOptionalColor(Options, "grid_color", Settings->GridColor);
			ApplyOptionalFloat(Options, "grid_thickness", Settings->GridThickness, 1.0f, 8.0f);
			ApplyOptionalBool(Options, "pixel_grid_enabled", Settings->bPixelGridEnabled);
			ApplyOptionalColor(Options, "pixel_grid_color", Settings->PixelGridColor);
			if (sol::object SnapState = Options.get<sol::object>("snap_state"); SnapState.valid() && SnapState.get_type() != sol::type::lua_nil)
			{
				Settings->SetSnapState(static_cast<EAvaViewportSnapState>(ParseSnapStateObject(SnapState, Settings->SnapState)));
			}
			ApplyOptionalBool(Options, "snap_indicators_enabled", Settings->bSnapIndicatorsEnabled);
			ApplyOptionalColor(Options, "snap_indicator_color", Settings->SnapIndicatorColor);
			ApplyOptionalFloat(Options, "snap_indicator_thickness", Settings->SnapIndicatorThickness, 1.0f, 8.0f);
			ApplyOptionalBool(Options, "guides_enabled", Settings->bGuidesEnabled);
			if (sol::optional<std::string> GuideConfigPath = Options.get<sol::optional<std::string>>("guide_config_path"))
			{
				Settings->GuideConfigPath = UTF8_TO_TCHAR(GuideConfigPath->c_str());
			}
			ApplyOptionalColor(Options, "enabled_guide_color", Settings->EnabledGuideColor);
			ApplyOptionalColor(Options, "enabled_locked_guide_color", Settings->EnabledLockedGuideColor);
			ApplyOptionalColor(Options, "disabled_guide_color", Settings->DisabledGuideColor);
			ApplyOptionalColor(Options, "disabled_locked_guide_color", Settings->DisabledLockedGuideColor);
			ApplyOptionalColor(Options, "dragged_guide_color", Settings->DraggedGuideColor);
			ApplyOptionalFloat(Options, "guide_thickness", Settings->GuideThickness, 1.0f, 8.0f);
			ApplyOptionalBool(Options, "safe_frames_enabled", Settings->bSafeFramesEnabled);
			if (sol::optional<sol::table> SafeFrames = Options.get<sol::optional<sol::table>>("safe_frames"))
			{
				Settings->SafeFrames.Reset();
				for (int32 LuaIndex = 1;; ++LuaIndex)
				{
					sol::object Entry = (*SafeFrames)[LuaIndex];
					if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
					{
						break;
					}
					if (!Entry.is<sol::table>())
					{
						continue;
					}
					sol::table SafeFrameTable = Entry.as<sol::table>();
					FAvaLevelViewportSafeFrame& SafeFrame = Settings->SafeFrames.AddDefaulted_GetRef();
					SafeFrame.ScreenPercentage = FMath::Clamp(static_cast<float>(SafeFrameTable.get_or("screen_percentage", 50.0)), 0.0f, 100.0f);
					if (sol::optional<sol::table> Color = SafeFrameTable.get<sol::optional<sol::table>>("color"))
					{
						SafeFrame.Color = TableToLinearColor(*Color, SafeFrame.Color);
					}
					SafeFrame.Thickness = FMath::Clamp(static_cast<float>(SafeFrameTable.get_or("thickness", 1.0)), 1.0f, 8.0f);
				}
			}
			ApplyOptionalColor(Options, "camera_bounds_shade_color", Settings->CameraBoundsShadeColor);
			ApplyOptionalBool(Options, "enable_texture_overlay", Settings->bEnableTextureOverlay);
			ApplyOptionalSoftPath(Options, "texture_overlay_texture", Settings->TextureOverlayTexture);
			ApplyOptionalFloat(Options, "texture_overlay_opacity", Settings->TextureOverlayOpacity, 0.0f, 1.0f);
			ApplyOptionalBool(Options, "texture_overlay_stretch", Settings->bTextureOverlayStretch);
		}

			bool TableToGuideInfo(const sol::table& Table, FAvaViewportGuideInfo& OutGuide)
			{
				if (sol::optional<std::string> Orientation = Table.get<sol::optional<std::string>>("orientation"))
			{
				OutGuide.Orientation = ParseOrientation(*Orientation, OutGuide.Orientation);
			}
			if (sol::optional<double> OffsetFraction = Table.get<sol::optional<double>>("offset_fraction"))
			{
				OutGuide.OffsetFraction = FMath::Clamp(static_cast<float>(*OffsetFraction), 0.0f, 1.0f);
			}
			if (sol::optional<std::string> State = Table.get<sol::optional<std::string>>("state"))
			{
				OutGuide.State = ParseGuideState(*State, OutGuide.State);
			}
			else if (sol::optional<bool> bEnabled = Table.get<sol::optional<bool>>("enabled"))
			{
				OutGuide.State = *bEnabled ? EAvaViewportGuideState::Enabled : EAvaViewportGuideState::Disabled;
			}
			if (sol::optional<bool> bLocked = Table.get<sol::optional<bool>>("locked"))
			{
				OutGuide.bLocked = *bLocked;
				}
				return true;
			}

			sol::table BuildIntPoint(sol::state_view State, const FIntPoint& Value)
			{
				sol::table Result = State.create_table();
				Result["x"] = Value.X;
				Result["y"] = Value.Y;
				return Result;
			}

			FIntPoint TableToIntPoint(const sol::table& Table, const FIntPoint& DefaultValue)
			{
				return FIntPoint(
					Table.get_or("x", Table.get_or("X", DefaultValue.X)),
					Table.get_or("y", Table.get_or("Y", DefaultValue.Y)));
			}

			const TCHAR* ViewportAspectStateToString(EAvaViewportVirtualSizeAspectRatioState State)
			{
				switch (State)
				{
				case EAvaViewportVirtualSizeAspectRatioState::Unlocked:
					return TEXT("Unlocked");
				case EAvaViewportVirtualSizeAspectRatioState::Locked:
					return TEXT("Locked");
				case EAvaViewportVirtualSizeAspectRatioState::LockedToCamera:
				default:
					return TEXT("LockedToCamera");
				}
			}

			EAvaViewportVirtualSizeAspectRatioState ParseViewportAspectState(const std::string& Value, EAvaViewportVirtualSizeAspectRatioState DefaultValue)
			{
				const FString Text = UTF8_TO_TCHAR(Value.c_str());
				if (Text.Equals(TEXT("Unlocked"), ESearchCase::IgnoreCase))
				{
					return EAvaViewportVirtualSizeAspectRatioState::Unlocked;
				}
				if (Text.Equals(TEXT("Locked"), ESearchCase::IgnoreCase))
				{
					return EAvaViewportVirtualSizeAspectRatioState::Locked;
				}
				if (Text.Equals(TEXT("LockedToCamera"), ESearchCase::IgnoreCase) ||
					Text.Equals(TEXT("Locked To Camera"), ESearchCase::IgnoreCase))
				{
					return EAvaViewportVirtualSizeAspectRatioState::LockedToCamera;
				}
				return DefaultValue;
			}

			const TCHAR* ViewportPostProcessTypeToString(EAvaViewportPostProcessType Type)
			{
				switch (Type)
				{
				case EAvaViewportPostProcessType::Background:
					return TEXT("Background");
				case EAvaViewportPostProcessType::RedChannel:
					return TEXT("RedChannel");
				case EAvaViewportPostProcessType::GreenChannel:
					return TEXT("GreenChannel");
				case EAvaViewportPostProcessType::BlueChannel:
					return TEXT("BlueChannel");
				case EAvaViewportPostProcessType::AlphaChannel:
					return TEXT("AlphaChannel");
				case EAvaViewportPostProcessType::Checkerboard:
					return TEXT("Checkerboard");
				case EAvaViewportPostProcessType::None:
				default:
					return TEXT("None");
				}
			}

			EAvaViewportPostProcessType ParseViewportPostProcessType(const std::string& Value, EAvaViewportPostProcessType DefaultValue)
			{
				const FString Text = UTF8_TO_TCHAR(Value.c_str());
				if (Text.Equals(TEXT("None"), ESearchCase::IgnoreCase))
				{
					return EAvaViewportPostProcessType::None;
				}
				if (Text.Equals(TEXT("Background"), ESearchCase::IgnoreCase))
				{
					return EAvaViewportPostProcessType::Background;
				}
				if (Text.Equals(TEXT("RedChannel"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("Red"), ESearchCase::IgnoreCase))
				{
					return EAvaViewportPostProcessType::RedChannel;
				}
				if (Text.Equals(TEXT("GreenChannel"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("Green"), ESearchCase::IgnoreCase))
				{
					return EAvaViewportPostProcessType::GreenChannel;
				}
				if (Text.Equals(TEXT("BlueChannel"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("Blue"), ESearchCase::IgnoreCase))
				{
					return EAvaViewportPostProcessType::BlueChannel;
				}
				if (Text.Equals(TEXT("AlphaChannel"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("Alpha"), ESearchCase::IgnoreCase))
				{
					return EAvaViewportPostProcessType::AlphaChannel;
				}
				if (Text.Equals(TEXT("Checkerboard"), ESearchCase::IgnoreCase))
				{
					return EAvaViewportPostProcessType::Checkerboard;
				}
				return DefaultValue;
			}

			sol::table BuildViewportDataInfo(sol::state_view State)
			{
				sol::table Result = State.create_table();
				UWorld* World = GetEditorWorld();
				UAvaViewportDataSubsystem* Subsystem = World ? UAvaViewportDataSubsystem::Get(World) : nullptr;
				Result["has_world"] = World != nullptr;
				Result["has_subsystem"] = Subsystem != nullptr;
				if (!Subsystem)
				{
					Result["has_data"] = false;
					return Result;
				}

				FAvaViewportData* Data = Subsystem->GetData();
				Result["has_data"] = Data != nullptr;
				if (!Data)
				{
					return Result;
				}

				Result["guide_data_count"] = Data->GuideData.Num();
				Result["post_process_type"] = std::string(TCHAR_TO_UTF8(ViewportPostProcessTypeToString(Data->PostProcessInfo.Type)));
				Result["post_process_texture"] = std::string(TCHAR_TO_UTF8(*Data->PostProcessInfo.Texture.ToSoftObjectPath().ToString()));
				Result["post_process_opacity"] = Data->PostProcessInfo.Opacity;
				Result["virtual_size"] = BuildIntPoint(State, Data->VirtualSize);
				Result["virtual_size_aspect_state"] = std::string(TCHAR_TO_UTF8(ViewportAspectStateToString(Data->VirtualSizeAspectRatioState)));
				Result["piloted_camera_path"] = std::string(TCHAR_TO_UTF8(*Data->PilotedCamera.ToSoftObjectPath().ToString()));
				AActor* PilotedCamera = Data->PilotedCamera.Get();
				Result["has_piloted_camera"] = PilotedCamera != nullptr;
				if (PilotedCamera)
				{
					Result["piloted_camera_label"] = std::string(TCHAR_TO_UTF8(*PilotedCamera->GetActorLabel()));
					Result["piloted_camera_name"] = std::string(TCHAR_TO_UTF8(*PilotedCamera->GetName()));
					Result["piloted_camera_class"] = std::string(TCHAR_TO_UTF8(*PilotedCamera->GetClass()->GetName()));
				}
				return Result;
			}

			const TCHAR* OverrunActionToString(EAvaBroadcastOutputOverrunAction Action)
			{
				switch (Action)
				{
				case EAvaBroadcastOutputOverrunAction::Flush:
					return TEXT("Flush");
				case EAvaBroadcastOutputOverrunAction::Skip:
				default:
					return TEXT("Skip");
				}
			}

			EAvaBroadcastOutputOverrunAction ParseOverrunAction(const std::string& Value, EAvaBroadcastOutputOverrunAction DefaultValue)
			{
				const FString Text = UTF8_TO_TCHAR(Value.c_str());
				if (Text.Equals(TEXT("Flush"), ESearchCase::IgnoreCase))
				{
					return EAvaBroadcastOutputOverrunAction::Flush;
				}
				if (Text.Equals(TEXT("Skip"), ESearchCase::IgnoreCase))
				{
					return EAvaBroadcastOutputOverrunAction::Skip;
				}
				return DefaultValue;
			}

			const TCHAR* MediaLogVerbosityToString(EAvaMediaLogVerbosity Verbosity)
			{
				switch (Verbosity)
				{
				case EAvaMediaLogVerbosity::Fatal:
					return TEXT("Fatal");
				case EAvaMediaLogVerbosity::Error:
					return TEXT("Error");
				case EAvaMediaLogVerbosity::Warning:
					return TEXT("Warning");
				case EAvaMediaLogVerbosity::Display:
					return TEXT("Display");
				case EAvaMediaLogVerbosity::Log:
					return TEXT("Log");
				case EAvaMediaLogVerbosity::Verbose:
					return TEXT("Verbose");
				case EAvaMediaLogVerbosity::VeryVerbose:
					return TEXT("VeryVerbose");
				case EAvaMediaLogVerbosity::NoLogging:
				default:
					return TEXT("NoLogging");
				}
			}

			EAvaMediaLogVerbosity ParseMediaLogVerbosity(const std::string& Value, EAvaMediaLogVerbosity DefaultValue)
			{
				const FString Text = UTF8_TO_TCHAR(Value.c_str());
				if (Text.Equals(TEXT("Fatal"), ESearchCase::IgnoreCase)) { return EAvaMediaLogVerbosity::Fatal; }
				if (Text.Equals(TEXT("Error"), ESearchCase::IgnoreCase)) { return EAvaMediaLogVerbosity::Error; }
				if (Text.Equals(TEXT("Warning"), ESearchCase::IgnoreCase)) { return EAvaMediaLogVerbosity::Warning; }
				if (Text.Equals(TEXT("Display"), ESearchCase::IgnoreCase)) { return EAvaMediaLogVerbosity::Display; }
				if (Text.Equals(TEXT("Log"), ESearchCase::IgnoreCase)) { return EAvaMediaLogVerbosity::Log; }
				if (Text.Equals(TEXT("Verbose"), ESearchCase::IgnoreCase)) { return EAvaMediaLogVerbosity::Verbose; }
				if (Text.Equals(TEXT("VeryVerbose"), ESearchCase::IgnoreCase)) { return EAvaMediaLogVerbosity::VeryVerbose; }
				if (Text.Equals(TEXT("NoLogging"), ESearchCase::IgnoreCase)) { return EAvaMediaLogVerbosity::NoLogging; }
				return DefaultValue;
			}

			sol::table BuildStringArray(sol::state_view State, const TArray<FString>& Values)
			{
				sol::table Result = State.create_table();
				for (int32 Index = 0; Index < Values.Num(); ++Index)
				{
					Result[Index + 1] = std::string(TCHAR_TO_UTF8(*Values[Index]));
				}
				return Result;
			}

			sol::table BuildMediaLoggingEntry(sol::state_view State, const FAvaPlaybackServerLoggingEntry& Entry)
			{
				sol::table Result = State.create_table();
				Result["category"] = std::string(TCHAR_TO_UTF8(*Entry.Category.ToString()));
				Result["verbosity"] = std::string(TCHAR_TO_UTF8(MediaLogVerbosityToString(Entry.VerbosityLevel)));
				return Result;
			}

			sol::table BuildMediaSettingsInfo(sol::state_view State)
			{
				const UAvaMediaSettings& Settings = UAvaMediaSettings::Get();
				sol::table Result = State.create_table();
				Result["channel_clear_color"] = BuildLinearColor(State, Settings.ChannelClearColor);
				Result["channel_default_pixel_format"] = static_cast<int32>(Settings.ChannelDefaultPixelFormat.GetValue());
				Result["channel_default_resolution"] = BuildIntPoint(State, Settings.ChannelDefaultResolution);
				Result["channel_output_overrun_action"] = std::string(TCHAR_TO_UTF8(OverrunActionToString(Settings.ChannelOutputOverrunAction)));
				Result["draw_placeholder_widget"] = Settings.bDrawPlaceholderWidget;
				Result["placeholder_widget_class"] = std::string(TCHAR_TO_UTF8(*Settings.PlaceholderWidgetClass.ToSoftObjectPath().ToString()));
				Result["preview_default_resolution"] = BuildIntPoint(State, Settings.PreviewDefaultResolution);
				Result["preview_channel_name"] = std::string(TCHAR_TO_UTF8(*Settings.PreviewChannelName));
				Result["enable_combo_template_special_logic"] = Settings.bEnableComboTemplateSpecialLogic;
				Result["enable_single_template_special_logic"] = Settings.bEnableSingleTemplateSpecialLogic;
				Result["rundown_push_controller_events_to_program"] = Settings.bRundownPushControllerEventsToProgram;
				Result["rundown_push_controller_events_to_preview"] = Settings.bRundownPushControllerEventsToPreview;
				Result["auto_start_playback_client"] = Settings.bAutoStartPlaybackClient;
				Result["verbose_playback_client_logging"] = Settings.bVerbosePlaybackClientLogging;
				Result["ping_interval"] = Settings.PingInterval;
				Result["ping_timeout_interval"] = Settings.PingTimeoutInterval;
				Result["client_pending_status_request_timeout"] = Settings.ClientPendingStatusRequestTimeout;
				Result["auto_start_playback_server"] = Settings.bAutoStartPlaybackServer;
				Result["playback_server_name"] = std::string(TCHAR_TO_UTF8(*Settings.PlaybackServerName));
				Result["playback_server_log_replication_verbosity"] = std::string(TCHAR_TO_UTF8(MediaLogVerbosityToString(Settings.PlaybackServerLogReplicationVerbosity)));
				Result["server_pending_status_request_timeout"] = Settings.ServerPendingStatusRequestTimeout;
				Result["server_pending_playback_command_timeout"] = Settings.ServerPendingPlaybackCommandTimeout;
				sol::table LocalServer = State.create_table();
				LocalServer["server_name"] = std::string(TCHAR_TO_UTF8(*Settings.LocalPlaybackServerSettings.ServerName));
				LocalServer["resolution"] = BuildIntPoint(State, Settings.LocalPlaybackServerSettings.Resolution);
				LocalServer["enable_log_console"] = Settings.LocalPlaybackServerSettings.bEnableLogConsole;
				LocalServer["extra_command_line_arguments"] = std::string(TCHAR_TO_UTF8(*Settings.LocalPlaybackServerSettings.ExtraCommandLineArguments));
				sol::table Logging = State.create_table();
				for (int32 Index = 0; Index < Settings.LocalPlaybackServerSettings.Logging.Num(); ++Index)
				{
					Logging[Index + 1] = BuildMediaLoggingEntry(State, Settings.LocalPlaybackServerSettings.Logging[Index]);
				}
				LocalServer["logging"] = Logging;
				LocalServer["logging_count"] = Settings.LocalPlaybackServerSettings.Logging.Num();
				Result["local_playback_server"] = LocalServer;
				Result["keep_pages_loaded"] = Settings.bKeepPagesLoaded;
				Result["ava_instance_enable_load_sub_playables"] = Settings.AvaInstanceSettings.bEnableLoadSubPlayables;
				Result["ava_instance_default_transition_wait_for_sequences"] = Settings.AvaInstanceSettings.bDefaultPlayableTransitionWaitForSequences;
				Result["playable_synchronized_events_feature"] = std::string(TCHAR_TO_UTF8(*Settings.PlayableSettings.SynchronizedEventsFeature.Implementation));
				Result["playable_hide_pawn_actors"] = Settings.PlayableSettings.bHidePawnActors;
				Result["playable_ignored_controller_postfix_count"] = Settings.PlayableSettings.IgnoredControllerPostfix.Num();
				Result["playable_ignored_controller_postfix"] = BuildStringArray(State, Settings.PlayableSettings.IgnoredControllerPostfix);
				Result["managed_instance_cache_maximum_size"] = Settings.ManagedInstanceCacheMaximumSize;
				Result["auto_start_web_server"] = Settings.bAutoStartWebServer;
				Result["http_server_port"] = static_cast<int32>(Settings.HttpServerPort);
				return Result;
			}

			void ApplyStringArray(sol::table Values, TArray<FString>& OutValues)
			{
				OutValues.Reset();
				for (int32 LuaIndex = 1;; ++LuaIndex)
				{
					sol::object Entry = Values[LuaIndex];
					if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
					{
						break;
					}
					if (Entry.is<std::string>())
					{
						OutValues.Add(UTF8_TO_TCHAR(Entry.as<std::string>().c_str()));
					}
				}
			}

			void ApplyMediaSettingsOptions(UAvaMediaSettings& Settings, sol::table Options)
			{
				ApplyOptionalColor(Options, "channel_clear_color", Settings.ChannelClearColor);
				if (sol::optional<int32> PixelFormat = Options.get<sol::optional<int32>>("channel_default_pixel_format"))
				{
					Settings.ChannelDefaultPixelFormat = static_cast<EPixelFormat>(FMath::Clamp(*PixelFormat, 0, static_cast<int32>(PF_MAX) - 1));
				}
				if (sol::optional<sol::table> Resolution = Options.get<sol::optional<sol::table>>("channel_default_resolution"))
				{
					Settings.ChannelDefaultResolution = TableToIntPoint(*Resolution, Settings.ChannelDefaultResolution).ComponentMax(FIntPoint(1, 1));
				}
				if (sol::optional<std::string> OverrunAction = Options.get<sol::optional<std::string>>("channel_output_overrun_action"))
				{
					Settings.ChannelOutputOverrunAction = ParseOverrunAction(*OverrunAction, Settings.ChannelOutputOverrunAction);
				}
				ApplyOptionalBool(Options, "draw_placeholder_widget", Settings.bDrawPlaceholderWidget);
				if (sol::optional<std::string> PlaceholderWidgetClass = Options.get<sol::optional<std::string>>("placeholder_widget_class"))
				{
					Settings.PlaceholderWidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(UTF8_TO_TCHAR(PlaceholderWidgetClass->c_str())));
				}
				if (sol::optional<sol::table> Resolution = Options.get<sol::optional<sol::table>>("preview_default_resolution"))
				{
					Settings.PreviewDefaultResolution = TableToIntPoint(*Resolution, Settings.PreviewDefaultResolution).ComponentMax(FIntPoint(1, 1));
				}
				if (sol::optional<std::string> PreviewChannelName = Options.get<sol::optional<std::string>>("preview_channel_name"))
				{
					Settings.PreviewChannelName = UTF8_TO_TCHAR(PreviewChannelName->c_str());
				}
				ApplyOptionalBool(Options, "enable_combo_template_special_logic", Settings.bEnableComboTemplateSpecialLogic);
				ApplyOptionalBool(Options, "enable_single_template_special_logic", Settings.bEnableSingleTemplateSpecialLogic);
				ApplyOptionalBool(Options, "rundown_push_controller_events_to_program", Settings.bRundownPushControllerEventsToProgram);
				ApplyOptionalBool(Options, "rundown_push_controller_events_to_preview", Settings.bRundownPushControllerEventsToPreview);
				ApplyOptionalBool(Options, "auto_start_playback_client", Settings.bAutoStartPlaybackClient);
				ApplyOptionalBool(Options, "verbose_playback_client_logging", Settings.bVerbosePlaybackClientLogging);
				ApplyOptionalFloat(Options, "ping_interval", Settings.PingInterval, 0.0f, TNumericLimits<float>::Max());
				ApplyOptionalFloat(Options, "ping_timeout_interval", Settings.PingTimeoutInterval, 0.0f, TNumericLimits<float>::Max());
				ApplyOptionalFloat(Options, "client_pending_status_request_timeout", Settings.ClientPendingStatusRequestTimeout, 0.0f, TNumericLimits<float>::Max());
				ApplyOptionalBool(Options, "auto_start_playback_server", Settings.bAutoStartPlaybackServer);
				if (sol::optional<std::string> PlaybackServerName = Options.get<sol::optional<std::string>>("playback_server_name"))
				{
					Settings.PlaybackServerName = UTF8_TO_TCHAR(PlaybackServerName->c_str());
				}
				if (sol::optional<std::string> Verbosity = Options.get<sol::optional<std::string>>("playback_server_log_replication_verbosity"))
				{
					Settings.PlaybackServerLogReplicationVerbosity = ParseMediaLogVerbosity(*Verbosity, Settings.PlaybackServerLogReplicationVerbosity);
				}
				ApplyOptionalFloat(Options, "server_pending_status_request_timeout", Settings.ServerPendingStatusRequestTimeout, 0.0f, TNumericLimits<float>::Max());
				ApplyOptionalFloat(Options, "server_pending_playback_command_timeout", Settings.ServerPendingPlaybackCommandTimeout, 0.0f, TNumericLimits<float>::Max());
				if (sol::optional<sol::table> LocalServer = Options.get<sol::optional<sol::table>>("local_playback_server"))
				{
					if (sol::optional<std::string> ServerName = LocalServer->get<sol::optional<std::string>>("server_name"))
					{
						Settings.LocalPlaybackServerSettings.ServerName = UTF8_TO_TCHAR(ServerName->c_str());
					}
					if (sol::optional<sol::table> Resolution = LocalServer->get<sol::optional<sol::table>>("resolution"))
					{
						Settings.LocalPlaybackServerSettings.Resolution = TableToIntPoint(*Resolution, Settings.LocalPlaybackServerSettings.Resolution).ComponentMax(FIntPoint(1, 1));
					}
					if (sol::optional<bool> bEnableLogConsole = LocalServer->get<sol::optional<bool>>("enable_log_console"))
					{
						Settings.LocalPlaybackServerSettings.bEnableLogConsole = *bEnableLogConsole;
					}
					if (sol::optional<std::string> ExtraArgs = LocalServer->get<sol::optional<std::string>>("extra_command_line_arguments"))
					{
						Settings.LocalPlaybackServerSettings.ExtraCommandLineArguments = UTF8_TO_TCHAR(ExtraArgs->c_str());
					}
					if (sol::optional<sol::table> Logging = LocalServer->get<sol::optional<sol::table>>("logging"))
					{
						Settings.LocalPlaybackServerSettings.Logging.Reset();
						for (int32 LuaIndex = 1;; ++LuaIndex)
						{
							sol::object Entry = (*Logging)[LuaIndex];
							if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
							{
								break;
							}
							if (!Entry.is<sol::table>())
							{
								continue;
							}
							sol::table EntryTable = Entry.as<sol::table>();
							FAvaPlaybackServerLoggingEntry& NewEntry = Settings.LocalPlaybackServerSettings.Logging.AddDefaulted_GetRef();
							NewEntry.Category = FName(UTF8_TO_TCHAR(EntryTable.get_or<std::string>("category", "").c_str()));
							NewEntry.VerbosityLevel = ParseMediaLogVerbosity(EntryTable.get_or<std::string>("verbosity", "VeryVerbose"), NewEntry.VerbosityLevel);
						}
					}
				}
				ApplyOptionalBool(Options, "keep_pages_loaded", Settings.bKeepPagesLoaded);
				ApplyOptionalBool(Options, "ava_instance_enable_load_sub_playables", Settings.AvaInstanceSettings.bEnableLoadSubPlayables);
				ApplyOptionalBool(Options, "ava_instance_default_transition_wait_for_sequences", Settings.AvaInstanceSettings.bDefaultPlayableTransitionWaitForSequences);
				if (sol::optional<std::string> Feature = Options.get<sol::optional<std::string>>("playable_synchronized_events_feature"))
				{
					Settings.PlayableSettings.SynchronizedEventsFeature.Implementation = UTF8_TO_TCHAR(Feature->c_str());
				}
				ApplyOptionalBool(Options, "playable_hide_pawn_actors", Settings.PlayableSettings.bHidePawnActors);
				if (sol::optional<sol::table> IgnoredPostfix = Options.get<sol::optional<sol::table>>("playable_ignored_controller_postfix"))
				{
					ApplyStringArray(*IgnoredPostfix, Settings.PlayableSettings.IgnoredControllerPostfix);
				}
				if (sol::optional<int32> CacheSize = Options.get<sol::optional<int32>>("managed_instance_cache_maximum_size"))
				{
					Settings.ManagedInstanceCacheMaximumSize = FMath::Max(0, *CacheSize);
				}
				ApplyOptionalBool(Options, "auto_start_web_server", Settings.bAutoStartWebServer);
				if (sol::optional<int32> HttpPort = Options.get<sol::optional<int32>>("http_server_port"))
				{
					Settings.HttpServerPort = static_cast<uint32>(FMath::Clamp(*HttpPort, 0, 65535));
				}
			}

			UAvaRundownMacroCollection* ResolveMacroCollection(const sol::table& AssetObj)
			{
				const std::string Path = AssetObj.get_or<std::string>("path", "");
				if (Path.empty())
				{
					return nullptr;
				}
				return LoadObject<UAvaRundownMacroCollection>(nullptr, UTF8_TO_TCHAR(Path.c_str()));
			}

			FArrayProperty* FindMacroKeyBindingsProperty(UAvaRundownMacroCollection* Collection)
			{
				return Collection ? FindFProperty<FArrayProperty>(Collection->GetClass(), TEXT("KeyBindings")) : nullptr;
			}

			bool GetMacroKeyBindings(UAvaRundownMacroCollection* Collection, FArrayProperty*& OutArrayProperty, FStructProperty*& OutStructProperty, FScriptArrayHelper*& OutHelper)
			{
				OutArrayProperty = FindMacroKeyBindingsProperty(Collection);
				OutStructProperty = OutArrayProperty ? CastField<FStructProperty>(OutArrayProperty->Inner) : nullptr;
				if (!Collection || !OutArrayProperty || !OutStructProperty)
				{
					return false;
				}
				OutHelper = new FScriptArrayHelper(OutArrayProperty, OutArrayProperty->ContainerPtrToValuePtr<void>(Collection));
				return true;
			}

			FInputChord ParseInputChord(sol::table Options, bool& bOutValid)
			{
				bOutValid = false;
				const std::string KeyString = Options.get_or<std::string>("key", "");
				if (KeyString.empty())
				{
					return FInputChord();
				}

				FKey Key(FName(UTF8_TO_TCHAR(KeyString.c_str())));
				if (!Key.IsValid())
				{
					return FInputChord();
				}

				bOutValid = true;
				return FInputChord(
					Key,
					Options.get_or("shift", Options.get_or("Shift", false)),
					Options.get_or("ctrl", Options.get_or("control", Options.get_or("Ctrl", false))),
					Options.get_or("alt", Options.get_or("Alt", false)),
					Options.get_or("cmd", Options.get_or("command", Options.get_or("Cmd", false))));
			}

			sol::table BuildInputChordInfo(sol::state_view State, const FInputChord& Chord)
			{
				sol::table Result = State.create_table();
				Result["key"] = std::string(TCHAR_TO_UTF8(*Chord.Key.GetFName().ToString()));
				Result["shift"] = static_cast<bool>(Chord.bShift);
				Result["ctrl"] = static_cast<bool>(Chord.bCtrl);
				Result["alt"] = static_cast<bool>(Chord.bAlt);
				Result["cmd"] = static_cast<bool>(Chord.bCmd);
				Result["valid"] = Chord.Key.IsValid();
				return Result;
			}

			sol::table BuildMacroCommandInfo(sol::state_view State, const FAvaRundownMacroCommand& Command)
			{
				sol::table Result = State.create_table();
				Result["name"] = std::string(TCHAR_TO_UTF8(*Command.Name.ToString()));
				Result["arguments"] = std::string(TCHAR_TO_UTF8(*Command.Arguments));
				return Result;
			}

			sol::table BuildMacroBindingInfo(sol::state_view State, const FAvaRundownMacroKeyBinding& Binding, int32 Index)
			{
				sol::table Result = State.create_table();
				Result["index"] = Index + 1;
				Result["description"] = std::string(TCHAR_TO_UTF8(*Binding.Description));
				Result["chord"] = BuildInputChordInfo(State, Binding.InputChord);
				Result["command_count"] = Binding.Commands.Num();

				sol::table Commands = State.create_table();
				for (int32 CommandIndex = 0; CommandIndex < Binding.Commands.Num(); ++CommandIndex)
				{
					Commands[CommandIndex + 1] = BuildMacroCommandInfo(State, Binding.Commands[CommandIndex]);
				}
				Result["commands"] = Commands;
				return Result;
			}

			sol::table BuildMacroCollectionInfo(sol::state_view State, UAvaRundownMacroCollection* Collection)
			{
				sol::table Result = State.create_table();
				if (!Collection)
				{
					Result["valid"] = false;
					Result["binding_count"] = 0;
					Result["bindings"] = State.create_table();
					return Result;
				}

				Result["valid"] = true;
				Result["class"] = std::string(TCHAR_TO_UTF8(*Collection->GetClass()->GetName()));
				Result["path"] = std::string(TCHAR_TO_UTF8(*Collection->GetPathName()));

				sol::table Bindings = State.create_table();
				FArrayProperty* ArrayProperty = nullptr;
				FStructProperty* StructProperty = nullptr;
				FScriptArrayHelper* HelperRaw = nullptr;
				TUniquePtr<FScriptArrayHelper> Helper;
				if (GetMacroKeyBindings(Collection, ArrayProperty, StructProperty, HelperRaw))
				{
					Helper.Reset(HelperRaw);
					for (int32 Index = 0; Index < Helper->Num(); ++Index)
					{
						const FAvaRundownMacroKeyBinding* Binding = reinterpret_cast<const FAvaRundownMacroKeyBinding*>(Helper->GetRawPtr(Index));
						Bindings[Index + 1] = BuildMacroBindingInfo(State, *Binding, Index);
					}
					Result["binding_count"] = Helper->Num();
				}
				else
				{
					Result["binding_count"] = 0;
				}

				Result["bindings"] = Bindings;
				return Result;
			}

			FAvaRundownMacroKeyBinding BuildMacroBindingFromOptions(sol::table Options, bool& bOutValid)
			{
				bOutValid = false;
				FAvaRundownMacroKeyBinding Binding;
				Binding.Description = UTF8_TO_TCHAR(Options.get_or<std::string>("description", "").c_str());

				sol::optional<sol::table> ChordTable = Options.get<sol::optional<sol::table>>("chord");
				if (!ChordTable)
				{
					return Binding;
				}

				bool bChordValid = false;
				Binding.InputChord = ParseInputChord(*ChordTable, bChordValid);
				if (!bChordValid)
				{
					return Binding;
				}

				if (sol::optional<sol::table> Commands = Options.get<sol::optional<sol::table>>("commands"))
				{
					for (int32 LuaIndex = 1;; ++LuaIndex)
					{
						sol::object Entry = (*Commands)[LuaIndex];
						if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
						{
							break;
						}
						if (!Entry.is<sol::table>())
						{
							continue;
						}

						sol::table CommandTable = Entry.as<sol::table>();
						const std::string Name = CommandTable.get_or<std::string>("name", "");
						if (Name.empty())
						{
							continue;
						}

						FAvaRundownMacroCommand& Command = Binding.Commands.AddDefaulted_GetRef();
						Command.Name = FName(UTF8_TO_TCHAR(Name.c_str()));
						Command.Arguments = UTF8_TO_TCHAR(CommandTable.get_or<std::string>("arguments", "").c_str());
					}
				}

				bOutValid = true;
				return Binding;
			}

			bool AddMacroBinding(UAvaRundownMacroCollection* Collection, const FAvaRundownMacroKeyBinding& Binding)
			{
				FArrayProperty* ArrayProperty = nullptr;
				FStructProperty* StructProperty = nullptr;
				FScriptArrayHelper* HelperRaw = nullptr;
				TUniquePtr<FScriptArrayHelper> Helper;
				if (!GetMacroKeyBindings(Collection, ArrayProperty, StructProperty, HelperRaw))
				{
					return false;
				}
				Helper.Reset(HelperRaw);
				const int32 Index = Helper->AddValue();
				StructProperty->CopyCompleteValue(Helper->GetRawPtr(Index), &Binding);
				Collection->MarkPackageDirty();
				return true;
			}

			int32 RemoveMacroBindingsForChord(UAvaRundownMacroCollection* Collection, const FInputChord& Chord)
			{
				FArrayProperty* ArrayProperty = nullptr;
				FStructProperty* StructProperty = nullptr;
				FScriptArrayHelper* HelperRaw = nullptr;
				TUniquePtr<FScriptArrayHelper> Helper;
				if (!GetMacroKeyBindings(Collection, ArrayProperty, StructProperty, HelperRaw))
				{
					return 0;
				}
				Helper.Reset(HelperRaw);

				int32 Removed = 0;
				for (int32 Index = Helper->Num() - 1; Index >= 0; --Index)
				{
					const FAvaRundownMacroKeyBinding* Binding = reinterpret_cast<const FAvaRundownMacroKeyBinding*>(Helper->GetRawPtr(Index));
					if (Binding->InputChord == Chord)
					{
						Helper->RemoveValues(Index);
						++Removed;
					}
				}

				if (Removed > 0)
				{
					Collection->MarkPackageDirty();
				}
				return Removed;
			}

			UAvaRundown* ResolveRundown(const sol::table& AssetObj)
			{
				const std::string Path = AssetObj.get_or<std::string>("path", "");
				if (Path.empty())
				{
					return nullptr;
				}
				return LoadObject<UAvaRundown>(nullptr, UTF8_TO_TCHAR(Path.c_str()));
			}

			TArray<int32> ReadIntArray(sol::table Values)
			{
				TArray<int32> Result;
				for (int32 LuaIndex = 1;; ++LuaIndex)
				{
					sol::object Entry = Values[LuaIndex];
					if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
					{
						break;
					}
					if (Entry.is<int32>())
					{
						Result.Add(Entry.as<int32>());
					}
				}
				return Result;
			}

			FAvaRundownPageIdGeneratorParams ReadPageIdParams(sol::table Options)
			{
				return FAvaRundownPageIdGeneratorParams(
					Options.get_or("reference_id", FAvaRundownPage::InvalidPageId),
					Options.get_or("increment", 1));
			}

			FAvaRundownPageInsertPosition ReadInsertPosition(sol::table Options)
			{
				if (sol::optional<sol::table> Insert = Options.get<sol::optional<sol::table>>("insert"))
				{
					return FAvaRundownPageInsertPosition(
						Insert->get_or("adjacent_id", FAvaRundownPage::InvalidPageId),
						Insert->get_or("below", Insert->get_or("add_below", true)));
				}
				return FAvaRundownPageInsertPosition();
			}

			const TCHAR* RundownListTypeToString(EAvaRundownPageListType Type)
			{
				switch (Type)
				{
				case EAvaRundownPageListType::Template:
					return TEXT("Template");
				case EAvaRundownPageListType::Instance:
					return TEXT("Instance");
				case EAvaRundownPageListType::View:
					return TEXT("View");
				default:
					return TEXT("Unknown");
				}
			}

			sol::table BuildRundownPageInfo(sol::state_view State, const UAvaRundown* Rundown, const FAvaRundownPage& Page)
			{
				sol::table Result = State.create_table();
				Result["valid"] = Page.IsValidPage();
				Result["page_id"] = Page.GetPageId();
				Result["template_id"] = Page.GetTemplateId();
				Result["is_template"] = Page.IsTemplate();
				Result["enabled"] = Page.IsEnabled();
				Result["page_name"] = std::string(TCHAR_TO_UTF8(*Page.GetPageName()));
				Result["friendly_name"] = std::string(TCHAR_TO_UTF8(*Page.GetPageFriendlyName().ToString()));
				Result["description"] = std::string(TCHAR_TO_UTF8(*Page.GetPageDescription().ToString()));
				Result["asset_path"] = std::string(TCHAR_TO_UTF8(*Page.GetAssetPathDirect().ToString()));
				Result["channel_name"] = std::string(TCHAR_TO_UTF8(*Page.GetChannelName().ToString()));
				Result["channel_index"] = Page.GetChannelIndex();
				Result["has_commands"] = Rundown ? Page.HasCommands(Rundown) : false;
				Result["has_transition_logic"] = Rundown ? Page.HasTransitionLogic(Rundown) : false;
				Result["transition_mode"] = std::string(TCHAR_TO_UTF8(AvalancheInstancingModeToString(Page.GetTransitionMode(Rundown))));
				const FAvaTagHandle TransitionLayer = Page.GetTransitionLayer(Rundown);
				Result["transition_layer_valid"] = TransitionLayer.IsValid();
				Result["transition_layer_name"] = std::string(TCHAR_TO_UTF8(*TransitionLayer.ToName().ToString()));
				Result["transition_layer_source"] = TransitionLayer.Source
					? std::string(TCHAR_TO_UTF8(*FSoftObjectPath(TransitionLayer.Source).ToString()))
					: std::string();
				Result["transition_layer"] = BuildAvaTransitionLayerInfo(State, TransitionLayer);
				const TArray<FAvaTagHandle> TransitionLayers = Page.GetTransitionLayers(Rundown);
				sol::table TransitionLayerRows = State.create_table();
				for (int32 Index = 0; Index < TransitionLayers.Num(); ++Index)
				{
					TransitionLayerRows[Index + 1] = BuildAvaTransitionLayerInfo(State, TransitionLayers[Index]);
				}
				Result["transition_layer_count"] = TransitionLayers.Num();
				Result["transition_layers"] = TransitionLayerRows;
				const TArray<EAvaTransitionInstancingMode> TransitionModes = Page.GetTransitionModes(Rundown);
				sol::table TransitionModeRows = State.create_table();
				for (int32 Index = 0; Index < TransitionModes.Num(); ++Index)
				{
					TransitionModeRows[Index + 1] = std::string(TCHAR_TO_UTF8(AvalancheInstancingModeToString(TransitionModes[Index])));
				}
				Result["transition_mode_count"] = TransitionModes.Num();
				Result["transition_modes"] = TransitionModeRows;
				Result["instance_count"] = Page.GetInstancedIds().Num();

				const FAvaPlayableRemoteControlValues& RemoteValues = Page.GetRemoteControlValues();
				auto BuildRemoteValue = [&State](const FGuid& Id, const FAvaPlayableRemoteControlValue& Value) -> sol::table
				{
					sol::table Entry = State.create_table();
					Entry["id"] = std::string(TCHAR_TO_UTF8(*Id.ToString(EGuidFormats::DigitsWithHyphens).ToLower()));
					Entry["value"] = std::string(TCHAR_TO_UTF8(*Value.Value));
					Entry["is_default"] = Value.bIsDefault;
					return Entry;
				};

				sol::table EntityValues = State.create_table();
				int32 EntityValueIndex = 0;
				for (const TPair<FGuid, FAvaPlayableRemoteControlValue>& Entry : RemoteValues.EntityValues)
				{
					EntityValues[++EntityValueIndex] = BuildRemoteValue(Entry.Key, Entry.Value);
				}
				sol::table ControllerValues = State.create_table();
				int32 ControllerValueIndex = 0;
				for (const TPair<FGuid, FAvaPlayableRemoteControlValue>& Entry : RemoteValues.ControllerValues)
				{
					ControllerValues[++ControllerValueIndex] = BuildRemoteValue(Entry.Key, Entry.Value);
				}
				Result["remote_entity_count"] = EntityValueIndex;
				Result["remote_entities"] = EntityValues;
				Result["remote_controller_count"] = ControllerValueIndex;
				Result["remote_controllers"] = ControllerValues;

				const TArray<FInstancedStruct>& Commands = Page.GetInstancedCommands();
				sol::table CommandInfos = State.create_table();
				int32 CommandIndex = 0;
				for (const FInstancedStruct& Command : Commands)
				{
					if (const FAvaRundownPageCommand* CommandPtr = Command.GetPtr<FAvaRundownPageCommand>())
					{
						UScriptStruct* CommandStruct = const_cast<UScriptStruct*>(Command.GetScriptStruct());
						sol::table CommandInfo = State.create_table();
						CommandInfo["index"] = CommandIndex + 1;
						CommandInfo["struct"] = CommandStruct ? std::string(TCHAR_TO_UTF8(*FSoftObjectPath(CommandStruct).ToString())) : std::string();
						CommandInfo["name"] = CommandStruct ? std::string(TCHAR_TO_UTF8(*CommandStruct->GetName())) : std::string();
						CommandInfo["description"] = std::string(TCHAR_TO_UTF8(*CommandPtr->GetDescription().ToString()));
						CommandInfo["has_transition_logic"] = CommandPtr->HasTransitionLogic();
						CommandInfo["properties"] = CommandStruct
							? NeoLuaProperty::ReadStructAsTable(CommandStruct, Command.GetMemory(), State)
							: State.create_table();
						if (CommandStruct && CommandStruct->GetFName() == TEXT("AvaRundownPageCommandStopLayers"))
						{
							FStructProperty* LayersProperty = FindFProperty<FStructProperty>(CommandStruct, TEXT("Layers"));
							const FAvaTagHandleContainer* Layers = LayersProperty && LayersProperty->Struct == FAvaTagHandleContainer::StaticStruct()
								? LayersProperty->ContainerPtrToValuePtr<FAvaTagHandleContainer>(const_cast<uint8*>(Command.GetMemory()))
								: nullptr;
							const UAvaTagCollection* Source = Layers ? Layers->Source.Get() : nullptr;
							sol::table LayerRows = State.create_table();
							int32 LayerIndex = 0;
							if (Layers)
							{
								for (const FAvaTagId& TagId : Layers->GetTagIds())
								{
									sol::table LayerRow = State.create_table();
									LayerRow["id"] = std::string(TCHAR_TO_UTF8(*TagId.ToString()));
									LayerRow["name"] = Source
										? std::string(TCHAR_TO_UTF8(*Source->GetTagName(TagId).ToString()))
										: std::string();
									LayerRows[++LayerIndex] = LayerRow;
								}
							}
							CommandInfo["layer_count"] = LayerIndex;
							CommandInfo["layers"] = LayerRows;
							CommandInfo["layer_source"] = Source
								? std::string(TCHAR_TO_UTF8(*FSoftObjectPath(Source).ToString()))
								: std::string();
						}
						CommandInfos[++CommandIndex] = CommandInfo;
					}
				}
				Result["command_count"] = CommandIndex;
				Result["commands"] = CommandInfos;

				sol::table Instances = State.create_table();
				int32 InstanceIndex = 0;
				for (const int32 InstanceId : Page.GetInstancedIds())
				{
					Instances[++InstanceIndex] = InstanceId;
				}
				Result["instances"] = Instances;

				sol::table Combined = State.create_table();
				const TArray<int32>& CombinedTemplateIds = Page.GetCombinedTemplateIds();
				for (int32 Index = 0; Index < CombinedTemplateIds.Num(); ++Index)
				{
					Combined[Index + 1] = CombinedTemplateIds[Index];
				}
				Result["combined_template_count"] = CombinedTemplateIds.Num();
				Result["combined_template_ids"] = Combined;
				return Result;
			}

			UScriptStruct* ResolveRundownPageCommandStruct(const std::string& Type)
			{
				const FString TypeString = UTF8_TO_TCHAR(Type.c_str());
				const FString Lower = TypeString.ToLower();
				FString Path = TypeString;
				if (!Path.StartsWith(TEXT("/Script/")) && !Path.Contains(TEXT(".")))
				{
					if (Lower == TEXT("settransform") || Lower == TEXT("set_transform") || Lower == TEXT("transform"))
					{
						Path = TEXT("/Script/AvalancheMedia.AvaRundownPageCommandSetTransform");
					}
					else if (Lower == TEXT("setspawnpoint") || Lower == TEXT("set_spawn_point") || Lower == TEXT("spawn_point"))
					{
						Path = TEXT("/Script/AvalancheMedia.AvaRundownPageCommandSetSpawnPoint");
					}
					else if (Lower == TEXT("stoplayers") || Lower == TEXT("stop_layers"))
					{
						Path = TEXT("/Script/AvalancheMedia.AvaRundownPageCommandStopLayers");
					}
					else
					{
						Path = FString::Printf(TEXT("/Script/AvalancheMedia.%s"), *TypeString);
					}
				}

				UScriptStruct* Struct = UClass::TryFindTypeSlow<UScriptStruct>(*Path);
				if (!Struct || !Struct->IsChildOf(FAvaRundownPageCommand::StaticStruct()))
				{
					return nullptr;
				}
				return Struct;
			}

			bool ReadGuidFromString(const std::string& IdString, FGuid& OutGuid)
			{
				return FGuid::Parse(UTF8_TO_TCHAR(IdString.c_str()), OutGuid);
			}

			FAvaPlayableRemoteControlValue ReadRemoteControlValue(sol::table Options)
			{
				std::string ValueString;
				if (sol::optional<std::string> Value = Options.get<sol::optional<std::string>>("value"))
				{
					ValueString = *Value;
				}
				else if (sol::optional<std::string> Json = Options.get<sol::optional<std::string>>("json"))
				{
					ValueString = *Json;
				}
				const bool bIsDefault = Options.get_or("is_default", Options.get_or("default", false));
				return FAvaPlayableRemoteControlValue(UTF8_TO_TCHAR(ValueString.c_str()), bIsDefault);
			}

			sol::optional<sol::table> GetStopLayersOptions(const sol::table& Props)
			{
				if (sol::optional<sol::table> Layers = Props.get<sol::optional<sol::table>>("Layers"))
				{
					return Layers;
				}
				return Props.get<sol::optional<sol::table>>("layers");
			}

			bool ApplyStopLayersProperties(FInstancedStruct& Command, UScriptStruct* CommandStruct, const sol::table& Props)
			{
				if (!CommandStruct || CommandStruct->GetFName() != TEXT("AvaRundownPageCommandStopLayers"))
				{
					return false;
				}

				sol::optional<sol::table> LayerOptions = GetStopLayersOptions(Props);
				if (!LayerOptions)
				{
					return false;
				}

				std::string CollectionPath = LayerOptions->get_or<std::string>("collection", "");
				if (CollectionPath.empty())
				{
					CollectionPath = LayerOptions->get_or<std::string>("source", "");
				}
				UAvaTagCollection* Collection = LoadTagCollection(CollectionPath);
				const TArray<FAvaTagHandle> Handles = ResolveTagHandles(Collection, *LayerOptions);
				if (!Collection || Handles.IsEmpty())
				{
					return false;
				}

				FStructProperty* LayersProperty = FindFProperty<FStructProperty>(CommandStruct, TEXT("Layers"));
				FAvaTagHandleContainer* Layers = LayersProperty && LayersProperty->Struct == FAvaTagHandleContainer::StaticStruct()
					? LayersProperty->ContainerPtrToValuePtr<FAvaTagHandleContainer>(Command.GetMutableMemory())
					: nullptr;
				if (!Layers)
				{
					return false;
				}

				FAvaTagHandleContainer TagContainer;
				for (const FAvaTagHandle& Handle : Handles)
				{
					TagContainer.AddTagHandle(Handle);
				}
				*Layers = TagContainer;
				return true;
			}

			bool ApplyRundownPageCommandProperties(FInstancedStruct& Command, UAvaRundown* Rundown, const sol::table& Options)
			{
				UScriptStruct* CommandStruct = const_cast<UScriptStruct*>(Command.GetScriptStruct());
				if (!CommandStruct || !Command.GetMutableMemory())
				{
					return false;
				}

				sol::optional<sol::table> Properties = Options.get<sol::optional<sol::table>>("properties");
				const sol::table& Props = Properties ? *Properties : Options;
				if (CommandStruct->GetFName() == TEXT("AvaRundownPageCommandStopLayers") && GetStopLayersOptions(Props))
				{
					return ApplyStopLayersProperties(Command, CommandStruct, Props);
				}
				FString Error;
				TArray<FString> Warnings;
				return NeoLuaProperty::ApplyTableToStruct(CommandStruct, Command.GetMutableMemory(), Rundown, Props, Error, &Warnings);
			}

			sol::table BuildRundownPageArray(sol::state_view State, const UAvaRundown* Rundown, const FAvaRundownPageCollection& Collection)
			{
				sol::table Result = State.create_table();
				for (int32 Index = 0; Index < Collection.Pages.Num(); ++Index)
				{
					Result[Index + 1] = BuildRundownPageInfo(State, Rundown, Collection.Pages[Index]);
				}
				return Result;
			}

			sol::table BuildRundownSubListInfo(sol::state_view State, const FAvaRundownSubList& SubList, int32 Index)
			{
				sol::table Result = State.create_table();
				Result["index"] = Index + 1;
				Result["id"] = std::string(TCHAR_TO_UTF8(*SubList.Id.ToString(EGuidFormats::DigitsWithHyphens)));
				Result["name"] = std::string(TCHAR_TO_UTF8(*SubList.Name.ToString()));
				Result["valid"] = SubList.IsValid();
				Result["page_count"] = SubList.PageIds.Num();

				sol::table PageIds = State.create_table();
				for (int32 PageIndex = 0; PageIndex < SubList.PageIds.Num(); ++PageIndex)
				{
					PageIds[PageIndex + 1] = SubList.PageIds[PageIndex];
				}
				Result["page_ids"] = PageIds;
				return Result;
			}

			sol::table BuildRundownInfo(sol::state_view State, UAvaRundown* Rundown)
			{
				sol::table Result = State.create_table();
				if (!Rundown)
				{
					Result["valid"] = false;
					return Result;
				}

				Result["valid"] = true;
				Result["class"] = std::string(TCHAR_TO_UTF8(*Rundown->GetClass()->GetName()));
				Result["path"] = std::string(TCHAR_TO_UTF8(*Rundown->GetPathName()));
				Result["is_empty"] = Rundown->IsEmpty();
				Result["template_count"] = Rundown->GetTemplatePages().Pages.Num();
				Result["page_count"] = Rundown->GetInstancedPages().Pages.Num();
				Result["sublist_count"] = Rundown->GetSubLists().Num();
				Result["active_list_type"] = std::string(TCHAR_TO_UTF8(RundownListTypeToString(Rundown->GetActivePageListReference().Type)));
				Result["has_active_sublist"] = Rundown->HasActiveSubList();
				Result["is_playing"] = Rundown->IsPlaying();
				Result["templates"] = BuildRundownPageArray(State, Rundown, Rundown->GetTemplatePages());
				Result["pages"] = BuildRundownPageArray(State, Rundown, Rundown->GetInstancedPages());

				sol::table SubLists = State.create_table();
				const TArray<FAvaRundownSubList>& EngineSubLists = Rundown->GetSubLists();
				for (int32 Index = 0; Index < EngineSubLists.Num(); ++Index)
				{
					SubLists[Index + 1] = BuildRundownSubListInfo(State, EngineSubLists[Index], Index);
				}
				Result["sublists"] = SubLists;
				return Result;
			}

			EAvaRundownPagePlayType ParseRundownPlayType(const std::string& Value)
			{
				const FString Text = UTF8_TO_TCHAR(Value.c_str());
				if (Text.Equals(TEXT("PreviewFromFrame"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("PreviewFrame"), ESearchCase::IgnoreCase))
				{
					return EAvaRundownPagePlayType::PreviewFromFrame;
				}
				if (Text.Equals(TEXT("PreviewFromStart"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("Preview"), ESearchCase::IgnoreCase))
				{
					return EAvaRundownPagePlayType::PreviewFromStart;
				}
				return EAvaRundownPagePlayType::PlayFromStart;
			}

			EAvaRundownPageStopOptions ParseRundownStopOptions(sol::optional<sol::table> Options)
			{
				if (Options && Options->get_or("force_no_transition", Options->get_or("force", false)))
				{
					return EAvaRundownPageStopOptions::ForceNoTransition;
				}
				return EAvaRundownPageStopOptions::Default;
			}

			void AddRundownPageIds(sol::state_view State, sol::table& Result, const char* CountKey, const char* ArrayKey, const TArray<int32>& PageIds)
			{
				Result[CountKey] = PageIds.Num();
				sol::table Values = State.create_table();
				for (int32 Index = 0; Index < PageIds.Num(); ++Index)
				{
					Values[Index + 1] = PageIds[Index];
				}
				Result[ArrayKey] = Values;
			}

			sol::table BuildRundownPlaybackInfo(sol::state_view State, UAvaRundown* Rundown)
			{
				sol::table Result = State.create_table();
				if (!Rundown)
				{
					Result["valid"] = false;
					return Result;
				}

				Result["valid"] = true;
				Result["is_playing"] = Rundown->IsPlaying();
				AddRundownPageIds(State, Result, "playing_page_count", "playing_page_ids", Rundown->GetPlayingPageIds());
				AddRundownPageIds(State, Result, "previewing_page_count", "previewing_page_ids", Rundown->GetPreviewingPageIds());

				const TArray<TObjectPtr<UAvaRundownPagePlayer>>& Players = Rundown->GetPagePlayers();
				Result["player_count"] = Players.Num();
				sol::table PlayerInfos = State.create_table();
				for (int32 Index = 0; Index < Players.Num(); ++Index)
				{
					const UAvaRundownPagePlayer* Player = Players[Index];
					sol::table PlayerInfo = State.create_table();
					PlayerInfo["index"] = Index + 1;
					PlayerInfo["valid"] = IsValid(Player);
					if (Player)
					{
						PlayerInfo["page_id"] = Player->PageId;
						PlayerInfo["is_preview"] = Player->bIsPreview;
						PlayerInfo["channel_name"] = std::string(TCHAR_TO_UTF8(*Player->ChannelName));
						PlayerInfo["is_loaded"] = Player->IsLoaded();
						PlayerInfo["is_playing"] = Player->IsPlaying();
						PlayerInfo["instance_player_count"] = Player->GetNumInstancePlayers();

						sol::table Instances = State.create_table();
						for (int32 InstanceIndex = 0; InstanceIndex < Player->GetNumInstancePlayers(); ++InstanceIndex)
						{
							sol::table InstanceInfo = State.create_table();
							const UAvaRundownPlaybackInstancePlayer* InstancePlayer = Player->GetInstancePlayer(InstanceIndex);
							InstanceInfo["index"] = InstanceIndex + 1;
							InstanceInfo["valid"] = IsValid(InstancePlayer);
							if (InstancePlayer)
							{
								InstanceInfo["is_loaded"] = InstancePlayer->IsLoaded();
								InstanceInfo["is_playing"] = InstancePlayer->IsPlaying();
								InstanceInfo["source_asset_path"] = std::string(TCHAR_TO_UTF8(*InstancePlayer->SourceAssetPath.ToString()));
								InstanceInfo["instance_id"] = std::string(TCHAR_TO_UTF8(*InstancePlayer->GetPlaybackInstanceId().ToString(EGuidFormats::DigitsWithHyphens)));
							}
							Instances[InstanceIndex + 1] = InstanceInfo;
						}
						PlayerInfo["instances"] = Instances;
					}
					PlayerInfos[Index + 1] = PlayerInfo;
				}
				Result["players"] = PlayerInfos;
				return Result;
			}

			sol::table BuildRundownLoadInfo(sol::state_view State, const TArray<UAvaRundown::FLoadedInstanceInfo>& Loaded)
			{
				sol::table Result = State.create_table();
				Result["loaded_count"] = Loaded.Num();
				sol::table Instances = State.create_table();
				for (int32 Index = 0; Index < Loaded.Num(); ++Index)
				{
					sol::table Entry = State.create_table();
					Entry["instance_id"] = std::string(TCHAR_TO_UTF8(*Loaded[Index].InstanceId.ToString(EGuidFormats::DigitsWithHyphens)));
					Entry["asset_path"] = std::string(TCHAR_TO_UTF8(*Loaded[Index].AssetPath.ToString()));
					Instances[Index + 1] = Entry;
				}
				Result["instances"] = Instances;
				return Result;
			}

			FAvaRundownPageListReference ResolveSubListReference(UAvaRundown* Rundown, int32 OneBasedIndex)
			{
				if (!Rundown || OneBasedIndex <= 0 || !Rundown->GetSubLists().IsValidIndex(OneBasedIndex - 1))
				{
					return FAvaRundownPageListReference();
				}
				return UAvaRundown::CreateSubListReference(Rundown->GetSubLists()[OneBasedIndex - 1]);
			}

			bool AreOrderIndicesValid(const TArray<int32>& Indices, int32 Count)
			{
				for (const int32 Index : Indices)
				{
					if (Index < 0 || Index >= Count)
					{
						return false;
					}
				}
				return true;
			}

			const TCHAR* BroadcastChannelTypeToString(EAvaBroadcastChannelType Type)
			{
				switch (Type)
				{
				case EAvaBroadcastChannelType::Preview:
					return TEXT("Preview");
				case EAvaBroadcastChannelType::Program:
				default:
					return TEXT("Program");
				}
			}

			EAvaBroadcastChannelType ParseBroadcastChannelType(const std::string& Value, EAvaBroadcastChannelType DefaultValue)
			{
				const FString Text = UTF8_TO_TCHAR(Value.c_str());
				if (Text.Equals(TEXT("Preview"), ESearchCase::IgnoreCase))
				{
					return EAvaBroadcastChannelType::Preview;
				}
				if (Text.Equals(TEXT("Program"), ESearchCase::IgnoreCase))
				{
					return EAvaBroadcastChannelType::Program;
				}
				return DefaultValue;
			}

			const TCHAR* BroadcastChannelStateToString(EAvaBroadcastChannelState State)
			{
				switch (State)
				{
				case EAvaBroadcastChannelState::Live:
					return TEXT("Live");
				case EAvaBroadcastChannelState::Offline:
					return TEXT("Offline");
				case EAvaBroadcastChannelState::Idle:
				default:
					return TEXT("Idle");
				}
			}

			sol::table BuildQualityFeatureInfo(sol::state_view State, const FAvaViewportQualitySettingsFeature& Feature)
			{
				sol::table Result = State.create_table();
				Result["name"] = std::string(TCHAR_TO_UTF8(*Feature.Name));
				Result["enabled"] = Feature.bEnabled;
				return Result;
			}

			sol::table BuildQualitySettingsInfo(sol::state_view State, const FAvaViewportQualitySettings& QualitySettings)
			{
				sol::table Result = State.create_table();
				Result["feature_count"] = QualitySettings.Features.Num();
				sol::table Features = State.create_table();
				for (int32 Index = 0; Index < QualitySettings.Features.Num(); ++Index)
				{
					Features[Index + 1] = BuildQualityFeatureInfo(State, QualitySettings.Features[Index]);
				}
				Result["features"] = Features;
				return Result;
			}

			sol::table BuildBroadcastMediaOutputInfo(sol::state_view State, const FAvaBroadcastOutputChannel& Channel, UMediaOutput* MediaOutput, int32 Index)
			{
				sol::table Result = State.create_table();
				Result["valid"] = IsValid(MediaOutput);
				Result["index"] = Index + 1;
				if (!IsValid(MediaOutput))
				{
					Result["class"] = std::string();
					Result["name"] = std::string();
					Result["properties"] = State.create_table();
					return Result;
				}

				const FAvaBroadcastMediaOutputInfo& OutputInfo = Channel.GetMediaOutputInfo(MediaOutput);
				Result["class"] = std::string(TCHAR_TO_UTF8(*MediaOutput->GetClass()->GetPathName()));
				Result["name"] = std::string(TCHAR_TO_UTF8(*MediaOutput->GetName()));
				Result["server_name"] = std::string(TCHAR_TO_UTF8(*OutputInfo.ServerName));
				Result["device_provider_name"] = std::string(TCHAR_TO_UTF8(*OutputInfo.DeviceProviderName.ToString()));
				Result["device_name"] = std::string(TCHAR_TO_UTF8(*OutputInfo.DeviceName.ToString()));
				Result["is_remote"] = OutputInfo.IsRemote();
				Result["is_info_valid"] = OutputInfo.IsValid();
				Result["state"] = static_cast<int32>(Channel.GetMediaOutputState(MediaOutput));
				Result["properties"] = NeoLuaProperty::ReadAsTable(MediaOutput, State);
				return Result;
			}

			sol::table BuildBroadcastChannelInfo(sol::state_view State, const UAvaBroadcast& Broadcast, const FAvaBroadcastOutputChannel* Channel)
			{
				sol::table Result = State.create_table();
				if (!Channel || !Channel->IsValidChannel())
				{
					Result["valid"] = false;
					Result["media_output_count"] = 0;
					Result["media_outputs"] = State.create_table();
					Result["quality"] = State.create_table();
					return Result;
				}

				const FName ChannelName = Channel->GetChannelName();
				Result["valid"] = true;
				Result["name"] = std::string(TCHAR_TO_UTF8(*ChannelName.ToString()));
				Result["profile_name"] = std::string(TCHAR_TO_UTF8(*Channel->GetProfileName().ToString()));
				Result["index"] = Channel->GetChannelIndex();
				Result["type"] = std::string(TCHAR_TO_UTF8(BroadcastChannelTypeToString(Broadcast.GetChannelType(ChannelName))));
				Result["state"] = std::string(TCHAR_TO_UTF8(BroadcastChannelStateToString(Channel->GetState())));
				Result["is_pinned"] = Broadcast.IsChannelPinned(ChannelName);
				Result["pinned_profile_name"] = std::string(TCHAR_TO_UTF8(*Broadcast.GetPinnedChannelProfileName(ChannelName).ToString()));
				Result["media_output_count"] = Channel->GetMediaOutputs().Num();
				Result["has_local_media_outputs"] = Channel->HasAnyLocalMediaOutputs();
				Result["has_remote_media_outputs"] = Channel->HasAnyRemoteMediaOutputs();
				Result["quality"] = BuildQualitySettingsInfo(State, Channel->GetViewportQualitySettings());
				sol::table MediaOutputs = State.create_table();
				const TArray<UMediaOutput*>& OutputObjects = Channel->GetMediaOutputs();
				for (int32 Index = 0; Index < OutputObjects.Num(); ++Index)
				{
					MediaOutputs[Index + 1] = BuildBroadcastMediaOutputInfo(State, *Channel, OutputObjects[Index], Index);
				}
				Result["media_outputs"] = MediaOutputs;
				return Result;
			}

			sol::table BuildBroadcastProfileInfo(sol::state_view State, const UAvaBroadcast& Broadcast, const FAvaBroadcastProfile& Profile)
			{
				sol::table Result = State.create_table();
				if (!Profile.IsValidProfile())
				{
					Result["valid"] = false;
					Result["channel_count"] = 0;
					Result["channels"] = State.create_table();
					return Result;
				}

				Result["valid"] = true;
				Result["name"] = std::string(TCHAR_TO_UTF8(*Profile.GetName().ToString()));
				Result["is_current"] = Broadcast.GetCurrentProfileName() == Profile.GetName();
				Result["is_broadcasting_any"] = Profile.IsBroadcastingAnyChannel();
				Result["is_broadcasting_all"] = Profile.IsBroadcastingAllChannels();

				const TArray<FAvaBroadcastOutputChannel*>& Channels = Profile.GetChannels();
				Result["channel_count"] = Channels.Num();
				sol::table ChannelInfos = State.create_table();
				for (int32 Index = 0; Index < Channels.Num(); ++Index)
				{
					ChannelInfos[Index + 1] = BuildBroadcastChannelInfo(State, Broadcast, Channels[Index]);
				}
				Result["channels"] = ChannelInfos;
				return Result;
			}

			sol::table BuildBroadcastInfo(sol::state_view State)
			{
				UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
				sol::table Result = State.create_table();
				Result["valid"] = true;
				Result["current_profile_name"] = std::string(TCHAR_TO_UTF8(*Broadcast.GetCurrentProfileName().ToString()));
				Result["profile_count"] = Broadcast.GetProfiles().Num();
				Result["channel_name_count"] = Broadcast.GetChannelNameCount();
				Result["is_broadcasting_any"] = Broadcast.IsBroadcastingAnyChannel();
				Result["is_broadcasting_all"] = Broadcast.IsBroadcastingAllChannels();
#if WITH_EDITOR
				Result["can_show_preview"] = Broadcast.CanShowPreview();
#endif

				sol::table ChannelNames = State.create_table();
				for (int32 Index = 0; Index < Broadcast.GetChannelNameCount(); ++Index)
				{
					ChannelNames[Index + 1] = std::string(TCHAR_TO_UTF8(*Broadcast.GetChannelName(Index).ToString()));
				}
				Result["channel_names"] = ChannelNames;

				sol::table Profiles = State.create_table();
				int32 ProfileIndex = 0;
				for (const TPair<FName, FAvaBroadcastProfile>& Pair : Broadcast.GetProfiles())
				{
					Profiles[++ProfileIndex] = BuildBroadcastProfileInfo(State, Broadcast, Pair.Value);
				}
				Result["profiles"] = Profiles;
				return Result;
			}

			FAvaBroadcastProfile* FindBroadcastProfileMutable(UAvaBroadcast& Broadcast, const std::string& ProfileName)
			{
				if (ProfileName.empty())
				{
					return nullptr;
				}
				FAvaBroadcastProfile& Profile = Broadcast.GetProfile(FName(UTF8_TO_TCHAR(ProfileName.c_str())));
				return Profile.IsValidProfile() ? &Profile : nullptr;
			}

			FAvaBroadcastOutputChannel* FindBroadcastChannelMutable(UAvaBroadcast& Broadcast, const std::string& ProfileName, const std::string& ChannelName)
			{
				FAvaBroadcastProfile* Profile = FindBroadcastProfileMutable(Broadcast, ProfileName);
				if (!Profile || ChannelName.empty())
				{
					return nullptr;
				}
				FAvaBroadcastOutputChannel& Channel = Profile->GetChannelMutable(FName(UTF8_TO_TCHAR(ChannelName.c_str())));
				return Channel.IsValidChannel() ? &Channel : nullptr;
			}

			UClass* ResolveMediaOutputClass(const std::string& ClassName)
			{
				if (ClassName.empty())
				{
					return nullptr;
				}

				const FString ClassText = UTF8_TO_TCHAR(ClassName.c_str());
				UClass* Class = FindObject<UClass>(nullptr, *ClassText);
				if (!Class)
				{
					Class = LoadObject<UClass>(nullptr, *ClassText);
				}
				if (!Class && !ClassText.StartsWith(TEXT("/Script/")))
				{
					const FString AvalancheClassPath = FString::Printf(TEXT("/Script/AvalancheMedia.%s"), *ClassText);
					Class = LoadObject<UClass>(nullptr, *AvalancheClassPath);
				}
				return Class && Class->IsChildOf(UMediaOutput::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract) ? Class : nullptr;
			}

			UMediaOutput* GetBroadcastMediaOutputByLuaIndex(FAvaBroadcastOutputChannel& Channel, int32 LuaIndex)
			{
				const int32 Index = LuaIndex - 1;
				const TArray<UMediaOutput*>& Outputs = Channel.GetMediaOutputs();
				return Outputs.IsValidIndex(Index) ? Outputs[Index] : nullptr;
			}

			void ApplyBroadcastMediaOutputInfoOptions(FAvaBroadcastMediaOutputInfo& OutputInfo, const sol::table& Options)
			{
				if (sol::optional<std::string> ServerName = Options.get<sol::optional<std::string>>("server_name"))
				{
					OutputInfo.ServerName = UTF8_TO_TCHAR(ServerName->c_str());
				}
				if (sol::optional<std::string> DeviceProviderName = Options.get<sol::optional<std::string>>("device_provider_name"))
				{
					OutputInfo.DeviceProviderName = FName(UTF8_TO_TCHAR(DeviceProviderName->c_str()));
				}
				if (sol::optional<std::string> DeviceName = Options.get<sol::optional<std::string>>("device_name"))
				{
					OutputInfo.DeviceName = FName(UTF8_TO_TCHAR(DeviceName->c_str()));
				}
			}

			bool ApplyBroadcastMediaOutputProperties(UMediaOutput* MediaOutput, const sol::table& Options, FString& OutError)
			{
				if (!IsValid(MediaOutput))
				{
					OutError = TEXT("Invalid media output.");
					return false;
				}

				sol::optional<sol::table> Properties = Options.get<sol::optional<sol::table>>("properties");
				if (!Properties)
				{
					return true;
				}

				TArray<FString> Warnings;
				if (!NeoLuaProperty::ApplyTable(MediaOutput, *Properties, OutError, &Warnings))
				{
					return false;
				}
				return true;
			}

			void ApplyQualityOptions(FAvaViewportQualitySettings& QualitySettings, const sol::table& Options)
			{
				if (sol::optional<bool> bAllFeatures = Options.get<sol::optional<bool>>("all_features"))
				{
					QualitySettings = FAvaViewportQualitySettings::All(*bAllFeatures);
				}
				if (sol::optional<std::string> Preset = Options.get<sol::optional<std::string>>("preset"))
				{
					QualitySettings = FAvaViewportQualitySettings::Preset(FText::FromString(UTF8_TO_TCHAR(Preset->c_str())));
				}
				if (sol::optional<sol::table> Features = Options.get<sol::optional<sol::table>>("features"))
				{
					for (int32 LuaIndex = 1;; ++LuaIndex)
					{
						sol::object Entry = (*Features)[LuaIndex];
						if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
						{
							break;
						}
						if (!Entry.is<sol::table>())
						{
							continue;
						}
						sol::table FeatureTable = Entry.as<sol::table>();
						const std::string FeatureName = FeatureTable.get_or<std::string>("name", "");
						if (FeatureName.empty())
						{
							continue;
						}
						if (FAvaViewportQualitySettingsFeature* Feature = FAvaViewportQualitySettings::FindFeatureByName(QualitySettings.Features, UTF8_TO_TCHAR(FeatureName.c_str())))
						{
							Feature->bEnabled = FeatureTable.get_or("enabled", Feature->bEnabled);
						}
					}
				}
				QualitySettings.VerifyIntegrity();
				QualitySettings.SortFeaturesByDisplayText();
			}

			UAvaPlaybackGraph* ResolvePlaybackGraph(const sol::table& AssetObj)
			{
				const std::string Path = AssetObj.get_or<std::string>("path", "");
				if (Path.empty())
				{
					return nullptr;
				}
				return LoadObject<UAvaPlaybackGraph>(nullptr, UTF8_TO_TCHAR(Path.c_str()));
			}

			const TArray<TObjectPtr<UAvaPlaybackNode>>& GetPlaybackNodeList(const UAvaPlaybackGraph* Graph)
			{
#if WITH_EDITOR
				static const TArray<TObjectPtr<UAvaPlaybackNode>> Empty;
				return Graph ? Graph->GetPlaybackNodes() : Empty;
#else
				static const TArray<TObjectPtr<UAvaPlaybackNode>> Empty;
				return Empty;
#endif
			}

			UAvaPlaybackNodeRoot* FindPlaybackRootNode(UAvaPlaybackGraph* Graph)
			{
				if (!Graph)
				{
					return nullptr;
				}
				if (UAvaPlaybackNodeRoot* Root = Graph->GetRootNode())
				{
					return Root;
				}
				for (const TObjectPtr<UAvaPlaybackNode>& Node : GetPlaybackNodeList(Graph))
				{
					if (UAvaPlaybackNodeRoot* Root = Cast<UAvaPlaybackNodeRoot>(Node.Get()))
					{
						Graph->SetRootNode(Root);
						return Root;
					}
				}
				return nullptr;
			}

			int32 FindPlaybackNodeIndex(const UAvaPlaybackGraph* Graph, const UAvaPlaybackNode* Node)
			{
				const TArray<TObjectPtr<UAvaPlaybackNode>>& Nodes = GetPlaybackNodeList(Graph);
				for (int32 Index = 0; Index < Nodes.Num(); ++Index)
				{
					if (Nodes[Index] == Node)
					{
						return Index + 1;
					}
				}
				return 0;
			}

			int32 ResolveBroadcastChannelIndex(const std::string& ChannelName)
			{
				const UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
				const int32 ChannelCount = Broadcast.GetChannelNameCount();
				if (ChannelCount <= 0)
				{
					return INDEX_NONE;
				}
				if (ChannelName.empty())
				{
					return 0;
				}
				const FName DesiredName(UTF8_TO_TCHAR(ChannelName.c_str()));
				for (int32 Index = 0; Index < ChannelCount; ++Index)
				{
					if (Broadcast.GetChannelName(Index) == DesiredName)
					{
						return Index;
					}
				}
				return INDEX_NONE;
			}

				sol::table BuildPlayAnimationEntries(sol::state_view State, const UAvaPlaybackNode* Node)
				{
					sol::table Entries = State.create_table();
					if (!Node)
					{
						Entries["count"] = 0;
						return Entries;
					}

					FMapProperty* AnimationMapProperty = FindFProperty<FMapProperty>(Node->GetClass(), TEXT("AnimationMap"));
					if (!AnimationMapProperty)
					{
						Entries["count"] = 0;
						return Entries;
					}

					FScriptMapHelper Helper(AnimationMapProperty, AnimationMapProperty->ContainerPtrToValuePtr<void>(const_cast<UAvaPlaybackNode*>(Node)));
					int32 EntryIndex = 0;
					for (FScriptMapHelper::FIterator It(Helper); It; ++It)
					{
						const FSoftObjectPath* AssetPath = reinterpret_cast<const FSoftObjectPath*>(Helper.GetKeyPtr(It));
						const FAvaPlaybackAnimations* Animations = reinterpret_cast<const FAvaPlaybackAnimations*>(Helper.GetValuePtr(It));
						sol::table Entry = State.create_table();
						Entry["asset_path"] = AssetPath ? std::string(TCHAR_TO_UTF8(*AssetPath->ToString())) : std::string();
						sol::table AnimationRows = State.create_table();
						int32 AnimationIndex = 0;
						if (Animations)
						{
							for (const FAvaPlaybackAnimPlaySettings& Settings : Animations->AvailableAnimations)
							{
								sol::table Row = State.create_table();
								Row["animation_name"] = std::string(TCHAR_TO_UTF8(*Settings.AnimationName.ToString()));
								Row["action"] = std::string(TCHAR_TO_UTF8(*UEnum::GetValueAsString(Settings.Action)));
								Row["start_at_time"] = Settings.StartAtTime;
								Row["loop_count"] = Settings.LoopCount;
								Row["playback_speed"] = Settings.PlaybackSpeed;
								Row["restore_state"] = Settings.bRestoreState;
								AnimationRows[++AnimationIndex] = Row;
							}
						}
						Entry["animation_count"] = AnimationIndex;
						Entry["animations"] = AnimationRows;
						Entries[++EntryIndex] = Entry;
					}
					Entries["count"] = EntryIndex;
					return Entries;
				}

				sol::table BuildPlaybackNodeInfo(sol::state_view State, const UAvaPlaybackGraph* Graph, const UAvaPlaybackNode* Node)
				{
				sol::table Result = State.create_table();
				if (!Node)
				{
					Result["valid"] = false;
					return Result;
				}

				Result["valid"] = true;
				Result["index"] = FindPlaybackNodeIndex(Graph, Node);
				Result["class"] = std::string(TCHAR_TO_UTF8(*Node->GetClass()->GetName()));
				Result["display_name"] = std::string(TCHAR_TO_UTF8(*Node->GetNodeDisplayNameText().ToString()));
				Result["category"] = std::string(TCHAR_TO_UTF8(*Node->GetNodeCategoryText().ToString()));
				Result["is_root"] = Node->IsA<UAvaPlaybackNodeRoot>();
				Result["is_player"] = Node->IsA<UAvaPlaybackNodePlayer>();
				Result["properties"] = NeoLuaProperty::ReadAsTable(const_cast<UAvaPlaybackNode*>(Node), State);
				Result["min_child_count"] = Node->GetMinChildNodes();
				Result["max_child_count"] = Node->GetMaxChildNodes();
				Result["last_time_ticked"] = Node->GetLastTimeTicked();

				const TArray<TObjectPtr<UAvaPlaybackNode>>& Children = Node->GetChildNodes();
				Result["child_count"] = Children.Num();
				sol::table ChildIndices = State.create_table();
				sol::table ChildLastTick = State.create_table();
				for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
				{
					ChildIndices[ChildIndex + 1] = FindPlaybackNodeIndex(Graph, Children[ChildIndex].Get());
					ChildLastTick[ChildIndex + 1] = Node->GetChildLastTimeTicked(ChildIndex);
				}
				Result["children"] = ChildIndices;
				Result["child_last_time_ticked"] = ChildLastTick;

					if (const UAvaPlaybackNodePlayer* Player = Cast<UAvaPlaybackNodePlayer>(Node))
					{
						Result["asset_path"] = std::string(TCHAR_TO_UTF8(*Player->GetAssetPath().ToString()));
						Result["load_options"] = std::string(TCHAR_TO_UTF8(*Player->GetLoadOptions()));
						Result["has_source_asset"] = Graph ? Graph->HasPlayerNodeForSourceAsset(Player->GetAssetPath()) : false;
					}
					if (Node->GetClass()->GetName().Contains(TEXT("PlayAnim")))
					{
						Result["play_animation_entries"] = BuildPlayAnimationEntries(State, Node);
					}

					return Result;
				}

				sol::table BuildPlayableSequencePlayerInfo(sol::state_view State, UAvaSequencePlayer* Player)
				{
					sol::table Result = State.create_table();
					UAvaSequence* Sequence = Player ? Player->GetAvaSequence() : nullptr;
					Result["valid"] = Player != nullptr;
					Result["player_class"] = Player ? std::string(TCHAR_TO_UTF8(*Player->GetClass()->GetName())) : std::string();
					Result["sequence_label"] = Sequence ? std::string(TCHAR_TO_UTF8(*Sequence->GetLabel().ToString())) : std::string();
					Result["sequence_name"] = Sequence ? std::string(TCHAR_TO_UTF8(*Sequence->GetName())) : std::string();
					Result["status"] = Player ? std::string(TCHAR_TO_UTF8(*UEnum::GetValueAsString(Player->GetPlaybackStatus()))) : std::string();
					Result["is_playing"] = Player ? Player->IsPlaying() : false;
					Result["play_rate"] = Player ? Player->GetPlayRate() : 0.0f;
					Result["current_seconds"] = Player ? Player->GetCurrentTime().AsSeconds() : 0.0;
					Result["global_seconds"] = Player ? Player->GetGlobalTime().AsSeconds() : 0.0;
					return Result;
				}

				EAvaPlaybackAnimAction ParsePlaybackAnimAction(const sol::table& Options)
				{
					if (sol::optional<int> NumericAction = Options.get<sol::optional<int>>("action"))
					{
						switch (*NumericAction)
						{
						case 1: return EAvaPlaybackAnimAction::Play;
						case 2: return EAvaPlaybackAnimAction::Continue;
						case 3: return EAvaPlaybackAnimAction::Stop;
						case 4: return EAvaPlaybackAnimAction::PreviewFrame;
						default: return EAvaPlaybackAnimAction::None;
						}
					}
					const FString Action = UTF8_TO_TCHAR(Options.get_or<std::string>("action", "Play").c_str());
					if (Action.Equals(TEXT("Play"), ESearchCase::IgnoreCase)) return EAvaPlaybackAnimAction::Play;
					if (Action.Equals(TEXT("Continue"), ESearchCase::IgnoreCase)) return EAvaPlaybackAnimAction::Continue;
					if (Action.Equals(TEXT("Stop"), ESearchCase::IgnoreCase)) return EAvaPlaybackAnimAction::Stop;
					if (Action.Equals(TEXT("PreviewFrame"), ESearchCase::IgnoreCase)) return EAvaPlaybackAnimAction::PreviewFrame;
					return EAvaPlaybackAnimAction::None;
				}

				EAvaSequencePlayMode ParseSequencePlayMode(const sol::table& Options)
				{
					if (sol::optional<int> NumericMode = Options.get<sol::optional<int>>("play_mode"))
					{
						return *NumericMode == 1 ? EAvaSequencePlayMode::Reverse : EAvaSequencePlayMode::Forward;
					}
					const FString Mode = UTF8_TO_TCHAR(Options.get_or<std::string>("play_mode", "Forward").c_str());
					return Mode.Equals(TEXT("Reverse"), ESearchCase::IgnoreCase) ? EAvaSequencePlayMode::Reverse : EAvaSequencePlayMode::Forward;
				}

				bool ConfigurePlayAnimationNode(UAvaPlaybackNode* Node, const sol::table& Options)
				{
					if (!Node || !Node->GetClass()->GetName().Contains(TEXT("PlayAnim")))
					{
						return false;
					}

					const std::string AssetPathText = Options.get_or<std::string>("asset_path", "");
					std::string AnimationNameText;
					if (sol::optional<std::string> AnimationName = Options.get<sol::optional<std::string>>("animation_name"))
					{
						AnimationNameText = *AnimationName;
					}
					else
					{
						AnimationNameText = Options.get_or<std::string>("sequence_name", "");
					}
					if (AssetPathText.empty() || AnimationNameText.empty())
					{
						return false;
					}

					FMapProperty* AnimationMapProperty = FindFProperty<FMapProperty>(Node->GetClass(), TEXT("AnimationMap"));
					if (!AnimationMapProperty)
					{
						return false;
					}

					FSoftObjectPath AssetPath(UTF8_TO_TCHAR(AssetPathText.c_str()));
					FAvaPlaybackAnimPlaySettings Settings(FName(UTF8_TO_TCHAR(AnimationNameText.c_str())));
					Settings.Action = ParsePlaybackAnimAction(Options);
					Settings.StartAtTime = static_cast<float>(Options.get_or("start_at_time", 0.0));
					Settings.LoopCount = Options.get_or("loop_count", 0);
					Settings.PlayMode = ParseSequencePlayMode(Options);
					Settings.PlaybackSpeed = static_cast<float>(Options.get_or("playback_speed", 1.0));
					Settings.bRestoreState = Options.get_or("restore_state", false);
					if (Settings.Action == EAvaPlaybackAnimAction::None)
					{
						return false;
					}

					FScriptMapHelper Helper(AnimationMapProperty, AnimationMapProperty->ContainerPtrToValuePtr<void>(Node));
					int32 TargetIndex = INDEX_NONE;
					for (FScriptMapHelper::FIterator It(Helper); It; ++It)
					{
						const FSoftObjectPath* ExistingPath = reinterpret_cast<const FSoftObjectPath*>(Helper.GetKeyPtr(It));
						if (ExistingPath && *ExistingPath == AssetPath)
						{
							TargetIndex = It.GetInternalIndex();
							break;
						}
					}

					if (TargetIndex == INDEX_NONE)
					{
						TargetIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
						FSoftObjectPath* NewPath = reinterpret_cast<FSoftObjectPath*>(Helper.GetKeyPtr(TargetIndex));
						*NewPath = AssetPath;
					}

					FAvaPlaybackAnimations* Animations = reinterpret_cast<FAvaPlaybackAnimations*>(Helper.GetValuePtr(TargetIndex));
					if (!Animations)
					{
						Helper.Rehash();
						return false;
					}
					Animations->AvailableAnimations.Remove(Settings);
					Animations->AvailableAnimations.Add(Settings);
					Helper.Rehash();
					Node->RefreshNode(false);
					return true;
				}

				UClass* ResolvePlaybackNodeClass(const std::string& ClassName)
				{
				if (ClassName.empty())
				{
					return nullptr;
				}

				const FString Text = UTF8_TO_TCHAR(ClassName.c_str());
				static const TMap<FString, FString> Aliases = {
					{ TEXT("Switcher"), TEXT("/Script/AvalancheMedia.AvaPlaybackNodeSwitcher") },
					{ TEXT("Combiner"), TEXT("/Script/AvalancheMedia.AvaPlaybackNodeCombiner") },
					{ TEXT("BeginPlay"), TEXT("/Script/AvalancheMedia.AvaPlaybackNode_BeginPlay") },
					{ TEXT("KeyInput"), TEXT("/Script/AvalancheMedia.AvaPlaybackNode_KeyInput") },
					{ TEXT("Hub"), TEXT("/Script/AvalancheMedia.AvaPlaybackNode_Hub") },
					{ TEXT("PreloadPlayer"), TEXT("/Script/AvalancheMedia.AvaPlaybackNode_PreloadPlayer") },
					{ TEXT("PlayAnim"), TEXT("/Script/AvalancheMedia.AvaPlaybackNode_PlayAnim") },
					{ TEXT("LevelPlayer"), TEXT("/Script/AvalancheMedia.AvaPlaybackNodeLevelPlayer") },
				};

				FString ClassPath = Text;
				if (const FString* AliasPath = Aliases.Find(Text))
				{
					ClassPath = *AliasPath;
				}
				else if (!ClassPath.StartsWith(TEXT("/Script/")) && !ClassPath.StartsWith(TEXT("Class'")))
				{
					ClassPath = FString::Printf(TEXT("/Script/AvalancheMedia.%s"), *Text);
				}

				UClass* Class = FindObject<UClass>(nullptr, *ClassPath);
				if (!Class)
				{
					Class = LoadObject<UClass>(nullptr, *ClassPath);
				}
				return Class && Class->IsChildOf(UAvaPlaybackNode::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract) ? Class : nullptr;
			}

			bool ApplyPlaybackNodeProperties(UAvaPlaybackNode* Node, const sol::table& Options)
			{
				if (!Node)
				{
					return false;
				}
				sol::optional<sol::table> Properties = Options.get<sol::optional<sol::table>>("properties");
				if (!Properties)
				{
					return true;
				}
				FString Error;
				TArray<FString> Warnings;
				if (!NeoLuaProperty::ApplyTable(Node, *Properties, Error, &Warnings))
				{
					return false;
				}
				Node->RefreshNode(false);
				return true;
			}

			UAvaPlaybackNode* ConstructPlaybackNodeOfClass(UAvaPlaybackGraph* Graph, UClass* NodeClass, const sol::table& Options)
			{
				if (!Graph || !NodeClass)
				{
					return nullptr;
				}
				UAvaPlaybackNode* Node = Graph->ConstructPlaybackNode<UAvaPlaybackNode>(NodeClass, false);
				if (!Node)
				{
					return nullptr;
				}
				Node->Modify();
				if (!ApplyPlaybackNodeProperties(Node, Options))
				{
					return nullptr;
				}
				Graph->MarkPackageDirty();
				return Node;
			}

			UAvaPlaybackNode* FindPlaybackNodeByLuaIndex(UAvaPlaybackGraph* Graph, int32 LuaIndex)
			{
				const int32 Index = LuaIndex - 1;
				const TArray<TObjectPtr<UAvaPlaybackNode>>& Nodes = GetPlaybackNodeList(Graph);
				return Nodes.IsValidIndex(Index) ? Nodes[Index].Get() : nullptr;
			}

			bool ConnectPlaybackNodeChild(UAvaPlaybackGraph* Graph, UAvaPlaybackNode* Parent, int32 LuaChildSlot, UAvaPlaybackNode* Child)
			{
				if (!Graph || !Parent || !Child || LuaChildSlot <= 0)
				{
					return false;
				}

				const int32 ChildSlot = LuaChildSlot - 1;
				const int32 MaxChildren = Parent->GetMaxChildNodes();
				const int32 MinChildren = Parent->GetMinChildNodes();
				if (ChildSlot < 0 || ChildSlot >= MaxChildren)
				{
					return false;
				}

				TArray<UAvaPlaybackNode*> Children;
				const TArray<TObjectPtr<UAvaPlaybackNode>>& ExistingChildren = Parent->GetChildNodes();
				const int32 DesiredCount = FMath::Max(ChildSlot + 1, FMath::Max(MinChildren, ExistingChildren.Num()));
				if (DesiredCount > MaxChildren)
				{
					return false;
				}
				Children.SetNum(DesiredCount);
				for (int32 Index = 0; Index < ExistingChildren.Num() && Index < DesiredCount; ++Index)
				{
					Children[Index] = ExistingChildren[Index].Get();
				}
				Children[ChildSlot] = Child;
				Parent->Modify();
				Parent->SetChildNodes(MoveTemp(Children));
				Parent->RefreshNode(false);
				Graph->MarkPackageDirty();
				return Parent->GetChildNodes().IsValidIndex(ChildSlot) && Parent->GetChildNodes()[ChildSlot] == Child;
			}

			void AddPlaybackWorldSceneDiagnostics(sol::state_view State, sol::table& Info, const TCHAR* Prefix, UWorld* World)
			{
				auto Key = [Prefix](const TCHAR* Suffix) -> std::string
				{
					return std::string(TCHAR_TO_UTF8(*FString::Printf(TEXT("%s_%s"), Prefix, Suffix)));
				};

				Info[Key(TEXT("loaded"))] = World != nullptr;
				Info[Key(TEXT("name"))] = World ? std::string(TCHAR_TO_UTF8(*World->GetName())) : std::string();
				Info[Key(TEXT("level_count"))] = World ? World->GetLevels().Num() : 0;

				int32 SceneCount = 0;
				int32 SceneSequenceCount = 0;
				sol::table SceneInfos = State.create_table();
				if (World)
				{
					for (ULevel* Level : World->GetLevels())
					{
						if (!Level)
						{
							continue;
						}
						for (AActor* Actor : Level->Actors)
						{
							if (AAvaScene* AvaScene = Cast<AAvaScene>(Actor))
							{
								++SceneCount;
								SceneSequenceCount += AvaScene->GetSequences().Num();
								sol::table SceneInfo = State.create_table();
								SceneInfo["name"] = std::string(TCHAR_TO_UTF8(*AvaScene->GetName()));
								SceneInfo["path"] = std::string(TCHAR_TO_UTF8(*AvaScene->GetPathName()));
								SceneInfo["sequence_count"] = AvaScene->GetSequences().Num();
								SceneInfos[SceneCount] = SceneInfo;
							}
						}
					}
				}
				Info[Key(TEXT("scene_count"))] = SceneCount;
				Info[Key(TEXT("scene_sequence_count"))] = SceneSequenceCount;
				Info[Key(TEXT("scenes"))] = SceneInfos;
			}

			sol::table BuildPlaybackGraphInfo(sol::state_view State, UAvaPlaybackGraph* Graph)
			{
				sol::table Result = State.create_table();
				if (!Graph)
				{
					Result["valid"] = false;
					Result["node_count"] = 0;
					Result["player_count"] = 0;
					Result["channel_count"] = UAvaBroadcast::Get().GetChannelNameCount();
					Result["nodes"] = State.create_table();
					return Result;
				}

				UAvaPlaybackNodeRoot* Root = FindPlaybackRootNode(Graph);
				const TArray<TObjectPtr<UAvaPlaybackNode>>& Nodes = GetPlaybackNodeList(Graph);
				sol::table NodeInfos = State.create_table();
				int32 PlayerCount = 0;
				for (int32 Index = 0; Index < Nodes.Num(); ++Index)
				{
					if (Nodes[Index].IsA<UAvaPlaybackNodePlayer>())
					{
						++PlayerCount;
					}
					NodeInfos[Index + 1] = BuildPlaybackNodeInfo(State, Graph, Nodes[Index].Get());
				}

				Result["valid"] = true;
				Result["class"] = std::string(TCHAR_TO_UTF8(*Graph->GetClass()->GetName()));
				Result["path"] = std::string(TCHAR_TO_UTF8(*Graph->GetPathName()));
					Result["preview_channel_name"] = std::string(TCHAR_TO_UTF8(*Graph->GetPreviewChannelName().ToString()));
					Result["is_preview_only"] = Graph->IsPreviewOnly();
					Result["is_playing"] = Graph->IsPlaying();
					Result["is_dry_running"] = Graph->IsDryRunningGraph();
					TArray<UAvaPlayable*> Playables;
					Graph->GetAllPlayables(Playables);
					Result["playable_count"] = Playables.Num();
					sol::table PlayableInfos = State.create_table();
					for (int32 PlayableIndex = 0; PlayableIndex < Playables.Num(); ++PlayableIndex)
					{
						UAvaPlayable* Playable = Playables[PlayableIndex];
						sol::table PlayableInfo = State.create_table();
						PlayableInfo["valid"] = Playable != nullptr;
						if (Playable)
						{
							UAvaPlayableGroup* PlayableGroup = Playable->GetPlayableGroup();
							UWorld* PlayWorld = Playable->GetPlayWorld();
							UTextureRenderTarget2D* RenderTarget = PlayableGroup ? PlayableGroup->GetRenderTarget() : nullptr;
							const FSoftObjectPath SourceAssetPath = Playable->GetSourceAssetPath();
							UObject* SourceAsset = SourceAssetPath.ResolveObject();
							if (!SourceAsset && SourceAssetPath.IsValid())
							{
								SourceAsset = SourceAssetPath.TryLoad();
							}
							UWorld* SourceWorld = Cast<UWorld>(SourceAsset);
							PlayableInfo["class"] = std::string(TCHAR_TO_UTF8(*Playable->GetClass()->GetName()));
							PlayableInfo["source_asset_path"] = std::string(TCHAR_TO_UTF8(*SourceAssetPath.ToString()));
							PlayableInfo["status"] = std::string(TCHAR_TO_UTF8(*UEnum::GetValueAsString(Playable->GetPlayableStatus())));
							PlayableInfo["is_playing"] = Playable->IsPlaying();
							PlayableInfo["instance_id"] = std::string(TCHAR_TO_UTF8(*Playable->GetInstanceId().ToString()));
							PlayableInfo["has_group"] = PlayableGroup != nullptr;
							PlayableInfo["channel_name"] = PlayableGroup ? std::string(TCHAR_TO_UTF8(*PlayableGroup->GetChannelName().ToString())) : std::string();
								PlayableInfo["has_play_world"] = PlayWorld != nullptr;
								PlayableInfo["play_world_name"] = PlayWorld ? std::string(TCHAR_TO_UTF8(*PlayWorld->GetName())) : std::string();
								PlayableInfo["group_world_playing"] = PlayableGroup ? PlayableGroup->IsWorldPlaying() : false;
								PlayableInfo["render_target_ready"] = PlayableGroup ? PlayableGroup->IsRenderTargetReady() : false;
								PlayableInfo["has_render_target"] = RenderTarget != nullptr;
								AddPlaybackWorldSceneDiagnostics(State, PlayableInfo, TEXT("source_world"), SourceWorld);
								AddPlaybackWorldSceneDiagnostics(State, PlayableInfo, TEXT("play_world"), PlayWorld);
								IAvaSceneInterface* Scene = Playable->GetSceneInterface();
								IAvaSequencePlaybackObject* SequencePlayback = Scene ? Scene->GetPlaybackObject() : nullptr;
								const IAvaSequenceProvider* SequenceProvider = Scene ? Scene->GetSequenceProvider() : nullptr;
								PlayableInfo["has_scene_interface"] = Scene != nullptr;
								PlayableInfo["play_world_level_count"] = PlayWorld ? PlayWorld->GetLevels().Num() : 0;
								int32 PlayWorldSceneCount = 0;
								int32 PlayWorldSceneSequenceCount = 0;
								if (PlayWorld)
								{
									for (ULevel* Level : PlayWorld->GetLevels())
									{
										if (!Level)
										{
											continue;
										}
										for (AActor* Actor : Level->Actors)
										{
											if (AAvaScene* AvaScene = Cast<AAvaScene>(Actor))
											{
												++PlayWorldSceneCount;
												PlayWorldSceneSequenceCount += AvaScene->GetSequences().Num();
											}
										}
									}
								}
								PlayableInfo["play_world_scene_count"] = PlayWorldSceneCount;
								PlayableInfo["play_world_scene_sequence_count"] = PlayWorldSceneSequenceCount;
								PlayableInfo["sequence_count"] = SequenceProvider ? SequenceProvider->GetSequences().Num() : 0;
								sol::table SequenceInfos = State.create_table();
								if (SequenceProvider)
								{
									const TArray<TObjectPtr<UAvaSequence>>& Sequences = SequenceProvider->GetSequences();
									for (int32 SequenceIndex = 0; SequenceIndex < Sequences.Num(); ++SequenceIndex)
									{
										sol::table SequenceInfo = State.create_table();
										UAvaSequence* Sequence = Sequences[SequenceIndex].Get();
										SequenceInfo["valid"] = Sequence != nullptr;
										SequenceInfo["name"] = Sequence ? std::string(TCHAR_TO_UTF8(*Sequence->GetName())) : std::string();
										SequenceInfo["label"] = Sequence ? std::string(TCHAR_TO_UTF8(*Sequence->GetLabel().ToString())) : std::string();
										SequenceInfos[SequenceIndex + 1] = SequenceInfo;
									}
								}
								PlayableInfo["sequences"] = SequenceInfos;
								TArray<UAvaSequencePlayer*> SequencePlayers = SequencePlayback ? SequencePlayback->GetAllSequencePlayers() : TArray<UAvaSequencePlayer*>();
								PlayableInfo["sequence_player_count"] = SequencePlayers.Num();
								sol::table SequencePlayerInfos = State.create_table();
								for (int32 SequencePlayerIndex = 0; SequencePlayerIndex < SequencePlayers.Num(); ++SequencePlayerIndex)
								{
									SequencePlayerInfos[SequencePlayerIndex + 1] = BuildPlayableSequencePlayerInfo(State, SequencePlayers[SequencePlayerIndex]);
								}
								PlayableInfo["sequence_players"] = SequencePlayerInfos;
							}
							PlayableInfos[PlayableIndex + 1] = PlayableInfo;
						}
					Result["playables"] = PlayableInfos;
					Result["active_game_instance_count"] = Graph->GetActiveGameInstances().Num();
					Result["has_root"] = Root != nullptr;
					Result["root_index"] = FindPlaybackNodeIndex(Graph, Root);
					Result["root_child_count"] = Root ? Root->GetChildNodes().Num() : 0;
				Result["channel_count"] = UAvaBroadcast::Get().GetChannelNameCount();
				Result["node_count"] = Nodes.Num();
				Result["player_count"] = PlayerCount;
				Result["nodes"] = NodeInfos;
				return Result;
			}

			bool PlaybackGraphMeetsWaitTarget(UAvaPlaybackGraph* Graph, int32 MinPlayableCount, int32 MinSequenceCount, int32 MinSequencePlayerCount)
			{
				if (!Graph)
				{
					return false;
				}

				TArray<UAvaPlayable*> Playables;
				Graph->GetAllPlayables(Playables);
				if (Playables.Num() < MinPlayableCount)
				{
					return false;
				}

				int32 SequenceCount = 0;
				int32 SequencePlayerCount = 0;
				for (UAvaPlayable* Playable : Playables)
				{
					if (!Playable)
					{
						continue;
					}

					if (const IAvaSceneInterface* Scene = Playable->GetSceneInterface())
					{
						if (const IAvaSequenceProvider* SequenceProvider = Scene->GetSequenceProvider())
						{
							SequenceCount += SequenceProvider->GetSequences().Num();
						}
						if (const IAvaSequencePlaybackObject* SequencePlayback = Scene->GetPlaybackObject())
						{
							SequencePlayerCount += SequencePlayback->GetAllSequencePlayers().Num();
						}
					}
				}

				return SequenceCount >= MinSequenceCount && SequencePlayerCount >= MinSequencePlayerCount;
			}

			void PumpPlaybackGraphStreaming(UAvaPlaybackGraph* Graph, float DeltaSeconds)
			{
				if (!Graph)
				{
					return;
				}

				TArray<UAvaPlayable*> Playables;
				Graph->GetAllPlayables(Playables);
				for (UAvaPlayable* Playable : Playables)
				{
					UAvaPlayableGroup* PlayableGroup = Playable ? Playable->GetPlayableGroup() : nullptr;
					UWorld* PlayWorld = PlayableGroup ? PlayableGroup->GetPlayWorld() : nullptr;
					if (PlayWorld)
					{
						PlayWorld->UpdateLevelStreaming();
						PlayWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);
						PlayWorld->UpdateLevelStreaming();
					}
				}

				Graph->Tick(FMath::Max(0.0f, DeltaSeconds));
				FTSTicker::GetCoreTicker().Tick(DeltaSeconds);
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().PumpMessages();
					FSlateApplication::Get().Tick();
				}
			}

			UAvaPlaybackNodeRoot* EnsurePlaybackRootNode(UAvaPlaybackGraph* Graph)
			{
				if (!Graph)
				{
					return nullptr;
				}
				if (UAvaPlaybackNodeRoot* ExistingRoot = FindPlaybackRootNode(Graph))
				{
					return ExistingRoot;
				}
				return Graph->ConstructPlaybackNode<UAvaPlaybackNodeRoot>(UAvaPlaybackNodeRoot::StaticClass(), false);
			}

			UAvaPlaybackNodePlayer* ConstructLevelPlayerNode(UAvaPlaybackGraph* Graph, const FSoftObjectPath& LevelPath, const FString& LoadOptions)
			{
				if (!Graph || LevelPath.IsNull())
				{
					return nullptr;
				}

				UClass* LevelPlayerClass = FindObject<UClass>(nullptr, TEXT("/Script/AvalancheMedia.AvaPlaybackNodeLevelPlayer"));
				if (!LevelPlayerClass)
				{
					LevelPlayerClass = LoadObject<UClass>(nullptr, TEXT("/Script/AvalancheMedia.AvaPlaybackNodeLevelPlayer"));
				}
				if (!LevelPlayerClass || !LevelPlayerClass->IsChildOf(UAvaPlaybackNodePlayer::StaticClass()) || LevelPlayerClass->HasAnyClassFlags(CLASS_Abstract))
				{
					return nullptr;
				}

				UAvaPlaybackNodePlayer* Player = Graph->ConstructPlaybackNode<UAvaPlaybackNodePlayer>(LevelPlayerClass, false);
				if (!Player)
				{
					return nullptr;
				}

				Player->Modify();
				if (FSoftObjectProperty* LevelAssetProperty = FindFProperty<FSoftObjectProperty>(Player->GetClass(), TEXT("LevelAsset")))
				{
					if (!LevelAssetProperty->PropertyClass || LevelAssetProperty->PropertyClass->IsChildOf(UWorld::StaticClass()))
					{
						void* ValuePtr = LevelAssetProperty->ContainerPtrToValuePtr<void>(Player);
						LevelAssetProperty->SetPropertyValue(ValuePtr, FSoftObjectPtr(LevelPath));
					}
				}
				Player->SetLoadOptions(LoadOptions);
				return Player;
			}

			bool ConnectPlaybackNodeToRoot(UAvaPlaybackGraph* Graph, UAvaPlaybackNodeRoot* Root, UAvaPlaybackNode* Node, int32 ChannelIndex)
			{
				if (!Graph || !Root || !Node)
				{
					return false;
				}

				const int32 ChannelCount = UAvaBroadcast::Get().GetChannelNameCount();
				if (ChannelIndex < 0 || ChannelIndex >= ChannelCount)
				{
					return false;
				}

				TArray<UAvaPlaybackNode*> Children;
				Children.SetNum(ChannelCount);
				const TArray<TObjectPtr<UAvaPlaybackNode>>& ExistingChildren = Root->GetChildNodes();
				for (int32 Index = 0; Index < ChannelCount && Index < ExistingChildren.Num(); ++Index)
				{
					Children[Index] = ExistingChildren[Index].Get();
				}
				Children[ChannelIndex] = Node;
				Root->Modify();
				Root->SetChildNodes(MoveTemp(Children));
				return Root->GetChildNodes().IsValidIndex(ChannelIndex) && Root->GetChildNodes()[ChannelIndex] == Node;
			}

		const TCHAR* HorizontalToString(EAvaHorizontalAlignment Alignment)
	{
		switch (Alignment)
		{
		case EAvaHorizontalAlignment::Left:
			return TEXT("Left");
		case EAvaHorizontalAlignment::Right:
			return TEXT("Right");
		case EAvaHorizontalAlignment::Center:
		default:
			return TEXT("Center");
		}
	}

	const TCHAR* VerticalToString(EAvaVerticalAlignment Alignment)
	{
		switch (Alignment)
		{
		case EAvaVerticalAlignment::Top:
			return TEXT("Top");
		case EAvaVerticalAlignment::Bottom:
			return TEXT("Bottom");
		case EAvaVerticalAlignment::Center:
		default:
			return TEXT("Center");
		}
	}

	const TCHAR* CornerToString(EAvaShapeCornerType Type)
	{
		switch (Type)
		{
		case EAvaShapeCornerType::CurveIn:
			return TEXT("CurveIn");
		case EAvaShapeCornerType::CurveOut:
			return TEXT("CurveOut");
		case EAvaShapeCornerType::Point:
		default:
			return TEXT("Point");
		}
	}

	EAvaHorizontalAlignment ParseHorizontal(const std::string& Value, EAvaHorizontalAlignment DefaultValue)
	{
		const FString Text = UTF8_TO_TCHAR(Value.c_str());
		if (Text.Equals(TEXT("Left"), ESearchCase::IgnoreCase))
		{
			return EAvaHorizontalAlignment::Left;
		}
		if (Text.Equals(TEXT("Right"), ESearchCase::IgnoreCase))
		{
			return EAvaHorizontalAlignment::Right;
		}
		if (Text.Equals(TEXT("Center"), ESearchCase::IgnoreCase))
		{
			return EAvaHorizontalAlignment::Center;
		}
		return DefaultValue;
	}

	EAvaVerticalAlignment ParseVertical(const std::string& Value, EAvaVerticalAlignment DefaultValue)
	{
		const FString Text = UTF8_TO_TCHAR(Value.c_str());
		if (Text.Equals(TEXT("Top"), ESearchCase::IgnoreCase))
		{
			return EAvaVerticalAlignment::Top;
		}
		if (Text.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase))
		{
			return EAvaVerticalAlignment::Bottom;
		}
		if (Text.Equals(TEXT("Center"), ESearchCase::IgnoreCase))
		{
			return EAvaVerticalAlignment::Center;
		}
		return DefaultValue;
	}

	EAvaShapeCornerType ParseCornerType(const std::string& Value, EAvaShapeCornerType DefaultValue)
	{
		const FString Text = UTF8_TO_TCHAR(Value.c_str());
		if (Text.Equals(TEXT("CurveIn"), ESearchCase::IgnoreCase))
		{
			return EAvaShapeCornerType::CurveIn;
		}
		if (Text.Equals(TEXT("CurveOut"), ESearchCase::IgnoreCase))
		{
			return EAvaShapeCornerType::CurveOut;
		}
		if (Text.Equals(TEXT("Point"), ESearchCase::IgnoreCase))
		{
			return EAvaShapeCornerType::Point;
		}
		return DefaultValue;
	}

	const TCHAR* UVModeToString(EAvaShapeUVMode Mode)
	{
		switch (Mode)
		{
		case EAvaShapeUVMode::Uniform:
			return TEXT("Uniform");
		case EAvaShapeUVMode::Stretch:
		default:
			return TEXT("Stretch");
		}
	}

	EAvaShapeUVMode ParseUVMode(const std::string& Value, EAvaShapeUVMode DefaultValue)
	{
		const FString Text = UTF8_TO_TCHAR(Value.c_str());
		if (Text.Equals(TEXT("Uniform"), ESearchCase::IgnoreCase))
		{
			return EAvaShapeUVMode::Uniform;
		}
		if (Text.Equals(TEXT("Stretch"), ESearchCase::IgnoreCase))
		{
			return EAvaShapeUVMode::Stretch;
		}
		return DefaultValue;
	}

	const TCHAR* ColorStyleToString(EAvaColorStyle Style)
	{
		switch (Style)
		{
		case EAvaColorStyle::Solid:
			return TEXT("Solid");
		case EAvaColorStyle::LinearGradient:
			return TEXT("LinearGradient");
		case EAvaColorStyle::None:
		default:
			return TEXT("None");
		}
	}

	EAvaColorStyle ParseColorStyle(const std::string& Value, EAvaColorStyle DefaultValue)
	{
		const FString Text = UTF8_TO_TCHAR(Value.c_str());
		if (Text.Equals(TEXT("Solid"), ESearchCase::IgnoreCase))
		{
			return EAvaColorStyle::Solid;
		}
		if (Text.Equals(TEXT("LinearGradient"), ESearchCase::IgnoreCase))
		{
			return EAvaColorStyle::LinearGradient;
		}
		if (Text.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			return EAvaColorStyle::None;
		}
		return DefaultValue;
	}

	const TCHAR* MaterialTypeToString(EMaterialType Type)
	{
		switch (Type)
		{
		case EMaterialType::Parametric:
			return TEXT("Parametric");
		case EMaterialType::MaterialDesigner:
			return TEXT("MaterialDesigner");
		case EMaterialType::Asset:
		default:
			return TEXT("Asset");
		}
	}

	const TCHAR* ParametricStyleToString(EAvaShapeParametricMaterialStyle Style)
	{
		switch (Style)
		{
		case EAvaShapeParametricMaterialStyle::LinearGradient:
			return TEXT("LinearGradient");
		case EAvaShapeParametricMaterialStyle::Texture:
			return TEXT("Texture");
		case EAvaShapeParametricMaterialStyle::Solid:
		default:
			return TEXT("Solid");
		}
	}

	EAvaShapeParametricMaterialStyle ParseParametricStyle(const std::string& Value, EAvaShapeParametricMaterialStyle DefaultValue)
	{
		const FString Text = UTF8_TO_TCHAR(Value.c_str());
		if (Text.Equals(TEXT("LinearGradient"), ESearchCase::IgnoreCase))
		{
			return EAvaShapeParametricMaterialStyle::LinearGradient;
		}
		if (Text.Equals(TEXT("Texture"), ESearchCase::IgnoreCase))
		{
			return EAvaShapeParametricMaterialStyle::Texture;
		}
		if (Text.Equals(TEXT("Solid"), ESearchCase::IgnoreCase))
		{
			return EAvaShapeParametricMaterialStyle::Solid;
		}
		return DefaultValue;
	}

	const TCHAR* ParametricTranslucencyToString(EAvaShapeParametricMaterialTranslucency Translucency)
	{
		switch (Translucency)
		{
		case EAvaShapeParametricMaterialTranslucency::Disabled:
			return TEXT("Disabled");
		case EAvaShapeParametricMaterialTranslucency::Enabled:
			return TEXT("Enabled");
		case EAvaShapeParametricMaterialTranslucency::Auto:
		default:
			return TEXT("Auto");
		}
	}

		EAvaShapeParametricMaterialTranslucency ParseParametricTranslucency(const std::string& Value, EAvaShapeParametricMaterialTranslucency DefaultValue)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("Disabled"), ESearchCase::IgnoreCase))
			{
				return EAvaShapeParametricMaterialTranslucency::Disabled;
		}
		if (Text.Equals(TEXT("Enabled"), ESearchCase::IgnoreCase))
		{
			return EAvaShapeParametricMaterialTranslucency::Enabled;
		}
		if (Text.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
		{
			return EAvaShapeParametricMaterialTranslucency::Auto;
			}
			return DefaultValue;
		}

		const TCHAR* TextBevelTypeToString(EText3DBevelType Type)
		{
			switch (Type)
			{
			case EText3DBevelType::HalfCircle:
				return TEXT("HalfCircle");
			case EText3DBevelType::Convex:
				return TEXT("Convex");
			case EText3DBevelType::Concave:
				return TEXT("Concave");
			case EText3DBevelType::OneStep:
				return TEXT("OneStep");
			case EText3DBevelType::TwoSteps:
				return TEXT("TwoSteps");
			case EText3DBevelType::Engraved:
				return TEXT("Engraved");
			case EText3DBevelType::Linear:
			default:
				return TEXT("Linear");
			}
		}

		EText3DBevelType ParseTextBevelType(const std::string& Value, EText3DBevelType DefaultValue)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("HalfCircle"), ESearchCase::IgnoreCase))
			{
				return EText3DBevelType::HalfCircle;
			}
			if (Text.Equals(TEXT("Convex"), ESearchCase::IgnoreCase))
			{
				return EText3DBevelType::Convex;
			}
			if (Text.Equals(TEXT("Concave"), ESearchCase::IgnoreCase))
			{
				return EText3DBevelType::Concave;
			}
			if (Text.Equals(TEXT("OneStep"), ESearchCase::IgnoreCase))
			{
				return EText3DBevelType::OneStep;
			}
			if (Text.Equals(TEXT("TwoSteps"), ESearchCase::IgnoreCase))
			{
				return EText3DBevelType::TwoSteps;
			}
			if (Text.Equals(TEXT("Engraved"), ESearchCase::IgnoreCase))
			{
				return EText3DBevelType::Engraved;
			}
			if (Text.Equals(TEXT("Linear"), ESearchCase::IgnoreCase))
			{
				return EText3DBevelType::Linear;
			}
			return DefaultValue;
		}

		const TCHAR* TextHorizontalToString(EText3DHorizontalTextAlignment Alignment)
		{
			switch (Alignment)
			{
			case EText3DHorizontalTextAlignment::Center:
				return TEXT("Center");
			case EText3DHorizontalTextAlignment::Right:
				return TEXT("Right");
			case EText3DHorizontalTextAlignment::Left:
			default:
				return TEXT("Left");
			}
		}

		EText3DHorizontalTextAlignment ParseTextHorizontal(const std::string& Value, EText3DHorizontalTextAlignment DefaultValue)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("Center"), ESearchCase::IgnoreCase))
			{
				return EText3DHorizontalTextAlignment::Center;
			}
			if (Text.Equals(TEXT("Right"), ESearchCase::IgnoreCase))
			{
				return EText3DHorizontalTextAlignment::Right;
			}
			if (Text.Equals(TEXT("Left"), ESearchCase::IgnoreCase))
			{
				return EText3DHorizontalTextAlignment::Left;
			}
			return DefaultValue;
		}

		const TCHAR* TextVerticalToString(EText3DVerticalTextAlignment Alignment)
		{
			switch (Alignment)
			{
			case EText3DVerticalTextAlignment::Top:
				return TEXT("Top");
			case EText3DVerticalTextAlignment::Center:
				return TEXT("Center");
			case EText3DVerticalTextAlignment::Bottom:
				return TEXT("Bottom");
			case EText3DVerticalTextAlignment::FirstLine:
			default:
				return TEXT("FirstLine");
			}
		}

		EText3DVerticalTextAlignment ParseTextVertical(const std::string& Value, EText3DVerticalTextAlignment DefaultValue)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("Top"), ESearchCase::IgnoreCase))
			{
				return EText3DVerticalTextAlignment::Top;
			}
			if (Text.Equals(TEXT("Center"), ESearchCase::IgnoreCase))
			{
				return EText3DVerticalTextAlignment::Center;
			}
			if (Text.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase))
			{
				return EText3DVerticalTextAlignment::Bottom;
			}
			if (Text.Equals(TEXT("FirstLine"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("First Line"), ESearchCase::IgnoreCase))
			{
				return EText3DVerticalTextAlignment::FirstLine;
			}
			return DefaultValue;
		}

		const TCHAR* TextEffectOrderToString(EText3DCharacterEffectOrder Order)
		{
			switch (Order)
			{
			case EText3DCharacterEffectOrder::FromCenter:
				return TEXT("FromCenter");
			case EText3DCharacterEffectOrder::ToCenter:
				return TEXT("ToCenter");
			case EText3DCharacterEffectOrder::Opposite:
				return TEXT("Opposite");
			case EText3DCharacterEffectOrder::Normal:
			default:
				return TEXT("Normal");
			}
		}

		EText3DCharacterEffectOrder ParseTextEffectOrder(const std::string& Value, EText3DCharacterEffectOrder DefaultValue)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("FromCenter"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("From Center"), ESearchCase::IgnoreCase))
			{
				return EText3DCharacterEffectOrder::FromCenter;
			}
			if (Text.Equals(TEXT("ToCenter"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("To Center"), ESearchCase::IgnoreCase))
			{
				return EText3DCharacterEffectOrder::ToCenter;
			}
			if (Text.Equals(TEXT("Opposite"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("RightToLeft"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("Right To Left"), ESearchCase::IgnoreCase))
			{
				return EText3DCharacterEffectOrder::Opposite;
			}
			if (Text.Equals(TEXT("Normal"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("LeftToRight"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("Left To Right"), ESearchCase::IgnoreCase))
			{
				return EText3DCharacterEffectOrder::Normal;
			}
			return DefaultValue;
		}

		const TCHAR* TextMaterialStyleToString(EText3DMaterialStyle Style)
		{
			switch (Style)
			{
			case EText3DMaterialStyle::Gradient:
				return TEXT("Gradient");
			case EText3DMaterialStyle::Texture:
				return TEXT("Texture");
			case EText3DMaterialStyle::Custom:
				return TEXT("Custom");
			case EText3DMaterialStyle::Solid:
			default:
				return TEXT("Solid");
			}
		}

		EText3DMaterialStyle ParseTextMaterialStyle(const std::string& Value, EText3DMaterialStyle DefaultValue)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("Gradient"), ESearchCase::IgnoreCase))
			{
				return EText3DMaterialStyle::Gradient;
			}
			if (Text.Equals(TEXT("Texture"), ESearchCase::IgnoreCase))
			{
				return EText3DMaterialStyle::Texture;
			}
			if (Text.Equals(TEXT("Custom"), ESearchCase::IgnoreCase))
			{
				return EText3DMaterialStyle::Custom;
			}
			if (Text.Equals(TEXT("Solid"), ESearchCase::IgnoreCase))
			{
				return EText3DMaterialStyle::Solid;
			}
			return DefaultValue;
		}

		const TCHAR* TextMaterialBlendModeToString(EText3DMaterialBlendMode BlendMode)
		{
			switch (BlendMode)
			{
			case EText3DMaterialBlendMode::Translucent:
				return TEXT("Translucent");
			case EText3DMaterialBlendMode::Opaque:
			default:
				return TEXT("Opaque");
			}
		}

		EText3DMaterialBlendMode ParseTextMaterialBlendMode(const std::string& Value, EText3DMaterialBlendMode DefaultValue)
		{
			const FString Text = UTF8_TO_TCHAR(Value.c_str());
			if (Text.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase))
			{
				return EText3DMaterialBlendMode::Translucent;
			}
			if (Text.Equals(TEXT("Opaque"), ESearchCase::IgnoreCase))
			{
				return EText3DMaterialBlendMode::Opaque;
			}
			return DefaultValue;
		}

		sol::table BuildVector2D(sol::state_view Lua, const FVector2D& Value)
		{
			sol::table Result = Lua.create_table();
		Result["x"] = Value.X;
		Result["y"] = Value.Y;
		return Result;
	}

	sol::table BuildVector(sol::state_view Lua, const FVector& Value)
	{
		sol::table Result = Lua.create_table();
		Result["x"] = Value.X;
		Result["y"] = Value.Y;
		Result["z"] = Value.Z;
		return Result;
	}

	sol::table BuildRotator(sol::state_view Lua, const FRotator& Value)
	{
		sol::table Result = Lua.create_table();
		Result["pitch"] = Value.Pitch;
		Result["yaw"] = Value.Yaw;
		Result["roll"] = Value.Roll;
		return Result;
	}

	sol::table BuildLinearColor(sol::state_view Lua, const FLinearColor& Value)
	{
		sol::table Result = Lua.create_table();
		Result["r"] = Value.R;
		Result["g"] = Value.G;
		Result["b"] = Value.B;
		Result["a"] = Value.A;
		return Result;
	}

	FString LuaStringToFString(const std::string& Value)
	{
		return FString(UTF8_TO_TCHAR(Value.c_str()));
	}

	FName LuaStringToFName(const std::string& Value)
	{
		return FName(*LuaStringToFString(Value));
	}

	FFrameRate TableToFrameRate(const sol::table& Table, const FFrameRate& DefaultValue)
	{
		const int32 Numerator = FMath::Max(1, Table.get_or("numerator", Table.get_or("Numerator", DefaultValue.Numerator)));
		const int32 Denominator = FMath::Max(1, Table.get_or("denominator", Table.get_or("Denominator", DefaultValue.Denominator)));
		return FFrameRate(Numerator, Denominator);
	}

	sol::table BuildFrameRateInfo(sol::state_view State, const FFrameRate& Value)
	{
		sol::table Result = State.create_table();
		Result["numerator"] = Value.Numerator;
		Result["denominator"] = Value.Denominator;
		return Result;
	}

	EAvaMarkRole ParseSequencerMarkRole(const std::string& Value, EAvaMarkRole DefaultValue)
	{
		const FString Role = LuaStringToFString(Value);
		if (Role.Equals(TEXT("Stop"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkRole::Stop;
		}
		if (Role.Equals(TEXT("Pause"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkRole::Pause;
		}
		if (Role.Equals(TEXT("Jump"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkRole::Jump;
		}
		if (Role.Equals(TEXT("Reverse"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkRole::Reverse;
		}
		if (Role.Equals(TEXT("None"), ESearchCase::IgnoreCase) || Role.Equals(TEXT("Mark"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkRole::None;
		}
		return DefaultValue;
	}

	EAvaMarkDirection ParseSequencerMarkDirection(const std::string& Value, EAvaMarkDirection DefaultValue)
	{
		const FString Direction = LuaStringToFString(Value);
		if (Direction.Equals(TEXT("Forwards"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("Forward"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkDirection::Forwards;
		}
		if (Direction.Equals(TEXT("Backwards"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("Backward"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkDirection::Backwards;
		}
		if (Direction.Equals(TEXT("Both"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkDirection::Both;
		}
		return DefaultValue;
	}

	EAvaMarkSearchDirection ParseSequencerMarkSearchDirection(const std::string& Value, EAvaMarkSearchDirection DefaultValue)
	{
		const FString Direction = LuaStringToFString(Value);
		if (Direction.Equals(TEXT("SameDirection"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("Same"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkSearchDirection::SameDirection;
		}
		if (Direction.Equals(TEXT("OppositeDirection"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("Opposite"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkSearchDirection::OppositeDirection;
		}
		if (Direction.Equals(TEXT("AbsoluteForwards"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("AbsoluteForward"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkSearchDirection::AbsoluteForwards;
		}
		if (Direction.Equals(TEXT("AbsoluteBackwards"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("AbsoluteBackward"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkSearchDirection::AbsoluteBackwards;
		}
		if (Direction.Equals(TEXT("All"), ESearchCase::IgnoreCase))
		{
			return EAvaMarkSearchDirection::All;
		}
		return DefaultValue;
	}

	const TCHAR* SequencerMarkRoleToString(EAvaMarkRole Role)
	{
		switch (Role)
		{
		case EAvaMarkRole::Stop:
			return TEXT("Stop");
		case EAvaMarkRole::Pause:
			return TEXT("Pause");
		case EAvaMarkRole::Jump:
			return TEXT("Jump");
		case EAvaMarkRole::Reverse:
			return TEXT("Reverse");
		case EAvaMarkRole::None:
		default:
			return TEXT("None");
		}
	}

	const TCHAR* SequencerMarkDirectionToString(EAvaMarkDirection Direction)
	{
		switch (Direction)
		{
		case EAvaMarkDirection::Forwards:
			return TEXT("Forwards");
		case EAvaMarkDirection::Backwards:
			return TEXT("Backwards");
		case EAvaMarkDirection::Both:
		default:
			return TEXT("Both");
		}
	}

	const TCHAR* SequencerMarkSearchDirectionToString(EAvaMarkSearchDirection Direction)
	{
		switch (Direction)
		{
		case EAvaMarkSearchDirection::SameDirection:
			return TEXT("SameDirection");
		case EAvaMarkSearchDirection::OppositeDirection:
			return TEXT("OppositeDirection");
		case EAvaMarkSearchDirection::AbsoluteForwards:
			return TEXT("AbsoluteForwards");
		case EAvaMarkSearchDirection::AbsoluteBackwards:
			return TEXT("AbsoluteBackwards");
		case EAvaMarkSearchDirection::All:
		default:
			return TEXT("All");
		}
	}

	void ApplySequencePresetMarkOptions(FAvaMark& Mark, const sol::table& Options)
	{
		const std::string Role = Options.get_or<std::string>("role", "");
		if (!Role.empty())
		{
			Mark.Role = ParseSequencerMarkRole(Role, Mark.Role);
		}

		const std::string Direction = Options.get_or<std::string>("direction", "");
		if (!Direction.empty())
		{
			Mark.Direction = ParseSequencerMarkDirection(Direction, Mark.Direction);
		}

		if (sol::optional<bool> bLocal = Options.get<sol::optional<bool>>("local"))
		{
			Mark.bIsLocalMark = *bLocal;
		}
		if (sol::optional<bool> bLimitPlayCount = Options.get<sol::optional<bool>>("limit_play_count_enabled"))
		{
			Mark.bLimitPlayCountEnabled = *bLimitPlayCount;
		}
		if (sol::optional<int32> LimitPlayCount = Options.get<sol::optional<int32>>("limit_play_count"))
		{
			Mark.LimitPlayCount = *LimitPlayCount;
		}
		if (sol::optional<float> PauseTime = Options.get<sol::optional<float>>("pause_time"))
		{
			Mark.PauseTime = *PauseTime;
		}

		const std::string JumpLabel = Options.get_or<std::string>("jump_label", "");
		if (!JumpLabel.empty())
		{
			Mark.JumpLabel = LuaStringToFString(JumpLabel);
		}

		const std::string SearchDirection = Options.get_or<std::string>("search_direction", "");
		if (!SearchDirection.empty())
		{
			Mark.SearchDirection = ParseSequencerMarkSearchDirection(SearchDirection, Mark.SearchDirection);
		}
	}

	FAvaMarkSetting TableToSequencePresetMarkSetting(const sol::table& Options)
	{
		FAvaMarkSetting MarkSetting;
		MarkSetting.Label = LuaStringToFString(Options.get_or<std::string>("label", ""));
		MarkSetting.FrameNumber = FMath::Max(0, Options.get_or("frame", Options.get_or("frame_number", MarkSetting.FrameNumber)));
		ApplySequencePresetMarkOptions(MarkSetting.Mark, Options);
		return MarkSetting;
	}

	sol::table BuildSequencePresetMarkInfo(sol::state_view State, const FAvaMarkSetting& MarkSetting)
	{
		sol::table Result = State.create_table();
		Result["label"] = std::string(TCHAR_TO_UTF8(*MarkSetting.Label));
		Result["frame"] = MarkSetting.FrameNumber;
		Result["role"] = std::string(TCHAR_TO_UTF8(SequencerMarkRoleToString(MarkSetting.Mark.Role)));
		Result["direction"] = std::string(TCHAR_TO_UTF8(SequencerMarkDirectionToString(MarkSetting.Mark.Direction)));
		Result["local"] = MarkSetting.Mark.bIsLocalMark;
		Result["limit_play_count_enabled"] = MarkSetting.Mark.bLimitPlayCountEnabled;
		Result["limit_play_count"] = MarkSetting.Mark.LimitPlayCount;
		Result["pause_time"] = MarkSetting.Mark.PauseTime;
		Result["jump_label"] = std::string(TCHAR_TO_UTF8(*MarkSetting.Mark.JumpLabel));
		Result["search_direction"] = std::string(TCHAR_TO_UTF8(SequencerMarkSearchDirectionToString(MarkSetting.Mark.SearchDirection)));
		return Result;
	}

	sol::table BuildSequencePresetInfo(sol::state_view State, const FAvaSequencePreset& Preset)
	{
		sol::table Result = State.create_table();
		Result["name"] = std::string(TCHAR_TO_UTF8(*Preset.PresetName.ToString()));
		Result["sequence_label"] = std::string(TCHAR_TO_UTF8(*Preset.SequenceLabel.ToString()));
		const FAvaTagHandle SequenceTag = Preset.SequenceTag.MakeTagHandle();
		Result["tag_valid"] = SequenceTag.IsValid();
		Result["tag_name"] = std::string(TCHAR_TO_UTF8(*SequenceTag.ToName().ToString()));
		Result["tag_string"] = std::string(TCHAR_TO_UTF8(*SequenceTag.ToString()));
		Result["tag_source"] = std::string(TCHAR_TO_UTF8(*Preset.SequenceTag.Source.ToSoftObjectPath().ToString()));
		Result["end_time"] = Preset.EndTime;
		Result["enable_label"] = Preset.bEnableLabel;
		Result["enable_tag"] = Preset.bEnableTag;
		Result["enable_end_time"] = Preset.bEnableEndTime;
		Result["enable_marks"] = Preset.bEnableMarks;
		Result["mark_count"] = Preset.Marks.Num();
		sol::table Marks = State.create_table();
		for (int32 Index = 0; Index < Preset.Marks.Num(); ++Index)
		{
			Marks[Index + 1] = BuildSequencePresetMarkInfo(State, Preset.Marks[Index]);
		}
		Result["marks"] = Marks;
		return Result;
	}

	sol::table BuildSequencePresetGroupInfo(sol::state_view State, const FAvaSequencePresetGroup& Group)
	{
		sol::table Result = State.create_table();
		Result["name"] = std::string(TCHAR_TO_UTF8(*Group.GroupName.ToString()));
		sol::table PresetNames = State.create_table();
		for (int32 Index = 0; Index < Group.PresetNames.Num(); ++Index)
		{
			PresetNames[Index + 1] = std::string(TCHAR_TO_UTF8(*Group.PresetNames[Index].ToString()));
		}
		Result["preset_name_count"] = Group.PresetNames.Num();
		Result["preset_names"] = PresetNames;
		return Result;
	}

	void SaveSequencerSettings(UAvaSequencerSettings* Settings)
	{
		if (Settings)
		{
			Settings->PostEditChange();
			Settings->SaveConfig();
		}
	}

	void SetSequencerDouble(UAvaSequencerSettings* Settings, const TCHAR* PropertyName, double Value)
	{
		if (FDoubleProperty* Property = FindFProperty<FDoubleProperty>(UAvaSequencerSettings::StaticClass(), PropertyName))
		{
			Property->SetPropertyValue_InContainer(Settings, Value);
		}
	}

	void SetSequencerDisplayRate(UAvaSequencerSettings* Settings, const FFrameRate& FrameRate)
	{
		FStructProperty* DisplayRateProperty = FindFProperty<FStructProperty>(UAvaSequencerSettings::StaticClass(), TEXT("DisplayRate"));
		if (!DisplayRateProperty)
		{
			return;
		}
		void* DisplayRatePtr = DisplayRateProperty->ContainerPtrToValuePtr<void>(Settings);
		if (FStructProperty* FrameRateProperty = FindFProperty<FStructProperty>(DisplayRateProperty->Struct, TEXT("FrameRate")))
		{
			void* FrameRatePtr = FrameRateProperty->ContainerPtrToValuePtr<void>(DisplayRatePtr);
			*reinterpret_cast<FFrameRate*>(FrameRatePtr) = FrameRate;
		}
	}

	bool RemoveNamedSetStruct(UObject* Owner, FSetProperty* SetProperty, const TCHAR* NamePropertyName, FName Name)
	{
		if (!Owner || !SetProperty)
		{
			return false;
		}
		FStructProperty* StructProperty = CastField<FStructProperty>(SetProperty->ElementProp);
		FNameProperty* NameProperty = StructProperty ? FindFProperty<FNameProperty>(StructProperty->Struct, NamePropertyName) : nullptr;
		if (!NameProperty)
		{
			return false;
		}
		void* SetPtr = SetProperty->ContainerPtrToValuePtr<void>(Owner);
		FScriptSetHelper Helper(SetProperty, SetPtr);
		bool bRemoved = false;
		for (int32 Index = Helper.GetMaxIndex() - 1; Index >= 0; --Index)
		{
			if (!Helper.IsValidIndex(Index))
			{
				continue;
			}
			void* ElementPtr = Helper.GetElementPtr(Index);
			if (NameProperty->GetPropertyValue_InContainer(ElementPtr) == Name)
			{
				Helper.RemoveAt(Index);
				bRemoved = true;
			}
		}
		if (bRemoved)
		{
			Helper.Rehash();
		}
		return bRemoved;
	}

	FSetProperty* FindSequencerSetProperty(const TCHAR* PropertyName)
	{
		return FindFProperty<FSetProperty>(UAvaSequencerSettings::StaticClass(), PropertyName);
	}

	FAvaSequencePreset TableToSequencePreset(const sol::table& Options)
	{
		FAvaSequencePreset Preset;
		std::string PresetName;
		if (sol::optional<std::string> Name = Options.get<sol::optional<std::string>>("name"))
		{
			PresetName = *Name;
		}
		else if (sol::optional<std::string> AlternateName = Options.get<sol::optional<std::string>>("preset_name"))
		{
			PresetName = *AlternateName;
		}
		Preset.PresetName = LuaStringToFName(PresetName);
		Preset.SequenceLabel = LuaStringToFName(Options.get_or<std::string>("sequence_label", ""));
		Preset.EndTime = FMath::Max(0.0, Options.get_or("end_time", Preset.EndTime));
		Preset.bEnableLabel = Options.get_or("enable_label", !Preset.SequenceLabel.IsNone());
		Preset.bEnableTag = Options.get_or("enable_tag", false);
		Preset.bEnableEndTime = Options.get_or("enable_end_time", true);
		Preset.bEnableMarks = Options.get_or("enable_marks", false);
		if (sol::optional<sol::table> TagOptions = Options.get<sol::optional<sol::table>>("tag"))
		{
			const std::string CollectionPathValue = TagOptions->get_or<std::string>("collection", "");
			const std::string NameValue = TagOptions->get_or<std::string>("name", "");
			UAvaTagCollection* Collection = LoadTagCollection(CollectionPathValue);
			const FAvaTagHandle TagHandle = FindTagHandle(Collection, LuaStringToFName(NameValue));
			if (TagHandle.IsValid())
			{
				Preset.SequenceTag = FAvaTagSoftHandle(TagHandle);
				Preset.bEnableTag = Options.get_or("enable_tag", true);
			}
		}
		if (sol::optional<sol::table> Marks = Options.get<sol::optional<sol::table>>("marks"))
		{
			Preset.Marks.Reset();
			for (int32 LuaIndex = 1;; ++LuaIndex)
			{
				sol::object Entry = (*Marks)[LuaIndex];
				if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
				{
					break;
				}
				if (!Entry.is<sol::table>())
				{
					continue;
				}
				FAvaMarkSetting MarkSetting = TableToSequencePresetMarkSetting(Entry.as<sol::table>());
				if (!MarkSetting.Label.IsEmpty())
				{
					Preset.Marks.Add(MarkSetting);
				}
			}
			Preset.bEnableMarks = Options.get_or("enable_marks", Preset.Marks.Num() > 0);
		}
		return Preset;
	}

	FAvaSequencePresetGroup TableToSequencePresetGroup(const sol::table& Options)
	{
		FAvaSequencePresetGroup Group;
		std::string GroupName;
		if (sol::optional<std::string> Name = Options.get<sol::optional<std::string>>("name"))
		{
			GroupName = *Name;
		}
		else if (sol::optional<std::string> AlternateName = Options.get<sol::optional<std::string>>("group_name"))
		{
			GroupName = *AlternateName;
		}
		Group.GroupName = LuaStringToFName(GroupName);
		if (sol::optional<sol::table> PresetNames = Options.get<sol::optional<sol::table>>("preset_names"))
		{
			for (int32 LuaIndex = 1;; ++LuaIndex)
			{
				sol::object Entry = (*PresetNames)[LuaIndex];
				if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
				{
					break;
				}
				if (Entry.is<std::string>())
				{
					Group.PresetNames.Add(LuaStringToFName(Entry.as<std::string>()));
				}
			}
		}
		return Group;
	}

	bool AddSequencerPreset(UAvaSequencerSettings* Settings, const FAvaSequencePreset& Preset)
	{
		if (!Settings || Preset.PresetName.IsNone())
		{
			return false;
		}
		FSetProperty* SetProperty = FindSequencerSetProperty(TEXT("CustomSequencePresets"));
		FStructProperty* StructProperty = SetProperty ? CastField<FStructProperty>(SetProperty->ElementProp) : nullptr;
		if (!StructProperty || StructProperty->Struct != FAvaSequencePreset::StaticStruct())
		{
			return false;
		}
		RemoveNamedSetStruct(Settings, SetProperty, TEXT("PresetName"), Preset.PresetName);
		FScriptSetHelper Helper(SetProperty, SetProperty->ContainerPtrToValuePtr<void>(Settings));
		const int32 InternalIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
		*reinterpret_cast<FAvaSequencePreset*>(Helper.GetElementPtr(InternalIndex)) = Preset;
		Helper.Rehash();
		return true;
	}

	bool AddSequencerPresetGroup(UAvaSequencerSettings* Settings, const FAvaSequencePresetGroup& Group)
	{
		if (!Settings || Group.GroupName.IsNone())
		{
			return false;
		}
		FSetProperty* SetProperty = FindSequencerSetProperty(TEXT("CustomPresetGroups"));
		FStructProperty* StructProperty = SetProperty ? CastField<FStructProperty>(SetProperty->ElementProp) : nullptr;
		if (!StructProperty || StructProperty->Struct != FAvaSequencePresetGroup::StaticStruct())
		{
			return false;
		}
		RemoveNamedSetStruct(Settings, SetProperty, TEXT("GroupName"), Group.GroupName);
		FScriptSetHelper Helper(SetProperty, SetProperty->ContainerPtrToValuePtr<void>(Settings));
		const int32 InternalIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
		*reinterpret_cast<FAvaSequencePresetGroup*>(Helper.GetElementPtr(InternalIndex)) = Group;
		Helper.Rehash();
		return true;
	}

	sol::table BuildAvaSequencerSettingsInfo(sol::state_view State)
	{
		const UAvaSequencerSettings* Settings = GetDefault<UAvaSequencerSettings>();
		sol::table Result = State.create_table();
		if (!Settings)
		{
			Result["valid"] = false;
			return Result;
		}
		Result["valid"] = true;
		Result["display_rate"] = BuildFrameRateInfo(State, Settings->GetDisplayRate());
		Result["start_time"] = Settings->GetStartTime();
		Result["end_time"] = Settings->GetEndTime();

		sol::table Presets = State.create_table();
		int32 PresetIndex = 0;
		for (const FAvaSequencePreset& Preset : Settings->GetCustomSequencePresets())
		{
			Presets[++PresetIndex] = BuildSequencePresetInfo(State, Preset);
		}
		Result["custom_preset_count"] = PresetIndex;
		Result["custom_presets"] = Presets;

		sol::table Groups = State.create_table();
		int32 GroupIndex = 0;
		for (const FAvaSequencePresetGroup& Group : Settings->GetCustomPresetGroups())
		{
			Groups[++GroupIndex] = BuildSequencePresetGroupInfo(State, Group);
		}
		Result["custom_group_count"] = GroupIndex;
		Result["custom_groups"] = Groups;
		return Result;
	}

	FLinearColor TableToLinearColor(const sol::table& Table, const FLinearColor& DefaultValue = FLinearColor::White)
	{
		return FLinearColor(
			Table.get_or("r", Table.get_or("R", DefaultValue.R)),
			Table.get_or("g", Table.get_or("G", DefaultValue.G)),
			Table.get_or("b", Table.get_or("B", DefaultValue.B)),
			Table.get_or("a", Table.get_or("A", DefaultValue.A)));
	}

	sol::table BuildCorner(sol::state_view Lua, EAvaShapeCornerType Type, float Size, uint8 Subdivisions)
	{
		sol::table Result = Lua.create_table();
		Result["type"] = TCHAR_TO_UTF8(CornerToString(Type));
		Result["bevel_size"] = Size;
		Result["bevel_subdivisions"] = static_cast<int32>(Subdivisions);
			return Result;
		}

		sol::table BuildStaticMeshInfo(sol::state_view Lua, UStaticMesh* StaticMesh)
		{
			sol::table Info = Lua.create_table();
			if (!StaticMesh)
			{
				return Info;
		}

		Info["path"] = TCHAR_TO_UTF8(*StaticMesh->GetPathName());
		Info["name"] = TCHAR_TO_UTF8(*StaticMesh->GetName());
		Info["class"] = TCHAR_TO_UTF8(*StaticMesh->GetClass()->GetName());
		Info["source_model_count"] = StaticMesh->GetNumSourceModels();
			Info["lod_count"] = StaticMesh->GetNumLODs();
			Info["section_count_lod0"] = StaticMesh->GetNumLODs() > 0 ? StaticMesh->GetNumSections(0) : 0;
			Info["material_slot_count"] = StaticMesh->GetStaticMaterials().Num();
			Info["is_asset"] = StaticMesh->IsAsset();
			Info["package_dirty"] = StaticMesh->GetOutermost() ? StaticMesh->GetOutermost()->IsDirty() : false;
			return Info;
		}

	sol::table BuildMaterialInfo(sol::state_view Lua, UMaterialInterface* Material)
	{
		sol::table Info = Lua.create_table();
			Info["path"] = Material ? TCHAR_TO_UTF8(*Material->GetPathName()) : "";
			Info["name"] = Material ? TCHAR_TO_UTF8(*Material->GetName()) : "";
			Info["class"] = Material ? TCHAR_TO_UTF8(*Material->GetClass()->GetName()) : "";
			Info["is_asset"] = Material ? Material->IsAsset() : false;
			Info["is_dynamic_instance"] = Material ? Material->IsA<UMaterialInstanceDynamic>() : false;
			if (UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(Material))
			{
				Info["solid_color"] = BuildLinearColor(Lua, DynamicMaterial->K2_GetVectorParameterValue(TEXT("Color")));
				Info["mode"] = DynamicMaterial->K2_GetScalarParameterValue(TEXT("Mode"));
				Info["opacity"] = DynamicMaterial->K2_GetScalarParameterValue(TEXT("GlobalOpacity"));
			}
			return Info;
		}

		sol::table BuildFontInfo(sol::state_view Lua, UFont* Font)
		{
			sol::table Info = Lua.create_table();
			Info["path"] = Font ? TCHAR_TO_UTF8(*Font->GetPathName()) : "";
			Info["name"] = Font ? TCHAR_TO_UTF8(*Font->GetName()) : "";
			Info["class"] = Font ? TCHAR_TO_UTF8(*Font->GetClass()->GetName()) : "";
			Info["is_asset"] = Font ? Font->IsAsset() : false;
			return Info;
		}

		sol::table BuildFontFaceInfo(sol::state_view Lua, UFontFace* FontFace)
		{
			sol::table Info = Lua.create_table();
			Info["path"] = FontFace ? TCHAR_TO_UTF8(*FontFace->GetPathName()) : "";
			Info["name"] = FontFace ? TCHAR_TO_UTF8(*FontFace->GetName()) : "";
			Info["class"] = FontFace ? TCHAR_TO_UTF8(*FontFace->GetClass()->GetName()) : "";
			Info["is_asset"] = FontFace ? FontFace->IsAsset() : false;
			return Info;
		}

		bool SetText3DStringProperty(UText3DProjectSettings* Settings, const FName PropertyName, const FString& Value)
		{
			if (!Settings)
			{
				return false;
			}
			FStrProperty* Property = FindFProperty<FStrProperty>(Settings->GetClass(), PropertyName);
			if (!Property)
			{
				return false;
			}

			Settings->Modify();
			Property->SetPropertyValue_InContainer(Settings, Value);
			Settings->SaveConfig();
			return true;
		}

		bool SetText3DBoolProperty(UText3DProjectSettings* Settings, const FName PropertyName, const bool bValue)
		{
			if (!Settings)
			{
				return false;
			}
			FBoolProperty* Property = FindFProperty<FBoolProperty>(Settings->GetClass(), PropertyName);
			if (!Property)
			{
				return false;
			}

			Settings->Modify();
			Property->SetPropertyValue_InContainer(Settings, bValue);
			Settings->SaveConfig();
			return true;
		}

		bool ReadText3DBoolProperty(const UText3DProjectSettings* Settings, const FName PropertyName)
		{
			if (!Settings)
			{
				return false;
			}
			const FBoolProperty* Property = FindFProperty<FBoolProperty>(Settings->GetClass(), PropertyName);
			return Property ? Property->GetPropertyValue_InContainer(Settings) : false;
		}

		bool SetText3DSoftObjectProperty(UText3DProjectSettings* Settings, const FName PropertyName, UObject* Object)
		{
			if (!Settings || !Object)
			{
				return false;
			}
			FSoftObjectProperty* Property = FindFProperty<FSoftObjectProperty>(Settings->GetClass(), PropertyName);
			if (!Property)
			{
				return false;
			}

			Settings->Modify();
			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Settings);
			Property->SetPropertyValue(ValuePtr, FSoftObjectPtr(Object));
			Settings->SaveConfig();
			return true;
		}

		sol::table BuildTextSettingsInfo(sol::state_view Lua)
		{
			sol::table Info = Lua.create_table();
			const UText3DProjectSettings* Settings = UText3DProjectSettings::Get();
			if (!Settings)
			{
				return Info;
			}

			Info["default_material"] = BuildMaterialInfo(Lua, Settings->GetDefaultMaterial());
			Info["fallback_font"] = BuildFontInfo(Lua, Settings->GetFallbackFont());
			Info["fallback_font_face"] = BuildFontFaceInfo(Lua, Settings->GetFallbackFontFace());
			Info["font_directory"] = TCHAR_TO_UTF8(*Settings->GetFontDirectory());
			Info["schedule_font_face_glyph_cleanup"] = Settings->GetScheduleFontFaceGlyphCleanup();
			Info["font_face_glyph_cleanup_period"] = Settings->GetFontFaceGlyphCleanupPeriod();
#if WITH_EDITOR
			Info["show_only_monospaced"] = Settings->GetShowOnlyMonospaced();
			Info["show_only_bold"] = Settings->GetShowOnlyBold();
			Info["show_only_italic"] = Settings->GetShowOnlyItalic();
			Info["debug_mode"] = ReadText3DBoolProperty(Settings, TEXT("bDebugMode"));
			sol::table Favorites = Lua.create_table();
			int32 FavoriteIndex = 0;
			for (const FString& Favorite : Settings->GetFavoriteFonts())
			{
				Favorites[++FavoriteIndex] = TCHAR_TO_UTF8(*Favorite);
			}
			Info["favorite_fonts"] = Favorites;
			Info["favorite_font_count"] = FavoriteIndex;
#else
			Info["show_only_monospaced"] = false;
			Info["show_only_bold"] = false;
			Info["show_only_italic"] = false;
			Info["debug_mode"] = false;
			Info["favorite_fonts"] = Lua.create_table();
			Info["favorite_font_count"] = 0;
#endif
			return Info;
		}

		void ApplyTextSettingsOptions(UText3DProjectSettings* Settings, const sol::table& Options)
		{
			if (!Settings)
			{
				return;
			}

			if (sol::optional<std::string> DefaultMaterial = Options.get<sol::optional<std::string>>("default_material"))
			{
				if (UMaterial* Material = LoadObject<UMaterial>(nullptr, UTF8_TO_TCHAR(DefaultMaterial->c_str())))
				{
					SetText3DSoftObjectProperty(Settings, TEXT("DefaultMaterial"), Material);
				}
			}
			if (sol::optional<std::string> FallbackFont = Options.get<sol::optional<std::string>>("fallback_font"))
			{
				if (UFont* Font = LoadObject<UFont>(nullptr, UTF8_TO_TCHAR(FallbackFont->c_str())))
				{
					SetText3DSoftObjectProperty(Settings, TEXT("FallbackFont"), Font);
				}
			}
			if (sol::optional<std::string> FallbackFontFace = Options.get<sol::optional<std::string>>("fallback_font_face"))
			{
				if (UFontFace* FontFace = LoadObject<UFontFace>(nullptr, UTF8_TO_TCHAR(FallbackFontFace->c_str())))
				{
					SetText3DSoftObjectProperty(Settings, TEXT("FallbackFontFace"), FontFace);
				}
			}
			if (sol::optional<std::string> FontDirectory = Options.get<sol::optional<std::string>>("font_directory"))
			{
				SetText3DStringProperty(Settings, TEXT("FontDirectory"), UTF8_TO_TCHAR(FontDirectory->c_str()));
			}
			if (sol::optional<bool> bScheduleCleanup = Options.get<sol::optional<bool>>("schedule_font_face_glyph_cleanup"))
			{
				Settings->Modify();
				Settings->SetScheduleFontFaceGlyphCleanup(*bScheduleCleanup);
				Settings->SaveConfig();
			}
			if (sol::optional<double> CleanupPeriod = Options.get<sol::optional<double>>("font_face_glyph_cleanup_period"))
			{
				Settings->Modify();
				Settings->SetFontFaceGlyphCleanupPeriod(static_cast<float>(FMath::Max(0.0, *CleanupPeriod)));
				Settings->SaveConfig();
			}
#if WITH_EDITOR
			if (sol::optional<bool> bOnlyMonospaced = Options.get<sol::optional<bool>>("show_only_monospaced"))
			{
				Settings->SetShowOnlyMonospaced(*bOnlyMonospaced);
			}
			if (sol::optional<bool> bOnlyBold = Options.get<sol::optional<bool>>("show_only_bold"))
			{
				Settings->SetShowOnlyBold(*bOnlyBold);
			}
			if (sol::optional<bool> bOnlyItalic = Options.get<sol::optional<bool>>("show_only_italic"))
			{
				Settings->SetShowOnlyItalic(*bOnlyItalic);
			}
			if (sol::optional<bool> bDebugMode = Options.get<sol::optional<bool>>("debug_mode"))
			{
				SetText3DBoolProperty(Settings, TEXT("bDebugMode"), *bDebugMode);
			}
			if (sol::optional<sol::table> AddFavorites = Options.get<sol::optional<sol::table>>("add_favorite_fonts"))
			{
				for (const auto& Pair : *AddFavorites)
				{
					const sol::optional<std::string> FontName = Pair.second.as<sol::optional<std::string>>();
					if (FontName)
					{
						Settings->AddFavoriteFont(UTF8_TO_TCHAR(FontName->c_str()));
					}
				}
			}
			if (sol::optional<sol::table> RemoveFavorites = Options.get<sol::optional<sol::table>>("remove_favorite_fonts"))
			{
				for (const auto& Pair : *RemoveFavorites)
				{
					const sol::optional<std::string> FontName = Pair.second.as<sol::optional<std::string>>();
					if (FontName)
					{
						Settings->RemoveFavoriteFont(UTF8_TO_TCHAR(FontName->c_str()));
					}
				}
			}
#endif
		}

	UStaticMesh* CreateOrLoadStaticMeshAsset(const FString& AssetPath)
	{
		if (!AssetPath.StartsWith(TEXT("/Game/")))
		{
			return nullptr;
		}

		FString PackageName;
		FString AssetName;
		if (!AssetPath.Split(TEXT("/"), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd) || AssetName.IsEmpty())
		{
			return nullptr;
		}

		PackageName = AssetPath;
		int32 LastSlash = INDEX_NONE;
		if (PackageName.FindLastChar(TEXT('/'), LastSlash))
		{
			PackageName.LeftInline(LastSlash);
			PackageName /= AssetName;
		}
		if (!FPackageName::IsValidLongPackageName(PackageName, false))
		{
			return nullptr;
		}

		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		if (UStaticMesh* Existing = LoadObject<UStaticMesh>(nullptr, *ObjectPath))
		{
			return Existing;
		}

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}

		UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (StaticMesh)
		{
			FAssetRegistryModule::AssetCreated(StaticMesh);
			StaticMesh->MarkPackageDirty();
		}
		return StaticMesh;
	}

		float ReadFloatProperty(const UObject* Object, const TCHAR* PropertyName, float DefaultValue = 0.0f)
		{
			if (!Object)
			{
				return DefaultValue;
			}

			if (const FFloatProperty* Property = FindFProperty<FFloatProperty>(Object->GetClass(), PropertyName))
			{
				return Property->GetPropertyValue_InContainer(Object);
			}

			return DefaultValue;
		}

		int32 ReadIntProperty(const UObject* Object, const TCHAR* PropertyName, int32 DefaultValue = 0)
		{
			if (!Object)
			{
				return DefaultValue;
			}

			if (const FIntProperty* Property = FindFProperty<FIntProperty>(Object->GetClass(), PropertyName))
			{
				return Property->GetPropertyValue_InContainer(Object);
			}

			return DefaultValue;
		}

		bool ReadBoolProperty(const UObject* Object, const TCHAR* PropertyName, bool bDefaultValue = false)
		{
			if (!Object)
			{
				return bDefaultValue;
			}

			if (const FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName))
			{
				return Property->GetPropertyValue_InContainer(Object);
			}

			return bDefaultValue;
		}

		uint8 ReadEnumByteProperty(const UObject* Object, const TCHAR* PropertyName, uint8 DefaultValue = 0)
		{
			if (!Object)
			{
				return DefaultValue;
			}

			if (const FEnumProperty* Property = FindFProperty<FEnumProperty>(Object->GetClass(), PropertyName))
			{
				return static_cast<uint8>(Property->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Object)));
			}
			if (const FByteProperty* Property = FindFProperty<FByteProperty>(Object->GetClass(), PropertyName))
			{
				return Property->GetPropertyValue_InContainer(Object);
			}

			return DefaultValue;
		}

	sol::table BuildShapeInfo(sol::state_view Lua, AAvaShapeActor* Actor)
	{
		sol::table Info = Lua.create_table();
		if (!Actor)
		{
			return Info;
		}

		UAvaShapeDynamicMeshBase* Mesh = Actor->GetDynamicMesh();
		UAvaShape2DArrowDynamicMesh* Arrow = Cast<UAvaShape2DArrowDynamicMesh>(Mesh);
		UAvaShapeChevronDynamicMesh* Chevron = Cast<UAvaShapeChevronDynamicMesh>(Mesh);
		UAvaShapeConeDynamicMesh* Cone = Cast<UAvaShapeConeDynamicMesh>(Mesh);
		UAvaShapeCubeDynamicMesh* Cube = Cast<UAvaShapeCubeDynamicMesh>(Mesh);
		UAvaShapeEllipseDynamicMesh* Ellipse = Cast<UAvaShapeEllipseDynamicMesh>(Mesh);
		UAvaShapeIrregularPolygonDynamicMesh* IrregularPolygon = Cast<UAvaShapeIrregularPolygonDynamicMesh>(Mesh);
		UAvaShapeLineDynamicMesh* Line = Cast<UAvaShapeLineDynamicMesh>(Mesh);
		UAvaShapeNGonDynamicMesh* NGon = Cast<UAvaShapeNGonDynamicMesh>(Mesh);
		UAvaShapeRectangleDynamicMesh* Rect = Cast<UAvaShapeRectangleDynamicMesh>(Mesh);
		UAvaShapeRingDynamicMesh* Ring = Cast<UAvaShapeRingDynamicMesh>(Mesh);
		UAvaShapeRoundedPolygonDynamicMesh* RoundedPolygon = Cast<UAvaShapeRoundedPolygonDynamicMesh>(Mesh);
		UAvaShapeSphereDynamicMesh* Sphere = Cast<UAvaShapeSphereDynamicMesh>(Mesh);
		UAvaShapeStarDynamicMesh* Star = Cast<UAvaShapeStarDynamicMesh>(Mesh);
		UAvaShapeTorusDynamicMesh* Torus = Cast<UAvaShapeTorusDynamicMesh>(Mesh);
		Info["label"] = TCHAR_TO_UTF8(*Actor->GetActorLabel());
		Info["class"] = TCHAR_TO_UTF8(*Actor->GetClass()->GetName());
		Info["has_shape_mesh_component"] = Actor->GetShapeMeshComponent() != nullptr;
		Info["finished_creation"] = Actor->HasFinishedCreation();
		Info["dynamic_mesh_class"] = Mesh ? TCHAR_TO_UTF8(*Mesh->GetClass()->GetName()) : "";
		Info["mesh_name"] = Mesh ? TCHAR_TO_UTF8(*Mesh->GetMeshName()) : "";
		Info["mesh_section_count"] = Mesh ? Mesh->GetMeshSectionNames().Num() : 0;
		Info["mesh_index_count"] = Mesh ? Mesh->GetMeshesIndexes().Num() : 0;
		Info["generated_vertex_count"] = 0;
		Info["generated_triangle_count"] = 0;
		Info["generated_component_has_mesh"] = false;
		Info["generated_bounds_origin"] = BuildVector(Lua, FVector::ZeroVector);
		Info["generated_bounds_extent"] = BuildVector(Lua, FVector::ZeroVector);
		Info["generated_bounds_volume"] = 0.0;
		Info["size2d"] = Mesh ? BuildVector2D(Lua, Mesh->GetSize2D()) : BuildVector2D(Lua, FVector2D::ZeroVector);
		Info["size3d"] = Mesh ? BuildVector(Lua, Mesh->GetSize3D()) : BuildVector(Lua, FVector::ZeroVector);
		Info["uniform_scaled_size"] = Mesh ? Mesh->GetUniformScaledSize() : 0.0f;
		Info["use_primary_material_everywhere"] = Mesh ? Mesh->GetUsePrimaryMaterialEverywhere() : false;
		Info["is_2d_arrow"] = Arrow != nullptr;
		Info["is_chevron"] = Chevron != nullptr;
		Info["is_cone"] = Cone != nullptr;
		Info["is_cube"] = Cube != nullptr;
		Info["is_ellipse"] = Ellipse != nullptr;
		Info["is_irregular_polygon"] = IrregularPolygon != nullptr;
		Info["is_line"] = Line != nullptr;
		Info["is_ngon"] = NGon != nullptr;
		Info["is_rectangle"] = Rect != nullptr;
		Info["is_ring"] = Ring != nullptr;
		Info["is_sphere"] = Sphere != nullptr;
		Info["is_star"] = Star != nullptr;
		Info["is_torus"] = Torus != nullptr;

		if (UDynamicMeshComponent* ShapeMeshComponent = Actor->GetShapeMeshComponent())
		{
			int32 VertexCount = 0;
			int32 TriangleCount = 0;
			ShapeMeshComponent->ProcessMesh([&VertexCount, &TriangleCount](const UE::Geometry::FDynamicMesh3& GeneratedMesh)
			{
				VertexCount = GeneratedMesh.VertexCount();
				TriangleCount = GeneratedMesh.TriangleCount();
			});

			const FVector BoundsOrigin = ShapeMeshComponent->Bounds.Origin;
			const FVector BoundsExtent = ShapeMeshComponent->Bounds.BoxExtent;

			Info["generated_vertex_count"] = VertexCount;
			Info["generated_triangle_count"] = TriangleCount;
			Info["generated_component_has_mesh"] = VertexCount > 0 && TriangleCount > 0;
			Info["generated_bounds_origin"] = BuildVector(Lua, BoundsOrigin);
			Info["generated_bounds_extent"] = BuildVector(Lua, BoundsExtent);
			Info["generated_bounds_volume"] = BoundsExtent.X * BoundsExtent.Y * BoundsExtent.Z * 8.0;
		}

		const FAvaColorChangeData ColorData = Actor->GetColorData();
		sol::table ColorInfo = Lua.create_table();
		ColorInfo["style"] = TCHAR_TO_UTF8(ColorStyleToString(ColorData.ColorStyle));
		ColorInfo["primary"] = BuildLinearColor(Lua, ColorData.PrimaryColor);
		ColorInfo["secondary"] = BuildLinearColor(Lua, ColorData.SecondaryColor);
		ColorInfo["is_unlit"] = ColorData.bIsUnlit;
		Info["color"] = ColorInfo;

		if (Mesh && Mesh->IsValidMeshIndex(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY))
		{
			UMaterialInterface* Material = Mesh->GetMaterial(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY);
			sol::table MaterialInfo = Lua.create_table();
			if (Mesh->IsMaterialType(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY, EMaterialType::Parametric))
			{
				MaterialInfo["type"] = TCHAR_TO_UTF8(MaterialTypeToString(EMaterialType::Parametric));
			}
			else if (Mesh->IsMaterialType(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY, EMaterialType::MaterialDesigner))
			{
				MaterialInfo["type"] = TCHAR_TO_UTF8(MaterialTypeToString(EMaterialType::MaterialDesigner));
			}
			else
			{
				MaterialInfo["type"] = TCHAR_TO_UTF8(MaterialTypeToString(EMaterialType::Asset));
			}
			MaterialInfo["path"] = Material ? TCHAR_TO_UTF8(*Material->GetPathName()) : "";
			MaterialInfo["name"] = Material ? TCHAR_TO_UTF8(*Material->GetName()) : "";
			MaterialInfo["class"] = Material ? TCHAR_TO_UTF8(*Material->GetClass()->GetName()) : "";
			MaterialInfo["is_asset"] = Material ? Material->IsAsset() : false;
			if (FAvaShapeParametricMaterial* Parametric = Mesh->GetParametricMaterialPtr(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY))
			{
				sol::table ParametricInfo = Lua.create_table();
				ParametricInfo["style"] = TCHAR_TO_UTF8(ParametricStyleToString(Parametric->GetStyle()));
				ParametricInfo["primary"] = BuildLinearColor(Lua, Parametric->GetPrimaryColor());
				ParametricInfo["secondary"] = BuildLinearColor(Lua, Parametric->GetSecondaryColor());
				ParametricInfo["gradient_offset"] = Parametric->GetGradientOffset();
				ParametricInfo["gradient_rotation"] = Parametric->GetGradientRotation();
				ParametricInfo["use_unlit"] = Parametric->GetUseUnlitMaterial();
				ParametricInfo["use_two_sided"] = Parametric->GetUseTwoSidedMaterial();
				ParametricInfo["translucency"] = TCHAR_TO_UTF8(ParametricTranslucencyToString(Parametric->GetTranslucency()));
				if (UTexture* Texture = Parametric->GetTexture())
				{
					ParametricInfo["texture_path"] = TCHAR_TO_UTF8(*Texture->GetPathName());
				}
				else
				{
					ParametricInfo["texture_path"] = "";
				}
				MaterialInfo["parametric"] = ParametricInfo;
			}
			Info["material0"] = MaterialInfo;

			sol::table UVInfo = Lua.create_table();
			UVInfo["mesh_index"] = UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY;
			UVInfo["mode"] = TCHAR_TO_UTF8(UVModeToString(Mesh->GetMaterialUVMode(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY)));
			UVInfo["anchor"] = BuildVector2D(Lua, Mesh->GetMaterialUVAnchor(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY));
			UVInfo["scale"] = BuildVector2D(Lua, Mesh->GetMaterialUVScale(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY));
			UVInfo["offset"] = BuildVector2D(Lua, Mesh->GetMaterialUVOffset(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY));
			UVInfo["rotation"] = Mesh->GetMaterialUVRotation(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY);
			UVInfo["flip_horizontal"] = Mesh->GetMaterialHorizontalFlip(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY);
			UVInfo["flip_vertical"] = Mesh->GetMaterialVerticalFlip(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY);
			Info["uv0"] = UVInfo;
		}

		if (RoundedPolygon)
		{
			Info["bevel_size"] = RoundedPolygon->GetBevelSize();
			Info["bevel_subdivisions"] = static_cast<int32>(RoundedPolygon->GetBevelSubdivisions());
		}

		if (Arrow)
		{
			Info["ratio_arrow_line"] = Arrow->GetRatioArrowLine();
			Info["ratio_line_height"] = Arrow->GetRatioLineHeight();
			Info["ratio_arrow_y"] = Arrow->GetRatioArrowY();
			Info["ratio_line_y"] = Arrow->GetRatioLineY();
			Info["both_side_arrows"] = Arrow->IsBothSideArrows();
		}

		if (Chevron)
		{
			Info["ratio_chevron"] = Chevron->GetRatioChevron();
		}

		if (Cone)
		{
			Info["num_sides"] = static_cast<int32>(Cone->GetNumSides());
			Info["top_radius"] = Cone->GetTopRadius();
			Info["angle_degree"] = Cone->GetAngleDegree();
			Info["start_degree"] = Cone->GetStartDegree();
		}

		if (Cube)
		{
			Info["segment"] = Cube->GetSegment();
			Info["bevel_size_ratio"] = Cube->GetBevelSizeRatio();
			Info["bevel_num"] = static_cast<int32>(Cube->GetBevelNum());
		}

		if (Ellipse)
		{
			Info["num_sides"] = static_cast<int32>(Ellipse->GetNumSides());
			Info["angle_degree"] = Ellipse->GetAngleDegree();
			Info["start_degree"] = Ellipse->GetStartDegree();
		}

		if (Line)
		{
			Info["line_width"] = Line->GetLineWidth();
			Info["vector"] = BuildVector2D(Lua, Line->GetVector());
		}

		if (IrregularPolygon)
		{
			Info["global_bevel_size"] = IrregularPolygon->GetGlobalBevelSize();
			Info["global_bevel_subdivisions"] = static_cast<int32>(IrregularPolygon->GetGlobalBevelSubdivisions());
			Info["point_count"] = IrregularPolygon->GetNumPoints();

			sol::table Points = Lua.create_table();
			const TArray<FAvaShapeRoundedCorner>& SourcePoints = IrregularPolygon->GetPoints();
			for (int32 PointIndex = 0; PointIndex < SourcePoints.Num(); ++PointIndex)
			{
				const FAvaShapeRoundedCorner& Point = SourcePoints[PointIndex];
				sol::table PointInfo = Lua.create_table();
				PointInfo["location"] = BuildVector2D(Lua, Point.Location);
				PointInfo["bevel_size"] = Point.Settings.BevelSize;
				PointInfo["bevel_subdivisions"] = static_cast<int32>(Point.Settings.BevelSubdivisions);
				Points[PointIndex + 1] = PointInfo;
			}
			Info["points"] = Points;
		}

		if (NGon)
		{
			Info["num_sides"] = static_cast<int32>(NGon->GetNumSides());
		}

		if (Rect)
		{
			Info["horizontal_alignment"] = TCHAR_TO_UTF8(HorizontalToString(Rect->GetHorizontalAlignment()));
			Info["vertical_alignment"] = TCHAR_TO_UTF8(VerticalToString(Rect->GetVerticalAlignment()));
			Info["left_slant"] = Rect->GetLeftSlant();
			Info["right_slant"] = Rect->GetRightSlant();
			Info["global_bevel_size"] = Rect->GetGlobalBevelSize();
			Info["global_bevel_subdivisions"] = static_cast<int32>(Rect->GetGlobalBevelSubdivisions());
			Info["top_left"] = BuildCorner(Lua, Rect->GetTopLeftCornerType(), Rect->GetTopLeftBevelSize(), Rect->GetTopLeftBevelSubdivisions());
			Info["top_right"] = BuildCorner(Lua, Rect->GetTopRightCornerType(), Rect->GetTopRightBevelSize(), Rect->GetTopRightBevelSubdivisions());
			Info["bottom_left"] = BuildCorner(Lua, Rect->GetBottomLeftCornerType(), Rect->GetBottomLeftBevelSize(), Rect->GetBottomLeftBevelSubdivisions());
			Info["bottom_right"] = BuildCorner(Lua, Rect->GetBottomRightCornerType(), Rect->GetBottomRightBevelSize(), Rect->GetBottomRightBevelSubdivisions());
		}

		if (Ring)
		{
			Info["num_sides"] = static_cast<int32>(Ring->GetNumSides());
			Info["inner_size"] = Ring->GetInnerSize();
			Info["angle_degree"] = Ring->GetAngleDegree();
			Info["start_degree"] = Ring->GetStartDegree();
		}

		if (Star)
		{
			Info["num_points"] = static_cast<int32>(Star->GetNumPoints());
			Info["inner_size"] = Star->GetInnerSize();
		}

		if (Sphere)
		{
			Info["num_sides"] = static_cast<int32>(Sphere->GetNumSides());
			Info["start_latitude"] = Sphere->GetStartLatitude();
			Info["latitude_degree"] = Sphere->GetLatitudeDegree();
			Info["start_longitude"] = Sphere->GetStartLongitude();
			Info["end_longitude"] = Sphere->GetEndLongitude();
		}

		if (Torus)
		{
			Info["num_slices"] = static_cast<int32>(Torus->GetNumSlices());
			Info["num_sides"] = static_cast<int32>(Torus->GetNumSides());
			Info["inner_size"] = Torus->GetInnerSize();
			Info["angle_degree"] = Torus->GetAngleDegree();
			Info["start_degree"] = Torus->GetStartDegree();
		}

		return Info;
	}

	sol::table BuildTextureInfo(sol::state_view Lua, UTexture* Texture)
	{
		sol::table Info = Lua.create_table();
		Info["path"] = Texture ? TCHAR_TO_UTF8(*Texture->GetPathName()) : "";
		Info["name"] = Texture ? TCHAR_TO_UTF8(*Texture->GetName()) : "";
		Info["class"] = Texture ? TCHAR_TO_UTF8(*Texture->GetClass()->GetName()) : "";
		Info["is_asset"] = Texture ? Texture->IsAsset() : false;
		return Info;
	}

	sol::table BuildDynamicMeshInfo(sol::state_view Lua, AActor* Actor)
	{
		sol::table Info = Lua.create_table();
		Info["valid"] = Actor != nullptr;
		Info["label"] = Actor ? TCHAR_TO_UTF8(*Actor->GetActorLabel()) : "";
		Info["class"] = Actor ? TCHAR_TO_UTF8(*Actor->GetClass()->GetName()) : "";
		Info["has_dynamic_mesh_component"] = false;
		Info["component_name"] = "";
		Info["vertex_count"] = 0;
		Info["triangle_count"] = 0;
		Info["local_bounds_min"] = BuildVector(Lua, FVector::ZeroVector);
		Info["local_bounds_max"] = BuildVector(Lua, FVector::ZeroVector);
		Info["local_bounds_extent"] = BuildVector(Lua, FVector::ZeroVector);
		Info["component_bounds_origin"] = BuildVector(Lua, FVector::ZeroVector);
		Info["component_bounds_extent"] = BuildVector(Lua, FVector::ZeroVector);
		Info["actor_scale"] = Actor ? BuildVector(Lua, Actor->GetActorScale3D()) : BuildVector(Lua, FVector::OneVector);

		UDynamicMeshComponent* MeshComponent = Actor ? Actor->FindComponentByClass<UDynamicMeshComponent>() : nullptr;
		if (!MeshComponent)
		{
			return Info;
		}

		int32 VertexCount = 0;
		int32 TriangleCount = 0;
		FVector LocalMin = FVector::ZeroVector;
		FVector LocalMax = FVector::ZeroVector;
		FVector LocalExtent = FVector::ZeroVector;
		MeshComponent->ProcessMesh([&VertexCount, &TriangleCount, &LocalMin, &LocalMax, &LocalExtent](const UE::Geometry::FDynamicMesh3& Mesh)
		{
			VertexCount = Mesh.VertexCount();
			TriangleCount = Mesh.TriangleCount();
			const FBox LocalBounds = static_cast<FBox>(Mesh.GetBounds(true));
			if (LocalBounds.IsValid)
			{
				LocalMin = LocalBounds.Min;
				LocalMax = LocalBounds.Max;
				LocalExtent = LocalBounds.GetExtent();
			}
		});

		Info["has_dynamic_mesh_component"] = true;
		Info["component_name"] = TCHAR_TO_UTF8(*MeshComponent->GetName());
		Info["vertex_count"] = VertexCount;
		Info["triangle_count"] = TriangleCount;
		Info["local_bounds_min"] = BuildVector(Lua, LocalMin);
		Info["local_bounds_max"] = BuildVector(Lua, LocalMax);
		Info["local_bounds_extent"] = BuildVector(Lua, LocalExtent);
		Info["component_bounds_origin"] = BuildVector(Lua, MeshComponent->Bounds.Origin);
		Info["component_bounds_extent"] = BuildVector(Lua, MeshComponent->Bounds.BoxExtent);
		return Info;
	}

	template <typename MeshType>
	MeshType* SetDynamicShapeMesh(AAvaShapeActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		MeshType* Mesh = NewObject<MeshType>(Actor, MeshType::StaticClass());
		if (!Mesh)
		{
			return nullptr;
		}

		Actor->SetDynamicMesh(Mesh);
		Mesh->SetRunAsync(false);
		return Mesh;
	}

	AAvaShapeActor* SpawnShapeActor(const std::string& Label, const FVector& Location)
	{
		UWorld* World = GetEditorWorld();
		ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
		if (!World || !Level || !GEditor || Label.empty())
		{
			return nullptr;
		}

		AAvaShapeActor* Actor = Cast<AAvaShapeActor>(GEditor->AddActor(Level, AAvaShapeActor::StaticClass(), FTransform(Location), true, RF_Transactional));
		if (Actor)
		{
			Actor->Modify();
			Actor->SetActorLabel(UTF8_TO_TCHAR(Label.c_str()), false);
		}
		return Actor;
	}

	void ApplyActorOptions(AAvaShapeActor* Actor, const sol::table& Options)
	{
		if (!Actor)
		{
			return;
		}

		if (sol::optional<sol::table> ColorTable = Options.get<sol::optional<sol::table>>("color"))
		{
			FAvaColorChangeData ColorData = Actor->GetColorData();
			if (sol::optional<std::string> Style = ColorTable->get<sol::optional<std::string>>("style"))
			{
				ColorData.ColorStyle = ParseColorStyle(*Style, ColorData.ColorStyle);
			}
			if (sol::optional<sol::table> Primary = ColorTable->get<sol::optional<sol::table>>("primary"))
			{
				ColorData.PrimaryColor = TableToLinearColor(*Primary, ColorData.PrimaryColor);
			}
			if (sol::optional<sol::table> Secondary = ColorTable->get<sol::optional<sol::table>>("secondary"))
			{
				ColorData.SecondaryColor = TableToLinearColor(*Secondary, ColorData.SecondaryColor);
			}
			if (sol::optional<bool> bIsUnlit = ColorTable->get<sol::optional<bool>>("is_unlit"))
			{
				ColorData.bIsUnlit = *bIsUnlit;
			}
			Actor->SetColorData(ColorData);
		}
	}

	void ApplyMaterialUVOptions(UAvaShapeDynamicMeshBase* Mesh, const sol::table& Options)
	{
		if (!Mesh)
		{
			return;
		}

		if (sol::optional<sol::table> UVTable = Options.get<sol::optional<sol::table>>("material_uv"))
		{
			const int32 MeshIndex = UVTable->get_or("mesh_index", static_cast<int32>(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY));
			if (!Mesh->IsValidMeshIndex(MeshIndex))
			{
				return;
			}

			if (sol::optional<std::string> Mode = UVTable->get<sol::optional<std::string>>("mode"))
			{
				Mesh->SetMaterialUVMode(MeshIndex, ParseUVMode(*Mode, Mesh->GetMaterialUVMode(MeshIndex)));
			}
			if (sol::optional<sol::table> Anchor = UVTable->get<sol::optional<sol::table>>("anchor"))
			{
				Mesh->SetMaterialUVAnchor(MeshIndex, TableToVector2D(*Anchor, Mesh->GetMaterialUVAnchor(MeshIndex)));
			}
			if (sol::optional<sol::table> Scale = UVTable->get<sol::optional<sol::table>>("scale"))
			{
				Mesh->SetMaterialUVScale(MeshIndex, TableToVector2D(*Scale, Mesh->GetMaterialUVScale(MeshIndex)));
			}
			if (sol::optional<sol::table> Offset = UVTable->get<sol::optional<sol::table>>("offset"))
			{
				Mesh->SetMaterialUVOffset(MeshIndex, TableToVector2D(*Offset, Mesh->GetMaterialUVOffset(MeshIndex)));
			}
			if (sol::optional<double> Rotation = UVTable->get<sol::optional<double>>("rotation"))
			{
				Mesh->SetMaterialUVRotation(MeshIndex, static_cast<float>(*Rotation));
			}
			if (sol::optional<bool> bFlipHorizontal = UVTable->get<sol::optional<bool>>("flip_horizontal"))
			{
				Mesh->SetMaterialHorizontalFlip(MeshIndex, *bFlipHorizontal);
			}
			if (sol::optional<bool> bFlipVertical = UVTable->get<sol::optional<bool>>("flip_vertical"))
			{
				Mesh->SetMaterialVerticalFlip(MeshIndex, *bFlipVertical);
			}
		}
	}

	void ApplyMaterialOptions(UAvaShapeDynamicMeshBase* Mesh, const sol::table& Options)
	{
		if (!Mesh)
		{
			return;
		}

		if (sol::optional<sol::table> MaterialTable = Options.get<sol::optional<sol::table>>("material"))
		{
			const int32 MeshIndex = MaterialTable->get_or("mesh_index", static_cast<int32>(UAvaShapeDynamicMeshBase::MESH_INDEX_PRIMARY));
			if (!Mesh->IsValidMeshIndex(MeshIndex))
			{
				return;
			}

			if (sol::optional<std::string> AssetPath = MaterialTable->get<sol::optional<std::string>>("asset"))
			{
				if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, UTF8_TO_TCHAR(AssetPath->c_str())))
				{
					Mesh->SetMaterial(MeshIndex, Material);
				}
			}

				if (sol::optional<sol::table> ParametricTable = MaterialTable->get<sol::optional<sol::table>>("parametric"))
				{
					FAvaShapeParametricMaterial* Parametric = Mesh->GetParametricMaterialPtr(MeshIndex);
					if (!Parametric)
					{
						return;
					}

					if (sol::optional<std::string> Style = ParametricTable->get<sol::optional<std::string>>("style"))
					{
						Parametric->SetStyle(ParseParametricStyle(*Style, Parametric->GetStyle()));
					}
					if (sol::optional<sol::table> Primary = ParametricTable->get<sol::optional<sol::table>>("primary"))
					{
						Parametric->SetPrimaryColor(TableToLinearColor(*Primary, Parametric->GetPrimaryColor()));
					}
					if (sol::optional<sol::table> Secondary = ParametricTable->get<sol::optional<sol::table>>("secondary"))
					{
						Parametric->SetSecondaryColor(TableToLinearColor(*Secondary, Parametric->GetSecondaryColor()));
					}
					if (sol::optional<double> GradientOffset = ParametricTable->get<sol::optional<double>>("gradient_offset"))
					{
						Parametric->SetGradientOffset(static_cast<float>(FMath::Clamp(*GradientOffset, 0.0, 1.0)));
					}
					if (sol::optional<double> GradientRotation = ParametricTable->get<sol::optional<double>>("gradient_rotation"))
					{
						Parametric->SetGradientRotation(static_cast<float>(FMath::Clamp(*GradientRotation, 0.0, 1.0)));
					}
					if (sol::optional<bool> bUseUnlit = ParametricTable->get<sol::optional<bool>>("use_unlit"))
					{
						Parametric->SetUseUnlitMaterial(*bUseUnlit);
					}
					if (sol::optional<bool> bUseTwoSided = ParametricTable->get<sol::optional<bool>>("use_two_sided"))
					{
						Parametric->SetUseTwoSidedMaterial(*bUseTwoSided);
					}
					if (sol::optional<std::string> Translucency = ParametricTable->get<sol::optional<std::string>>("translucency"))
					{
						Parametric->SetTranslucency(ParseParametricTranslucency(*Translucency, Parametric->GetTranslucency()));
					}
					if (sol::optional<std::string> TexturePath = ParametricTable->get<sol::optional<std::string>>("texture"))
					{
						UTexture* Texture = LoadObject<UTexture>(nullptr, UTF8_TO_TCHAR(TexturePath->c_str()));
						Parametric->SetTexture(Texture);
					}

					Mesh->SetMaterial(MeshIndex, nullptr);
				}
			}
		}

	void ApplyBaseShapeOptions(UAvaShapeDynamicMeshBase* Mesh, const sol::table& Options)
	{
		if (!Mesh)
		{
			return;
		}

		if (sol::optional<sol::table> Size = Options.get<sol::optional<sol::table>>("size"))
		{
			Mesh->SetSize2D(TableToVector2D(*Size, Mesh->GetSize2D()));
		}
		if (sol::optional<sol::table> Size3D = Options.get<sol::optional<sol::table>>("size3d"))
		{
			Mesh->SetSize3D(TableToVector(*Size3D, Mesh->GetSize3D()));
		}
		if (sol::optional<double> UniformSize = Options.get<sol::optional<double>>("uniform_scaled_size"))
		{
			Mesh->SetUniformScaledSize(static_cast<float>(*UniformSize));
		}
		if (sol::optional<bool> bUsePrimary = Options.get<sol::optional<bool>>("use_primary_material_everywhere"))
		{
			Mesh->SetUsePrimaryMaterialEverywhere(*bUsePrimary);
		}
		ApplyMaterialOptions(Mesh, Options);
		ApplyMaterialUVOptions(Mesh, Options);
	}

	void ApplyConeOptions(UAvaShapeConeDynamicMesh* Cone, const sol::table& Options)
	{
		if (!Cone)
		{
			return;
		}

		ApplyBaseShapeOptions(Cone, Options);
		if (sol::optional<int32> NumSides = Options.get<sol::optional<int32>>("num_sides"))
		{
			Cone->SetNumSides(static_cast<uint8>(FMath::Clamp(*NumSides, 3, 128)));
		}
		if (sol::optional<double> TopRadius = Options.get<sol::optional<double>>("top_radius"))
		{
			Cone->SetTopRadius(static_cast<float>(FMath::Clamp(*TopRadius, 0.0, 1.0)));
		}
		if (sol::optional<double> Angle = Options.get<sol::optional<double>>("angle_degree"))
		{
			Cone->SetAngleDegree(static_cast<float>(*Angle));
		}
		if (sol::optional<double> Start = Options.get<sol::optional<double>>("start_degree"))
		{
			Cone->SetStartDegree(static_cast<float>(*Start));
		}
	}

	void ApplyCubeOptions(UAvaShapeCubeDynamicMesh* Cube, const sol::table& Options)
	{
		if (!Cube)
		{
			return;
		}

		ApplyBaseShapeOptions(Cube, Options);
		if (sol::optional<double> Segment = Options.get<sol::optional<double>>("segment"))
		{
			Cube->SetSegment(static_cast<float>(*Segment));
		}
		if (sol::optional<double> Bevel = Options.get<sol::optional<double>>("bevel_size_ratio"))
		{
			Cube->SetBevelSizeRatio(static_cast<float>(*Bevel));
		}
		if (sol::optional<int32> BevelNum = Options.get<sol::optional<int32>>("bevel_num"))
		{
			Cube->SetBevelNum(static_cast<uint8>(FMath::Clamp(*BevelNum, 1, 8)));
		}
	}

	void ApplyRoundedPolygonOptions(UAvaShapeRoundedPolygonDynamicMesh* Mesh, const sol::table& Options)
	{
		if (!Mesh)
		{
			return;
		}

		ApplyBaseShapeOptions(Mesh, Options);
		if (sol::optional<double> BevelSize = Options.get<sol::optional<double>>("bevel_size"))
		{
			Mesh->SetBevelSize(static_cast<float>(*BevelSize));
		}
		if (sol::optional<int32> BevelSubdivisions = Options.get<sol::optional<int32>>("bevel_subdivisions"))
		{
			Mesh->SetBevelSubdivisions(static_cast<uint8>(FMath::Clamp(*BevelSubdivisions, 0, 64)));
		}
	}

	void ApplyArrowOptions(UAvaShape2DArrowDynamicMesh* Arrow, const sol::table& Options)
	{
		if (!Arrow)
		{
			return;
		}

		ApplyBaseShapeOptions(Arrow, Options);
		if (sol::optional<double> Ratio = Options.get<sol::optional<double>>("ratio_arrow_line"))
		{
			Arrow->SetRatioArrowLine(static_cast<float>(FMath::Clamp(*Ratio, 0.0, 1.0)));
		}
		if (sol::optional<double> Ratio = Options.get<sol::optional<double>>("ratio_line_height"))
		{
			Arrow->SetRatioLineHeight(static_cast<float>(FMath::Clamp(*Ratio, 0.0, 1.0)));
		}
		if (sol::optional<double> Ratio = Options.get<sol::optional<double>>("ratio_arrow_y"))
		{
			Arrow->SetRatioArrowY(static_cast<float>(FMath::Clamp(*Ratio, 0.0, 1.0)));
		}
		if (sol::optional<double> Ratio = Options.get<sol::optional<double>>("ratio_line_y"))
		{
			Arrow->SetRatioLineY(static_cast<float>(FMath::Clamp(*Ratio, 0.0, 1.0)));
		}
		if (sol::optional<bool> bBothSide = Options.get<sol::optional<bool>>("both_side_arrows"))
		{
			Arrow->SetBothSideArrows(*bBothSide);
		}
	}

	void ApplyChevronOptions(UAvaShapeChevronDynamicMesh* Chevron, const sol::table& Options)
	{
		if (!Chevron)
		{
			return;
		}

		ApplyBaseShapeOptions(Chevron, Options);
		if (sol::optional<double> Ratio = Options.get<sol::optional<double>>("ratio_chevron"))
		{
			Chevron->SetRatioChevron(static_cast<float>(FMath::Clamp(*Ratio, 0.0, 0.99)));
		}
	}

	void ApplyEllipseOptions(UAvaShapeEllipseDynamicMesh* Ellipse, const sol::table& Options)
	{
		if (!Ellipse)
		{
			return;
		}

		ApplyBaseShapeOptions(Ellipse, Options);
		if (sol::optional<int32> NumSides = Options.get<sol::optional<int32>>("num_sides"))
		{
			Ellipse->SetNumSides(static_cast<uint8>(FMath::Clamp(*NumSides, 3, 128)));
		}
		if (sol::optional<double> Angle = Options.get<sol::optional<double>>("angle_degree"))
		{
			Ellipse->SetAngleDegree(static_cast<float>(*Angle));
		}
		if (sol::optional<double> Start = Options.get<sol::optional<double>>("start_degree"))
		{
			Ellipse->SetStartDegree(static_cast<float>(*Start));
		}
	}

	void ApplyNGonOptions(UAvaShapeNGonDynamicMesh* NGon, const sol::table& Options)
	{
		if (!NGon)
		{
			return;
		}

		ApplyRoundedPolygonOptions(NGon, Options);
		if (sol::optional<int32> NumSides = Options.get<sol::optional<int32>>("num_sides"))
		{
			NGon->SetNumSides(static_cast<uint8>(FMath::Clamp(*NumSides, 3, 128)));
		}
	}

	void ApplyLineOptions(UAvaShapeLineDynamicMesh* Line, const sol::table& Options)
	{
		if (!Line)
		{
			return;
		}

		ApplyBaseShapeOptions(Line, Options);
		if (sol::optional<double> Width = Options.get<sol::optional<double>>("line_width"))
		{
			Line->SetLineWidth(static_cast<float>(*Width));
		}
		if (sol::optional<sol::table> Vector = Options.get<sol::optional<sol::table>>("vector"))
		{
			Line->SetVector(TableToVector2D(*Vector, Line->GetVector()));
		}
	}

	void ApplyIrregularPolygonOptions(UAvaShapeIrregularPolygonDynamicMesh* Mesh, const sol::table& Options)
	{
		if (!Mesh)
		{
			return;
		}

		ApplyBaseShapeOptions(Mesh, Options);
		if (sol::optional<double> BevelSize = Options.get<sol::optional<double>>("global_bevel_size"))
		{
			Mesh->SetGlobalBevelSize(static_cast<float>(*BevelSize));
		}
		if (sol::optional<int32> BevelSubdivisions = Options.get<sol::optional<int32>>("global_bevel_subdivisions"))
		{
			Mesh->SetGlobalBevelSubdivisions(static_cast<uint8>(FMath::Clamp(*BevelSubdivisions, 0, 64)));
		}
		if (sol::optional<sol::table> PointsTable = Options.get<sol::optional<sol::table>>("points"))
		{
			TArray<FAvaShapeRoundedCorner> Points;
			for (const auto& Entry : *PointsTable)
			{
				const sol::optional<sol::table> PointTable = Entry.second.as<sol::optional<sol::table>>();
				if (!PointTable)
				{
					continue;
				}

				sol::table LocationTable = *PointTable;
				if (sol::optional<sol::table> NestedLocation = PointTable->get<sol::optional<sol::table>>("location"))
				{
					LocationTable = *NestedLocation;
				}

				FAvaShapeRoundedCorner Point(TableToVector2D(LocationTable, FVector2D::ZeroVector));
				if (sol::optional<double> BevelSize = PointTable->get<sol::optional<double>>("bevel_size"))
				{
					Point.Settings.BevelSize = static_cast<float>(FMath::Clamp(*BevelSize, 0.0, 1.0));
				}
				if (sol::optional<int32> BevelSubdivisions = PointTable->get<sol::optional<int32>>("bevel_subdivisions"))
				{
					Point.Settings.BevelSubdivisions = static_cast<uint8>(FMath::Clamp(*BevelSubdivisions, 0, 64));
				}
				Points.Add(Point);
			}

			if (Points.Num() >= 3)
			{
				Mesh->SetPoints(Points);
			}
		}
	}

	void ApplyStarOptions(UAvaShapeStarDynamicMesh* Star, const sol::table& Options)
	{
		if (!Star)
		{
			return;
		}

		ApplyRoundedPolygonOptions(Star, Options);
		if (sol::optional<int32> NumPoints = Options.get<sol::optional<int32>>("num_points"))
		{
			Star->SetNumPoints(static_cast<uint8>(FMath::Clamp(*NumPoints, 2, 128)));
		}
		if (sol::optional<double> InnerSize = Options.get<sol::optional<double>>("inner_size"))
		{
			Star->SetInnerSize(static_cast<float>(FMath::Clamp(*InnerSize, 0.0, 0.99)));
		}
	}

	void ApplyRingOptions(UAvaShapeRingDynamicMesh* Ring, const sol::table& Options)
	{
		if (!Ring)
		{
			return;
		}

		ApplyBaseShapeOptions(Ring, Options);
		if (sol::optional<int32> NumSides = Options.get<sol::optional<int32>>("num_sides"))
		{
			Ring->SetNumSides(static_cast<uint8>(FMath::Clamp(*NumSides, 3, 128)));
		}
		if (sol::optional<double> InnerSize = Options.get<sol::optional<double>>("inner_size"))
		{
			Ring->SetInnerSize(static_cast<float>(FMath::Clamp(*InnerSize, 0.0, 1.0)));
		}
		if (sol::optional<double> Angle = Options.get<sol::optional<double>>("angle_degree"))
		{
			Ring->SetAngleDegree(static_cast<float>(*Angle));
		}
		if (sol::optional<double> Start = Options.get<sol::optional<double>>("start_degree"))
		{
			Ring->SetStartDegree(static_cast<float>(*Start));
		}
	}

	void ApplySphereOptions(UAvaShapeSphereDynamicMesh* Sphere, const sol::table& Options)
	{
		if (!Sphere)
		{
			return;
		}

		ApplyBaseShapeOptions(Sphere, Options);
		if (sol::optional<int32> NumSides = Options.get<sol::optional<int32>>("num_sides"))
		{
			Sphere->SetNumSides(static_cast<uint8>(FMath::Clamp(*NumSides, 4, 128)));
		}
		if (sol::optional<double> StartLatitude = Options.get<sol::optional<double>>("start_latitude"))
		{
			Sphere->SetStartLatitude(static_cast<float>(*StartLatitude));
		}
		if (sol::optional<double> LatitudeDegree = Options.get<sol::optional<double>>("latitude_degree"))
		{
			Sphere->SetLatitudeDegree(static_cast<float>(*LatitudeDegree));
		}
		if (sol::optional<double> StartLongitude = Options.get<sol::optional<double>>("start_longitude"))
		{
			Sphere->SetStartLongitude(static_cast<float>(*StartLongitude));
		}
		if (sol::optional<double> EndLongitude = Options.get<sol::optional<double>>("end_longitude"))
		{
			Sphere->SetEndLongitude(static_cast<float>(*EndLongitude));
		}
	}

	void ApplyTorusOptions(UAvaShapeTorusDynamicMesh* Torus, const sol::table& Options)
	{
		if (!Torus)
		{
			return;
		}

		ApplyBaseShapeOptions(Torus, Options);
		if (sol::optional<int32> NumSlices = Options.get<sol::optional<int32>>("num_slices"))
		{
			Torus->SetNumSlices(static_cast<uint8>(FMath::Clamp(*NumSlices, 3, 128)));
		}
		if (sol::optional<int32> NumSides = Options.get<sol::optional<int32>>("num_sides"))
		{
			Torus->SetNumSides(static_cast<uint8>(FMath::Clamp(*NumSides, 4, 128)));
		}
		if (sol::optional<double> InnerSize = Options.get<sol::optional<double>>("inner_size"))
		{
			Torus->SetInnerSize(static_cast<float>(FMath::Clamp(*InnerSize, 0.5, 0.99)));
		}
		if (sol::optional<double> Angle = Options.get<sol::optional<double>>("angle_degree"))
		{
			Torus->SetAngleDegree(static_cast<float>(*Angle));
		}
		if (sol::optional<double> Start = Options.get<sol::optional<double>>("start_degree"))
		{
			Torus->SetStartDegree(static_cast<float>(*Start));
		}
	}

	UText3DLayoutTransformEffect* FindLayoutTransformEffect(UText3DComponent* Component, int32 LuaIndex = 1)
	{
		if (!Component || LuaIndex < 1)
		{
			return nullptr;
		}

		int32 TransformIndex = 0;
		for (UText3DLayoutEffectBase* LayoutEffect : Component->GetLayoutEffects())
		{
			if (UText3DLayoutTransformEffect* TransformEffect = Cast<UText3DLayoutTransformEffect>(LayoutEffect))
			{
				++TransformIndex;
				if (TransformIndex == LuaIndex)
				{
					return TransformEffect;
				}
			}
		}
		return nullptr;
	}

	UText3DLayoutTransformEffect* AddLayoutTransformEffect(UText3DComponent* Component)
	{
		if (!Component)
		{
			return nullptr;
		}

		FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(UText3DComponent::StaticClass(), TEXT("LayoutEffects"));
		FObjectPropertyBase* InnerProperty = ArrayProperty ? CastField<FObjectPropertyBase>(ArrayProperty->Inner) : nullptr;
		if (!ArrayProperty || !InnerProperty)
		{
			return nullptr;
		}

		UText3DLayoutTransformEffect* Effect = NewObject<UText3DLayoutTransformEffect>(Component, UText3DLayoutTransformEffect::StaticClass(), NAME_None, RF_Transactional);
		if (!Effect)
		{
			return nullptr;
		}

		Component->Modify();
		Effect->Modify();
		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Component));
		const int32 NewIndex = Helper.AddValue();
		InnerProperty->SetObjectPropertyValue(Helper.GetRawPtr(NewIndex), Effect);
		Component->RequestUpdate(EText3DRendererFlags::Layout | EText3DRendererFlags::Material, true);
		return Effect;
	}

	bool RemoveLayoutEffect(UText3DComponent* Component, int32 LuaIndex)
	{
		if (!Component || LuaIndex < 1)
		{
			return false;
		}

		FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(UText3DComponent::StaticClass(), TEXT("LayoutEffects"));
		if (!ArrayProperty)
		{
			return false;
		}

		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Component));
		if (LuaIndex > Helper.Num())
		{
			return false;
		}

		Component->Modify();
		Helper.RemoveValues(LuaIndex - 1, 1);
		Component->RequestUpdate(EText3DRendererFlags::Layout | EText3DRendererFlags::Material, true);
		return true;
	}

	void ApplyLayoutTransformEffectOptions(UText3DLayoutTransformEffect* Effect, const sol::table& Options)
	{
		if (!Effect)
		{
			return;
		}

		Effect->Modify();
		if (sol::optional<bool> bEnabled = Options.get<sol::optional<bool>>("location_enabled"))
		{
			Effect->SetLocationEnabled(*bEnabled);
		}
		if (sol::optional<double> Progress = Options.get<sol::optional<double>>("location_progress"))
		{
			Effect->SetLocationProgress(static_cast<float>(FMath::Clamp(*Progress, 0.0, 100.0)));
		}
		if (sol::optional<std::string> Order = Options.get<sol::optional<std::string>>("location_order"))
		{
			Effect->SetLocationOrder(ParseTextEffectOrder(*Order, Effect->GetLocationOrder()));
		}
		if (sol::optional<sol::table> Begin = Options.get<sol::optional<sol::table>>("location_begin"))
		{
			Effect->SetLocationBegin(TableToVector(*Begin, Effect->GetLocationBegin()));
		}
		if (sol::optional<sol::table> End = Options.get<sol::optional<sol::table>>("location_end"))
		{
			Effect->SetLocationEnd(TableToVector(*End, Effect->GetLocationEnd()));
		}

		if (sol::optional<bool> bEnabled = Options.get<sol::optional<bool>>("rotation_enabled"))
		{
			Effect->SetRotationEnabled(*bEnabled);
		}
		if (sol::optional<double> Progress = Options.get<sol::optional<double>>("rotation_progress"))
		{
			Effect->SetRotationProgress(static_cast<float>(FMath::Clamp(*Progress, 0.0, 100.0)));
		}
		if (sol::optional<std::string> Order = Options.get<sol::optional<std::string>>("rotation_order"))
		{
			Effect->SetRotationOrder(ParseTextEffectOrder(*Order, Effect->GetRotationOrder()));
		}
		if (sol::optional<sol::table> Begin = Options.get<sol::optional<sol::table>>("rotation_begin"))
		{
			Effect->SetRotationBegin(TableToRotator(*Begin, Effect->GetRotationBegin()));
		}
		if (sol::optional<sol::table> End = Options.get<sol::optional<sol::table>>("rotation_end"))
		{
			Effect->SetRotationEnd(TableToRotator(*End, Effect->GetRotationEnd()));
		}

		if (sol::optional<bool> bEnabled = Options.get<sol::optional<bool>>("scale_enabled"))
		{
			Effect->SetScaleEnabled(*bEnabled);
		}
		if (sol::optional<double> Progress = Options.get<sol::optional<double>>("scale_progress"))
		{
			Effect->SetScaleProgress(static_cast<float>(FMath::Clamp(*Progress, 0.0, 100.0)));
		}
		if (sol::optional<std::string> Order = Options.get<sol::optional<std::string>>("scale_order"))
		{
			Effect->SetScaleOrder(ParseTextEffectOrder(*Order, Effect->GetScaleOrder()));
		}
		if (sol::optional<sol::table> Begin = Options.get<sol::optional<sol::table>>("scale_begin"))
		{
			Effect->SetScaleBegin(TableToVector(*Begin, Effect->GetScaleBegin()));
		}
		if (sol::optional<sol::table> End = Options.get<sol::optional<sol::table>>("scale_end"))
		{
			Effect->SetScaleEnd(TableToVector(*End, Effect->GetScaleEnd()));
		}
	}

	sol::table BuildLayoutTransformEffectInfo(sol::state_view Lua, UText3DLayoutTransformEffect* Effect)
	{
		sol::table Info = Lua.create_table();
		if (!Effect)
		{
			return Info;
		}

		Info["name"] = TCHAR_TO_UTF8(*Effect->GetName());
		Info["class"] = TCHAR_TO_UTF8(*Effect->GetClass()->GetName());
		Info["location_enabled"] = Effect->GetLocationEnabled();
		Info["location_progress"] = Effect->GetLocationProgress();
		Info["location_order"] = TCHAR_TO_UTF8(TextEffectOrderToString(Effect->GetLocationOrder()));
		Info["location_begin"] = BuildVector(Lua, Effect->GetLocationBegin());
		Info["location_end"] = BuildVector(Lua, Effect->GetLocationEnd());
		Info["rotation_enabled"] = Effect->GetRotationEnabled();
		Info["rotation_progress"] = Effect->GetRotationProgress();
		Info["rotation_order"] = TCHAR_TO_UTF8(TextEffectOrderToString(Effect->GetRotationOrder()));
		Info["rotation_begin"] = BuildRotator(Lua, Effect->GetRotationBegin());
		Info["rotation_end"] = BuildRotator(Lua, Effect->GetRotationEnd());
		Info["scale_enabled"] = Effect->GetScaleEnabled();
		Info["scale_progress"] = Effect->GetScaleProgress();
		Info["scale_order"] = TCHAR_TO_UTF8(TextEffectOrderToString(Effect->GetScaleOrder()));
		Info["scale_begin"] = BuildVector(Lua, Effect->GetScaleBegin());
		Info["scale_end"] = BuildVector(Lua, Effect->GetScaleEnd());
		return Info;
	}

	UText3DTokenBase* AddTextToken(UText3DComponent* Component, const FName TokenName, const FText& Content)
	{
		UText3DDefaultTokenExtension* Extension = Component ? Component->GetCastedTokenExtension<UText3DDefaultTokenExtension>() : nullptr;
		FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(UText3DDefaultTokenExtension::StaticClass(), TEXT("Tokens"));
		FObjectPropertyBase* InnerProperty = ArrayProperty ? CastField<FObjectPropertyBase>(ArrayProperty->Inner) : nullptr;
		if (!Extension || !ArrayProperty || !InnerProperty)
		{
			return nullptr;
		}

		UText3DTokenBase* Token = NewObject<UText3DTokenBase>(Extension, UText3DTokenBase::StaticClass(), NAME_None, RF_Transactional);
		if (!Token)
		{
			return nullptr;
		}

		Component->Modify();
		Extension->Modify();
		Token->Modify();
		if (FNameProperty* NameProperty = FindFProperty<FNameProperty>(UText3DTokenBase::StaticClass(), TEXT("TokenName")))
		{
			NameProperty->SetPropertyValue_InContainer(Token, TokenName);
		}
		if (FTextProperty* ContentProperty = FindFProperty<FTextProperty>(UText3DTokenBase::StaticClass(), TEXT("Content")))
		{
			ContentProperty->SetPropertyValue_InContainer(Token, Content);
		}
		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Extension));
		const int32 NewIndex = Helper.AddValue();
		InnerProperty->SetObjectPropertyValue(Helper.GetRawPtr(NewIndex), Token);
		Component->RequestUpdate(EText3DRendererFlags::All, true);
		return Token;
	}

	UText3DStyleBase* AddTextStyle(UText3DComponent* Component, const FName StyleName)
	{
		UText3DDefaultStyleExtension* Extension = Component ? Component->GetCastedStyleExtension<UText3DDefaultStyleExtension>() : nullptr;
		FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(UText3DDefaultStyleExtension::StaticClass(), TEXT("Styles"));
		FObjectPropertyBase* InnerProperty = ArrayProperty ? CastField<FObjectPropertyBase>(ArrayProperty->Inner) : nullptr;
		if (!Extension || !ArrayProperty || !InnerProperty)
		{
			return nullptr;
		}

		UText3DStyleBase* Style = NewObject<UText3DStyleBase>(Extension, UText3DStyleBase::StaticClass(), NAME_None, RF_Transactional);
		if (!Style)
		{
			return nullptr;
		}

		Component->Modify();
		Extension->Modify();
		Style->Modify();
		Style->SetStyleName(StyleName);
		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Extension));
		const int32 NewIndex = Helper.AddValue();
		InnerProperty->SetObjectPropertyValue(Helper.GetRawPtr(NewIndex), Style);
		Component->RequestUpdate(EText3DRendererFlags::All, true);
		return Style;
	}

	sol::table BuildTextTokenInfo(sol::state_view Lua, UText3DTokenBase* Token, int32 Index)
	{
		sol::table Info = Lua.create_table();
		if (!Token)
		{
			return Info;
		}
		Info["index"] = Index;
		Info["name"] = TCHAR_TO_UTF8(*Token->GetTokenName().ToString());
		Info["content"] = TCHAR_TO_UTF8(*Token->GetContent().ToString());
		return Info;
	}

	sol::table BuildTextStyleInfo(sol::state_view Lua, UText3DStyleBase* Style, int32 Index)
	{
		sol::table Info = Lua.create_table();
		if (!Style)
		{
			return Info;
		}
		Info["index"] = Index;
		Info["name"] = TCHAR_TO_UTF8(*Style->GetStyleName().ToString());
		Info["override_font"] = Style->GetOverrideFont();
		Info["font"] = BuildFontInfo(Lua, Style->GetFont());
		Info["font_typeface"] = TCHAR_TO_UTF8(*Style->GetFontTypeface().ToString());
		Info["override_font_size"] = Style->GetOverrideFontSize();
		Info["font_size"] = Style->GetFontSize();
		Info["override_front_color"] = Style->GetOverrideFrontColor();
		Info["front_color"] = BuildLinearColor(Lua, Style->GetFrontColor());
		return Info;
	}

	void ApplyTextStyleOptions(UText3DStyleBase* Style, const sol::table& Options)
	{
		if (!Style)
		{
			return;
		}
		if (sol::optional<std::string> Name = Options.get<sol::optional<std::string>>("name"))
		{
			Style->SetStyleName(FName(UTF8_TO_TCHAR(Name->c_str())));
		}
		if (sol::optional<bool> bOverrideFont = Options.get<sol::optional<bool>>("override_font"))
		{
			Style->SetOverrideFont(*bOverrideFont);
		}
		if (sol::optional<std::string> FontPath = Options.get<sol::optional<std::string>>("font"))
		{
			if (UFont* Font = LoadObject<UFont>(nullptr, UTF8_TO_TCHAR(FontPath->c_str())))
			{
				Style->SetFont(Font);
			}
		}
		if (sol::optional<std::string> Typeface = Options.get<sol::optional<std::string>>("typeface"))
		{
			Style->SetFontTypeface(FName(UTF8_TO_TCHAR(Typeface->c_str())));
		}
		if (sol::optional<bool> bOverrideFontSize = Options.get<sol::optional<bool>>("override_font_size"))
		{
			Style->SetOverrideFontSize(*bOverrideFontSize);
		}
		if (sol::optional<double> FontSize = Options.get<sol::optional<double>>("font_size"))
		{
			Style->SetFontSize(static_cast<float>(*FontSize));
		}
		if (sol::optional<bool> bOverrideFrontColor = Options.get<sol::optional<bool>>("override_front_color"))
		{
			Style->SetOverrideFrontColor(*bOverrideFrontColor);
		}
		if (sol::optional<sol::table> FrontColor = Options.get<sol::optional<sol::table>>("front_color"))
		{
			Style->SetFrontColor(TableToLinearColor(*FrontColor, Style->GetFrontColor()));
		}
	}

	sol::table BuildTextInfo(sol::state_view Lua, AText3DActor* Actor)
	{
		sol::table Info = Lua.create_table();
		if (!Actor)
		{
			return Info;
		}

			UText3DComponent* Component = Actor->GetText3DComponent();
			const UObject* Geometry = Component ? Component->GetGeometryExtension() : nullptr;
			const UObject* Layout = Component ? Component->GetLayoutExtension() : nullptr;
			const UText3DDefaultMaterialExtension* MaterialExtension = Component
				? Component->GetCastedMaterialExtension<UText3DDefaultMaterialExtension>()
				: nullptr;
			const UText3DDefaultRenderingExtension* RenderingExtension = Component
				? Component->GetCastedRenderingExtension<UText3DDefaultRenderingExtension>()
				: nullptr;
			const FString BevelTypeText = Geometry
				? TextBevelTypeToString(static_cast<EText3DBevelType>(ReadEnumByteProperty(Geometry, TEXT("BevelType"))))
				: TEXT("");

			Info["label"] = TCHAR_TO_UTF8(*Actor->GetActorLabel());
			Info["class"] = TCHAR_TO_UTF8(*Actor->GetClass()->GetName());
			Info["component_class"] = Component ? TCHAR_TO_UTF8(*Component->GetClass()->GetName()) : "";
			Info["has_geometry_extension"] = Geometry != nullptr;
			Info["has_layout_extension"] = Layout != nullptr;
			Info["has_material_extension"] = Component ? Component->GetMaterialExtension() != nullptr : false;
			Info["has_rendering_extension"] = Component ? Component->GetRenderingExtension() != nullptr : false;
			Info["material_style"] = MaterialExtension ? TCHAR_TO_UTF8(TextMaterialStyleToString(MaterialExtension->GetStyle())) : "";
			Info["blend_mode"] = MaterialExtension ? TCHAR_TO_UTF8(TextMaterialBlendModeToString(MaterialExtension->GetBlendMode())) : "";
			Info["material_is_unlit"] = MaterialExtension ? MaterialExtension->GetIsUnlit() : false;
			Info["opacity"] = MaterialExtension ? MaterialExtension->GetOpacity() : 0.0f;
			Info["use_mask"] = MaterialExtension ? MaterialExtension->GetUseMask() : false;
			Info["mask_offset"] = MaterialExtension ? MaterialExtension->GetMaskOffset() : 0.0f;
			Info["mask_smoothness"] = MaterialExtension ? MaterialExtension->GetMaskSmoothness() : 0.0f;
			Info["mask_rotation"] = MaterialExtension ? MaterialExtension->GetMaskRotation() : 0.0f;
			Info["use_single_material"] = MaterialExtension ? MaterialExtension->GetUseSingleMaterial() : false;
			Info["front_color"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetFrontColor()) : BuildLinearColor(Lua, FLinearColor::Transparent);
			Info["back_color"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetBackColor()) : BuildLinearColor(Lua, FLinearColor::Transparent);
			Info["extrude_color"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetExtrudeColor()) : BuildLinearColor(Lua, FLinearColor::Transparent);
			Info["bevel_color"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetBevelColor()) : BuildLinearColor(Lua, FLinearColor::Transparent);
			Info["gradient_color_a"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetGradientColorA()) : BuildLinearColor(Lua, FLinearColor::Transparent);
			Info["gradient_color_b"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetGradientColorB()) : BuildLinearColor(Lua, FLinearColor::Transparent);
			Info["gradient_smoothness"] = MaterialExtension ? MaterialExtension->GetGradientSmoothness() : 0.0f;
			Info["gradient_offset"] = MaterialExtension ? MaterialExtension->GetGradientOffset() : 0.0f;
			Info["gradient_rotation"] = MaterialExtension ? MaterialExtension->GetGradientRotation() : 0.0f;
			Info["texture_asset"] = MaterialExtension ? BuildTextureInfo(Lua, MaterialExtension->GetTextureAsset()) : BuildTextureInfo(Lua, nullptr);
			Info["texture_tiling"] = MaterialExtension ? BuildVector2D(Lua, MaterialExtension->GetTextureTiling()) : BuildVector2D(Lua, FVector2D::ZeroVector);
			Info["text"] = Component ? TCHAR_TO_UTF8(*Component->GetText().ToString()) : "";
			Info["formatted_text"] = Component ? TCHAR_TO_UTF8(*Component->GetFormattedText().ToString()) : "";
			Info["font"] = Component ? BuildFontInfo(Lua, Component->GetFont()) : BuildFontInfo(Lua, nullptr);
			Info["font_size"] = Component ? Component->GetFontSize() : 0.0f;
			Info["enforce_uppercase"] = Component ? Component->GetEnforceUpperCase() : false;
			Info["extrude"] = ReadFloatProperty(Geometry, TEXT("Extrude"));
			Info["bevel"] = ReadFloatProperty(Geometry, TEXT("Bevel"));
			Info["bevel_type"] = TCHAR_TO_UTF8(*BevelTypeText);
			Info["bevel_segments"] = ReadIntProperty(Geometry, TEXT("BevelSegments"));
			Info["has_outline"] = ReadBoolProperty(Geometry, TEXT("bUseOutline"));
			Info["outline_expand"] = ReadFloatProperty(Geometry, TEXT("Outline"));
			Info["kerning"] = Component ? Component->GetKerning() : 0.0f;
			Info["line_spacing"] = Component ? Component->GetLineSpacing() : 0.0f;
			Info["word_spacing"] = Component ? Component->GetWordSpacing() : 0.0f;
			Info["horizontal_alignment"] = Component ? TCHAR_TO_UTF8(TextHorizontalToString(Component->GetHorizontalAlignment())) : "";
			Info["vertical_alignment"] = Component ? TCHAR_TO_UTF8(TextVerticalToString(Component->GetVerticalAlignment())) : "";
			Info["max_width"] = Component ? Component->GetMaxWidth() : 0.0f;
			Info["max_height"] = Component ? Component->GetMaxHeight() : 0.0f;
			Info["scale_proportionally"] = Component ? Component->ScalesProportionally() : false;
			Info["use_max_width"] = Component ? Component->HasMaxWidth() : false;
			Info["use_max_height"] = Component ? Component->HasMaxHeight() : false;
			Info["cast_shadow"] = Component ? Component->CastsShadow() : false;
			Info["render_cast_shadow"] = RenderingExtension ? RenderingExtension->GetCastShadow() : false;
			Info["render_cast_hidden_shadow"] = RenderingExtension ? RenderingExtension->GetCastHiddenShadow() : false;
			Info["render_affect_dynamic_indirect_lighting"] = RenderingExtension ? RenderingExtension->GetAffectDynamicIndirectLighting() : false;
			Info["render_affect_indirect_lighting_while_hidden"] = RenderingExtension ? RenderingExtension->GetAffectIndirectLightingWhileHidden() : false;
			Info["render_holdout"] = RenderingExtension ? RenderingExtension->GetHoldout() : false;
			Info["front_material"] = Component ? BuildMaterialInfo(Lua, Component->GetFrontMaterial()) : BuildMaterialInfo(Lua, nullptr);
			Info["bevel_material"] = Component ? BuildMaterialInfo(Lua, Component->GetBevelMaterial()) : BuildMaterialInfo(Lua, nullptr);
			Info["extrude_material"] = Component ? BuildMaterialInfo(Lua, Component->GetExtrudeMaterial()) : BuildMaterialInfo(Lua, nullptr);
			Info["back_material"] = Component ? BuildMaterialInfo(Lua, Component->GetBackMaterial()) : BuildMaterialInfo(Lua, nullptr);

			if (Component)
			{
				Component->RequestUpdate(EText3DRendererFlags::All, true);
				Component->RequestUpdate(EText3DRendererFlags::Material, true);

				TMap<FName, TArray<UMaterialInterface*>> MaterialsByStyleTag;
				sol::table MaterialOverrides = Lua.create_table();
				int32 LuaMaterialOverrideIndex = 0;
				if (MaterialExtension)
				{
					if (FArrayProperty* OverridesProperty = FindFProperty<FArrayProperty>(UText3DDefaultMaterialExtension::StaticClass(), TEXT("MaterialOverrides")))
					{
						if (FStructProperty* StructProperty = CastField<FStructProperty>(OverridesProperty->Inner);
							StructProperty && StructProperty->Struct == FText3DMaterialOverride::StaticStruct())
						{
							FScriptArrayHelper Helper(OverridesProperty, OverridesProperty->ContainerPtrToValuePtr<void>(MaterialExtension));
							for (int32 OverrideIndex = 0; OverrideIndex < Helper.Num(); ++OverrideIndex)
							{
								const FText3DMaterialOverride* Override = reinterpret_cast<const FText3DMaterialOverride*>(Helper.GetRawPtr(OverrideIndex));
								if (!Override)
								{
									continue;
								}

								TArray<UMaterialInterface*>& MaterialsForTag = MaterialsByStyleTag.FindOrAdd(Override->Tag);
								MaterialsForTag.Reset(Override->Materials.Num());
								for (UMaterialInterface* Material : Override->Materials)
								{
									MaterialsForTag.Add(Material);
								}

								sol::table Row = Lua.create_table();
								Row["index"] = ++LuaMaterialOverrideIndex;
								Row["tag"] = TCHAR_TO_UTF8(*Override->Tag.ToString());
								Row["material_count"] = Override->Materials.Num();
								Row["front_material"] = BuildMaterialInfo(Lua, Override->Materials.IsValidIndex(static_cast<int32>(EText3DGroupType::Front)) ? Override->Materials[static_cast<int32>(EText3DGroupType::Front)] : nullptr);
								Row["bevel_material"] = BuildMaterialInfo(Lua, Override->Materials.IsValidIndex(static_cast<int32>(EText3DGroupType::Bevel)) ? Override->Materials[static_cast<int32>(EText3DGroupType::Bevel)] : nullptr);
								Row["extrude_material"] = BuildMaterialInfo(Lua, Override->Materials.IsValidIndex(static_cast<int32>(EText3DGroupType::Extrude)) ? Override->Materials[static_cast<int32>(EText3DGroupType::Extrude)] : nullptr);
								Row["back_material"] = BuildMaterialInfo(Lua, Override->Materials.IsValidIndex(static_cast<int32>(EText3DGroupType::Back)) ? Override->Materials[static_cast<int32>(EText3DGroupType::Back)] : nullptr);
								MaterialOverrides[LuaMaterialOverrideIndex] = Row;
							}
						}
					}
				}
				Info["material_overrides"] = MaterialOverrides;
				Info["material_override_count"] = LuaMaterialOverrideIndex;

				sol::table Tokens = Lua.create_table();
				int32 LuaTokenIndex = 0;
				if (UText3DDefaultTokenExtension* TokenExtension = Component->GetCastedTokenExtension<UText3DDefaultTokenExtension>())
				{
					if (FArrayProperty* TokenArrayProperty = FindFProperty<FArrayProperty>(UText3DDefaultTokenExtension::StaticClass(), TEXT("Tokens")))
					{
						if (FObjectPropertyBase* TokenInnerProperty = CastField<FObjectPropertyBase>(TokenArrayProperty->Inner))
						{
							FScriptArrayHelper Helper(TokenArrayProperty, TokenArrayProperty->ContainerPtrToValuePtr<void>(TokenExtension));
							for (int32 TokenIndex = 0; TokenIndex < Helper.Num(); ++TokenIndex)
							{
								if (UText3DTokenBase* Token = Cast<UText3DTokenBase>(TokenInnerProperty->GetObjectPropertyValue(Helper.GetRawPtr(TokenIndex))))
								{
									Tokens[++LuaTokenIndex] = BuildTextTokenInfo(Lua, Token, LuaTokenIndex);
								}
							}
						}
					}
				}
				Info["tokens"] = Tokens;
				Info["token_count"] = LuaTokenIndex;

				sol::table Styles = Lua.create_table();
				int32 LuaStyleIndex = 0;
				if (UText3DDefaultStyleExtension* StyleExtension = Component->GetCastedStyleExtension<UText3DDefaultStyleExtension>())
				{
					if (FArrayProperty* StyleArrayProperty = FindFProperty<FArrayProperty>(UText3DDefaultStyleExtension::StaticClass(), TEXT("Styles")))
					{
						if (FObjectPropertyBase* StyleInnerProperty = CastField<FObjectPropertyBase>(StyleArrayProperty->Inner))
						{
							FScriptArrayHelper Helper(StyleArrayProperty, StyleArrayProperty->ContainerPtrToValuePtr<void>(StyleExtension));
							for (int32 StyleIndex = 0; StyleIndex < Helper.Num(); ++StyleIndex)
							{
								if (UText3DStyleBase* Style = Cast<UText3DStyleBase>(StyleInnerProperty->GetObjectPropertyValue(Helper.GetRawPtr(StyleIndex))))
								{
									Styles[++LuaStyleIndex] = BuildTextStyleInfo(Lua, Style, LuaStyleIndex);
								}
							}
						}
					}
				}
				Info["styles"] = Styles;
				Info["style_count"] = LuaStyleIndex;

				sol::table LayoutEffects = Lua.create_table();
				int32 LuaLayoutEffectIndex = 0;
				for (UText3DLayoutEffectBase* LayoutEffect : Component->GetLayoutEffects())
				{
					if (UText3DLayoutTransformEffect* TransformEffect = Cast<UText3DLayoutTransformEffect>(LayoutEffect))
					{
						LayoutEffects[++LuaLayoutEffectIndex] = BuildLayoutTransformEffectInfo(Lua, TransformEffect);
					}
					else if (LayoutEffect)
					{
						sol::table Row = Lua.create_table();
						Row["name"] = TCHAR_TO_UTF8(*LayoutEffect->GetName());
						Row["class"] = TCHAR_TO_UTF8(*LayoutEffect->GetClass()->GetName());
						LayoutEffects[++LuaLayoutEffectIndex] = Row;
					}
				}
				Info["layout_effects"] = LayoutEffects;
				Info["layout_effect_count"] = LuaLayoutEffectIndex;

				FVector BoundsOrigin = FVector::ZeroVector;
				FVector BoundsExtent = FVector::ZeroVector;
				Component->GetBounds(BoundsOrigin, BoundsExtent);
				Info["bounds_origin"] = BuildVector(Lua, BoundsOrigin);
				Info["bounds_extent"] = BuildVector(Lua, BoundsExtent);
				Info["bounds_volume"] = BoundsExtent.X * BoundsExtent.Y * BoundsExtent.Z * 8.0;
				Info["character_count"] = static_cast<int32>(Component->GetCharacterCount());

				sol::table Characters = Lua.create_table();
				int32 LuaCharacterIndex = 0;
				Component->ForEachCharacter([&](UText3DCharacterBase* Character, uint16 CharacterIndex, uint16)
				{
					if (!Character)
					{
						return;
					}

					sol::table Row = Lua.create_table();
					Row["index"] = static_cast<int32>(CharacterIndex) + 1;
#if WITH_EDITORONLY_DATA
					Row["character"] = TCHAR_TO_UTF8(*Character->GetCharacter());
#else
					Row["character"] = "";
#endif
					Row["visible"] = Character->GetVisibility();
					Row["style_tag"] = TCHAR_TO_UTF8(*Character->GetStyleTag().ToString());
					if (const TArray<UMaterialInterface*>* OverrideMaterials = MaterialsByStyleTag.Find(Character->GetStyleTag()))
					{
						Row["front_material"] = BuildMaterialInfo(Lua, OverrideMaterials->IsValidIndex(static_cast<int32>(EText3DGroupType::Front)) ? (*OverrideMaterials)[static_cast<int32>(EText3DGroupType::Front)] : nullptr);
					}
					else
					{
						Row["front_material"] = Component ? BuildMaterialInfo(Lua, Component->GetFrontMaterial()) : BuildMaterialInfo(Lua, nullptr);
					}
					Row["relative_location"] = BuildVector(Lua, Character->GetRelativeLocation());
					Row["relative_scale"] = BuildVector(Lua, Character->GetRelativeScale());
					Row["relative_rotation"] = BuildRotator(Lua, Character->GetRelativeRotation());
					constexpr bool bResetCharacterTransform = false;
					const FTransform& CharacterTransform = Character->GetTransform(bResetCharacterTransform);
					Row["transform_location"] = BuildVector(Lua, CharacterTransform.GetLocation());
					Row["transform_scale"] = BuildVector(Lua, CharacterTransform.GetScale3D());
					Row["transform_rotation"] = BuildRotator(Lua, CharacterTransform.Rotator());
					Characters[++LuaCharacterIndex] = Row;
				});
				Info["characters"] = Characters;
			}
			else
			{
				Info["bounds_origin"] = BuildVector(Lua, FVector::ZeroVector);
				Info["bounds_extent"] = BuildVector(Lua, FVector::ZeroVector);
				Info["bounds_volume"] = 0.0;
				Info["character_count"] = 0;
				Info["characters"] = Lua.create_table();
				Info["layout_effects"] = Lua.create_table();
				Info["layout_effect_count"] = 0;
				Info["tokens"] = Lua.create_table();
				Info["token_count"] = 0;
				Info["styles"] = Lua.create_table();
				Info["style_count"] = 0;
			}
			return Info;
		}

	sol::table BuildColorDataInfo(sol::state_view Lua, const FAvaColorChangeData& ColorData)
	{
		sol::table Info = Lua.create_table();
		Info["style"] = TCHAR_TO_UTF8(ColorStyleToString(ColorData.ColorStyle));
		Info["primary"] = BuildLinearColor(Lua, ColorData.PrimaryColor);
		Info["secondary"] = BuildLinearColor(Lua, ColorData.SecondaryColor);
		Info["is_unlit"] = ColorData.bIsUnlit;
		return Info;
	}

	sol::table BuildLegacyTextInfo(sol::state_view Lua, AAvaTextActor* Actor)
	{
		sol::table Info = Lua.create_table();
		if (!Actor)
		{
			return Info;
		}

		UText3DComponent* Component = Actor->GetText3DComponent();
		const UText3DDefaultMaterialExtension* MaterialExtension = Component
			? Component->GetCastedMaterialExtension<UText3DDefaultMaterialExtension>()
			: nullptr;
		Info["label"] = TCHAR_TO_UTF8(*Actor->GetActorLabel());
		Info["class"] = TCHAR_TO_UTF8(*Actor->GetClass()->GetName());
		Info["component_class"] = Component ? TCHAR_TO_UTF8(*Component->GetClass()->GetName()) : "";
		Info["is_not_placeable_legacy"] = Actor->GetClass()->HasAnyClassFlags(CLASS_NotPlaceable);
		Info["has_material_extension"] = MaterialExtension != nullptr;
		Info["material_style"] = MaterialExtension ? TCHAR_TO_UTF8(TextMaterialStyleToString(MaterialExtension->GetStyle())) : "";
		Info["material_is_unlit"] = MaterialExtension ? MaterialExtension->GetIsUnlit() : false;
		Info["front_color"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetFrontColor()) : BuildLinearColor(Lua, FLinearColor::Transparent);
		Info["back_color"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetBackColor()) : BuildLinearColor(Lua, FLinearColor::Transparent);
		Info["extrude_color"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetExtrudeColor()) : BuildLinearColor(Lua, FLinearColor::Transparent);
		Info["bevel_color"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetBevelColor()) : BuildLinearColor(Lua, FLinearColor::Transparent);
		Info["gradient_color_a"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetGradientColorA()) : BuildLinearColor(Lua, FLinearColor::Transparent);
		Info["gradient_color_b"] = MaterialExtension ? BuildLinearColor(Lua, MaterialExtension->GetGradientColorB()) : BuildLinearColor(Lua, FLinearColor::Transparent);
		Info["color_data"] = BuildColorDataInfo(Lua, Actor->GetColorData());
		return Info;
	}

	void ApplyLegacyTextColorOptions(AAvaTextActor* Actor, const sol::table& Options)
	{
		if (!Actor)
		{
			return;
		}

		FAvaColorChangeData ColorData = Actor->GetColorData();
		if (sol::optional<std::string> Style = Options.get<sol::optional<std::string>>("style"))
		{
			ColorData.ColorStyle = ParseColorStyle(*Style, ColorData.ColorStyle);
		}
		if (sol::optional<sol::table> Primary = Options.get<sol::optional<sol::table>>("primary"))
		{
			ColorData.PrimaryColor = TableToLinearColor(*Primary, ColorData.PrimaryColor);
		}
		if (sol::optional<sol::table> Secondary = Options.get<sol::optional<sol::table>>("secondary"))
		{
			ColorData.SecondaryColor = TableToLinearColor(*Secondary, ColorData.SecondaryColor);
		}
		if (sol::optional<bool> bIsUnlit = Options.get<sol::optional<bool>>("is_unlit"))
		{
			ColorData.bIsUnlit = *bIsUnlit;
		}
		Actor->SetColorData(ColorData);
	}

	void ApplyRectangleOptions(UAvaShapeRectangleDynamicMesh* Rect, const sol::table& Options)
	{
		if (!Rect)
		{
			return;
		}

		ApplyBaseShapeOptions(Rect, Options);
		if (sol::optional<sol::table> Size = Options.get<sol::optional<sol::table>>("size"))
		{
			Rect->SetSize2D(TableToVector2D(*Size, Rect->GetSize2D()));
		}
		if (sol::optional<double> LeftSlant = Options.get<sol::optional<double>>("left_slant"))
		{
			Rect->SetLeftSlant(static_cast<float>(*LeftSlant));
		}
		if (sol::optional<double> RightSlant = Options.get<sol::optional<double>>("right_slant"))
		{
			Rect->SetRightSlant(static_cast<float>(*RightSlant));
		}
		if (sol::optional<double> BevelSize = Options.get<sol::optional<double>>("global_bevel_size"))
		{
			Rect->SetGlobalBevelSize(static_cast<float>(*BevelSize));
		}
		if (sol::optional<int32> BevelSubdivisions = Options.get<sol::optional<int32>>("global_bevel_subdivisions"))
		{
			Rect->SetGlobalBevelSubdivisions(static_cast<uint8>(FMath::Clamp(*BevelSubdivisions, 0, 64)));
		}
		if (sol::optional<std::string> Horizontal = Options.get<sol::optional<std::string>>("horizontal_alignment"))
		{
			Rect->SetHorizontalAlignment(ParseHorizontal(*Horizontal, Rect->GetHorizontalAlignment()));
		}
		if (sol::optional<std::string> Vertical = Options.get<sol::optional<std::string>>("vertical_alignment"))
		{
			Rect->SetVerticalAlignment(ParseVertical(*Vertical, Rect->GetVerticalAlignment()));
		}
		if (sol::optional<std::string> TopLeft = Options.get<sol::optional<std::string>>("top_left_type"))
		{
			Rect->SetTopLeftCornerType(ParseCornerType(*TopLeft, Rect->GetTopLeftCornerType()));
		}
		if (sol::optional<double> TopLeftSize = Options.get<sol::optional<double>>("top_left_bevel_size"))
		{
			Rect->SetTopLeftBevelSize(static_cast<float>(*TopLeftSize));
		}
		if (sol::optional<int32> TopLeftSubdivisions = Options.get<sol::optional<int32>>("top_left_bevel_subdivisions"))
		{
			Rect->SetTopLeftBevelSubdivisions(static_cast<uint8>(FMath::Clamp(*TopLeftSubdivisions, 0, 64)));
		}
		if (sol::optional<std::string> BottomRight = Options.get<sol::optional<std::string>>("bottom_right_type"))
		{
			Rect->SetBottomRightCornerType(ParseCornerType(*BottomRight, Rect->GetBottomRightCornerType()));
		}
		if (sol::optional<double> BottomRightSize = Options.get<sol::optional<double>>("bottom_right_bevel_size"))
		{
			Rect->SetBottomRightBevelSize(static_cast<float>(*BottomRightSize));
		}
	}

	void ApplyTextOptions(UText3DComponent* Component, const sol::table& Options)
	{
		if (!Component)
		{
			return;
		}

		if (sol::optional<std::string> Text = Options.get<sol::optional<std::string>>("text"))
		{
			Component->SetText(FText::FromString(UTF8_TO_TCHAR(Text->c_str())));
		}
		if (sol::optional<std::string> FontPath = Options.get<sol::optional<std::string>>("font"))
		{
			if (UFont* Font = LoadObject<UFont>(nullptr, UTF8_TO_TCHAR(FontPath->c_str())))
			{
				Component->SetFont(Font);
			}
		}
		if (sol::optional<double> FontSize = Options.get<sol::optional<double>>("font_size"))
		{
			Component->SetFontSize(static_cast<float>(*FontSize));
		}
		if (sol::optional<bool> bUppercase = Options.get<sol::optional<bool>>("enforce_uppercase"))
		{
			Component->SetEnforceUpperCase(*bUppercase);
		}
		if (sol::optional<double> Extrude = Options.get<sol::optional<double>>("extrude"))
		{
			Component->SetExtrude(static_cast<float>(*Extrude));
		}
			if (sol::optional<double> Bevel = Options.get<sol::optional<double>>("bevel"))
			{
				Component->SetBevel(static_cast<float>(*Bevel));
			}
			if (sol::optional<std::string> BevelType = Options.get<sol::optional<std::string>>("bevel_type"))
			{
				Component->SetBevelType(ParseTextBevelType(*BevelType, EText3DBevelType::Linear));
			}
			if (sol::optional<int32> BevelSegments = Options.get<sol::optional<int32>>("bevel_segments"))
			{
				Component->SetBevelSegments(FMath::Max(0, *BevelSegments));
			}
			if (sol::optional<bool> bHasOutline = Options.get<sol::optional<bool>>("has_outline"))
			{
				Component->SetHasOutline(*bHasOutline);
			}
			if (sol::optional<double> OutlineExpand = Options.get<sol::optional<double>>("outline_expand"))
			{
				Component->SetOutlineExpand(static_cast<float>(*OutlineExpand));
			}
			if (sol::optional<double> Kerning = Options.get<sol::optional<double>>("kerning"))
			{
				Component->SetKerning(static_cast<float>(*Kerning));
			}
			if (sol::optional<double> LineSpacing = Options.get<sol::optional<double>>("line_spacing"))
			{
				Component->SetLineSpacing(static_cast<float>(*LineSpacing));
			}
			if (sol::optional<double> WordSpacing = Options.get<sol::optional<double>>("word_spacing"))
			{
				Component->SetWordSpacing(static_cast<float>(*WordSpacing));
			}
			if (sol::optional<std::string> Horizontal = Options.get<sol::optional<std::string>>("horizontal_alignment"))
			{
				Component->SetHorizontalAlignment(ParseTextHorizontal(*Horizontal, Component->GetHorizontalAlignment()));
			}
			if (sol::optional<std::string> Vertical = Options.get<sol::optional<std::string>>("vertical_alignment"))
			{
				Component->SetVerticalAlignment(ParseTextVertical(*Vertical, Component->GetVerticalAlignment()));
			}
			if (sol::optional<bool> bScale = Options.get<sol::optional<bool>>("scale_proportionally"))
			{
				Component->SetScaleProportionally(*bScale);
			}
			if (sol::optional<bool> bHasMaxWidth = Options.get<sol::optional<bool>>("use_max_width"))
			{
				Component->SetHasMaxWidth(*bHasMaxWidth);
			}
			if (sol::optional<double> MaxWidth = Options.get<sol::optional<double>>("max_width"))
			{
				Component->SetMaxWidth(static_cast<float>(*MaxWidth));
			}
			if (sol::optional<bool> bHasMaxHeight = Options.get<sol::optional<bool>>("use_max_height"))
			{
				Component->SetHasMaxHeight(*bHasMaxHeight);
			}
			if (sol::optional<double> MaxHeight = Options.get<sol::optional<double>>("max_height"))
			{
				Component->SetMaxHeight(static_cast<float>(*MaxHeight));
			}
			if (sol::optional<bool> bCastShadow = Options.get<sol::optional<bool>>("cast_shadow"))
			{
				Component->SetCastShadow(*bCastShadow);
			}
			if (UText3DDefaultRenderingExtension* RenderingExtension = Component->GetCastedRenderingExtension<UText3DDefaultRenderingExtension>())
			{
				RenderingExtension->Modify();
				if (sol::optional<bool> bRenderCastShadow = Options.get<sol::optional<bool>>("render_cast_shadow"))
				{
					InvokeBoolSetter(RenderingExtension, TEXT("SetCastShadow"), *bRenderCastShadow);
				}
				if (sol::optional<bool> bCastHiddenShadow = Options.get<sol::optional<bool>>("render_cast_hidden_shadow"))
				{
					InvokeBoolSetter(RenderingExtension, TEXT("SetCastHiddenShadow"), *bCastHiddenShadow);
				}
				if (sol::optional<bool> bAffectDynamicIndirectLighting = Options.get<sol::optional<bool>>("render_affect_dynamic_indirect_lighting"))
				{
					InvokeBoolSetter(RenderingExtension, TEXT("SetAffectDynamicIndirectLighting"), *bAffectDynamicIndirectLighting);
				}
				if (sol::optional<bool> bAffectIndirectLightingWhileHidden = Options.get<sol::optional<bool>>("render_affect_indirect_lighting_while_hidden"))
				{
					InvokeBoolSetter(RenderingExtension, TEXT("SetAffectIndirectLightingWhileHidden"), *bAffectIndirectLightingWhileHidden);
				}
				if (sol::optional<bool> bHoldout = Options.get<sol::optional<bool>>("render_holdout"))
				{
					InvokeBoolSetter(RenderingExtension, TEXT("SetHoldout"), *bHoldout);
				}
			}
			if (UText3DDefaultMaterialExtension* MaterialExtension = Component->GetCastedMaterialExtension<UText3DDefaultMaterialExtension>())
			{
				MaterialExtension->Modify();
				if (sol::optional<std::string> Style = Options.get<sol::optional<std::string>>("material_style"))
				{
					MaterialExtension->SetStyle(ParseTextMaterialStyle(*Style, MaterialExtension->GetStyle()));
				}
				if (sol::optional<sol::table> FrontColor = Options.get<sol::optional<sol::table>>("front_color"))
				{
					MaterialExtension->SetFrontColor(TableToLinearColor(*FrontColor, MaterialExtension->GetFrontColor()));
				}
				if (sol::optional<sol::table> BackColor = Options.get<sol::optional<sol::table>>("back_color"))
				{
					MaterialExtension->SetBackColor(TableToLinearColor(*BackColor, MaterialExtension->GetBackColor()));
				}
				if (sol::optional<sol::table> ExtrudeColor = Options.get<sol::optional<sol::table>>("extrude_color"))
				{
					MaterialExtension->SetExtrudeColor(TableToLinearColor(*ExtrudeColor, MaterialExtension->GetExtrudeColor()));
				}
				if (sol::optional<sol::table> BevelColor = Options.get<sol::optional<sol::table>>("bevel_color"))
				{
					MaterialExtension->SetBevelColor(TableToLinearColor(*BevelColor, MaterialExtension->GetBevelColor()));
				}
				if (sol::optional<sol::table> GradientColorA = Options.get<sol::optional<sol::table>>("gradient_color_a"))
				{
					MaterialExtension->SetGradientColorA(TableToLinearColor(*GradientColorA, MaterialExtension->GetGradientColorA()));
				}
				if (sol::optional<sol::table> GradientColorB = Options.get<sol::optional<sol::table>>("gradient_color_b"))
				{
					MaterialExtension->SetGradientColorB(TableToLinearColor(*GradientColorB, MaterialExtension->GetGradientColorB()));
				}
				if (sol::optional<double> Smoothness = Options.get<sol::optional<double>>("gradient_smoothness"))
				{
					MaterialExtension->SetGradientSmoothness(static_cast<float>(*Smoothness));
				}
				if (sol::optional<double> Offset = Options.get<sol::optional<double>>("gradient_offset"))
				{
					MaterialExtension->SetGradientOffset(static_cast<float>(*Offset));
				}
				if (sol::optional<double> Rotation = Options.get<sol::optional<double>>("gradient_rotation"))
				{
					MaterialExtension->SetGradientRotation(static_cast<float>(*Rotation));
				}
				if (sol::optional<std::string> TexturePath = Options.get<sol::optional<std::string>>("texture_asset"))
				{
					if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, UTF8_TO_TCHAR(TexturePath->c_str())))
					{
						MaterialExtension->SetTextureAsset(Texture);
					}
				}
				if (sol::optional<sol::table> Tiling = Options.get<sol::optional<sol::table>>("texture_tiling"))
				{
					MaterialExtension->SetTextureTiling(TableToVector2D(*Tiling, MaterialExtension->GetTextureTiling()));
				}
				if (sol::optional<std::string> BlendMode = Options.get<sol::optional<std::string>>("blend_mode"))
				{
					MaterialExtension->SetBlendMode(ParseTextMaterialBlendMode(*BlendMode, MaterialExtension->GetBlendMode()));
				}
				if (sol::optional<bool> bIsUnlit = Options.get<sol::optional<bool>>("material_is_unlit"))
				{
					MaterialExtension->SetIsUnlit(*bIsUnlit);
				}
				if (sol::optional<double> Opacity = Options.get<sol::optional<double>>("opacity"))
				{
					MaterialExtension->SetOpacity(static_cast<float>(*Opacity));
				}
				if (sol::optional<bool> bUseMask = Options.get<sol::optional<bool>>("use_mask"))
				{
					MaterialExtension->SetUseMask(*bUseMask);
				}
				if (sol::optional<double> MaskOffset = Options.get<sol::optional<double>>("mask_offset"))
				{
					MaterialExtension->SetMaskOffset(static_cast<float>(*MaskOffset));
				}
				if (sol::optional<double> MaskSmoothness = Options.get<sol::optional<double>>("mask_smoothness"))
				{
					MaterialExtension->SetMaskSmoothness(static_cast<float>(*MaskSmoothness));
				}
				if (sol::optional<double> MaskRotation = Options.get<sol::optional<double>>("mask_rotation"))
				{
					MaterialExtension->SetMaskRotation(static_cast<float>(*MaskRotation));
				}
				if (sol::optional<bool> bUseSingleMaterial = Options.get<sol::optional<bool>>("use_single_material"))
				{
					MaterialExtension->SetUseSingleMaterial(*bUseSingleMaterial);
				}
			}
			if (sol::optional<std::string> FrontMaterial = Options.get<sol::optional<std::string>>("front_material"))
			{
				if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, UTF8_TO_TCHAR(FrontMaterial->c_str())))
				{
					Component->SetFrontMaterial(Material);
				}
			}
			if (sol::optional<std::string> BevelMaterial = Options.get<sol::optional<std::string>>("bevel_material"))
			{
				if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, UTF8_TO_TCHAR(BevelMaterial->c_str())))
				{
					Component->SetBevelMaterial(Material);
				}
			}
			if (sol::optional<std::string> ExtrudeMaterial = Options.get<sol::optional<std::string>>("extrude_material"))
			{
				if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, UTF8_TO_TCHAR(ExtrudeMaterial->c_str())))
				{
					Component->SetExtrudeMaterial(Material);
				}
			}
			if (sol::optional<std::string> BackMaterial = Options.get<sol::optional<std::string>>("back_material"))
			{
				if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, UTF8_TO_TCHAR(BackMaterial->c_str())))
				{
					Component->SetBackMaterial(Material);
				}
			}
			if (sol::optional<sol::table> AddToken = Options.get<sol::optional<sol::table>>("add_token"))
			{
				const FString TokenName = UTF8_TO_TCHAR(AddToken->get_or<std::string>("name", "").c_str());
				const FString Content = UTF8_TO_TCHAR(AddToken->get_or<std::string>("content", "").c_str());
				if (!TokenName.IsEmpty())
				{
					AddTextToken(Component, FName(*TokenName), FText::FromString(Content));
				}
			}
			if (sol::optional<sol::table> AddStyle = Options.get<sol::optional<sol::table>>("add_style"))
			{
				const FString StyleName = UTF8_TO_TCHAR(AddStyle->get_or<std::string>("name", "").c_str());
				if (!StyleName.IsEmpty())
				{
					UText3DStyleBase* Style = AddTextStyle(Component, FName(*StyleName));
					ApplyTextStyleOptions(Style, *AddStyle);
				}
			}
			if (sol::optional<int32> RemoveLayoutIndex = Options.get<sol::optional<int32>>("remove_layout_effect"))
			{
				RemoveLayoutEffect(Component, *RemoveLayoutIndex);
			}
			else if (sol::optional<bool> bRemoveFirstLayoutEffect = Options.get<sol::optional<bool>>("remove_layout_effect"))
			{
				if (*bRemoveFirstLayoutEffect)
				{
					RemoveLayoutEffect(Component, 1);
				}
			}
			if (sol::optional<sol::table> LayoutTransformTable = Options.get<sol::optional<sol::table>>("layout_transform_effect"))
			{
				const int32 LuaIndex = LayoutTransformTable->get_or("index", 1);
				UText3DLayoutTransformEffect* Effect = FindLayoutTransformEffect(Component, LuaIndex);
				if (!Effect)
				{
					Effect = AddLayoutTransformEffect(Component);
				}
				ApplyLayoutTransformEffectOptions(Effect, *LayoutTransformTable);
				Component->RequestUpdate(EText3DRendererFlags::All, true);
			}
			if (sol::optional<sol::table> AddLayoutTransformTable = Options.get<sol::optional<sol::table>>("add_layout_transform_effect"))
			{
				UText3DLayoutTransformEffect* Effect = AddLayoutTransformEffect(Component);
				ApplyLayoutTransformEffectOptions(Effect, *AddLayoutTransformTable);
				Component->RequestUpdate(EText3DRendererFlags::All, true);
			}
			else if (sol::optional<bool> bAddLayoutTransform = Options.get<sol::optional<bool>>("add_layout_transform_effect"))
			{
				if (*bAddLayoutTransform)
				{
					AddLayoutTransformEffect(Component);
					Component->RequestUpdate(EText3DRendererFlags::All, true);
				}
			}
		}

	void BindAvalancheLua(sol::state& Lua, FLuaSessionData&)
	{
		Lua.set_function("ava_shapes", [](sol::this_state S) -> sol::object
		{
			sol::state_view State(S);
			sol::table Handle = State.create_table();

			Handle.set_function("add_rectangle", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				if (!World)
				{
					return sol::lua_nil;
				}

				const std::string Label = Options.get_or<std::string>("label", "");
				if (Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}
				FVector2D Size(50.0, 50.0);
				if (sol::optional<sol::table> SizeTable = Options.get<sol::optional<sol::table>>("size"))
				{
					Size = TableToVector2D(*SizeTable, Size);
				}
				if (Size.IsNearlyZero())
				{
					return sol::lua_nil;
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddRectangle", "NeoStack: Add Motion Design Rectangle"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				if (!Actor)
				{
					return sol::lua_nil;
				}

				UAvaShapeRectangleDynamicMesh* Rect = UAvaShapeMeshFunctions::SetRectangle(Actor, Size, FTransform::Identity);
				if (!Rect)
				{
					World->EditorDestroyActor(Actor, false);
					return sol::lua_nil;
				}
				Rect->SetRunAsync(false);
				ApplyRectangleOptions(Rect, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();

				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_2d_arrow", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "Add2DArrow", "NeoStack: Add Motion Design 2D Arrow"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShape2DArrowDynamicMesh* Arrow = SetDynamicShapeMesh<UAvaShape2DArrowDynamicMesh>(Actor);
				if (!Actor || !Arrow)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyArrowOptions(Arrow, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_chevron", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddChevron", "NeoStack: Add Motion Design Chevron"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeChevronDynamicMesh* Chevron = SetDynamicShapeMesh<UAvaShapeChevronDynamicMesh>(Actor);
				if (!Actor || !Chevron)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyChevronOptions(Chevron, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_cone", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddCone", "NeoStack: Add Motion Design Cone"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeConeDynamicMesh* Cone = SetDynamicShapeMesh<UAvaShapeConeDynamicMesh>(Actor);
				if (!Actor || !Cone)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyConeOptions(Cone, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_cube", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddCube", "NeoStack: Add Motion Design Cube"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeCubeDynamicMesh* Cube = SetDynamicShapeMesh<UAvaShapeCubeDynamicMesh>(Actor);
				if (!Actor || !Cube)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyCubeOptions(Cube, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_ellipse", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddEllipse", "NeoStack: Add Motion Design Ellipse"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeEllipseDynamicMesh* Ellipse = SetDynamicShapeMesh<UAvaShapeEllipseDynamicMesh>(Actor);
				if (!Actor || !Ellipse)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyEllipseOptions(Ellipse, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_line", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddLine", "NeoStack: Add Motion Design Line"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeLineDynamicMesh* Line = SetDynamicShapeMesh<UAvaShapeLineDynamicMesh>(Actor);
				if (!Actor || !Line)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyLineOptions(Line, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_ngon", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddNGon", "NeoStack: Add Motion Design NGon"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeNGonDynamicMesh* NGon = SetDynamicShapeMesh<UAvaShapeNGonDynamicMesh>(Actor);
				if (!Actor || !NGon)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyNGonOptions(NGon, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_irregular_polygon", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddIrregularPolygon", "NeoStack: Add Motion Design Irregular Polygon"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeIrregularPolygonDynamicMesh* Irregular = SetDynamicShapeMesh<UAvaShapeIrregularPolygonDynamicMesh>(Actor);
				if (!Actor || !Irregular)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyIrregularPolygonOptions(Irregular, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_ring", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddRing", "NeoStack: Add Motion Design Ring"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeRingDynamicMesh* Ring = SetDynamicShapeMesh<UAvaShapeRingDynamicMesh>(Actor);
				if (!Actor || !Ring)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyRingOptions(Ring, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_sphere", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddSphere", "NeoStack: Add Motion Design Sphere"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeSphereDynamicMesh* Sphere = SetDynamicShapeMesh<UAvaShapeSphereDynamicMesh>(Actor);
				if (!Actor || !Sphere)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplySphereOptions(Sphere, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_star", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddStar", "NeoStack: Add Motion Design Star"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeStarDynamicMesh* Star = SetDynamicShapeMesh<UAvaShapeStarDynamicMesh>(Actor);
				if (!Actor || !Star)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyStarOptions(Star, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("add_torus", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				const std::string Label = Options.get_or<std::string>("label", "");
				if (!World || Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddTorus", "NeoStack: Add Motion Design Torus"));
				AAvaShapeActor* Actor = SpawnShapeActor(Label, Location);
				UAvaShapeTorusDynamicMesh* Torus = SetDynamicShapeMesh<UAvaShapeTorusDynamicMesh>(Actor);
				if (!Actor || !Torus)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				ApplyTorusOptions(Torus, Options);
				ApplyActorOptions(Actor, Options);
				Actor->FinishCreation();
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("configure", [](sol::this_state InnerS, sol::table, const std::string& Label, sol::table Options) -> sol::object
			{
				AAvaShapeActor* Actor = FindShapeActor(Label);
				UAvaShapeDynamicMeshBase* Mesh = Actor ? Actor->GetDynamicMesh() : nullptr;
				if (!Actor || !Mesh)
				{
					return sol::lua_nil;
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureShape", "NeoStack: Configure Motion Design Shape"));
				Actor->Modify();
				Mesh->Modify();
				if (UAvaShape2DArrowDynamicMesh* Arrow = Cast<UAvaShape2DArrowDynamicMesh>(Mesh))
				{
					ApplyArrowOptions(Arrow, Options);
				}
				else if (UAvaShapeChevronDynamicMesh* Chevron = Cast<UAvaShapeChevronDynamicMesh>(Mesh))
				{
					ApplyChevronOptions(Chevron, Options);
				}
				else if (UAvaShapeConeDynamicMesh* Cone = Cast<UAvaShapeConeDynamicMesh>(Mesh))
				{
					ApplyConeOptions(Cone, Options);
				}
				else if (UAvaShapeCubeDynamicMesh* Cube = Cast<UAvaShapeCubeDynamicMesh>(Mesh))
				{
					ApplyCubeOptions(Cube, Options);
				}
				else if (UAvaShapeEllipseDynamicMesh* Ellipse = Cast<UAvaShapeEllipseDynamicMesh>(Mesh))
				{
					ApplyEllipseOptions(Ellipse, Options);
				}
				else if (UAvaShapeLineDynamicMesh* Line = Cast<UAvaShapeLineDynamicMesh>(Mesh))
				{
					ApplyLineOptions(Line, Options);
				}
				else if (UAvaShapeIrregularPolygonDynamicMesh* Irregular = Cast<UAvaShapeIrregularPolygonDynamicMesh>(Mesh))
				{
					ApplyIrregularPolygonOptions(Irregular, Options);
				}
				else if (UAvaShapeNGonDynamicMesh* NGon = Cast<UAvaShapeNGonDynamicMesh>(Mesh))
				{
					ApplyNGonOptions(NGon, Options);
				}
				else if (UAvaShapeRectangleDynamicMesh* Rect = Cast<UAvaShapeRectangleDynamicMesh>(Mesh))
				{
					ApplyRectangleOptions(Rect, Options);
				}
				else if (UAvaShapeRingDynamicMesh* Ring = Cast<UAvaShapeRingDynamicMesh>(Mesh))
				{
					ApplyRingOptions(Ring, Options);
				}
				else if (UAvaShapeStarDynamicMesh* Star = Cast<UAvaShapeStarDynamicMesh>(Mesh))
				{
					ApplyStarOptions(Star, Options);
				}
				else if (UAvaShapeSphereDynamicMesh* Sphere = Cast<UAvaShapeSphereDynamicMesh>(Mesh))
				{
					ApplySphereOptions(Sphere, Options);
				}
				else if (UAvaShapeTorusDynamicMesh* Torus = Cast<UAvaShapeTorusDynamicMesh>(Mesh))
				{
					ApplyTorusOptions(Torus, Options);
				}
				else
				{
					ApplyBaseShapeOptions(Mesh, Options);
				}
				ApplyActorOptions(Actor, Options);
				Actor->MarkPackageDirty();

				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("info", [](sol::this_state InnerS, sol::table, const std::string& Label) -> sol::object
			{
				AAvaShapeActor* Actor = FindShapeActor(Label);
				if (!Actor)
				{
					return sol::lua_nil;
				}
				return sol::make_object(InnerS, BuildShapeInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("export_static_mesh", [](sol::this_state InnerS, sol::table, const std::string& Label, const std::string& AssetPath) -> sol::object
			{
				AAvaShapeActor* Actor = FindShapeActor(Label);
				UAvaShapeDynamicMeshBase* Mesh = Actor ? Actor->GetDynamicMesh() : nullptr;
				if (!Actor || !Mesh || AssetPath.empty())
				{
					return sol::lua_nil;
				}

				UStaticMesh* StaticMesh = CreateOrLoadStaticMeshAsset(UTF8_TO_TCHAR(AssetPath.c_str()));
				if (!StaticMesh)
				{
					return sol::lua_nil;
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ExportShapeStaticMesh", "NeoStack: Export Motion Design Shape Static Mesh"));
				StaticMesh->Modify();
				const bool bExported = Mesh->ExportToStaticMesh(StaticMesh);
				if (!bExported)
				{
					return sol::lua_nil;
				}

				StaticMesh->MarkPackageDirty();
				if (UPackage* Package = StaticMesh->GetOutermost())
				{
					Package->MarkPackageDirty();
				}

				sol::table Info = BuildStaticMeshInfo(sol::state_view(InnerS), StaticMesh);
				Info["exported"] = true;
				return sol::make_object(InnerS, Info);
			});

			Handle.set_function("delete", [](sol::this_state InnerS, sol::table, const std::string& Label) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				AAvaShapeActor* Actor = FindShapeActor(Label);
				if (!World || !Actor)
				{
					return sol::make_object(InnerS, false);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "DeleteShape", "NeoStack: Delete Motion Design Shape"));
				const bool bDeleted = World->EditorDestroyActor(Actor, true);
				return sol::make_object(InnerS, bDeleted);
			});

			return sol::make_object(S, Handle);
		});

			Lua.set_function("ava_dynamic_mesh_info", [](sol::this_state S, const std::string& ActorId) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				AActor* Actor = World ? NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(ActorId.c_str())) : nullptr;
				if (!Actor)
				{
					return sol::lua_nil;
				}
				return sol::make_object(S, BuildDynamicMeshInfo(sol::state_view(S), Actor));
			});

			Lua.set_function("ava_remote_control_controlled_actors_proof", [](sol::this_state S, const std::string& ActorId, const std::string& ControllerName) -> sol::object
			{
				return sol::make_object(S, BuildRemoteControlControlledActorsProof(sol::state_view(S), ActorId, ControllerName));
			});

			Lua.set_function("ava_remote_control_rebind_proof", [](sol::this_state S, const std::string& TargetActorId, const std::string& DecoyActorId, const std::string& ExposedLabelSuffix) -> sol::object
			{
				return sol::make_object(S, BuildRemoteControlRebindProof(sol::state_view(S), TargetActorId, DecoyActorId, ExposedLabelSuffix));
			});

			Lua.set_function("ava_texts", [](sol::this_state S) -> sol::object
		{
			sol::state_view State(S);
			sol::table Handle = State.create_table();

			Handle.set_function("add_motion_design_text", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
				if (!World || !Level || !GEditor)
				{
					return sol::lua_nil;
				}

				const std::string Label = Options.get_or<std::string>("label", "");
				if (Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddMotionDesignText", "NeoStack: Add Motion Design Text"));
				AText3DActor* Actor = Cast<AText3DActor>(GEditor->AddActor(Level, AText3DActor::StaticClass(), FTransform(Location), true, RF_Transactional));
				UText3DComponent* Component = Actor ? Actor->GetText3DComponent() : nullptr;
				if (!Actor || !Component)
				{
					if (Actor)
					{
						World->EditorDestroyActor(Actor, false);
					}
					return sol::lua_nil;
				}

				Actor->Modify();
				Component->Modify();
				Actor->SetActorLabel(UTF8_TO_TCHAR(Label.c_str()), false);

				// Mirrors UAvaTextActorFactory::PostSpawnActor in UE 5.7.
				Component->SetExtrude(0.0f);
				Component->SetScaleProportionally(false);
				Component->SetMaxWidth(100.0f);
				Component->SetMaxHeight(100.0f);
				ApplyTextOptions(Component, Options);
				Actor->MarkPackageDirty();

				return sol::make_object(InnerS, BuildTextInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("configure", [](sol::this_state InnerS, sol::table, const std::string& Label, sol::table Options) -> sol::object
			{
				AText3DActor* Actor = FindTextActor(Label);
				UText3DComponent* Component = Actor ? Actor->GetText3DComponent() : nullptr;
				if (!Actor || !Component)
				{
					return sol::lua_nil;
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureMotionDesignText", "NeoStack: Configure Motion Design Text"));
				Actor->Modify();
				Component->Modify();
				ApplyTextOptions(Component, Options);
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildTextInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("info", [](sol::this_state InnerS, sol::table, const std::string& Label) -> sol::object
			{
				AText3DActor* Actor = FindTextActor(Label);
				if (!Actor)
				{
					return sol::lua_nil;
				}
				return sol::make_object(InnerS, BuildTextInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("delete", [](sol::this_state InnerS, sol::table, const std::string& Label) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				AText3DActor* Actor = FindTextActor(Label);
				if (!World || !Actor)
				{
					return sol::make_object(InnerS, false);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "DeleteMotionDesignText", "NeoStack: Delete Motion Design Text"));
				const bool bDeleted = World->EditorDestroyActor(Actor, true);
				return sol::make_object(InnerS, bDeleted);
			});

			return sol::make_object(S, Handle);
		});

		Lua.set_function("ava_legacy_texts", [](sol::this_state S) -> sol::object
		{
			sol::state_view State(S);
			sol::table Handle = State.create_table();

			Handle.set_function("add", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
				if (!World || !Level)
				{
					return sol::lua_nil;
				}

				const std::string Label = Options.get_or<std::string>("label", "");
				if (Label.empty())
				{
					return sol::lua_nil;
				}

				FVector Location = FVector::ZeroVector;
				if (sol::optional<sol::table> LocationTable = Options.get<sol::optional<sol::table>>("location"))
				{
					Location = TableToVector(*LocationTable);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddLegacyMotionDesignText", "NeoStack: Add Legacy Motion Design Text"));
				FActorSpawnParameters SpawnParams;
				SpawnParams.OverrideLevel = Level;
				SpawnParams.ObjectFlags = RF_Transactional;
				AAvaTextActor* Actor = World->SpawnActor<AAvaTextActor>(AAvaTextActor::StaticClass(), FTransform(Location), SpawnParams);
				if (!Actor)
				{
					return sol::lua_nil;
				}

				Actor->Modify();
				Actor->SetActorLabel(UTF8_TO_TCHAR(Label.c_str()), false);
				if (UText3DComponent* Component = Actor->GetText3DComponent())
				{
					Component->Modify();
				}
				ApplyLegacyTextColorOptions(Actor, Options);
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildLegacyTextInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("configure_color", [](sol::this_state InnerS, sol::table, const std::string& Label, sol::table Options) -> sol::object
			{
				AAvaTextActor* Actor = FindLegacyTextActor(Label);
				if (!Actor)
				{
					return sol::lua_nil;
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureLegacyMotionDesignTextColor", "NeoStack: Configure Legacy Motion Design Text Color"));
				Actor->Modify();
				if (UText3DComponent* Component = Actor->GetText3DComponent())
				{
					Component->Modify();
				}
				ApplyLegacyTextColorOptions(Actor, Options);
				Actor->MarkPackageDirty();
				return sol::make_object(InnerS, BuildLegacyTextInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("info", [](sol::this_state InnerS, sol::table, const std::string& Label) -> sol::object
			{
				AAvaTextActor* Actor = FindLegacyTextActor(Label);
				if (!Actor)
				{
					return sol::lua_nil;
				}
				return sol::make_object(InnerS, BuildLegacyTextInfo(sol::state_view(InnerS), Actor));
			});

			Handle.set_function("delete", [](sol::this_state InnerS, sol::table, const std::string& Label) -> sol::object
			{
				UWorld* World = GetEditorWorld();
				AAvaTextActor* Actor = FindLegacyTextActor(Label);
				if (!World || !Actor)
				{
					return sol::make_object(InnerS, false);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "DeleteLegacyMotionDesignText", "NeoStack: Delete Legacy Motion Design Text"));
				const bool bDeleted = World->EditorDestroyActor(Actor, true);
				return sol::make_object(InnerS, bDeleted);
			});

			return sol::make_object(S, Handle);
		});

			Lua.set_function("text3d_settings", [](sol::this_state S) -> sol::object
			{
				sol::state_view State(S);
				sol::table Handle = State.create_table();

			Handle.set_function("info", [](sol::this_state InnerS, sol::table) -> sol::object
			{
				return sol::make_object(InnerS, BuildTextSettingsInfo(sol::state_view(InnerS)));
			});

			Handle.set_function("configure", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
			{
				UText3DProjectSettings* Settings = UText3DProjectSettings::GetMutable();
				if (!Settings)
				{
					return sol::lua_nil;
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureText3DSettings", "NeoStack: Configure Text3D Settings"));
				ApplyTextSettingsOptions(Settings, Options);
				return sol::make_object(InnerS, BuildTextSettingsInfo(sol::state_view(InnerS)));
			});

				return sol::make_object(S, Handle);
			});

			Lua.set_function("ava_scene", [](sol::this_state S) -> sol::object
			{
				sol::state_view State(S);
				sol::table Handle = State.create_table();

				Handle.set_function("ensure", [](sol::this_state InnerS, sol::table) -> sol::object
				{
					return BuildSceneInfo(sol::state_view(InnerS), true);
				});

				Handle.set_function("info", [](sol::this_state InnerS, sol::table) -> sol::object
				{
					return BuildSceneInfo(sol::state_view(InnerS), false);
				});

				Handle.set_function("configure", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
				{
					AAvaScene* Scene = GetMotionDesignScene(true);
					UAvaSceneSettings* Settings = Scene ? Scene->GetSceneSettings() : nullptr;
					if (!Scene || !Settings)
					{
						return sol::lua_nil;
					}

					if (sol::optional<std::string> SceneRig = Options.get<sol::optional<std::string>>("scene_rig"))
					{
						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureSceneSettings", "NeoStack: Configure Motion Design Scene Settings"));
						Scene->Modify();
						Settings->Modify();
						Settings->SetSceneRig(FSoftObjectPath(UTF8_TO_TCHAR(SceneRig->c_str())));
						Scene->MarkPackageDirty();
					}

					return BuildSceneInfo(sol::state_view(InnerS), false);
				});

				Handle.set_function("configure_transition_logic", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
				{
					sol::state_view State(InnerS);
					UWorld* World = GetEditorWorld();
					ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
					AAvaScene* Scene = GetMotionDesignScene(true);
					UAvaTransitionSubsystem* TransitionSubsystem = World ? World->GetSubsystem<UAvaTransitionSubsystem>() : nullptr;
					IAvaTransitionBehavior* TransitionBehavior = TransitionSubsystem ? TransitionSubsystem->GetOrCreateTransitionBehavior(Level) : nullptr;
					UAvaTransitionTree* TransitionTree = TransitionBehavior ? TransitionBehavior->GetTransitionTree() : nullptr;
					if (!World || !Level || !Scene || !TransitionTree)
					{
						return sol::lua_nil;
					}

					int32 SetCount = 0;
					FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureSceneTransitionLogic", "NeoStack: Configure Motion Design Scene Transition Logic"));
					Scene->Modify();
					TransitionBehavior->AsUObject().Modify();
					TransitionTree->Modify();

					if (sol::optional<bool> bEnabled = Options.get<sol::optional<bool>>("enabled"))
					{
						TransitionTree->SetEnabled(*bEnabled);
						++SetCount;
					}

					std::string ModeValue = Options.get_or<std::string>("instancing_mode", "");
					if (ModeValue.empty())
					{
						ModeValue = Options.get_or<std::string>("mode", "");
					}
					if (!ModeValue.empty())
					{
						TransitionTree->SetInstancingMode(ParseAvalancheInstancingMode(ModeValue, TransitionTree->GetInstancingMode()));
						++SetCount;
					}

					if (sol::optional<sol::table> LayerOptions = Options.get<sol::optional<sol::table>>("transition_layer"))
					{
						std::string CollectionPath = LayerOptions->get_or<std::string>("collection", "");
						if (CollectionPath.empty())
						{
							CollectionPath = LayerOptions->get_or<std::string>("collection_path", "");
						}
						const std::string TagNameValue = LayerOptions->get_or<std::string>("tag", "");
						UAvaTagCollection* Collection = LoadTagCollection(CollectionPath);
						const FAvaTagHandle LayerHandle = FindTagHandle(Collection, FName(UTF8_TO_TCHAR(TagNameValue.c_str())));
						if (!LayerHandle.IsValid())
						{
							return sol::lua_nil;
						}
						TransitionTree->SetTransitionLayer(LayerHandle);
						++SetCount;
					}
					else if (Options.get_or("clear_transition_layer", false))
					{
						TransitionTree->SetTransitionLayer(FAvaTagHandle());
						++SetCount;
					}

					if (SetCount == 0)
					{
						return sol::lua_nil;
					}

					TransitionBehavior->AsUObject().MarkPackageDirty();
					TransitionTree->MarkPackageDirty();
					Scene->MarkPackageDirty();

					sol::table Result = State.create_table();
					Result["valid"] = true;
					Result["enabled"] = TransitionTree->IsEnabled();
					Result["instancing_mode"] = std::string(TCHAR_TO_UTF8(AvalancheInstancingModeToString(TransitionTree->GetInstancingMode())));
					Result["transition_layer"] = BuildAvaTransitionLayerInfo(State, TransitionTree->GetTransitionLayer());
					Result["transition_layer_valid"] = TransitionTree->GetTransitionLayer().IsValid();
					Result["transition_layer_name"] = std::string(TCHAR_TO_UTF8(*TransitionTree->GetTransitionLayer().ToName().ToString()));
					Result["transition_layer_source"] = TransitionTree->GetTransitionLayer().Source
						? std::string(TCHAR_TO_UTF8(*FSoftObjectPath(TransitionTree->GetTransitionLayer().Source).ToString()))
						: std::string();
					Result["behavior_actor"] = std::string(TCHAR_TO_UTF8(*TransitionBehavior->AsUObject().GetName()));
					return sol::make_object(InnerS, Result);
				});

				Handle.set_function("tree_info", [](sol::this_state InnerS, sol::table, sol::optional<std::string> ActorId) -> sol::object
				{
					return sol::make_object(InnerS, BuildSceneTreeInfo(sol::state_view(InnerS), ActorId));
				});

				Handle.set_function("add_tree_root", [](sol::this_state InnerS, sol::table, const std::string& ActorId) -> sol::object
				{
					return AddSceneTreeActor(sol::state_view(InnerS), ActorId, sol::optional<std::string>());
				});

				Handle.set_function("add_tree_child", [](sol::this_state InnerS, sol::table, const std::string& ActorId, const std::string& ParentActorId) -> sol::object
				{
					return AddSceneTreeActor(sol::state_view(InnerS), ActorId, ParentActorId);
				});

				Handle.set_function("add_name_attribute", [](sol::this_state InnerS, sol::table, const std::string& Name) -> sol::object
				{
					AAvaScene* Scene = GetMotionDesignScene(true);
					UAvaAttributeContainer* AttributeContainer = Scene ? Scene->GetAttributeContainer() : nullptr;
					if (!Scene || !AttributeContainer)
					{
						return sol::lua_nil;
					}

					const FName AttributeName(UTF8_TO_TCHAR(Name.c_str()));
					if (AttributeName.IsNone())
					{
						return sol::lua_nil;
					}

					FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddSceneNameAttribute", "NeoStack: Add Motion Design Scene Name Attribute"));
					Scene->Modify();
					AttributeContainer->Modify();
					const bool bAdded = AttributeContainer->AddNameAttribute(AttributeName);
					Scene->MarkPackageDirty();

					sol::table Result = BuildSceneInfo(sol::state_view(InnerS), false).as<sol::table>();
					Result["added"] = bAdded;
					Result["contains"] = AttributeContainer->ContainsNameAttribute(AttributeName);
					return sol::make_object(InnerS, Result);
				});

				Handle.set_function("remove_name_attribute", [](sol::this_state InnerS, sol::table, const std::string& Name) -> sol::object
				{
					AAvaScene* Scene = GetMotionDesignScene(false);
					UAvaAttributeContainer* AttributeContainer = Scene ? Scene->GetAttributeContainer() : nullptr;
					if (!Scene || !AttributeContainer)
					{
						return sol::lua_nil;
					}

					const FName AttributeName(UTF8_TO_TCHAR(Name.c_str()));
					FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveSceneNameAttribute", "NeoStack: Remove Motion Design Scene Name Attribute"));
					Scene->Modify();
					AttributeContainer->Modify();
					const bool bRemoved = AttributeContainer->RemoveNameAttribute(AttributeName);
					Scene->MarkPackageDirty();

					sol::table Result = BuildSceneInfo(sol::state_view(InnerS), false).as<sol::table>();
					Result["removed"] = bRemoved;
					Result["contains"] = AttributeContainer->ContainsNameAttribute(AttributeName);
					return sol::make_object(InnerS, Result);
				});

				Handle.set_function("contains_name_attribute", [](sol::table, const std::string& Name) -> bool
				{
					AAvaScene* Scene = GetMotionDesignScene(false);
					UAvaAttributeContainer* AttributeContainer = Scene ? Scene->GetAttributeContainer() : nullptr;
					return AttributeContainer && AttributeContainer->ContainsNameAttribute(FName(UTF8_TO_TCHAR(Name.c_str())));
				});

				Handle.set_function("add_tag_attribute", [](sol::this_state InnerS, sol::table, const std::string& CollectionPath, const std::string& TagName) -> sol::object
				{
					sol::state_view State(InnerS);
					AAvaScene* Scene = GetMotionDesignScene(true);
					UAvaAttributeContainer* AttributeContainer = Scene ? Scene->GetAttributeContainer() : nullptr;
					UAvaTagCollection* Collection = LoadTagCollection(CollectionPath);
					const FAvaTagHandle TagHandle = FindTagHandle(Collection, FName(UTF8_TO_TCHAR(TagName.c_str())));
					if (!Scene || !AttributeContainer || !TagHandle.IsValid())
					{
						return sol::lua_nil;
					}

					FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddSceneTagAttribute", "NeoStack: Add Motion Design Scene Tag Attribute"));
					Scene->Modify();
					AttributeContainer->Modify();
					const bool bAdded = AttributeContainer->AddTagAttribute(TagHandle);
					Scene->MarkPackageDirty();

					sol::table Result = BuildSceneInfo(State, false).as<sol::table>();
					TArray<FAvaTagHandle> Handles;
					Handles.Add(TagHandle);
					Result["added"] = bAdded;
					Result["contains"] = AttributeContainer->ContainsTagAttribute(TagHandle);
					Result["tags"] = BuildSceneTagContainsInfo(State, AttributeContainer, Handles);
					return sol::make_object(InnerS, Result);
				});

				Handle.set_function("remove_tag_attribute", [](sol::this_state InnerS, sol::table, const std::string& CollectionPath, const std::string& TagName) -> sol::object
				{
					sol::state_view State(InnerS);
					AAvaScene* Scene = GetMotionDesignScene(false);
					UAvaAttributeContainer* AttributeContainer = Scene ? Scene->GetAttributeContainer() : nullptr;
					UAvaTagCollection* Collection = LoadTagCollection(CollectionPath);
					const FAvaTagHandle TagHandle = FindTagHandle(Collection, FName(UTF8_TO_TCHAR(TagName.c_str())));
					if (!Scene || !AttributeContainer || !TagHandle.IsValid())
					{
						return sol::lua_nil;
					}

					FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveSceneTagAttribute", "NeoStack: Remove Motion Design Scene Tag Attribute"));
					Scene->Modify();
					AttributeContainer->Modify();
					const bool bRemoved = AttributeContainer->RemoveTagAttribute(TagHandle);
					Scene->MarkPackageDirty();

					sol::table Result = BuildSceneInfo(State, false).as<sol::table>();
					TArray<FAvaTagHandle> Handles;
					Handles.Add(TagHandle);
					Result["removed"] = bRemoved;
					Result["contains"] = AttributeContainer->ContainsTagAttribute(TagHandle);
					Result["tags"] = BuildSceneTagContainsInfo(State, AttributeContainer, Handles);
					return sol::make_object(InnerS, Result);
				});

				Handle.set_function("contains_tag_attribute", [](sol::table, const std::string& CollectionPath, const std::string& TagName) -> bool
				{
					AAvaScene* Scene = GetMotionDesignScene(false);
					UAvaAttributeContainer* AttributeContainer = Scene ? Scene->GetAttributeContainer() : nullptr;
					UAvaTagCollection* Collection = LoadTagCollection(CollectionPath);
					const FAvaTagHandle TagHandle = FindTagHandle(Collection, FName(UTF8_TO_TCHAR(TagName.c_str())));
					return AttributeContainer && TagHandle.IsValid() && AttributeContainer->ContainsTagAttribute(TagHandle);
				});

				Handle.set_function("add_tag_container_attribute", [](sol::this_state InnerS, sol::table, const std::string& CollectionPath, sol::table Options) -> sol::object
				{
					sol::state_view State(InnerS);
					AAvaScene* Scene = GetMotionDesignScene(true);
					UAvaSceneSettings* Settings = Scene ? Scene->GetSceneSettings() : nullptr;
					UAvaAttributeContainer* AttributeContainer = Scene ? Scene->GetAttributeContainer() : nullptr;
					UAvaTagCollection* Collection = LoadTagCollection(CollectionPath);
					const TArray<FAvaTagHandle> Handles = ResolveTagHandles(Collection, Options);
					if (!Scene || !Settings || !AttributeContainer || Handles.IsEmpty())
					{
						return sol::lua_nil;
					}

					FAvaTagHandleContainer TagContainer;
					for (const FAvaTagHandle& Handle : Handles)
					{
						TagContainer.AddTagHandle(Handle);
					}

					UAvaTagContainerAttribute* ContainerAttribute = NewObject<UAvaTagContainerAttribute>(Settings, NAME_None, RF_Transactional);
					if (!ContainerAttribute)
					{
						return sol::lua_nil;
					}
					ContainerAttribute->SetTagContainer(TagContainer);

					FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddSceneTagContainerAttribute", "NeoStack: Add Motion Design Scene Tag Container Attribute"));
					Scene->Modify();
					Settings->Modify();
					AttributeContainer->Modify();
					const bool bAddedToSettings = AddObjectArrayAttribute(Settings, UAvaSceneSettings::GetSceneAttributesPropertyName(), ContainerAttribute);
					const bool bAddedToRuntime = AddObjectArrayAttribute(AttributeContainer, TEXT("SceneAttributes"), ContainerAttribute);
					if (!bAddedToSettings || !bAddedToRuntime)
					{
						return sol::lua_nil;
					}
					Scene->MarkPackageDirty();

					sol::table Result = BuildSceneInfo(State, false).as<sol::table>();
					Result["added"] = true;
					Result["tags"] = BuildSceneTagContainsInfo(State, AttributeContainer, Handles);
					return sol::make_object(InnerS, Result);
				});

				Handle.set_function("remove_tag_container_attribute", [](sol::this_state InnerS, sol::table, const std::string& CollectionPath, sol::table Options) -> sol::object
				{
					sol::state_view State(InnerS);
					AAvaScene* Scene = GetMotionDesignScene(false);
					UAvaSceneSettings* Settings = Scene ? Scene->GetSceneSettings() : nullptr;
					UAvaAttributeContainer* AttributeContainer = Scene ? Scene->GetAttributeContainer() : nullptr;
					UAvaTagCollection* Collection = LoadTagCollection(CollectionPath);
					const TArray<FAvaTagHandle> Handles = ResolveTagHandles(Collection, Options);
					if (!Scene || !Settings || !AttributeContainer || Handles.IsEmpty())
					{
						return sol::lua_nil;
					}

					FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveSceneTagContainerAttribute", "NeoStack: Remove Motion Design Scene Tag Container Attribute"));
					Scene->Modify();
					Settings->Modify();
					AttributeContainer->Modify();
					const int32 RemovedSettingsCount = RemoveObjectArrayTagContainerAttributes(Settings, UAvaSceneSettings::GetSceneAttributesPropertyName(), Handles);
					const int32 RemovedRuntimeCount = RemoveObjectArrayTagContainerAttributes(AttributeContainer, TEXT("SceneAttributes"), Handles);
					Scene->MarkPackageDirty();

					sol::table Result = BuildSceneInfo(State, false).as<sol::table>();
					Result["removed_count"] = RemovedSettingsCount;
					Result["removed_runtime_count"] = RemovedRuntimeCount;
					Result["tags"] = BuildSceneTagContainsInfo(State, AttributeContainer, Handles);
					return sol::make_object(InnerS, Result);
				});

					return sol::make_object(S, Handle);
				});

				Lua.set_function("ava_scene_rig", [](sol::this_state S) -> sol::object
				{
					sol::state_view State(S);
					sol::table Handle = State.create_table();

					Handle.set_function("info", [](sol::this_state InnerS, sol::table, sol::optional<std::string> ActorId) -> sol::object
					{
						return sol::make_object(InnerS, BuildSceneRigInfo(sol::state_view(InnerS), ActorId));
					});

					Handle.set_function("set_active", [](sol::this_state InnerS, sol::table, const std::string& SceneRigAssetPath) -> sol::object
					{
						UWorld* World = GetEditorWorld();
						if (!World)
						{
							return sol::lua_nil;
						}

						GetMotionDesignScene(true);
						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "SetActiveSceneRig", "NeoStack: Set Active Motion Design Scene Rig"));
						ULevelStreaming* StreamingLevel = IAvaSceneRigEditorModule::Get().SetActiveSceneRig(
							World,
							FSoftObjectPath(UTF8_TO_TCHAR(SceneRigAssetPath.c_str())));
						if (!IsValid(StreamingLevel))
						{
							return sol::lua_nil;
						}
						return sol::make_object(InnerS, BuildSceneRigInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("remove_all", [](sol::this_state InnerS, sol::table) -> sol::object
					{
						UWorld* World = GetEditorWorld();
						if (!World)
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveActiveSceneRig", "NeoStack: Remove Active Motion Design Scene Rig"));
						const bool bRemoved = IAvaSceneRigEditorModule::Get().RemoveAllSceneRigs(World);
						sol::table Result = BuildSceneRigInfo(sol::state_view(InnerS));
						Result["removed"] = bRemoved;
						return sol::make_object(InnerS, Result);
					});

					Handle.set_function("add_actor", [](sol::this_state InnerS, sol::table, const std::string& ActorId) -> sol::object
					{
						UWorld* World = GetEditorWorld();
						AActor* Actor = World ? NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(ActorId.c_str())) : nullptr;
						if (!World || !Actor || !UAvaSceneRigSubsystem::IsSupportedActorClass(Actor->GetClass()))
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddActorToActiveSceneRig", "NeoStack: Add Actor To Active Scene Rig"));
						TArray<AActor*> Actors;
						Actors.Add(Actor);
						IAvaSceneRigEditorModule::Get().AddActiveSceneRigActors(World, Actors);
						return sol::make_object(InnerS, BuildSceneRigInfo(sol::state_view(InnerS), ActorId));
					});

					Handle.set_function("remove_actor", [](sol::this_state InnerS, sol::table, const std::string& ActorId) -> sol::object
					{
						UWorld* World = GetEditorWorld();
						AActor* Actor = FindActorInEditorOrActiveSceneRig(ActorId);
						if (!World || !Actor)
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveActorFromActiveSceneRig", "NeoStack: Remove Actor From Active Scene Rig"));
						TArray<AActor*> Actors;
						Actors.Add(Actor);
						IAvaSceneRigEditorModule::Get().RemoveActiveSceneRigActors(World, Actors);
						return sol::make_object(InnerS, BuildSceneRigInfo(sol::state_view(InnerS), ActorId));
					});

					return sol::make_object(S, Handle);
				});

				Lua.set_function("ava_outliner_settings", [](sol::this_state S) -> sol::object
				{
					sol::state_view State(S);
					sol::table Handle = State.create_table();

					Handle.set_function("info", [](sol::this_state InnerS, sol::table) -> sol::object
					{
						return BuildOutlinerSettingsInfo(sol::state_view(InnerS));
					});

					Handle.set_function("configure", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
					{
						UObject* Settings = GetOutlinerSettings();
						if (!Settings)
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureOutlinerSettings", "NeoStack: Configure Motion Design Outliner Settings"));
						Settings->Modify();
						if (sol::optional<bool> Value = Options.get<sol::optional<bool>>("always_show_visibility_state"))
						{
							SetOutlinerBool(Settings, TEXT("bAlwaysShowVisibilityState"), *Value);
						}
						if (sol::optional<bool> Value = Options.get<sol::optional<bool>>("always_show_lock_state"))
						{
							SetOutlinerBool(Settings, TEXT("bAlwaysShowLockState"), *Value);
						}
						if (sol::optional<bool> Value = Options.get<sol::optional<bool>>("use_muted_hierarchy"))
						{
							SetOutlinerBool(Settings, TEXT("bUseMutedHierarchy"), *Value);
						}
						if (sol::optional<bool> Value = Options.get<sol::optional<bool>>("auto_expand_to_selection"))
						{
							SetOutlinerBool(Settings, TEXT("bAutoExpandToSelection"), *Value);
						}
						if (sol::optional<int32> Value = Options.get<sol::optional<int32>>("item_default_view_mode"))
						{
							SetOutlinerInt(Settings, TEXT("ItemDefaultViewMode"), *Value);
						}
						if (sol::optional<int32> Value = Options.get<sol::optional<int32>>("item_proxy_view_mode"))
						{
							SetOutlinerInt(Settings, TEXT("ItemProxyViewMode"), *Value);
						}
						if (sol::optional<sol::table> Rows = Options.get<sol::optional<sol::table>>("colors"))
						{
							ApplyOutlinerColorMap(Settings, *Rows);
						}
						if (sol::optional<sol::table> Rows = Options.get<sol::optional<sol::table>>("custom_filters"))
						{
							ApplyOutlinerFilterMap(Settings, *Rows);
						}
						Settings->SaveConfig();
						return BuildOutlinerSettingsInfo(sol::state_view(InnerS));
					});

					return sol::make_object(S, Handle);
				});

				Lua.set_function("ava_sequencer_settings", [](sol::this_state S) -> sol::object
				{
					sol::state_view State(S);
					sol::table Handle = State.create_table();

					Handle.set_function("info", [](sol::this_state InnerS, sol::table) -> sol::object
					{
						return sol::make_object(InnerS, BuildAvaSequencerSettingsInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("configure", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
					{
						UAvaSequencerSettings* Settings = GetMutableDefault<UAvaSequencerSettings>();
						if (!Settings)
						{
							return sol::lua_nil;
						}
						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureSequencerSettings", "NeoStack: Configure Motion Design Sequencer Settings"));
						Settings->Modify();
						if (sol::optional<sol::table> DisplayRate = Options.get<sol::optional<sol::table>>("display_rate"))
						{
							SetSequencerDisplayRate(Settings, TableToFrameRate(*DisplayRate, Settings->GetDisplayRate()));
						}
						if (sol::optional<double> StartTime = Options.get<sol::optional<double>>("start_time"))
						{
							SetSequencerDouble(Settings, TEXT("StartTime"), *StartTime);
						}
						if (sol::optional<double> EndTime = Options.get<sol::optional<double>>("end_time"))
						{
							SetSequencerDouble(Settings, TEXT("EndTime"), FMath::Max(0.0, *EndTime));
						}
						SaveSequencerSettings(Settings);
						return sol::make_object(InnerS, BuildAvaSequencerSettingsInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("add_custom_preset", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
					{
						UAvaSequencerSettings* Settings = GetMutableDefault<UAvaSequencerSettings>();
						if (!Settings)
						{
							return sol::lua_nil;
						}
						FAvaSequencePreset Preset = TableToSequencePreset(Options);
						if (Preset.PresetName.IsNone())
						{
							return sol::lua_nil;
						}
						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddSequencerCustomPreset", "NeoStack: Add Motion Design Sequencer Custom Preset"));
						Settings->Modify();
						if (!AddSequencerPreset(Settings, Preset))
						{
							return sol::lua_nil;
						}
						SaveSequencerSettings(Settings);
						return sol::make_object(InnerS, BuildAvaSequencerSettingsInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("remove_custom_preset", [](sol::this_state InnerS, sol::table, const std::string& Name) -> sol::object
					{
						UAvaSequencerSettings* Settings = GetMutableDefault<UAvaSequencerSettings>();
						FSetProperty* SetProperty = FindSequencerSetProperty(TEXT("CustomSequencePresets"));
						if (!Settings || !SetProperty)
						{
							return sol::lua_nil;
						}
						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveSequencerCustomPreset", "NeoStack: Remove Motion Design Sequencer Custom Preset"));
						Settings->Modify();
						RemoveNamedSetStruct(Settings, SetProperty, TEXT("PresetName"), LuaStringToFName(Name));
						SaveSequencerSettings(Settings);
						return sol::make_object(InnerS, BuildAvaSequencerSettingsInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("add_custom_group", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
					{
						UAvaSequencerSettings* Settings = GetMutableDefault<UAvaSequencerSettings>();
						if (!Settings)
						{
							return sol::lua_nil;
						}
						FAvaSequencePresetGroup Group = TableToSequencePresetGroup(Options);
						if (Group.GroupName.IsNone())
						{
							return sol::lua_nil;
						}
						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddSequencerCustomPresetGroup", "NeoStack: Add Motion Design Sequencer Custom Preset Group"));
						Settings->Modify();
						if (!AddSequencerPresetGroup(Settings, Group))
						{
							return sol::lua_nil;
						}
						SaveSequencerSettings(Settings);
						return sol::make_object(InnerS, BuildAvaSequencerSettingsInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("remove_custom_group", [](sol::this_state InnerS, sol::table, const std::string& Name) -> sol::object
					{
						UAvaSequencerSettings* Settings = GetMutableDefault<UAvaSequencerSettings>();
						FSetProperty* SetProperty = FindSequencerSetProperty(TEXT("CustomPresetGroups"));
						if (!Settings || !SetProperty)
						{
							return sol::lua_nil;
						}
						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveSequencerCustomPresetGroup", "NeoStack: Remove Motion Design Sequencer Custom Preset Group"));
						Settings->Modify();
						RemoveNamedSetStruct(Settings, SetProperty, TEXT("GroupName"), LuaStringToFName(Name));
						SaveSequencerSettings(Settings);
						return sol::make_object(InnerS, BuildAvaSequencerSettingsInfo(sol::state_view(InnerS)));
					});

					return sol::make_object(S, Handle);
				});

				Lua.set_function("ava_mask_modifier", [](sol::this_state S, const std::string& ActorId, int32 ModifierIndex) -> sol::object
				{
					sol::state_view State(S);
					UAvaMask2DBaseModifier* Modifier = FindMaskModifier(ActorId, ModifierIndex);
					if (!Modifier)
					{
						return sol::lua_nil;
					}

					TWeakObjectPtr<UAvaMask2DBaseModifier> ModifierPtr = Modifier;
					sol::table Handle = State.create_table();
					Handle.set_function("info", [ModifierPtr](sol::this_state InnerS, sol::table) -> sol::object
					{
						return sol::make_object(InnerS, BuildMaskModifierInfo(sol::state_view(InnerS), ModifierPtr.Get()));
					});
					Handle.set_function("configure", [ModifierPtr](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
					{
						UAvaMask2DBaseModifier* LocalModifier = ModifierPtr.Get();
						if (!LocalModifier)
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureMask2DModifier", "NeoStack: Configure Motion Design Mask Modifier"));
						LocalModifier->Modify();
						ApplyMaskModifierOptions(LocalModifier, Options);
						return sol::make_object(InnerS, BuildMaskModifierInfo(sol::state_view(InnerS), LocalModifier));
					});
					return sol::make_object(S, Handle);
				});

				Lua.set_function("ava_camera_runtime", [](sol::optional<sol::table> OptsOpt, sol::this_state S) -> sol::object
				{
					sol::state_view State(S);
					sol::table Result = State.create_table();
					Result["ok"] = false;

					UWorld* World = GetPIEWorld();
					Result["has_pie_world"] = World != nullptr;
					if (!World)
					{
						Result["message"] = std::string("No active Play In Editor world. Call playtest_start() first.");
						return sol::make_object(S, Result);
					}

					ULevel* Level = World->PersistentLevel;
					UAvaCameraSubsystem* CameraSubsystem = UAvaCameraSubsystem::Get(World);
					Result["has_subsystem"] = CameraSubsystem != nullptr;
					if (!Level || !CameraSubsystem)
					{
						Result["message"] = std::string("Motion Design camera subsystem or persistent level is unavailable.");
						return sol::make_object(S, Result);
					}

					sol::table Opts = OptsOpt.value_or(State.create_table());
					if (Opts.get_or("unregister_scene", false))
					{
						CameraSubsystem->UnregisterScene(Level);
						Result["unregistered_scene"] = true;
					}
					if (Opts.get_or("register_scene", false))
					{
						CameraSubsystem->RegisterScene(Level);
						Result["registered_scene"] = true;
					}
					if (Opts.get_or("conditional_update", false))
					{
						Result["conditional_update_result"] = CameraSubsystem->ConditionallyUpdateViewTarget(Level);
					}
					if (Opts.get_or("update_view_target", false))
					{
						Result["conditional_update_result"] = CameraSubsystem->ConditionallyUpdateViewTarget(Level);
						Result["updated_view_target"] = true;
					}

					APlayerController* PlayerController = World->GetFirstPlayerController();
					Result["has_player_controller"] = PlayerController != nullptr;
					AActor* ViewTarget = PlayerController ? PlayerController->GetViewTarget() : nullptr;
					Result["has_view_target"] = ViewTarget != nullptr;
					Result["view_target_label"] = ViewTarget ? std::string(TCHAR_TO_UTF8(*ViewTarget->GetActorLabel())) : std::string();
					Result["view_target_name"] = ViewTarget ? std::string(TCHAR_TO_UTF8(*ViewTarget->GetName())) : std::string();
					Result["view_target_class"] = ViewTarget ? std::string(TCHAR_TO_UTF8(*ViewTarget->GetClass()->GetName())) : std::string();
					Result["blend_time_to_go"] = (PlayerController && PlayerController->PlayerCameraManager)
						? PlayerController->PlayerCameraManager->BlendTimeToGo
						: 0.0f;
					Result["ok"] = true;
					Result["message"] = std::string();
					return sol::make_object(S, Result);
				});

				Lua.set_function("ava_component_visualizers", [](sol::this_state S) -> sol::object
				{
					sol::state_view State(S);
					sol::table Handle = State.create_table();

					Handle.set_function("info", [](sol::this_state InnerS, sol::table) -> sol::object
					{
						return sol::make_object(InnerS, BuildComponentVisualizerSettingsInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("configure", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
					{
						IAvaComponentVisualizersSettings* SettingsInterface = IAvalancheComponentVisualizersModule::Get().GetSettings();
						UObject* SettingsObject = GetComponentVisualizerSettingsObject();
						if (!SettingsInterface || !SettingsObject)
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureComponentVisualizersSettings", "NeoStack: Configure Motion Design Component Visualizers Settings"));
						SettingsObject->Modify();
						if (sol::optional<double> SpriteSize = Options.get<sol::optional<double>>("sprite_size"))
						{
							SettingsInterface->SetSpriteSize(FMath::Max(0.0f, static_cast<float>(*SpriteSize)));
						}
						SettingsInterface->SaveSettings();
						return sol::make_object(InnerS, BuildComponentVisualizerSettingsInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("set_sprite", [](sol::this_state InnerS, sol::table, const std::string& NameText, const std::string& TexturePathText) -> sol::object
					{
						IAvaComponentVisualizersSettings* SettingsInterface = IAvalancheComponentVisualizersModule::Get().GetSettings();
						UObject* SettingsObject = GetComponentVisualizerSettingsObject();
						if (!SettingsInterface || !SettingsObject || NameText.empty() || TexturePathText.empty())
						{
							return sol::lua_nil;
						}

						UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, UTF8_TO_TCHAR(TexturePathText.c_str()));
						if (!Texture)
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "SetComponentVisualizerSprite", "NeoStack: Set Motion Design Component Visualizer Sprite"));
						SettingsObject->Modify();
						SettingsInterface->SetVisualizerSprite(FName(UTF8_TO_TCHAR(NameText.c_str())), Texture);
						SettingsInterface->SaveSettings();
						return sol::make_object(InnerS, BuildComponentVisualizerSettingsInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("remove_sprite", [](sol::this_state InnerS, sol::table, const std::string& NameText) -> sol::object
					{
						IAvaComponentVisualizersSettings* SettingsInterface = IAvalancheComponentVisualizersModule::Get().GetSettings();
						UObject* SettingsObject = GetComponentVisualizerSettingsObject();
						if (!SettingsInterface || !SettingsObject || NameText.empty())
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveComponentVisualizerSprite", "NeoStack: Remove Motion Design Component Visualizer Sprite"));
						SettingsObject->Modify();
						const bool bRemoved = RemoveComponentVisualizerSprite(SettingsObject, FName(UTF8_TO_TCHAR(NameText.c_str())));
						SettingsInterface->SaveSettings();
						sol::table Result = BuildComponentVisualizerSettingsInfo(sol::state_view(InnerS));
						Result["removed"] = bRemoved;
						return sol::make_object(InnerS, Result);
					});

					return sol::make_object(S, Handle);
				});

				Lua.set_function("ava_viewport", [](sol::this_state S) -> sol::object
				{
					sol::state_view State(S);
					sol::table Handle = State.create_table();

					Handle.set_function("info", [](sol::this_state InnerS, sol::table) -> sol::object
					{
						return sol::make_object(InnerS, BuildViewportSettingsInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("data_info", [](sol::this_state InnerS, sol::table) -> sol::object
					{
						return sol::make_object(InnerS, BuildViewportDataInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("configure", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
					{
						UAvaViewportSettings* Settings = GetMutableDefault<UAvaViewportSettings>();
						if (!Settings)
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureViewportSettings", "NeoStack: Configure Motion Design Viewport Settings"));
						Settings->Modify();
						ApplyViewportSettingsOptions(Settings, Options);
						Settings->SaveConfig();
						Settings->BroadcastSettingChanged(NAME_None);
						return sol::make_object(InnerS, BuildViewportSettingsInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("configure_data", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
					{
						UWorld* World = GetEditorWorld();
						UAvaViewportDataSubsystem* Subsystem = World ? UAvaViewportDataSubsystem::Get(World) : nullptr;
						FAvaViewportData* Data = Subsystem ? Subsystem->GetData() : nullptr;
						if (!World || !Subsystem || !Data)
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureViewportData", "NeoStack: Configure Motion Design Viewport Data"));
						Subsystem->ModifyDataSource();

						if (sol::optional<sol::table> VirtualSize = Options.get<sol::optional<sol::table>>("virtual_size"))
						{
							const FIntPoint Parsed = TableToIntPoint(*VirtualSize, Data->VirtualSize);
							Data->VirtualSize = FIntPoint(FMath::Max(0, Parsed.X), FMath::Max(0, Parsed.Y));
						}
						if (sol::optional<std::string> AspectState = Options.get<sol::optional<std::string>>("virtual_size_aspect_state"))
						{
							Data->VirtualSizeAspectRatioState = ParseViewportAspectState(*AspectState, Data->VirtualSizeAspectRatioState);
						}
						if (sol::optional<std::string> PostProcessType = Options.get<sol::optional<std::string>>("post_process_type"))
						{
							Data->PostProcessInfo.Type = ParseViewportPostProcessType(*PostProcessType, Data->PostProcessInfo.Type);
						}
						if (sol::optional<std::string> PostProcessTexture = Options.get<sol::optional<std::string>>("post_process_texture"))
						{
							Data->PostProcessInfo.Texture = TSoftObjectPtr<UTexture>(FSoftObjectPath(UTF8_TO_TCHAR(PostProcessTexture->c_str())));
						}
						if (sol::optional<double> PostProcessOpacity = Options.get<sol::optional<double>>("post_process_opacity"))
						{
							Data->PostProcessInfo.Opacity = FMath::Clamp(static_cast<float>(*PostProcessOpacity), 0.0f, 1.0f);
						}
						if (sol::optional<std::string> PilotedCameraId = Options.get<sol::optional<std::string>>("piloted_camera"))
						{
							if (PilotedCameraId->empty())
							{
								Data->PilotedCamera.Reset();
							}
							else
							{
								AActor* Actor = NeoLuaActor::FindByNameOrLabel(World, UTF8_TO_TCHAR(PilotedCameraId->c_str()));
								if (!Actor)
								{
									return sol::lua_nil;
								}
								Data->PilotedCamera = TSoftObjectPtr<AActor>(Actor);
							}
						}

						return sol::make_object(InnerS, BuildViewportDataInfo(sol::state_view(InnerS)));
					});

					Handle.set_function("save_guide_preset", [](sol::this_state InnerS, sol::table, const std::string& Name, sol::table Guides, sol::table Options) -> sol::object
					{
						UWorld* World = GetEditorWorld();
						UAvaViewportDataSubsystem* Subsystem = World ? UAvaViewportDataSubsystem::Get(World) : nullptr;
						if (!Subsystem)
						{
							return sol::lua_nil;
						}

						TArray<FAvaViewportGuideInfo> GuideInfos;
						for (int32 LuaIndex = 1;; ++LuaIndex)
						{
							sol::object Entry = Guides[LuaIndex];
							if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
							{
								break;
							}
							if (!Entry.is<sol::table>())
							{
								continue;
							}
							FAvaViewportGuideInfo& Guide = GuideInfos.AddDefaulted_GetRef();
							TableToGuideInfo(Entry.as<sol::table>(), Guide);
						}

						const FVector2f ViewportSize(
							static_cast<float>(Options.get_or("width", 1920.0)),
							static_cast<float>(Options.get_or("height", 1080.0)));
						if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
						{
							return sol::lua_nil;
						}

						const FString PresetName = UTF8_TO_TCHAR(Name.c_str());
						const bool bSaved = Subsystem->GetGuidePresetProvider().SaveGuidePreset(PresetName, GuideInfos, ViewportSize);
						sol::table Result = sol::state_view(InnerS).create_table();
						Result["saved"] = bSaved;
						Result["name"] = Name;
						Result["guide_count"] = GuideInfos.Num();
						Result["last_accessed"] = std::string(TCHAR_TO_UTF8(*Subsystem->GetGuidePresetProvider().GetLastAccessedGuidePresetName()));
						return sol::make_object(InnerS, Result);
					});

					Handle.set_function("load_guide_preset", [](sol::this_state InnerS, sol::table, const std::string& Name, sol::table Options) -> sol::object
					{
						UWorld* World = GetEditorWorld();
						UAvaViewportDataSubsystem* Subsystem = World ? UAvaViewportDataSubsystem::Get(World) : nullptr;
						if (!Subsystem)
						{
							return sol::lua_nil;
						}

						const FVector2f ViewportSize(
							static_cast<float>(Options.get_or("width", 1920.0)),
							static_cast<float>(Options.get_or("height", 1080.0)));
						if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
						{
							return sol::lua_nil;
						}

						TArray<FAvaViewportGuideInfo> Guides;
						const FString PresetName = UTF8_TO_TCHAR(Name.c_str());
						const bool bLoaded = Subsystem->GetGuidePresetProvider().LoadGuidePreset(PresetName, Guides, ViewportSize);
						sol::state_view InnerState(InnerS);
						sol::table Result = InnerState.create_table();
						Result["loaded"] = bLoaded;
						Result["name"] = Name;
						Result["guide_count"] = Guides.Num();
						Result["last_accessed"] = std::string(TCHAR_TO_UTF8(*Subsystem->GetGuidePresetProvider().GetLastAccessedGuidePresetName()));
						sol::table GuideTable = InnerState.create_table();
						for (int32 Index = 0; Index < Guides.Num(); ++Index)
						{
							GuideTable[Index + 1] = BuildGuideInfo(InnerState, Guides[Index]);
						}
						Result["guides"] = GuideTable;
						return sol::make_object(InnerS, Result);
					});

					Handle.set_function("remove_guide_preset", [](sol::this_state InnerS, sol::table, const std::string& Name) -> sol::object
					{
						UWorld* World = GetEditorWorld();
						UAvaViewportDataSubsystem* Subsystem = World ? UAvaViewportDataSubsystem::Get(World) : nullptr;
						if (!Subsystem)
						{
							return sol::lua_nil;
						}
						const bool bRemoved = Subsystem->GetGuidePresetProvider().RemoveGuidePreset(UTF8_TO_TCHAR(Name.c_str()));
						sol::table Result = sol::state_view(InnerS).create_table();
						Result["removed"] = bRemoved;
						Result["name"] = Name;
						return sol::make_object(InnerS, Result);
					});

					Handle.set_function("list_guide_presets", [](sol::this_state InnerS, sol::table) -> sol::object
					{
						UWorld* World = GetEditorWorld();
						UAvaViewportDataSubsystem* Subsystem = World ? UAvaViewportDataSubsystem::Get(World) : nullptr;
						if (!Subsystem)
						{
							return sol::lua_nil;
						}
						sol::state_view InnerState(InnerS);
						sol::table Result = InnerState.create_table();
						TArray<FString> PresetNames = Subsystem->GetGuidePresetProvider().GetGuidePresetNames();
						Result["preset_count"] = PresetNames.Num();
						sol::table Names = InnerState.create_table();
						for (int32 Index = 0; Index < PresetNames.Num(); ++Index)
						{
							Names[Index + 1] = std::string(TCHAR_TO_UTF8(*PresetNames[Index]));
						}
						Result["presets"] = Names;
						Result["last_accessed"] = std::string(TCHAR_TO_UTF8(*Subsystem->GetGuidePresetProvider().GetLastAccessedGuidePresetName()));
						return sol::make_object(InnerS, Result);
					});

						return sol::make_object(S, Handle);
					});

					Lua.set_function("ava_media_settings", [](sol::this_state S) -> sol::object
					{
						sol::state_view State(S);
						sol::table Handle = State.create_table();

						Handle.set_function("info", [](sol::this_state InnerS, sol::table) -> sol::object
						{
							return sol::make_object(InnerS, BuildMediaSettingsInfo(sol::state_view(InnerS)));
						});

						Handle.set_function("configure", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
						{
							UAvaMediaSettings& Settings = UAvaMediaSettings::GetMutable();
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureMediaSettings", "NeoStack: Configure Motion Design Media Settings"));
							Settings.Modify();
							ApplyMediaSettingsOptions(Settings, Options);
							Settings.SaveConfig();
							return sol::make_object(InnerS, BuildMediaSettingsInfo(sol::state_view(InnerS)));
						});

						return sol::make_object(S, Handle);
					});

					Lua.set_function("ava_broadcast", [](sol::this_state S) -> sol::object
					{
						sol::state_view State(S);
						sol::table Handle = State.create_table();

						Handle.set_function("info", [](sol::this_state InnerS, sol::table) -> sol::object
						{
							return sol::make_object(InnerS, BuildBroadcastInfo(sol::state_view(InnerS)));
						});

						Handle.set_function("create_profile", [](sol::this_state InnerS, sol::table, const std::string& Name, sol::optional<bool> bMakeCurrent) -> sol::object
						{
							if (Name.empty())
							{
								return sol::lua_nil;
							}
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "CreateBroadcastProfile", "NeoStack: Create Motion Design Broadcast Profile"));
							Broadcast.Modify();
							const FName ProfileName = Broadcast.CreateProfile(FName(UTF8_TO_TCHAR(Name.c_str())), bMakeCurrent.value_or(true));
							return sol::make_object(InnerS, BuildBroadcastProfileInfo(sol::state_view(InnerS), Broadcast, Broadcast.GetProfile(ProfileName)));
						});

						Handle.set_function("duplicate_profile", [](sol::this_state InnerS, sol::table, const std::string& NewName, const std::string& TemplateName, sol::optional<bool> bMakeCurrent) -> sol::object
						{
							if (NewName.empty() || TemplateName.empty())
							{
								return sol::lua_nil;
							}
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "DuplicateBroadcastProfile", "NeoStack: Duplicate Motion Design Broadcast Profile"));
							Broadcast.Modify();
							const FName NewProfileName(UTF8_TO_TCHAR(NewName.c_str()));
							const bool bDuplicated = Broadcast.DuplicateProfile(NewProfileName, FName(UTF8_TO_TCHAR(TemplateName.c_str())), bMakeCurrent.value_or(true));
							if (!bDuplicated)
							{
								return sol::lua_nil;
							}
							return sol::make_object(InnerS, BuildBroadcastProfileInfo(sol::state_view(InnerS), Broadcast, Broadcast.GetProfile(NewProfileName)));
						});

						Handle.set_function("rename_profile", [](sol::this_state InnerS, sol::table, const std::string& OldName, const std::string& NewName) -> sol::object
						{
							if (OldName.empty() || NewName.empty())
							{
								return sol::lua_nil;
							}
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RenameBroadcastProfile", "NeoStack: Rename Motion Design Broadcast Profile"));
							Broadcast.Modify();
							const FName NewProfileName(UTF8_TO_TCHAR(NewName.c_str()));
							if (!Broadcast.RenameProfile(FName(UTF8_TO_TCHAR(OldName.c_str())), NewProfileName))
							{
								return sol::lua_nil;
							}
							return sol::make_object(InnerS, BuildBroadcastProfileInfo(sol::state_view(InnerS), Broadcast, Broadcast.GetProfile(NewProfileName)));
						});

						Handle.set_function("remove_profile", [](sol::this_state InnerS, sol::table, const std::string& Name) -> sol::object
						{
							if (Name.empty())
							{
								return sol::lua_nil;
							}
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveBroadcastProfile", "NeoStack: Remove Motion Design Broadcast Profile"));
							Broadcast.Modify();
							const bool bRemoved = Broadcast.RemoveProfile(FName(UTF8_TO_TCHAR(Name.c_str())));
							sol::table Result = sol::state_view(InnerS).create_table();
							Result["removed"] = bRemoved;
							Result["info"] = BuildBroadcastInfo(sol::state_view(InnerS));
							return sol::make_object(InnerS, Result);
						});

						Handle.set_function("set_current_profile", [](sol::this_state InnerS, sol::table, const std::string& Name) -> sol::object
						{
							if (Name.empty())
							{
								return sol::lua_nil;
							}
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "SetCurrentBroadcastProfile", "NeoStack: Set Current Motion Design Broadcast Profile"));
							Broadcast.Modify();
							const bool bSet = Broadcast.SetCurrentProfile(FName(UTF8_TO_TCHAR(Name.c_str())));
							sol::table Result = sol::state_view(InnerS).create_table();
							Result["set"] = bSet || Broadcast.GetCurrentProfileName() == FName(UTF8_TO_TCHAR(Name.c_str()));
							Result["info"] = BuildBroadcastInfo(sol::state_view(InnerS));
							return sol::make_object(InnerS, Result);
						});

						Handle.set_function("add_channel", [](sol::this_state InnerS, sol::table, const std::string& ProfileName, const std::string& ChannelName) -> sol::object
						{
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FAvaBroadcastProfile* Profile = FindBroadcastProfileMutable(Broadcast, ProfileName);
							if (!Profile || ChannelName.empty())
							{
								return sol::lua_nil;
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddBroadcastChannel", "NeoStack: Add Motion Design Broadcast Channel"));
							Broadcast.Modify();
							FAvaBroadcastOutputChannel& Channel = Profile->AddChannel(FName(UTF8_TO_TCHAR(ChannelName.c_str())));
							return sol::make_object(InnerS, BuildBroadcastChannelInfo(sol::state_view(InnerS), Broadcast, &Channel));
						});

						Handle.set_function("remove_channel", [](sol::this_state InnerS, sol::table, const std::string& ProfileName, const std::string& ChannelName) -> sol::object
						{
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FAvaBroadcastProfile* Profile = FindBroadcastProfileMutable(Broadcast, ProfileName);
							if (!Profile || ChannelName.empty())
							{
								return sol::lua_nil;
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveBroadcastChannel", "NeoStack: Remove Motion Design Broadcast Channel"));
							Broadcast.Modify();
							const bool bRemoved = Profile->RemoveChannel(FName(UTF8_TO_TCHAR(ChannelName.c_str())));
							sol::table Result = sol::state_view(InnerS).create_table();
							Result["removed"] = bRemoved;
							Result["profile"] = BuildBroadcastProfileInfo(sol::state_view(InnerS), Broadcast, *Profile);
							return sol::make_object(InnerS, Result);
						});

						Handle.set_function("rename_channel", [](sol::this_state InnerS, sol::table, const std::string& OldName, const std::string& NewName) -> sol::object
						{
							if (OldName.empty() || NewName.empty())
							{
								return sol::lua_nil;
							}
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RenameBroadcastChannel", "NeoStack: Rename Motion Design Broadcast Channel"));
							Broadcast.Modify();
							if (!Broadcast.RenameChannel(FName(UTF8_TO_TCHAR(OldName.c_str())), FName(UTF8_TO_TCHAR(NewName.c_str()))))
							{
								return sol::lua_nil;
							}
							return sol::make_object(InnerS, BuildBroadcastInfo(sol::state_view(InnerS)));
						});

						Handle.set_function("set_channel_type", [](sol::this_state InnerS, sol::table, const std::string& ChannelName, const std::string& Type) -> sol::object
						{
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							if (ChannelName.empty() || Broadcast.GetChannelIndex(FName(UTF8_TO_TCHAR(ChannelName.c_str()))) == INDEX_NONE)
							{
								return sol::lua_nil;
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "SetBroadcastChannelType", "NeoStack: Set Motion Design Broadcast Channel Type"));
							Broadcast.Modify();
							const FName ChannelFName(UTF8_TO_TCHAR(ChannelName.c_str()));
							Broadcast.SetChannelType(ChannelFName, ParseBroadcastChannelType(Type, Broadcast.GetChannelType(ChannelFName)));
							return sol::make_object(InnerS, BuildBroadcastInfo(sol::state_view(InnerS)));
						});

						Handle.set_function("pin_channel", [](sol::this_state InnerS, sol::table, const std::string& ChannelName, const std::string& ProfileName) -> sol::object
						{
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							if (ChannelName.empty() || !FindBroadcastProfileMutable(Broadcast, ProfileName) || Broadcast.GetChannelIndex(FName(UTF8_TO_TCHAR(ChannelName.c_str()))) == INDEX_NONE)
							{
								return sol::lua_nil;
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "PinBroadcastChannel", "NeoStack: Pin Motion Design Broadcast Channel"));
							Broadcast.Modify();
							Broadcast.PinChannel(FName(UTF8_TO_TCHAR(ChannelName.c_str())), FName(UTF8_TO_TCHAR(ProfileName.c_str())));
							Broadcast.RebuildProfiles();
							return sol::make_object(InnerS, BuildBroadcastInfo(sol::state_view(InnerS)));
						});

						Handle.set_function("unpin_channel", [](sol::this_state InnerS, sol::table, const std::string& ChannelName) -> sol::object
						{
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							if (ChannelName.empty())
							{
								return sol::lua_nil;
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "UnpinBroadcastChannel", "NeoStack: Unpin Motion Design Broadcast Channel"));
							Broadcast.Modify();
							Broadcast.UnpinChannel(FName(UTF8_TO_TCHAR(ChannelName.c_str())));
							Broadcast.RebuildProfiles();
							return sol::make_object(InnerS, BuildBroadcastInfo(sol::state_view(InnerS)));
						});

						Handle.set_function("configure_channel_quality", [](sol::this_state InnerS, sol::table, const std::string& ProfileName, const std::string& ChannelName, sol::table Options) -> sol::object
						{
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FAvaBroadcastOutputChannel* Channel = FindBroadcastChannelMutable(Broadcast, ProfileName, ChannelName);
							if (!Channel)
							{
								return sol::lua_nil;
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureBroadcastChannelQuality", "NeoStack: Configure Motion Design Broadcast Channel Quality"));
							Broadcast.Modify();
							FAvaViewportQualitySettings QualitySettings = Channel->GetViewportQualitySettings();
							ApplyQualityOptions(QualitySettings, Options);
							Channel->SetViewportQualitySettings(QualitySettings);
							return sol::make_object(InnerS, BuildBroadcastChannelInfo(sol::state_view(InnerS), Broadcast, Channel));
						});

						Handle.set_function("add_media_output", [](sol::this_state InnerS, sol::table, const std::string& ProfileName, const std::string& ChannelName, const std::string& ClassName, sol::optional<sol::table> OptionsOpt) -> sol::object
						{
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FAvaBroadcastProfile* Profile = FindBroadcastProfileMutable(Broadcast, ProfileName);
							FAvaBroadcastOutputChannel* Channel = FindBroadcastChannelMutable(Broadcast, ProfileName, ChannelName);
							UClass* MediaOutputClass = ResolveMediaOutputClass(ClassName);
							if (!Profile || !Channel || !MediaOutputClass)
							{
								return sol::lua_nil;
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddBroadcastMediaOutput", "NeoStack: Add Motion Design Broadcast Media Output"));
							Broadcast.Modify();
							FAvaBroadcastMediaOutputInfo OutputInfo;
							if (OptionsOpt)
							{
								ApplyBroadcastMediaOutputInfoOptions(OutputInfo, *OptionsOpt);
							}
							UMediaOutput* MediaOutput = Profile->AddChannelMediaOutput(FName(UTF8_TO_TCHAR(ChannelName.c_str())), MediaOutputClass, OutputInfo);
							if (!IsValid(MediaOutput))
							{
								return sol::lua_nil;
							}
							if (OptionsOpt)
							{
								FString Error;
								if (!ApplyBroadcastMediaOutputProperties(MediaOutput, *OptionsOpt, Error))
								{
									return sol::lua_nil;
								}
								Channel->OnMediaOutputModified(MediaOutput);
							}
							return sol::make_object(InnerS, BuildBroadcastChannelInfo(sol::state_view(InnerS), Broadcast, Channel));
						});

						Handle.set_function("configure_media_output", [](sol::this_state InnerS, sol::table, const std::string& ProfileName, const std::string& ChannelName, int32 LuaIndex, sol::table Options) -> sol::object
						{
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FAvaBroadcastOutputChannel* Channel = FindBroadcastChannelMutable(Broadcast, ProfileName, ChannelName);
							UMediaOutput* MediaOutput = Channel ? GetBroadcastMediaOutputByLuaIndex(*Channel, LuaIndex) : nullptr;
							if (!Channel || !IsValid(MediaOutput))
							{
								return sol::lua_nil;
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureBroadcastMediaOutput", "NeoStack: Configure Motion Design Broadcast Media Output"));
							Broadcast.Modify();
							MediaOutput->Modify();
							FString Error;
							if (!ApplyBroadcastMediaOutputProperties(MediaOutput, Options, Error))
							{
								return sol::lua_nil;
							}
							Channel->OnMediaOutputModified(MediaOutput);
							if (FAvaBroadcastMediaOutputInfo* OutputInfo = Channel->GetMediaOutputInfoMutable(MediaOutput))
							{
								ApplyBroadcastMediaOutputInfoOptions(*OutputInfo, Options);
							}
							return sol::make_object(InnerS, BuildBroadcastChannelInfo(sol::state_view(InnerS), Broadcast, Channel));
						});

						Handle.set_function("remove_media_output", [](sol::this_state InnerS, sol::table, const std::string& ProfileName, const std::string& ChannelName, int32 LuaIndex) -> sol::object
						{
							UAvaBroadcast& Broadcast = UAvaBroadcast::Get();
							FAvaBroadcastProfile* Profile = FindBroadcastProfileMutable(Broadcast, ProfileName);
							FAvaBroadcastOutputChannel* Channel = FindBroadcastChannelMutable(Broadcast, ProfileName, ChannelName);
							UMediaOutput* MediaOutput = Channel ? GetBroadcastMediaOutputByLuaIndex(*Channel, LuaIndex) : nullptr;
							if (!Profile || !Channel || !IsValid(MediaOutput))
							{
								return sol::lua_nil;
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveBroadcastMediaOutput", "NeoStack: Remove Motion Design Broadcast Media Output"));
							Broadcast.Modify();
							const int32 RemovedCount = Profile->RemoveChannelMediaOutputs(FName(UTF8_TO_TCHAR(ChannelName.c_str())), TArray<UMediaOutput*>{ MediaOutput });
							sol::table Result = sol::state_view(InnerS).create_table();
							Result["removed_count"] = RemovedCount;
							Result["channel"] = BuildBroadcastChannelInfo(sol::state_view(InnerS), Broadcast, Channel);
							return sol::make_object(InnerS, Result);
						});

						return sol::make_object(S, Handle);
					});

					Lua.set_function("_enrich_avalanche_rundown_macro_collection", [](sol::table AssetObj, sol::this_state S)
					{
						sol::state_view State(S);

						AssetObj.set_function("info", [](sol::this_state InnerS, sol::table Self) -> sol::object
						{
							return sol::make_object(InnerS, BuildMacroCollectionInfo(sol::state_view(InnerS), ResolveMacroCollection(Self)));
						});

						AssetObj.set_function("list_bindings", [](sol::this_state InnerS, sol::table Self) -> sol::object
						{
							UAvaRundownMacroCollection* Collection = ResolveMacroCollection(Self);
							if (!Collection)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							return sol::make_object(InnerS, BuildMacroCollectionInfo(sol::state_view(InnerS), Collection)["bindings"]);
						});

						AssetObj.set_function("add_binding", [](sol::this_state InnerS, sol::table Self, sol::table Options) -> sol::object
						{
							UAvaRundownMacroCollection* Collection = ResolveMacroCollection(Self);
							if (!Collection)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							bool bValid = false;
							FAvaRundownMacroKeyBinding Binding = BuildMacroBindingFromOptions(Options, bValid);
							if (!bValid)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddRundownMacroBinding", "NeoStack: Add Motion Design Rundown Macro Binding"));
							Collection->Modify();
							if (!AddMacroBinding(Collection, Binding))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							return sol::make_object(InnerS, BuildMacroCollectionInfo(sol::state_view(InnerS), Collection));
						});

						AssetObj.set_function("remove_binding", [](sol::this_state InnerS, sol::table Self, sol::table ChordOptions) -> sol::object
						{
							UAvaRundownMacroCollection* Collection = ResolveMacroCollection(Self);
							if (!Collection)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							bool bValid = false;
							const FInputChord Chord = ParseInputChord(ChordOptions, bValid);
							if (!bValid)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveRundownMacroBinding", "NeoStack: Remove Motion Design Rundown Macro Binding"));
							Collection->Modify();
							const int32 Removed = RemoveMacroBindingsForChord(Collection, Chord);
							sol::table Result = sol::state_view(InnerS).create_table();
							Result["removed_count"] = Removed;
							Result["info"] = BuildMacroCollectionInfo(sol::state_view(InnerS), Collection);
							return sol::make_object(InnerS, Result);
						});

						AssetObj.set_function("has_binding", [](sol::this_state InnerS, sol::table Self, sol::table ChordOptions) -> sol::object
						{
							UAvaRundownMacroCollection* Collection = ResolveMacroCollection(Self);
							bool bValid = false;
							const FInputChord Chord = ParseInputChord(ChordOptions, bValid);
							return sol::make_object(InnerS, Collection && bValid && Collection->HasBindingFor(Chord));
						});

						AssetObj.set_function("commands_for", [](sol::this_state InnerS, sol::table Self, sol::table ChordOptions) -> sol::object
						{
							sol::state_view InnerState(InnerS);
							UAvaRundownMacroCollection* Collection = ResolveMacroCollection(Self);
							bool bValid = false;
							const FInputChord Chord = ParseInputChord(ChordOptions, bValid);
							if (!Collection || !bValid)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							sol::table Commands = InnerState.create_table();
							int32 CommandCount = 0;
							Collection->ForEachCommand(Chord, [&Commands, &CommandCount, &InnerState](const FAvaRundownMacroCommand& Command)
							{
								++CommandCount;
								Commands[CommandCount] = BuildMacroCommandInfo(InnerState, Command);
								return true;
							});
							Commands["count"] = CommandCount;
							return sol::make_object(InnerS, Commands);
						});
					});

					Lua.set_function("_enrich_avalanche_rundown", [](sol::table AssetObj, sol::this_state S)
					{
						AssetObj.set_function("info", [](sol::this_state InnerS, sol::table Self) -> sol::object
						{
							return sol::make_object(InnerS, BuildRundownInfo(sol::state_view(InnerS), ResolveRundown(Self)));
						});

						AssetObj.set_function("playback_info", [](sol::this_state InnerS, sol::table Self) -> sol::object
						{
							return sol::make_object(InnerS, BuildRundownPlaybackInfo(sol::state_view(InnerS), ResolveRundown(Self)));
						});

						AssetObj.set_function("initialize_playback", [](sol::this_state InnerS, sol::table Self) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							Rundown->InitializePlaybackContext();
							return sol::make_object(InnerS, BuildRundownPlaybackInfo(sol::state_view(InnerS), Rundown));
						});

						AssetObj.set_function("close_playback", [](sol::this_state InnerS, sol::table Self, sol::optional<sol::table> Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							const bool bStopAllPages = Options ? Options->get_or("stop_all_pages", true) : true;
							Rundown->ClosePlaybackContext(bStopAllPages);
							return sol::make_object(InnerS, BuildRundownPlaybackInfo(sol::state_view(InnerS), Rundown));
						});

						AssetObj.set_function("can_play_page", [](sol::this_state InnerS, sol::table Self, int32 PageId, sol::optional<sol::table> Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							const bool bPreview = Options ? Options->get_or("preview", false) : false;
							const FName PreviewChannel = Options && Options->get<sol::optional<std::string>>("preview_channel")
								? FName(UTF8_TO_TCHAR(Options->get<std::string>("preview_channel").c_str()))
								: NAME_None;
							FString FailureReason;
							const bool bCanPlay = Rundown->CanPlayPage(PageId, bPreview, PreviewChannel, &FailureReason);
							sol::table Result = sol::state_view(InnerS).create_table();
							Result["can_play"] = bCanPlay;
							Result["page_id"] = PageId;
							Result["preview"] = bPreview;
							Result["reason"] = std::string(TCHAR_TO_UTF8(*FailureReason));
							return sol::make_object(InnerS, Result);
						});

						AssetObj.set_function("load_page", [](sol::this_state InnerS, sol::table Self, int32 PageId, sol::optional<sol::table> Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							const bool bPreview = Options ? Options->get_or("preview", false) : false;
							const FName PreviewChannel = Options && Options->get<sol::optional<std::string>>("preview_channel")
								? FName(UTF8_TO_TCHAR(Options->get<std::string>("preview_channel").c_str()))
								: NAME_None;
							Rundown->InitializePlaybackContext();
							return sol::make_object(InnerS, BuildRundownLoadInfo(sol::state_view(InnerS), Rundown->LoadPage(PageId, bPreview, PreviewChannel)));
						});

						AssetObj.set_function("play_page", [](sol::this_state InnerS, sol::table Self, int32 PageId, sol::optional<sol::table> Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							std::string Type = "PlayFromStart";
							if (Options)
							{
								if (sol::optional<std::string> ExplicitType = Options->get<sol::optional<std::string>>("type"))
								{
									Type = *ExplicitType;
								}
								else if (sol::optional<std::string> PlayType = Options->get<sol::optional<std::string>>("play_type"))
								{
									Type = *PlayType;
								}
							}
							const EAvaRundownPagePlayType PlayType = ParseRundownPlayType(Type);
							const bool bPreview = UE::AvaRundown::IsPreviewPlayType(PlayType);
							const FName PreviewChannel = Options && Options->get<sol::optional<std::string>>("preview_channel")
								? FName(UTF8_TO_TCHAR(Options->get<std::string>("preview_channel").c_str()))
								: NAME_None;
							Rundown->InitializePlaybackContext();
							const bool bPlayed = Rundown->PlayPage(PageId, PlayType, PreviewChannel);
							sol::table Result = BuildRundownPlaybackInfo(sol::state_view(InnerS), Rundown);
							Result["played"] = bPlayed;
							Result["page_id"] = PageId;
							Result["preview"] = bPreview;
							return sol::make_object(InnerS, Result);
						});

						AssetObj.set_function("stop_page", [](sol::this_state InnerS, sol::table Self, int32 PageId, sol::optional<sol::table> Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							const bool bPreview = Options ? Options->get_or("preview", false) : false;
							const FName PreviewChannel = Options && Options->get<sol::optional<std::string>>("preview_channel")
								? FName(UTF8_TO_TCHAR(Options->get<std::string>("preview_channel").c_str()))
								: NAME_None;
							const bool bStopped = Rundown->StopPage(PageId, ParseRundownStopOptions(Options), bPreview, PreviewChannel);
							sol::table Result = BuildRundownPlaybackInfo(sol::state_view(InnerS), Rundown);
							Result["stopped"] = bStopped;
							Result["page_id"] = PageId;
							Result["preview"] = bPreview;
							return sol::make_object(InnerS, Result);
						});

						AssetObj.set_function("empty", [](sol::this_state InnerS, sol::table Self) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "EmptyRundown", "NeoStack: Empty Motion Design Rundown"));
							Rundown->Modify();
							const bool bOk = Rundown->Empty();
							if (bOk)
							{
								Rundown->MarkPackageDirty();
							}
							return sol::make_object(InnerS, BuildRundownInfo(sol::state_view(InnerS), Rundown));
						});

						AssetObj.set_function("add_template", [](sol::this_state InnerS, sol::table Self, sol::optional<sol::table> Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							const FAvaRundownPageIdGeneratorParams Params = Options ? ReadPageIdParams(*Options) : FAvaRundownPageIdGeneratorParams();
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddRundownTemplate", "NeoStack: Add Motion Design Rundown Template"));
							Rundown->Modify();
							const int32 PageId = Rundown->AddTemplate(Params);
							if (PageId == FAvaRundownPage::InvalidPageId)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownPageInfo(sol::state_view(InnerS), Rundown, Rundown->GetPage(PageId)));
						});

						AssetObj.set_function("add_page_from_template", [](sol::this_state InnerS, sol::table Self, int32 TemplateId, sol::optional<sol::table> Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							sol::table Opts = Options ? *Options : sol::state_view(InnerS).create_table();
							const FAvaRundownPageIdGeneratorParams Params = ReadPageIdParams(Opts);
							const FAvaRundownPageInsertPosition Insert = ReadInsertPosition(Opts);
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddRundownPage", "NeoStack: Add Motion Design Rundown Page"));
							Rundown->Modify();
							const int32 PageId = Rundown->AddPageFromTemplate(TemplateId, Params, Insert);
							if (PageId == FAvaRundownPage::InvalidPageId)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownPageInfo(sol::state_view(InnerS), Rundown, Rundown->GetPage(PageId)));
						});

						AssetObj.set_function("configure_page", [](sol::this_state InnerS, sol::table Self, int32 PageId, sol::table Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FAvaRundownPage& Page = Rundown->GetPage(PageId);
							if (!Page.IsValidPage())
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							if (Options.get<sol::optional<std::string>>("asset_path") && !Page.IsTemplate())
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							if (Options.get_or("refresh_transition_logic", false) && !Page.IsTemplate())
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureRundownPage", "NeoStack: Configure Motion Design Rundown Page"));
							Rundown->Modify();
							if (sol::optional<std::string> Name = Options.get<sol::optional<std::string>>("name"))
							{
								Page.Rename(UTF8_TO_TCHAR(Name->c_str()));
							}
							if (sol::optional<std::string> FriendlyName = Options.get<sol::optional<std::string>>("friendly_name"))
							{
								Page.RenameFriendlyName(UTF8_TO_TCHAR(FriendlyName->c_str()));
							}
							if (sol::optional<bool> bEnabled = Options.get<sol::optional<bool>>("enabled"))
							{
								Page.SetEnabled(*bEnabled);
							}
							if (sol::optional<std::string> AssetPath = Options.get<sol::optional<std::string>>("asset_path"))
							{
								Page.UpdateAsset(
									FSoftObjectPath(UTF8_TO_TCHAR(AssetPath->c_str())),
									Options.get_or("reimport_page", false));
							}
							else if (Options.get_or("refresh_transition_logic", false))
							{
								Page.UpdateTransitionLogic();
							}
							if (sol::optional<std::string> ChannelName = Options.get<sol::optional<std::string>>("channel_name"))
							{
								Page.SetChannelName(FName(UTF8_TO_TCHAR(ChannelName->c_str())));
							}
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownPageInfo(sol::state_view(InnerS), Rundown, Page));
						});

						AssetObj.set_function("set_remote_control_value", [](sol::this_state InnerS, sol::table Self, int32 PageId, const std::string& Kind, const std::string& IdString, sol::table Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FAvaRundownPage& Page = Rundown->GetPage(PageId);
							if (!Page.IsValidPage())
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FGuid Id;
							if (!ReadGuidFromString(IdString, Id))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							const FAvaPlayableRemoteControlValue Value = ReadRemoteControlValue(Options);
							const FString LowerKind = FString(UTF8_TO_TCHAR(Kind.c_str())).ToLower();

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "SetRundownRemoteControlValue", "NeoStack: Set Motion Design Rundown Remote Control Value"));
							Rundown->Modify();
							bool bOk = false;
							if (LowerKind == TEXT("entity") || LowerKind == TEXT("entities"))
							{
								bOk = Rundown->SetRemoteControlEntityValue(PageId, Id, Value);
							}
							else if (LowerKind == TEXT("controller") || LowerKind == TEXT("controllers"))
							{
								bOk = Rundown->SetRemoteControlControllerValue(PageId, Id, Value);
							}
							if (!bOk)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownPageInfo(sol::state_view(InnerS), Rundown, Page));
						});

						AssetObj.set_function("reset_remote_control_values", [](sol::this_state InnerS, sol::table Self, int32 PageId, sol::optional<sol::table> Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FAvaRundownPage& Page = Rundown->GetPage(PageId);
							if (!Page.IsValidPage())
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							const bool bUseTemplateValues = Options ? Options->get_or("use_template_values", false) : false;
							const bool bIsDefault = Options ? Options->get_or("is_default", false) : false;
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ResetRundownRemoteControlValues", "NeoStack: Reset Motion Design Rundown Remote Control Values"));
							Rundown->Modify();
							Rundown->ResetRemoteControlValues(PageId, bUseTemplateValues, bIsDefault);
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownPageInfo(sol::state_view(InnerS), Rundown, Page));
						});

						AssetObj.set_function("add_page_command", [](sol::this_state InnerS, sol::table Self, int32 PageId, const std::string& Type, sol::optional<sol::table> Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FAvaRundownPage& Page = Rundown->GetPage(PageId);
							UScriptStruct* CommandStruct = ResolveRundownPageCommandStruct(Type);
							if (!Page.IsValidPage() || !CommandStruct)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FInstancedStruct Command;
							Command.InitializeAs(CommandStruct);
							if (Options && !ApplyRundownPageCommandProperties(Command, Rundown, *Options))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddRundownPageCommand", "NeoStack: Add Motion Design Rundown Page Command"));
							Rundown->Modify();
							TArray<FInstancedStruct> Commands = Page.GetInstancedCommands();
							Commands.Add(MoveTemp(Command));
							Page.SetInstancedCommands(Commands);
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownPageInfo(sol::state_view(InnerS), Rundown, Page));
						});

						AssetObj.set_function("configure_page_command", [](sol::this_state InnerS, sol::table Self, int32 PageId, int32 CommandIndex, sol::table Options) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown || CommandIndex < 1)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FAvaRundownPage& Page = Rundown->GetPage(PageId);
							if (!Page.IsValidPage())
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							TArray<FInstancedStruct> Commands = Page.GetInstancedCommands();
							const int32 ZeroIndex = CommandIndex - 1;
							if (!Commands.IsValidIndex(ZeroIndex))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							if (!ApplyRundownPageCommandProperties(Commands[ZeroIndex], Rundown, Options))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigureRundownPageCommand", "NeoStack: Configure Motion Design Rundown Page Command"));
							Rundown->Modify();
							Page.SetInstancedCommands(Commands);
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownPageInfo(sol::state_view(InnerS), Rundown, Page));
						});

						AssetObj.set_function("remove_page_command", [](sol::this_state InnerS, sol::table Self, int32 PageId, int32 CommandIndex) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown || CommandIndex < 1)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FAvaRundownPage& Page = Rundown->GetPage(PageId);
							if (!Page.IsValidPage())
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							TArray<FInstancedStruct> Commands = Page.GetInstancedCommands();
							const int32 ZeroIndex = CommandIndex - 1;
							if (!Commands.IsValidIndex(ZeroIndex))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveRundownPageCommand", "NeoStack: Remove Motion Design Rundown Page Command"));
							Rundown->Modify();
							Commands.RemoveAt(ZeroIndex);
							Page.SetInstancedCommands(Commands);
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownPageInfo(sol::state_view(InnerS), Rundown, Page));
						});

						AssetObj.set_function("renumber_page", [](sol::this_state InnerS, sol::table Self, int32 PageId, int32 NewPageId) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RenumberRundownPage", "NeoStack: Renumber Motion Design Rundown Page"));
							Rundown->Modify();
							const bool bOk = Rundown->RenumberPageId(PageId, NewPageId);
							if (!bOk)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownPageInfo(sol::state_view(InnerS), Rundown, Rundown->GetPage(NewPageId)));
						});

						AssetObj.set_function("remove_page", [](sol::this_state InnerS, sol::table Self, int32 PageId) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemoveRundownPage", "NeoStack: Remove Motion Design Rundown Page"));
							Rundown->Modify();
							const bool bOk = Rundown->RemovePage(PageId);
							if (bOk)
							{
								Rundown->MarkPackageDirty();
							}
							sol::table Result = sol::state_view(InnerS).create_table();
							Result["removed"] = bOk;
							Result["info"] = BuildRundownInfo(sol::state_view(InnerS), Rundown);
							return sol::make_object(InnerS, Result);
						});

						AssetObj.set_function("change_order", [](sol::this_state InnerS, sol::table Self, const std::string& ListType, sol::table PageIds) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							const FString TypeText = UTF8_TO_TCHAR(ListType.c_str());
							FAvaRundownPageListReference Reference = TypeText.Equals(TEXT("template"), ESearchCase::IgnoreCase)
								? UAvaRundown::TemplatePageList
								: UAvaRundown::InstancePageList;
							const int32 Count = Reference.Type == EAvaRundownPageListType::Template
								? Rundown->GetTemplatePages().Pages.Num()
								: Rundown->GetInstancedPages().Pages.Num();
							const TArray<int32> Indices = ReadIntArray(PageIds);
							if (!AreOrderIndicesValid(Indices, Count))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ReorderRundownPages", "NeoStack: Reorder Motion Design Rundown Pages"));
							Rundown->Modify();
							const bool bOk = Rundown->ChangePageOrder(Reference, Indices);
							if (bOk)
							{
								Rundown->MarkPackageDirty();
							}
							if (!bOk)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							return sol::make_object(InnerS, BuildRundownInfo(sol::state_view(InnerS), Rundown));
						});

						AssetObj.set_function("add_sublist", [](sol::this_state InnerS, sol::table Self, sol::optional<std::string> Name) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							if (!Rundown)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddRundownSubList", "NeoStack: Add Motion Design Rundown Sublist"));
							Rundown->Modify();
							const FAvaRundownPageListReference Reference = Rundown->AddSubList();
							if (Reference.Type != EAvaRundownPageListType::View || !Rundown->IsValidSubList(Reference))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							if (Name)
							{
								Rundown->RenameSubList(Reference, FText::FromString(UTF8_TO_TCHAR(Name->c_str())));
							}
							Rundown->MarkPackageDirty();
							const FAvaRundownSubList& SubList = Rundown->GetSubList(Reference);
							return sol::make_object(InnerS, BuildRundownSubListInfo(sol::state_view(InnerS), SubList, Rundown->GetSubListIndex(SubList)));
						});

						AssetObj.set_function("rename_sublist", [](sol::this_state InnerS, sol::table Self, int32 OneBasedIndex, const std::string& Name) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							const FAvaRundownPageListReference Reference = ResolveSubListReference(Rundown, OneBasedIndex);
							if (!Rundown || !Rundown->IsValidSubList(Reference))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RenameRundownSubList", "NeoStack: Rename Motion Design Rundown Sublist"));
							Rundown->Modify();
							const bool bOk = Rundown->RenameSubList(Reference, FText::FromString(UTF8_TO_TCHAR(Name.c_str())));
							if (!bOk)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownSubListInfo(sol::state_view(InnerS), Rundown->GetSubList(Reference), OneBasedIndex - 1));
						});

						AssetObj.set_function("add_page_to_sublist", [](sol::this_state InnerS, sol::table Self, int32 OneBasedIndex, int32 PageId) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							const FAvaRundownPageListReference Reference = ResolveSubListReference(Rundown, OneBasedIndex);
							if (!Rundown || !Rundown->IsValidSubList(Reference))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddPageToRundownSubList", "NeoStack: Add Page To Motion Design Rundown Sublist"));
							Rundown->Modify();
							const bool bOk = Rundown->AddPageToSubList(Reference, PageId);
							if (!bOk)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							Rundown->MarkPackageDirty();
							return sol::make_object(InnerS, BuildRundownSubListInfo(sol::state_view(InnerS), Rundown->GetSubList(Reference), OneBasedIndex - 1));
						});

						AssetObj.set_function("remove_pages_from_sublist", [](sol::this_state InnerS, sol::table Self, int32 OneBasedIndex, sol::table PageIds) -> sol::object
						{
							UAvaRundown* Rundown = ResolveRundown(Self);
							const FAvaRundownPageListReference Reference = ResolveSubListReference(Rundown, OneBasedIndex);
							if (!Rundown || !Rundown->IsValidSubList(Reference))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "RemovePageFromRundownSubList", "NeoStack: Remove Page From Motion Design Rundown Sublist"));
							Rundown->Modify();
							const int32 Removed = Rundown->RemovePagesFromSubList(Reference, ReadIntArray(PageIds));
							if (Removed > 0)
							{
								Rundown->MarkPackageDirty();
							}
							sol::table Result = sol::state_view(InnerS).create_table();
							Result["removed_count"] = Removed;
							Result["sublist"] = BuildRundownSubListInfo(sol::state_view(InnerS), Rundown->GetSubList(Reference), OneBasedIndex - 1);
							return sol::make_object(InnerS, Result);
						});
					});

					Lua.set_function("_enrich_avalanche_playback_graph", [](sol::table AssetObj, sol::this_state S)
					{
						AssetObj.set_function("info", [](sol::this_state InnerS, sol::table Self) -> sol::object
						{
							return sol::make_object(InnerS, BuildPlaybackGraphInfo(sol::state_view(InnerS), ResolvePlaybackGraph(Self)));
						});

							AssetObj.set_function("set_preview_channel", [](sol::this_state InnerS, sol::table Self, const std::string& ChannelName) -> sol::object
							{
								UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
								if (!Graph)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "SetPlaybackPreviewChannel", "NeoStack: Set Motion Design Playback Preview Channel"));
							Graph->Modify();
							Graph->SetPreviewChannelName(FName(UTF8_TO_TCHAR(ChannelName.c_str())));
								Graph->MarkPackageDirty();
								return sol::make_object(InnerS, BuildPlaybackGraphInfo(sol::state_view(InnerS), Graph));
							});

							AssetObj.set_function("play", [](sol::this_state InnerS, sol::table Self) -> sol::object
							{
								UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
								if (!Graph)
								{
									return sol::make_object(InnerS, sol::lua_nil);
								}
								Graph->Play();
								return sol::make_object(InnerS, BuildPlaybackGraphInfo(sol::state_view(InnerS), Graph));
							});

							AssetObj.set_function("load_instances", [](sol::this_state InnerS, sol::table Self) -> sol::object
							{
								UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
								if (!Graph)
								{
									return sol::make_object(InnerS, sol::lua_nil);
								}
								Graph->LoadInstances();
								return sol::make_object(InnerS, BuildPlaybackGraphInfo(sol::state_view(InnerS), Graph));
							});

							AssetObj.set_function("tick", [](sol::this_state InnerS, sol::table Self, sol::optional<double> DeltaSeconds) -> sol::object
							{
								UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
								if (!Graph)
								{
									return sol::make_object(InnerS, sol::lua_nil);
								}
								const float Delta = static_cast<float>(DeltaSeconds.value_or(0.016));
								Graph->Tick(FMath::Max(0.0f, Delta));
								return sol::make_object(InnerS, BuildPlaybackGraphInfo(sol::state_view(InnerS), Graph));
							});

							AssetObj.set_function("wait_for_playables", [](sol::this_state InnerS, sol::table Self, sol::optional<sol::table> Options) -> sol::object
							{
								UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
								if (!Graph)
								{
									return sol::make_object(InnerS, sol::lua_nil);
								}

								const double TimeoutSeconds = Options
									? FMath::Clamp(Options->get_or("timeout", 2.0), 0.0, 10.0)
									: 2.0;
								const float StepSeconds = static_cast<float>(Options
									? FMath::Clamp(Options->get_or("interval", 0.05), 0.01, 0.5)
									: 0.05);
								const int32 MinPlayableCount = Options
									? FMath::Max(0, Options->get_or("min_playable_count", 1))
									: 1;
								const int32 MinSequenceCount = Options
									? FMath::Max(0, Options->get_or("min_sequence_count", 0))
									: 0;
								const int32 MinSequencePlayerCount = Options
									? FMath::Max(0, Options->get_or("min_sequence_player_count", 0))
									: 0;

								const double EndTime = FPlatformTime::Seconds() + TimeoutSeconds;
								do
								{
									PumpPlaybackGraphStreaming(Graph, StepSeconds);
									if (PlaybackGraphMeetsWaitTarget(Graph, MinPlayableCount, MinSequenceCount, MinSequencePlayerCount))
									{
										break;
									}
									FPlatformProcess::SleepNoStats(0.0f);
								}
								while (FPlatformTime::Seconds() < EndTime);

								return sol::make_object(InnerS, BuildPlaybackGraphInfo(sol::state_view(InnerS), Graph));
							});

							AssetObj.set_function("unload_instances", [](sol::this_state InnerS, sol::table Self, sol::optional<sol::table> Options) -> sol::object
							{
								UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
								if (!Graph)
								{
									return sol::make_object(InnerS, sol::lua_nil);
								}
								EAvaPlaybackUnloadOptions UnloadOptions = EAvaPlaybackUnloadOptions::Default;
								if (Options && Options->get_or("force_immediate", false))
								{
									UnloadOptions |= EAvaPlaybackUnloadOptions::ForceImmediate;
								}
								Graph->UnloadInstances(UnloadOptions);
								return sol::make_object(InnerS, BuildPlaybackGraphInfo(sol::state_view(InnerS), Graph));
							});

							AssetObj.set_function("stop", [](sol::this_state InnerS, sol::table Self, sol::optional<sol::table> Options) -> sol::object
							{
								UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
								if (!Graph)
								{
									return sol::make_object(InnerS, sol::lua_nil);
								}
								EAvaPlaybackStopOptions StopOptions = EAvaPlaybackStopOptions::Default;
								if (Options)
								{
									if (Options->get_or("force_immediate", false))
									{
										StopOptions |= EAvaPlaybackStopOptions::ForceImmediate;
									}
									if (Options->get_or("unload", false))
									{
										StopOptions |= EAvaPlaybackStopOptions::Unload;
									}
								}
								Graph->Stop(StopOptions);
								return sol::make_object(InnerS, BuildPlaybackGraphInfo(sol::state_view(InnerS), Graph));
							});

							AssetObj.set_function("add_root", [](sol::this_state InnerS, sol::table Self) -> sol::object
							{
								UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
								if (!Graph)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddPlaybackRoot", "NeoStack: Add Motion Design Playback Root"));
							Graph->Modify();
							UAvaPlaybackNodeRoot* Root = EnsurePlaybackRootNode(Graph);
							if (!Root)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							Graph->MarkPackageDirty();
							return sol::make_object(InnerS, BuildPlaybackNodeInfo(sol::state_view(InnerS), Graph, Root));
						});

						AssetObj.set_function("add_level_player", [](sol::this_state InnerS, sol::table Self, sol::table Options) -> sol::object
						{
							UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
							if (!Graph)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							const std::string AssetPath = Options.get_or<std::string>("asset_path", "");
							if (AssetPath.empty())
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							const int32 ChannelIndex = ResolveBroadcastChannelIndex(Options.get_or<std::string>("channel_name", ""));
							if (ChannelIndex == INDEX_NONE)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddPlaybackLevelPlayer", "NeoStack: Add Motion Design Playback Level Player"));
							Graph->Modify();
							UAvaPlaybackNodeRoot* Root = EnsurePlaybackRootNode(Graph);
							UAvaPlaybackNodePlayer* Player = ConstructLevelPlayerNode(
								Graph,
								FSoftObjectPath(UTF8_TO_TCHAR(AssetPath.c_str())),
								UTF8_TO_TCHAR(Options.get_or<std::string>("load_options", "").c_str()));
							if (!Root || !Player || !ConnectPlaybackNodeToRoot(Graph, Root, Player, ChannelIndex))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							Graph->MarkPackageDirty();
							return sol::make_object(InnerS, BuildPlaybackNodeInfo(sol::state_view(InnerS), Graph, Player));
						});

						AssetObj.set_function("add_node", [](sol::this_state InnerS, sol::table Self, sol::table Options) -> sol::object
						{
							UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
							std::string Type;
							if (sol::optional<std::string> TypeOpt = Options.get<sol::optional<std::string>>("type"))
							{
								Type = *TypeOpt;
							}
							else if (sol::optional<std::string> ClassOpt = Options.get<sol::optional<std::string>>("class"))
							{
								Type = *ClassOpt;
							}
							UClass* NodeClass = ResolvePlaybackNodeClass(Type);
							if (!Graph || !NodeClass)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "AddPlaybackNode", "NeoStack: Add Motion Design Playback Node"));
							Graph->Modify();
							UAvaPlaybackNode* Node = ConstructPlaybackNodeOfClass(Graph, NodeClass, Options);
							if (!Node)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							return sol::make_object(InnerS, BuildPlaybackNodeInfo(sol::state_view(InnerS), Graph, Node));
						});

							AssetObj.set_function("configure_node", [](sol::this_state InnerS, sol::table Self, int32 NodeIndex, sol::table Options) -> sol::object
							{
								UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
								UAvaPlaybackNode* Node = Graph ? FindPlaybackNodeByLuaIndex(Graph, NodeIndex) : nullptr;
							if (!Graph || !Node)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigurePlaybackNode", "NeoStack: Configure Motion Design Playback Node"));
							Graph->Modify();
							Node->Modify();
							if (!ApplyPlaybackNodeProperties(Node, Options))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
								Graph->MarkPackageDirty();
								return sol::make_object(InnerS, BuildPlaybackNodeInfo(sol::state_view(InnerS), Graph, Node));
							});

							AssetObj.set_function("configure_play_animation", [](sol::this_state InnerS, sol::table Self, int32 NodeIndex, sol::table Options) -> sol::object
							{
								UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
								UAvaPlaybackNode* Node = Graph ? FindPlaybackNodeByLuaIndex(Graph, NodeIndex) : nullptr;
								if (!Graph || !Node)
								{
									return sol::make_object(InnerS, sol::lua_nil);
								}

								FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConfigurePlaybackAnimationNode", "NeoStack: Configure Motion Design Playback Animation Node"));
								Graph->Modify();
								Node->Modify();
								if (!ConfigurePlayAnimationNode(Node, Options))
								{
									return sol::make_object(InnerS, sol::lua_nil);
								}
								Graph->MarkPackageDirty();
								return sol::make_object(InnerS, BuildPlaybackNodeInfo(sol::state_view(InnerS), Graph, Node));
							});

							AssetObj.set_function("connect_node", [](sol::this_state InnerS, sol::table Self, int32 ParentNodeIndex, int32 ChildSlotIndex, int32 ChildNodeIndex) -> sol::object
							{
							UAvaPlaybackGraph* Graph = ResolvePlaybackGraph(Self);
							UAvaPlaybackNode* Parent = Graph ? FindPlaybackNodeByLuaIndex(Graph, ParentNodeIndex) : nullptr;
							UAvaPlaybackNode* Child = Graph ? FindPlaybackNodeByLuaIndex(Graph, ChildNodeIndex) : nullptr;
							if (!Graph || !Parent || !Child)
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}

							FScopedTransaction Tx(NSLOCTEXT("NSAI_Avalanche", "ConnectPlaybackNode", "NeoStack: Connect Motion Design Playback Node"));
							Graph->Modify();
							if (!ConnectPlaybackNodeChild(Graph, Parent, ChildSlotIndex, Child))
							{
								return sol::make_object(InnerS, sol::lua_nil);
							}
							return sol::make_object(InnerS, BuildPlaybackNodeInfo(sol::state_view(InnerS), Graph, Parent));
						});
					});
				}
			}

class FNSAI_AvalancheModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildAvalancheDescriptor());
		Registrar.RegisterAssetCapability(
			AvalancheExtensionId,
			TEXT("AvalancheRundownMacroCollection"),
			TEXT("_enrich_avalanche_rundown_macro_collection"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->IsA<UAvaRundownMacroCollection>();
			});
		Registrar.RegisterAssetCapability(
			AvalancheExtensionId,
			TEXT("AvalancheRundown"),
			TEXT("_enrich_avalanche_rundown"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->IsA<UAvaRundown>();
			});
		Registrar.RegisterAssetCapability(
			AvalancheExtensionId,
			TEXT("AvalanchePlaybackGraph"),
			TEXT("_enrich_avalanche_playback_graph"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->IsA<UAvaPlaybackGraph>();
			});
		Registrar.RegisterLuaBinding(
			AvalancheExtensionId,
			TEXT("Avalanche"),
			{
				{ TEXT("ava_shapes()"), TEXT("Open Motion Design shape authoring handle for the editor world"), TEXT("handle") },
				{ TEXT("handle:add_rectangle(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeRectangleDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_2d_arrow(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShape2DArrowDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_chevron(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeChevronDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_cone(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeConeDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_cube(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeCubeDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_ellipse(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeEllipseDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_irregular_polygon(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeIrregularPolygonDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_line(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeLineDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_ngon(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeNGonDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_ring(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeRingDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_sphere(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeSphereDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_star(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeStarDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:add_torus(opts)"), TEXT("Create an AAvaShapeActor and attach a UAvaShapeTorusDynamicMesh"), TEXT("shape info or nil") },
				{ TEXT("handle:configure(label, opts)"), TEXT("Configure supported shape mesh size and class-specific fields"), TEXT("shape info or nil") },
				{ TEXT("handle:info(label)"), TEXT("Inspect Motion Design shape actor and dynamic mesh state"), TEXT("shape info or nil") },
				{ TEXT("handle:export_static_mesh(label, path)"), TEXT("Export a generated Motion Design shape mesh to a StaticMesh asset"), TEXT("static mesh info or nil") },
				{ TEXT("handle:delete(label)"), TEXT("Delete a Motion Design shape actor"), TEXT("boolean") },
				{ TEXT("ava_dynamic_mesh_info(label)"), TEXT("Inspect a live DynamicMeshComponent's counts and bounds for an actor"), TEXT("dynamic mesh info or nil") },
				{ TEXT("ava_remote_control_controlled_actors_proof(actor, controller_name)"), TEXT("Author a real RC controller/Bind action fixture and inspect UAvaRCLibrary controlled actors"), TEXT("remote control proof info") },
				{ TEXT("ava_remote_control_rebind_proof(target_actor, decoy_actor, suffix)"), TEXT("Author a real exposed property binding, disturb it, and prove FAvaRemoteControlRebind restores it"), TEXT("remote control proof info") },
				{ TEXT("ava_texts()"), TEXT("Open Motion Design text authoring handle for the editor world"), TEXT("handle") },
				{ TEXT("handle:add_motion_design_text(opts)"), TEXT("Create a Text3D actor with Motion Design text factory defaults"), TEXT("text info or nil") },
				{ TEXT("handle:configure(label, opts)"), TEXT("Configure Motion Design text content and Text3D sizing fields"), TEXT("text info or nil") },
					{ TEXT("handle:info(label)"), TEXT("Inspect Motion Design Text3D actor/component state"), TEXT("text info or nil") },
					{ TEXT("text3d_settings()"), TEXT("Open Text3D project settings authoring handle"), TEXT("handle") },
					{ TEXT("ava_scene()"), TEXT("Open Motion Design scene settings and attribute-container authoring handle"), TEXT("handle") },
					{ TEXT("handle:ensure()"), TEXT("Find or create the hidden AAvaScene for the current level and inspect its subobjects"), TEXT("scene info or nil") },
					{ TEXT("handle:configure({scene_rig=path})"), TEXT("Set/clear UAvaSceneSettings SceneRig soft path"), TEXT("scene info or nil") },
						{ TEXT("handle:configure_transition_logic{enabled=?, instancing_mode=?, transition_layer={collection=path, tag=name}}"), TEXT("Create/configure the current level's UAvaTransitionSubsystem behavior tree state used by rundown template transition caches"), TEXT("transition info or nil") },
						{ TEXT("handle:tree_info([label])"), TEXT("Inspect persisted FAvaSceneTree root/actor hierarchy for the current level"), TEXT("scene tree info") },
						{ TEXT("handle:add_tree_root(label) / add_tree_child(label, parent_label)"), TEXT("Author Motion Design FAvaSceneTree actor ordering through AAvaScene"), TEXT("scene tree info or nil") },
						{ TEXT("handle:add_name_attribute(name)"), TEXT("Add a runtime UAvaNameAttribute through UAvaAttributeContainer"), TEXT("scene info or nil") },
						{ TEXT("handle:remove_name_attribute(name)"), TEXT("Remove a runtime UAvaNameAttribute through UAvaAttributeContainer"), TEXT("scene info or nil") },
						{ TEXT("handle:contains_name_attribute(name)"), TEXT("Check UAvaAttributeContainer name-attribute state"), TEXT("boolean") },
						{ TEXT("handle:add_tag_attribute(collection, tag) / remove_tag_attribute(collection, tag)"), TEXT("Author runtime UAvaTagAttribute state through UAvaAttributeContainer"), TEXT("scene info or nil") },
						{ TEXT("handle:add_tag_container_attribute(collection, {tags={...}}) / remove_tag_container_attribute(collection, {tags={...}})"), TEXT("Author instanced UAvaTagContainerAttribute scene-setting state and reinitialize the runtime container"), TEXT("scene info or nil") },
						{ TEXT("handle:contains_tag_attribute(collection, tag)"), TEXT("Check resolved FAvaTagHandle containment in UAvaAttributeContainer"), TEXT("boolean") },
						{ TEXT("ava_scene_rig()"), TEXT("Open Motion Design Scene Rig streaming-level authoring handle"), TEXT("handle") },
						{ TEXT("handle:set_active(level_path) / remove_all()"), TEXT("Set or clear the active Scene Rig through IAvaSceneRigEditorModule"), TEXT("scene rig info or nil") },
						{ TEXT("handle:add_actor(label) / remove_actor(label)"), TEXT("Move supported actors into or out of the active Scene Rig level"), TEXT("scene rig info or nil") },
						{ TEXT("ava_outliner_settings()"), TEXT("Open Motion Design Outliner developer-settings authoring handle"), TEXT("handle") },
						{ TEXT("handle:configure(opts)"), TEXT("Configure Outliner colors, custom item filters, UX flags, and view-mode bitmasks"), TEXT("outliner settings info") },
						{ TEXT("ava_sequencer_settings()"), TEXT("Open Motion Design Sequencer developer-settings authoring handle"), TEXT("handle") },
						{ TEXT("handle:configure(opts) / add_custom_preset(opts) / remove_custom_preset(name) / add_custom_group(opts) / remove_custom_group(name)"), TEXT("Configure default timing and custom sequence presets/groups"), TEXT("sequencer settings info") },
						{ TEXT("ava_mask_modifier(actor, index)"), TEXT("Open a typed Mask2D Read/Write modifier handle from an ActorModifier stack"), TEXT("handle or nil") },
						{ TEXT("handle:configure(opts)"), TEXT("Configure Mask2D channel, inversion, blur, feathering, base opacity, or write operation"), TEXT("mask modifier info") },
						{ TEXT("ava_camera_runtime(opts?)"), TEXT("Register/update Motion Design camera subsystem in PIE and inspect the player-controller view target"), TEXT("camera runtime info") },
						{ TEXT("ava_component_visualizers()"), TEXT("Open Motion Design component visualizer settings authoring handle"), TEXT("handle") },
						{ TEXT("handle:configure({sprite_size=n}) / set_sprite(name, texture_path) / remove_sprite(name)"), TEXT("Configure sprite size and visualizer sprite map entries"), TEXT("component visualizer settings info") },
						{ TEXT("ava_viewport()"), TEXT("Open Motion Design viewport settings and guide-preset authoring handle"), TEXT("handle") },
						{ TEXT("handle:configure(opts)"), TEXT("Configure UAvaViewportSettings developer-settings fields"), TEXT("viewport settings info") },
						{ TEXT("handle:data_info() / configure_data(opts)"), TEXT("Inspect or configure UAvaViewportDataSubsystem virtual size, post-process state, and piloted camera"), TEXT("viewport data info") },
							{ TEXT("handle:save_guide_preset(name, guides, {width,height})"), TEXT("Save Motion Design viewport guides through UAvaViewportDataSubsystem"), TEXT("preset info or nil") },
							{ TEXT("handle:load_guide_preset(name, {width,height})"), TEXT("Load and inspect a Motion Design viewport guide preset"), TEXT("preset info or nil") },
							{ TEXT("handle:list_guide_presets() / remove_guide_preset(name)"), TEXT("List or remove Motion Design viewport guide presets"), TEXT("preset info or nil") },
							{ TEXT("ava_media_settings()"), TEXT("Open Motion Design Playback & Broadcast settings authoring handle"), TEXT("handle") },
							{ TEXT("handle:configure(opts)"), TEXT("Configure UAvaMediaSettings fields without launching playback services"), TEXT("media settings info") },
							{ TEXT("ava_broadcast()"), TEXT("Open Motion Design broadcast profile/channel authoring handle"), TEXT("handle") },
							{ TEXT("handle:create_profile(name) / duplicate_profile(new, template) / rename_profile(old, new) / remove_profile(name)"), TEXT("Author UAvaBroadcast profile metadata through public broadcast APIs"), TEXT("broadcast/profile info") },
							{ TEXT("handle:add_channel(profile, name) / remove_channel(profile, name) / rename_channel(old, new)"), TEXT("Author FAvaBroadcastProfile channel membership and global channel names"), TEXT("broadcast/channel info") },
							{ TEXT("handle:set_channel_type(name, type) / pin_channel(name, profile) / configure_channel_quality(profile, name, opts)"), TEXT("Configure broadcast channel type, pinning, and viewport quality settings"), TEXT("broadcast/channel info") },
							{ TEXT("handle:add_media_output(profile, channel, class, opts?) / configure_media_output(profile, channel, index, opts) / remove_media_output(profile, channel, index)"), TEXT("Author broadcast channel UMediaOutput subobjects and output metadata"), TEXT("broadcast/channel info") },
							{ TEXT("open_asset(path):add_root()"), TEXT("Create a UAvaPlaybackNodeRoot for a UAvaPlaybackGraph asset"), TEXT("node info or nil") },
								{ TEXT("open_asset(path):add_level_player(opts)"), TEXT("Create and connect a UAvaPlaybackNodeLevelPlayer to a broadcast channel"), TEXT("node info or nil") },
								{ TEXT("open_asset(path):add_node({type, properties?}) / configure_node(index, {properties}) / connect_node(parent, slot, child)"), TEXT("Author concrete UAvaPlaybackNode subclasses and child-slot graph links"), TEXT("node info or nil") },
								{ TEXT("open_asset(path):set_preview_channel(name)"), TEXT("Configure UAvaPlaybackGraph preview-only channel routing"), TEXT("graph info or nil") },
								{ TEXT("open_asset(path):load_instances() / unload_instances({force_immediate?})"), TEXT("Create or unload runtime UAvaPlayable instances through UAvaPlaybackGraph"), TEXT("graph info or nil") },
								{ TEXT("open_asset(path):tick(delta?)"), TEXT("Tick UAvaPlaybackGraph once and inspect runtime node/event state"), TEXT("graph info or nil") },
								{ TEXT("open_asset(path):wait_for_playables({timeout?, interval?, min_playable_count?, min_sequence_count?, min_sequence_player_count?})"), TEXT("Flush streamed playable worlds and tick until runtime playable, sequence, or sequence-player state is inspectable"), TEXT("graph info or nil") },
								{ TEXT("open_asset(path):play() / stop({force_immediate?, unload?})"), TEXT("Drive UAvaPlaybackGraph runtime playing state through public playback APIs"), TEXT("graph info or nil") },
								{ TEXT("open_asset(path):add_binding(opts)"), TEXT("Add a key-bound command macro to a UAvaRundownMacroCollection asset"), TEXT("macro collection info or nil") },
							{ TEXT("open_asset(path):remove_binding(chord)"), TEXT("Remove key-bound macros from a UAvaRundownMacroCollection asset"), TEXT("{removed_count, info}") },
							{ TEXT("open_asset(path):has_binding(chord) / commands_for(chord)"), TEXT("Inspect UAvaRundownMacroCollection bindings through engine iteration APIs"), TEXT("boolean / commands table") },
							{ TEXT("open_asset(path):add_template(opts?) / add_page_from_template(template_id, opts?)"), TEXT("Create UAvaRundown template and instanced pages through public rundown APIs"), TEXT("page info or nil") },
							{ TEXT("open_asset(path):configure_page(page_id, opts)"), TEXT("Configure rundown page name, friendly name, enabled flag, asset path, output channel, and template transition cache refresh"), TEXT("page info or nil") },
							{ TEXT("open_asset(path):can_play_page(id, opts) / load_page(id, opts) / play_page(id, opts) / stop_page(id, opts) / playback_info()"), TEXT("Drive and inspect UAvaRundown public playback context, load, play, and stop APIs"), TEXT("playback/load info or nil") },
							{ TEXT("open_asset(path):set_remote_control_value(page_id, kind, guid, opts) / reset_remote_control_values(page_id, opts?)"), TEXT("Author rundown page remote-control entity/controller JSON values through UAvaRundown APIs"), TEXT("page info or nil") },
							{ TEXT("open_asset(path):add_page_command(page_id, type, opts?) / configure_page_command(page_id, index, opts) / remove_page_command(page_id, index)"), TEXT("Author rundown page FInstancedStruct commands such as SetTransform, SetSpawnPoint, and StopLayers"), TEXT("page info or nil") },
							{ TEXT("open_asset(path):renumber_page(old_id, new_id) / remove_page(page_id) / change_order(list, ids)"), TEXT("Mutate rundown page IDs, membership, and ordering"), TEXT("page/info result") },
							{ TEXT("open_asset(path):add_sublist(name?) / add_page_to_sublist(index, page_id)"), TEXT("Author rundown page-view sublists and membership"), TEXT("sublist info or nil") },
						},
					FLuaBindingFunc::CreateStatic(&BindAvalancheLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(AvalancheExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_AvalancheModule, NSAI_Avalanche)
