#include "Lua/LuaBindingRegistry.h"
#include "Lua/LuaEnumReflection.h"
#include "Lua/LuaTypeResolver.h"
#include "Lua/LuaAssetResolver.h"
#include "Lua/LuaStr.h"

// Control Rig includes
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "Rigs/RigHierarchyDefines.h"
#include "Rigs/RigHierarchyElements.h"
#include "Rigs/RigHierarchyMetadata.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "Rigs/RigHierarchyComponents.h"
#endif
#include "Units/RigUnit.h"
#include "RigVMTypeUtils.h"
#include "RigVMCore/RigVMRegistry.h"
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/SavePackage.h"
#include "Modules/ModuleManager.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

// ============================================================================
// Helpers
// ============================================================================

namespace
{

// ── Enum parse / format ─────────────────────────────────────────────────────
//
// All UENUM ladders below delegate to NeoLuaEnum::Parse / ToString, which scan
// StaticEnum<T>() with case-insensitive matching plus authored DisplayName
// aliases. Adding a new enum value in the engine is auto-supported with no
// binding change. The optional alias maps below capture binding-side aliases
// the engine doesn't provide (e.g. "Int" -> Integer for ergonomics).

ERigElementType ParseElemType(const FString& T)
{
	// "space" -> Null is handled by the engine's hidden alias `Space = Null UMETA(Hidden)`,
	// which our case-insensitive scan picks up automatically.
	return NeoLuaEnum::ParseOr<ERigElementType>(T, ERigElementType::None);
}

ERigControlType ParseCtrlType(const FString& T)
{
	static const TMap<FString, ERigControlType> Aliases = {
		{ TEXT("Int"), ERigControlType::Integer },
	};
	return NeoLuaEnum::ParseOr<ERigControlType>(T, ERigControlType::EulerTransform, &Aliases);
}

ERigControlAnimationType ParseAnimType(const FString& T)
{
	static const TMap<FString, ERigControlAnimationType> Aliases = {
		{ TEXT("Channel"), ERigControlAnimationType::AnimationChannel },
		{ TEXT("Proxy"),   ERigControlAnimationType::ProxyControl },
	};
	return NeoLuaEnum::ParseOr<ERigControlAnimationType>(T, ERigControlAnimationType::AnimationControl, &Aliases);
}

// EAxis::Type is a plain (non-UENUM) enum, so reflection helpers don't apply.
EAxis::Type ParseAxis(const FString& AxisStr)
{
	if (AxisStr.Equals(TEXT("X"), ESearchCase::IgnoreCase)) return EAxis::X;
	if (AxisStr.Equals(TEXT("Y"), ESearchCase::IgnoreCase)) return EAxis::Y;
	if (AxisStr.Equals(TEXT("Z"), ESearchCase::IgnoreCase)) return EAxis::Z;
	return EAxis::X;
}

FString ElemTypeStr(ERigElementType T) { return NeoLuaEnum::ToString(T); }
FString CtrlTypeStr(ERigControlType T) { return NeoLuaEnum::ToString(T); }
FString MetadataTypeStr(ERigMetadataType T) { return NeoLuaEnum::ToString(T); }

FRigElementKey FindElemKey(URigHierarchy* H, const FString& Name)
{
	if (!H || Name.IsEmpty()) return FRigElementKey();
	static const ERigElementType Types[] = {
		ERigElementType::Bone, ERigElementType::Control, ERigElementType::Null,
		ERigElementType::Curve, ERigElementType::Connector, ERigElementType::Socket
	};
	for (ERigElementType T : Types)
	{
		FRigElementKey Key(FName(*Name), T);
		if (H->Contains(Key)) return Key;
	}
	return FRigElementKey();
}

FLinearColor ParseColorStr(const FString& ColorStr)
{
	FLinearColor Color = FLinearColor::White;
	FString Trimmed = ColorStr;
	Trimmed.ReplaceInline(TEXT("("), TEXT(""), ESearchCase::CaseSensitive);
	Trimmed.ReplaceInline(TEXT(")"), TEXT(""), ESearchCase::CaseSensitive);
	TArray<FString> Parts;
	Trimmed.ParseIntoArray(Parts, TEXT(","));
	for (const FString& Part : Parts)
	{
		FString Key, Value;
		if (Part.Split(TEXT("="), &Key, &Value))
		{
			Key.TrimStartAndEndInline();
			float Val = FCString::Atof(*Value);
			if (Key.Equals(TEXT("R"), ESearchCase::IgnoreCase)) Color.R = Val;
			else if (Key.Equals(TEXT("G"), ESearchCase::IgnoreCase)) Color.G = Val;
			else if (Key.Equals(TEXT("B"), ESearchCase::IgnoreCase)) Color.B = Val;
			else if (Key.Equals(TEXT("A"), ESearchCase::IgnoreCase)) Color.A = Val;
		}
	}
	return Color;
}

float GetControlValueNumber(sol::table Tbl, const char* PrimaryKey, const char* FallbackKey, double DefaultValue)
{
	return (float)Tbl.get_or(PrimaryKey, Tbl.get_or(FallbackKey, DefaultValue));
}

FRotator ParseControlValueRotator(sol::table Tbl)
{
	return FRotator(
		(float)Tbl.get_or("pitch", 0.0),
		(float)Tbl.get_or("yaw", 0.0),
		(float)Tbl.get_or("roll", 0.0));
}

FRigControlValue::FTransform_Float ParseControlValueTransform(sol::table Tbl)
{
	FTransform Transform = FTransform::Identity;
	Transform.SetTranslation(FVector(
		GetControlValueNumber(Tbl, "tx", "x", 0.0),
		GetControlValueNumber(Tbl, "ty", "y", 0.0),
		GetControlValueNumber(Tbl, "tz", "z", 0.0)));
	Transform.SetRotation(ParseControlValueRotator(Tbl).Quaternion());
	Transform.SetScale3D(FVector(
		(float)Tbl.get_or("sx", 1.0),
		(float)Tbl.get_or("sy", 1.0),
		(float)Tbl.get_or("sz", 1.0)));
	return FRigControlValue::FTransform_Float(Transform);
}

FRigControlValue::FTransformNoScale_Float ParseControlValueTransformNoScale(sol::table Tbl)
{
	const FTransformNoScale TransformNoScale(
		FVector(
			GetControlValueNumber(Tbl, "tx", "x", 0.0),
			GetControlValueNumber(Tbl, "ty", "y", 0.0),
			GetControlValueNumber(Tbl, "tz", "z", 0.0)),
		ParseControlValueRotator(Tbl).Quaternion());
	return FRigControlValue::FTransformNoScale_Float(TransformNoScale);
}

bool HasControlValueScaleFields(sol::table Tbl)
{
	return Tbl["sx"].valid() || Tbl["sy"].valid() || Tbl["sz"].valid() || Tbl["scale"].valid();
}

// Resolve a Lua-supplied type name to the path-form CPPType expected by
// URigVMBlueprint::AddMemberVariable (which feeds it into
// RigVMTypeUtils::FindObjectFromCPPTypeObjectPath).
//
// Delegates to NeoLuaType::ResolveType (header-only, reflection-driven, no
// hardcoded struct map) and converts the resolved object to its path. Supports
// primitives, engine structs/classes/enums, user-defined structs/enums via
// /Game/... paths, and TArray<>/TSubclassOf<>/TObjectPtr<>/TScriptInterface<>
// wrappers — all resolved through StaticEnum/FindFirstObject/LoadObject.
FString ResolveCPPType(const FString& TypeName)
{
	FString Canonical;
	UObject* Resolved = NeoLuaType::ResolveType(TypeName, Canonical);
	if (!Resolved)
	{
		// Primitives (Canonical == "bool"/"int32"/etc.) round-trip as their CPP keyword.
		return NeoLuaType::IsPrimitiveType(Canonical) ? Canonical : FString();
	}
	// For arrays, hand back path-wrapped form: TArray</Script/CoreUObject.Vector>.
	if (NeoLuaType::IsArrayType(Canonical))
	{
		return FString::Printf(TEXT("TArray<%s>"), *Resolved->GetPathName());
	}
	return Resolved->GetPathName();
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
UScriptStruct* ResolveRigComponentStruct(const FString& ComponentType)
{
	if (ComponentType.IsEmpty()) return nullptr;

	if (ComponentType.StartsWith(TEXT("/Script/")) || ComponentType.Contains(TEXT(".")))
	{
		UScriptStruct* StructByPath = LoadObject<UScriptStruct>(nullptr, *ComponentType);
		if (StructByPath && StructByPath->IsChildOf(FRigBaseComponent::StaticStruct()))
		{
			return StructByPath;
		}
	}

	const FString QueryLower = ComponentType.ToLower();
	const FString QueryNoPrefix = QueryLower.StartsWith(TEXT("f")) ? QueryLower.Mid(1) : QueryLower;
	const TArray<UScriptStruct*> ComponentStructs = FRigBaseComponent::GetAllComponentScriptStructs(false);
	for (UScriptStruct* Struct : ComponentStructs)
	{
		if (!Struct) continue;
		const FString StructNameLower = Struct->GetName().ToLower();
		const FString StructNameNoPrefix = StructNameLower.StartsWith(TEXT("f")) ? StructNameLower.Mid(1) : StructNameLower;
		if (StructNameLower == QueryLower || StructNameNoPrefix == QueryNoPrefix || Struct->GetPathName().Equals(ComponentType, ESearchCase::IgnoreCase))
		{
			return Struct;
		}
	}
	return nullptr;
}
#endif

static FString GetRigUnitTooltip(const UScriptStruct* Struct)
{
#if WITH_EDITOR
	FString Tooltip = Struct ? Struct->GetMetaData(TEXT("ToolTip")) : FString();
	if (Tooltip.Len() > 120)
	{
		Tooltip = Tooltip.Left(117) + TEXT("...");
	}
	return Tooltip;
#else
	(void)Struct;
	return FString();
#endif
}

static TArray<FRigElementKey> GetAvailableSpaceKeys(const FRigControlElement* ControlElement)
{
	TArray<FRigElementKey> Result;
	if (!ControlElement)
	{
		return Result;
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	for (const FRigElementKeyWithLabel& Existing : ControlElement->Settings.Customization.AvailableSpaces)
	{
		Result.Add(Existing.Key);
	}
#else
	Result = ControlElement->Settings.Customization.AvailableSpaces;
#endif
	return Result;
}

static bool AddAvailableSpaceCompat(URigHierarchyController* Ctrl, FRigElementKey ControlKey, FRigElementKey SpaceKey)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	return Ctrl->AddAvailableSpace(ControlKey, SpaceKey, NAME_None, false);
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	return Ctrl->AddAvailableSpace(ControlKey, SpaceKey, false, false);
#else
	return false; // AddAvailableSpace not available in UE 5.4
#endif
}

static bool SelectElementCompat(URigHierarchyController* Ctrl, FRigElementKey Key, bool bSelect)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	return Ctrl->SelectElement(Key, bSelect, false, false);
#else
	return Ctrl->SelectElement(Key, bSelect, false);
#endif
}

static bool SetSelectionCompat(URigHierarchyController* Ctrl, const TArray<FRigElementKey>& Keys)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	return Ctrl->SetSelection(Keys, false, false);
#else
	return Ctrl->SetSelection(Keys, false);
#endif
}

static bool ClearSelectionCompat(URigHierarchyController* Ctrl)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	return Ctrl->ClearSelection(false);
#else
	return Ctrl->ClearSelection();
#endif
}

static bool AddParentCompat(URigHierarchyController* Ctrl, FRigElementKey ChildKey, FRigElementKey ParentKey, float Weight, bool bMaintainGlobal, const FString& Label)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	return Ctrl->AddParent(ChildKey, ParentKey, Weight, bMaintainGlobal, Label.IsEmpty() ? NAME_None : FName(*Label), false);
#else
	(void)Label;
	return Ctrl->AddParent(ChildKey, ParentKey, Weight, bMaintainGlobal, false);
#endif
}

static bool SupportsRigComponents()
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	return true;
#else
	return false;
#endif
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
static FRigComponentKey AddControlRigComponentCompat(URigHierarchyController* Ctrl, UScriptStruct* ComponentStruct, const FString& Name, FRigElementKey ElementKey, const FString& Content)
{
	return Ctrl->AddComponent(ComponentStruct, FName(*Name), ElementKey, Content, false, false);
}

static bool RemoveControlRigComponentCompat(URigHierarchyController* Ctrl, FRigComponentKey ComponentKey)
{
	return Ctrl->RemoveComponent(ComponentKey, false, false);
}

static FRigComponentKey RenameControlRigComponentCompat(URigHierarchyController* Ctrl, FRigComponentKey ComponentKey, const FString& NewName)
{
	return Ctrl->RenameComponent(ComponentKey, FName(*NewName), false, false, true);
}

static FRigComponentKey ReparentControlRigComponentCompat(URigHierarchyController* Ctrl, FRigComponentKey ComponentKey, FRigElementKey NewElementKey)
{
	return Ctrl->ReparentComponent(ComponentKey, NewElementKey, false, false, true);
}
#endif

} // namespace

// ============================================================================
// Safe Access Helper (Fix for dangling raw Hierarchy/Controller pointers)
// ============================================================================
//
// We NEVER store raw URigHierarchy* or URigHierarchyController* across Lua
// function calls. Every method re-resolves using TWeakObjectPtr + asset path.
// This prevents crashes when:
//   - The Control Rig editor tab is closed
//   - The controller becomes unavailable (documented engine behavior)
//   - The hierarchy is recreated during certain operations
//
// See RigHierarchy.h:3905 : "The controller may not be available during certain events."

struct FSafeCR
{
	UControlRigBlueprint* BP = nullptr;
	URigHierarchy* Hierarchy = nullptr;
	URigHierarchyController* Ctrl = nullptr;
};

static FSafeCR ResolveControlRigAccess(TWeakObjectPtr<UControlRigBlueprint> WeakBP,
                                       const FString& Path,
                                       FLuaSessionData& Session)
{
	FSafeCR Access;

	Access.BP = WeakBP.Get();
	if (!Access.BP)
	{
		Access.BP = NeoLuaAsset::Resolve<UControlRigBlueprint>(Path);
	}
	if (!Access.BP)
	{
		Session.Log(FString::Printf(TEXT("[FAIL] Control Rig no longer valid (\"%s\")"), *Path));
		return {};
	}

	Access.Hierarchy = Access.BP->Hierarchy;
	if (!Access.Hierarchy)
	{
		Session.Log(TEXT("[FAIL] Control Rig has no hierarchy"));
		return {};
	}

	if (!Access.Hierarchy->IsControllerAvailable())
	{
		Session.Log(TEXT("[FAIL] Control Rig controller is currently unavailable (editor state changed)"));
		return {};
	}

	Access.Ctrl = Access.Hierarchy->GetController(true);
	if (!Access.Ctrl)
	{
		Session.Log(TEXT("[FAIL] Failed to obtain Control Rig hierarchy controller"));
		return {};
	}

	return Access;
}

// ============================================================================
// Lua Binding
// ============================================================================

static TArray<FLuaFunctionDoc> ControlRigDocs = {};

static void BindControlRig(sol::state& Lua, FLuaSessionData& Session)
{
	Lua.set_function("_enrich_control_rig", [&Session](sol::table AssetObj, sol::this_state S)
	{
		sol::state_view LuaView(S);
		std::string PathStr = AssetObj.get<std::string>("path");
		FString FPath = NeoLuaStr::ToFString(PathStr);

		UControlRigBlueprint* BP = NeoLuaAsset::Resolve<UControlRigBlueprint>(FPath);
		if (!BP) return;

		URigHierarchy* Hierarchy = BP->Hierarchy;
		if (!Hierarchy) return;
		URigHierarchyController* Ctrl = Hierarchy->GetController(true);
		if (!Ctrl) return;

		// ---- save() ----
		TWeakObjectPtr<UControlRigBlueprint> WeakBP(BP);
		AssetObj.set_function("save", [WeakBP, FPath, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.BP)
			{
				Session.Log(TEXT("[FAIL] save -> asset no longer valid"));
				return sol::lua_nil;
			}
			UPackage* Package = Access.BP->GetOutermost();
			if (!Package)
			{
				Session.Log(TEXT("[FAIL] save -> no package"));
				return sol::lua_nil;
			}
			FString PackageFilename;
			if (!FPackageName::DoesPackageExist(Package->GetName(), &PackageFilename))
			{
				PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(),
					FPackageName::GetAssetPackageExtension());
			}
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			SaveArgs.Error = GWarn;
			FSavePackageResultStruct SaveResult = UPackage::Save(Package, Access.BP, *PackageFilename, SaveArgs);
			bool bSuccess = (SaveResult.Result == ESavePackageResult::Success);
			if (bSuccess)
				Session.Log(FString::Printf(TEXT("[OK] save(\"%s\")"), *FPath));
			else
				Session.Log(FString::Printf(TEXT("[FAIL] save(\"%s\") -> save failed"), *FPath));
			return sol::make_object(Lua, bSuccess);
		});

		AssetObj["_help_text"] =
			"Element types for add/remove/list:\n"
			"  bone      — skeleton bone\n"
			"  control   — animation control (Float/Int/Position/Rotator/EulerTransform/...)\n"
			"  null      — null/space element\n"
			"  curve     — animation curve\n"
			"  connector — connector element\n"
			"  socket    — socket element\n"
			"  variable  — blueprint member variable\n"
			"\n"
			"add(type, params):\n"
			"  add(\"bone\", {name=\"MyBone\", parent=\"root\", transform={location={x=0,y=0,z=0}}})\n"
			"  add(\"control\", {name=\"CTRL_Hand\", parent=\"root\", control_type=\"EulerTransform\", anim_type=\"AnimationControl\"})\n"
			"  add(\"null\", {name=\"Space_IK\", parent=\"root\"})\n"
			"  add(\"curve\", {name=\"MyCurve\", value=0.5})\n"
			"  add(\"variable\", {name=\"Speed\", type=\"float\", default_value=\"0.0\"})\n"
			"\n"
			"remove(type, id):\n"
			"  remove(\"bone\", \"MyBone\")\n"
			"  remove(\"variable\", \"Speed\")\n"
			"\n"
			"configure(type, id, params):\n"
			"  configure(\"control\", \"CTRL_Hand\", {control_type=\"Position\", shape_visible=true})\n"
			"  configure(\"variable\", \"Speed\", {value=\"1.0\"})\n"
			"\n"
			"list(type):\n"
			"  list(\"bones\"), list(\"controls\"), list(\"nulls\"), list(\"all\"), list(\"variables\")\n"
			"  list(\"selection\") — currently selected elements\n"
			"  list(\"metadata\") or list(\"metadata\", \"ElementName\") — element metadata\n"
			"  list(\"unit_types\") or list(\"unit_types\", \"IK\") — search RigUnit types\n"
			"  list(\"tags\") or list(\"tags\", \"ElementName\") — element tags\n"
			"\n"
			"Action methods:\n"
			"  reparent(name, new_parent) — reparent element\n"
			"  rename(name, new_name) — rename element\n"
			"  set_display_name(control, display_name, {rename_element=false}) — set control display name\n"
			"  import_hierarchy({skeleton=\"/Game/..\"}) or ({skeletal_mesh=\"/Game/..\"})\n"
			"  import_curves({skeleton=\"/Game/..\"}) or ({skeletal_mesh=\"/Game/..\", namespace=\"\"}) — import curves from skeleton/mesh\n"
			"  import_sockets({skeletal_mesh=\"/Game/..\"}) — import sockets from skeletal mesh (replace_existing=true, remove_obsolete=true)\n"
			"  export_text({\"elem1\", \"elem2\"}) — export elements to text\n"
			"  import_text(content, {replace_existing=false}) — import elements from text\n"
			"  duplicate({\"elem1\", \"elem2\"}) — duplicate elements\n"
			"  mirror({elements={\"Arm_L\"}, search=\"_L\", replace=\"_R\", mirror_axis=\"X\", axis_to_flip=\"Z\"})\n"
			"  reorder(name, new_index) — reorder element among siblings\n"
			"  set_spaces(control, {spaces={\"Bone1\",\"Bone2\"}, clear=false, active_space=\"Bone1\"})\n"
			"  set_metadata(element, name, type, value) — set metadata on element\n"
			"  remove_metadata(element, name) — remove metadata from element\n"
			"  set_tag(element, tag) — add a tag to an element\n"
			"  remove_tag(element, tag) — remove a tag from an element\n"
			"  select({\"elem1\", \"elem2\"}) — select hierarchy elements\n"
			"  deselect({\"elem1\"}) — deselect hierarchy elements\n"
			"  set_selection({\"elem1\", \"elem2\"}) — replace full selection\n"
			"  clear_selection() — clear hierarchy selection\n"
			"  add_parent(child, parent, {weight=0, maintain_global=true}) — multi-parent add\n"
			"  remove_parent(child, parent, {maintain_global=true}) — remove specific parent\n"
			"  clear_parents(child, {maintain_global=true}) — remove all parents\n"
			"  add_component(element, {name, component_type, content}) — add hierarchy component (5.6+)\n"
			"  remove_component(element, name) — remove component (5.6+)\n"
			"  rename_component(element, name, new_name) — rename component (5.6+)\n"
			"  reparent_component(element, name, new_element) — reparent component (5.6+)\n"
			"  set_component_content(element, name, content) — update component content (5.6+)\n"
			"  add_animation_channel({name, parent_control, control_type}) — add animation channel under a control\n"
			"  add_channel_host(channel, host) — add a host to an animation channel\n"
			"  remove_channel_host(channel, host) — remove a host from an animation channel\n"
			"  get_control_value(control, {value_type=\"current\"}) — read control value\n"
			"  set_control_value(control, {x=0,y=0,z=0}, {value_type=\"current\"}) — set control value\n"
			"  info() — summary of hierarchy\n";

		// ---- add(type, params) ----
		AssetObj.set_function("add", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Type, sol::optional<sol::table> Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl)
			{
				return sol::lua_nil;
			}

			FString FType = NeoLuaStr::ToFString(Type);

			if (!Params.has_value()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> params required"), *FType)); return sol::lua_nil; }
			sol::table P = Params.value();
			std::string Name = P.get_or<std::string>("name", "");
			if (Name.empty()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> name required"), *FType)); return sol::lua_nil; }

			FString FElemName = NeoLuaStr::ToFString(Name);
			std::string ParentStr = P.get_or<std::string>("parent", "");
			FRigElementKey ParentKey;
			if (!ParentStr.empty())
			{
				ParentKey = FindElemKey(Access.Hierarchy, NeoLuaStr::ToFString(ParentStr));
				if (!ParentKey.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> parent '%s' not found"), *FType, *NeoLuaStr::ToFString(ParentStr))); return sol::lua_nil; }
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddCRElement", "Add ControlRig Element"));
			static_cast<UObject*>(Access.BP)->Modify();

			// Parse optional transform from params
			FTransform Transform = FTransform::Identity;
			sol::optional<sol::table> TransOpt = P.get<sol::optional<sol::table>>("transform");
			if (TransOpt.has_value())
			{
				sol::table T = TransOpt.value();
				sol::optional<sol::table> Loc = T.get<sol::optional<sol::table>>("location");
				if (Loc.has_value()) Transform.SetLocation(FVector(Loc.value().get_or("x", 0.0), Loc.value().get_or("y", 0.0), Loc.value().get_or("z", 0.0)));
				sol::optional<sol::table> Rot = T.get<sol::optional<sol::table>>("rotation");
				if (Rot.has_value()) Transform.SetRotation(FRotator(Rot.value().get_or("pitch", 0.0), Rot.value().get_or("yaw", 0.0), Rot.value().get_or("roll", 0.0)).Quaternion());
				sol::optional<sol::table> Scl = T.get<sol::optional<sol::table>>("scale");
				if (Scl.has_value()) Transform.SetScale3D(FVector(Scl.value().get_or("x", 1.0), Scl.value().get_or("y", 1.0), Scl.value().get_or("z", 1.0)));
			}

			ERigElementType ElemType = ParseElemType(FType);

			if (ElemType == ERigElementType::Bone)
			{
				FRigElementKey Key = Access.Ctrl->AddBone(FName(*FElemName), ParentKey, Transform, true, ERigBoneType::User, false);
				if (!Key.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"bone\") -> failed to add '%s'"), *FElemName)); return sol::lua_nil; }
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"bone\", name=\"%s\")"), *FElemName));
				return sol::make_object(Lua, true);
			}
			else if (ElemType == ERigElementType::Control)
			{
				FRigControlSettings Settings;
				std::string CtrlTypeStr2 = P.get_or<std::string>("control_type", "EulerTransform");
				Settings.ControlType = ParseCtrlType(NeoLuaStr::ToFString(CtrlTypeStr2));
				std::string AnimTypeStr = P.get_or<std::string>("anim_type", "AnimationControl");
				Settings.AnimationType = ParseAnimType(NeoLuaStr::ToFString(AnimTypeStr));
				Settings.bShapeVisible = P.get_or("shape_visible", true);
				Settings.ShapeColor = FLinearColor::Red;

				std::string DisplayName = P.get_or<std::string>("display_name", "");
				if (!DisplayName.empty()) Settings.DisplayName = FName(NeoLuaStr::ToFString(DisplayName));

				FRigControlValue Value;
				FRigElementKey Key = Access.Ctrl->AddControl(FName(*FElemName), ParentKey, Settings, Value, FTransform::Identity, FTransform::Identity, false);
				if (!Key.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"control\") -> failed to add '%s'"), *FElemName)); return sol::lua_nil; }
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"control\", name=\"%s\", type=\"%s\")"), *FElemName, *NeoLuaStr::ToFString(CtrlTypeStr2)));
				return sol::make_object(Lua, true);
			}
			else if (ElemType == ERigElementType::Null)
			{
				FRigElementKey Key = Access.Ctrl->AddNull(FName(*FElemName), ParentKey, Transform, true, false);
				if (!Key.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"null\") -> failed to add '%s'"), *FElemName)); return sol::lua_nil; }
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"null\", name=\"%s\")"), *FElemName));
				return sol::make_object(Lua, true);
			}
			else if (ElemType == ERigElementType::Curve)
			{
				float Value = static_cast<float>(P.get_or("value", 0.0));
				FRigElementKey Key = Access.Ctrl->AddCurve(FName(*FElemName), Value, false);
				if (!Key.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"curve\") -> failed to add '%s'"), *FElemName)); return sol::lua_nil; }
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"curve\", name=\"%s\")"), *FElemName));
				return sol::make_object(Lua, true);
			}
			else if (ElemType == ERigElementType::Connector)
			{
				FRigConnectorSettings ConnSettings;
				FRigElementKey Key = Access.Ctrl->AddConnector(FName(*FElemName), ConnSettings, false);
				if (!Key.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"connector\") -> failed to add '%s'"), *FElemName)); return sol::lua_nil; }
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"connector\", name=\"%s\")"), *FElemName));
				return sol::make_object(Lua, true);
			}
			else if (ElemType == ERigElementType::Socket)
			{
				FRigElementKey Key = Access.Ctrl->AddSocket(FName(*FElemName), ParentKey, Transform, true, FLinearColor::White, TEXT(""), false);
				if (!Key.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] add(\"socket\") -> failed to add '%s'"), *FElemName)); return sol::lua_nil; }
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"socket\", name=\"%s\")"), *FElemName));
				return sol::make_object(Lua, true);
			}
			else if (FType.Equals(TEXT("variable"), ESearchCase::IgnoreCase))
			{
				std::string VarType = P.get_or<std::string>("type", "float");
				FString FVarType = NeoLuaStr::ToFString(VarType);
				std::string DefaultVal = P.get_or<std::string>("default_value", "");
				FString FDefaultVal = NeoLuaStr::ToFString(DefaultVal);

				FString CPPType = ResolveCPPType(FVarType);
				if (CPPType.IsEmpty())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"variable\") -> unknown type '%s'"), *FVarType));
					return sol::lua_nil;
				}

				// Validate non-primitive types before calling AddMemberVariable
				bool bIsPrimitive = (CPPType == TEXT("bool") || CPPType == TEXT("float") || CPPType == TEXT("double")
					|| CPPType == TEXT("int32") || CPPType == TEXT("FString") || CPPType == TEXT("FName") || CPPType == TEXT("FText"));

				UScriptStruct* ResolvedStruct = nullptr;
				UEnum* ResolvedEnum = nullptr;
				if (!bIsPrimitive)
				{
					ResolvedStruct = LoadObject<UScriptStruct>(nullptr, *CPPType);
					if (!ResolvedStruct)
					{
						ResolvedEnum = LoadObject<UEnum>(nullptr, *CPPType);
					}
					if (!ResolvedStruct && !ResolvedEnum)
					{
						Session.Log(FString::Printf(TEXT("[FAIL] add(\"variable\") -> type '%s' could not be loaded"), *CPPType));
						return sol::lua_nil;
					}
				}

				FName Result = Access.BP->AddMemberVariable(FName(*FElemName), CPPType, false, false, FDefaultVal);
				if (Result.IsNone())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"variable\") -> failed to add '%s'"), *FElemName));
					return sol::lua_nil;
				}

				// Register struct/enum in RigVM registry
				if (ResolvedStruct)
				{
					FString StructCPPName = RigVMTypeUtils::GetUniqueStructTypeName(ResolvedStruct);
					FRigVMRegistry::Get().FindOrAddType(FRigVMTemplateArgumentType(*StructCPPName, ResolvedStruct));
				}
				else if (ResolvedEnum)
				{
					FString EnumCPPName = RigVMTypeUtils::CPPTypeFromEnum(ResolvedEnum);
					FRigVMRegistry::Get().FindOrAddType(FRigVMTemplateArgumentType(*EnumCPPName, ResolvedEnum));
				}

				Access.BP->RequestAutoVMRecompilation();
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add(\"variable\", name=\"%s\", type=\"%s\")"), *FElemName, *CPPType));
				return sol::make_object(Lua, true);
			}

			Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> unknown type. Valid: bone, control, null, curve, connector, socket, variable"), *FType));
			return sol::lua_nil;
		});

		// ---- remove(type, id) ----
		AssetObj.set_function("remove", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Type, sol::object Id, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl)
			{
				return sol::lua_nil;
			}

			FString FType = NeoLuaStr::ToFString(Type);

			if (!Id.is<std::string>()) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"%s\") -> name required"), *FType)); return sol::lua_nil; }
			FString Name = NeoLuaStr::ToFString(Id.as<std::string>());

			if (FType.Equals(TEXT("variable"), ESearchCase::IgnoreCase))
			{
				const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemCRVar", "Remove ControlRig Variable"));
				static_cast<UObject*>(Access.BP)->Modify();
				if (Access.BP->RemoveMemberVariable(FName(*Name)))
				{
					Access.BP->MarkPackageDirty();
					Session.Log(FString::Printf(TEXT("[OK] remove(\"variable\", \"%s\")"), *Name));
					return sol::make_object(Lua, true);
				}
				Session.Log(FString::Printf(TEXT("[FAIL] remove(\"variable\", \"%s\") -> not found"), *Name));
				return sol::lua_nil;
			}

			FRigElementKey Key = FindElemKey(Access.Hierarchy, Name);
			if (!Key.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"%s\") -> '%s' not found"), *FType, *Name)); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemCRElement", "Remove ControlRig Element"));
			static_cast<UObject*>(Access.BP)->Modify();
			bool bOK = Access.Ctrl->RemoveElement(Key, false);
			if (!bOK) { Session.Log(FString::Printf(TEXT("[FAIL] remove(\"%s\") -> failed to remove '%s'"), *FType, *Name)); return sol::lua_nil; }
			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] remove(\"%s\", \"%s\")"), *FType, *Name));
			return sol::make_object(Lua, true);
		});

		// ---- configure(type, id, params) ----
		AssetObj.set_function("configure", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Type, const std::string& Id, sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl)
			{
				return sol::lua_nil;
			}

			FString FType = NeoLuaStr::ToFString(Type);
			FString FId = NeoLuaStr::ToFString(Id);

			// --- Variable configure ---
			if (FType.Equals(TEXT("variable"), ESearchCase::IgnoreCase))
			{
				std::string ValStr = Params.get_or<std::string>("value", "");
				if (ValStr.empty())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"variable\", \"%s\") -> value required"), *FId));
					return sol::lua_nil;
				}
				FString FVal = NeoLuaStr::ToFString(ValStr);

				// Find the variable to get its type
				TArray<FRigVMGraphVariableDescription> MemberVars = Access.BP->GetMemberVariables();
				const FRigVMGraphVariableDescription* Found = nullptr;
				for (const FRigVMGraphVariableDescription& Desc : MemberVars)
				{
					if (Desc.Name == FName(*FId))
					{
						Found = &Desc;
						break;
					}
				}
				if (!Found)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"variable\", \"%s\") -> not found"), *FId));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgCRVar", "Configure ControlRig Variable"));
				static_cast<UObject*>(Access.BP)->Modify();
				if (Access.BP->ChangeMemberVariableType(FName(*FId), Found->CPPType, false, false, FVal))
				{
					Access.BP->MarkPackageDirty();
					Session.Log(FString::Printf(TEXT("[OK] configure(\"variable\", \"%s\", value=\"%s\")"), *FId, *FVal.Left(100)));
					return sol::make_object(Lua, true);
				}
				Session.Log(FString::Printf(TEXT("[FAIL] configure(\"variable\", \"%s\") -> failed to set default"), *FId));
				return sol::lua_nil;
			}

			// --- Bone/Null/Socket/Connector configure (transform) ---
			if (FType.Equals(TEXT("bone"), ESearchCase::IgnoreCase) || FType.Equals(TEXT("null"), ESearchCase::IgnoreCase)
				|| FType.Equals(TEXT("socket"), ESearchCase::IgnoreCase) || FType.Equals(TEXT("connector"), ESearchCase::IgnoreCase))
			{
				ERigElementType ElemType = ParseElemType(FType);
				FRigElementKey Key(FName(*FId), ElemType);
				if (!Access.Hierarchy->Contains(Key)) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"%s\", \"%s\") -> not found"), *FType, *FId)); return sol::lua_nil; }

				const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgCRElem", "Configure ControlRig Element"));
				static_cast<UObject*>(Access.BP)->Modify();

				// Set initial transform
				sol::optional<sol::table> TransOpt = Params.get<sol::optional<sol::table>>("transform");
				if (TransOpt.has_value())
				{
					sol::table T = TransOpt.value();
					FTransform Transform = FTransform::Identity;
					sol::optional<sol::table> Loc = T.get<sol::optional<sol::table>>("location");
					if (Loc.has_value()) Transform.SetLocation(FVector(Loc.value().get_or("x", 0.0), Loc.value().get_or("y", 0.0), Loc.value().get_or("z", 0.0)));
					sol::optional<sol::table> Rot = T.get<sol::optional<sol::table>>("rotation");
					if (Rot.has_value()) Transform.SetRotation(FRotator(Rot.value().get_or("pitch", 0.0), Rot.value().get_or("yaw", 0.0), Rot.value().get_or("roll", 0.0)).Quaternion());
					sol::optional<sol::table> Scl = T.get<sol::optional<sol::table>>("scale");
					if (Scl.has_value()) Transform.SetScale3D(FVector(Scl.value().get_or("x", 1.0), Scl.value().get_or("y", 1.0), Scl.value().get_or("z", 1.0)));
					Access.Hierarchy->SetInitialGlobalTransform(Access.Hierarchy->GetIndex(Key), Transform);
				}

				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] configure(\"%s\", \"%s\")"), *FType, *FId));
				return sol::make_object(Lua, true);
			}

			// --- Control configure ---
			if (!FType.Equals(TEXT("control"), ESearchCase::IgnoreCase))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] configure(\"%s\") -> unknown type. Valid: control, variable, bone, null, socket, connector"), *FType));
				return sol::lua_nil;
			}

			FRigElementKey Key(FName(*FId), ERigElementType::Control);
			if (!Access.Hierarchy->Contains(Key)) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"control\") -> '%s' not found"), *FId)); return sol::lua_nil; }

			FRigControlElement* Elem = Cast<FRigControlElement>(Access.Hierarchy->Find(Key));
			if (!Elem) { Session.Log(FString::Printf(TEXT("[FAIL] configure(\"control\") -> element cast failed for '%s'"), *FId)); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "CfgCRCtrl", "Configure ControlRig Control"));
			static_cast<UObject*>(Access.BP)->Modify();

			FRigControlSettings Settings = Elem->Settings;

			sol::optional<std::string> CtrlOpt = Params.get<sol::optional<std::string>>("control_type");
			if (CtrlOpt.has_value()) Settings.ControlType = ParseCtrlType(NeoLuaStr::ToFStringOpt(CtrlOpt));

			sol::optional<std::string> AnimOpt = Params.get<sol::optional<std::string>>("anim_type");
			if (AnimOpt.has_value()) Settings.AnimationType = ParseAnimType(NeoLuaStr::ToFStringOpt(AnimOpt));

			sol::optional<bool> ShapeVis = Params.get<sol::optional<bool>>("shape_visible");
			if (ShapeVis.has_value()) Settings.bShapeVisible = ShapeVis.value();

			sol::optional<std::string> DispName = Params.get<sol::optional<std::string>>("display_name");
			if (DispName.has_value()) Settings.DisplayName = FName(NeoLuaStr::ToFStringOpt(DispName));

			sol::optional<std::string> ShapeName = Params.get<sol::optional<std::string>>("shape_name");
			if (ShapeName.has_value()) Settings.ShapeName = FName(NeoLuaStr::ToFStringOpt(ShapeName));

			sol::optional<std::string> ShapeColor = Params.get<sol::optional<std::string>>("shape_color");
			if (ShapeColor.has_value()) Settings.ShapeColor = ParseColorStr(NeoLuaStr::ToFStringOpt(ShapeColor));

			sol::optional<bool> DrawLimits = Params.get<sol::optional<bool>>("draw_limits");
			if (DrawLimits.has_value()) Settings.bDrawLimits = DrawLimits.value();

			sol::optional<bool> GroupParent = Params.get<sol::optional<bool>>("group_with_parent");
			if (GroupParent.has_value()) Settings.bGroupWithParentControl = GroupParent.value();

			sol::optional<bool> RestrictSpace = Params.get<sol::optional<bool>>("restrict_space_switching");
			if (RestrictSpace.has_value()) Settings.bRestrictSpaceSwitching = RestrictSpace.value();

			// Limits — per-axis enable (array of {min, max} booleans)
			sol::optional<sol::table> LimitEnabled = Params.get<sol::optional<sol::table>>("limit_enabled");
			if (LimitEnabled.has_value())
			{
				Settings.LimitEnabled.Empty();
				for (auto& kv : LimitEnabled.value())
				{
					if (kv.second.is<sol::table>())
					{
						sol::table LE = kv.second.as<sol::table>();
						Settings.LimitEnabled.Add(FRigControlLimitEnabled(
							LE.get_or("min", false), LE.get_or("max", false)));
					}
					else if (kv.second.is<bool>())
					{
						Settings.LimitEnabled.Add(FRigControlLimitEnabled(kv.second.as<bool>()));
					}
				}
			}

			// Minimum/Maximum values
			auto ParseControlValue = [&](sol::table Tbl, FRigControlValue& OutVal, ERigControlType Type)
			{
				switch (Type)
				{
				case ERigControlType::Bool:
					OutVal.Set<bool>(Tbl.get_or("x", false));
					break;
				case ERigControlType::Float:
				case ERigControlType::ScaleFloat:
					OutVal.Set<float>(Tbl.get_or("x", 0.0f));
					break;
				case ERigControlType::Integer:
					OutVal.Set<int32>((int32)Tbl.get_or("x", 0.0));
					break;
				case ERigControlType::Vector2D:
					OutVal.Set<FVector3f>(FVector3f(
						(float)Tbl.get_or("x", 0.0),
						(float)Tbl.get_or("y", 0.0),
						0.f));
					break;
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
					OutVal.Set<FVector3f>(FVector3f(
						(float)Tbl.get_or("x", 0.0),
						(float)Tbl.get_or("y", 0.0),
						(float)Tbl.get_or("z", 0.0)));
					break;
				case ERigControlType::EulerTransform:
				{
					FEulerTransform ZeroET;
					FRigControlValue::FEulerTransform_Float ET(ZeroET);
					ET.TranslationX = (float)Tbl.get_or("tx", 0.0);
					ET.TranslationY = (float)Tbl.get_or("ty", 0.0);
					ET.TranslationZ = (float)Tbl.get_or("tz", 0.0);
					ET.RotationPitch = (float)Tbl.get_or("pitch", 0.0);
					ET.RotationYaw = (float)Tbl.get_or("yaw", 0.0);
					ET.RotationRoll = (float)Tbl.get_or("roll", 0.0);
					ET.ScaleX = (float)Tbl.get_or("sx", 1.0);
					ET.ScaleY = (float)Tbl.get_or("sy", 1.0);
					ET.ScaleZ = (float)Tbl.get_or("sz", 1.0);
					OutVal.Set<FRigControlValue::FEulerTransform_Float>(ET);
					break;
				}
				case ERigControlType::Transform:
					OutVal.Set<FRigControlValue::FTransform_Float>(ParseControlValueTransform(Tbl));
					break;
				case ERigControlType::TransformNoScale:
					// TransformNoScale has no scale storage in UE; scale fields are ignored for min/max authoring.
					OutVal.Set<FRigControlValue::FTransformNoScale_Float>(ParseControlValueTransformNoScale(Tbl));
					break;
				default:
					break;
				}
			};
			sol::optional<sol::table> MinTbl = Params.get<sol::optional<sol::table>>("minimum");
			if (MinTbl.has_value())
			{
				ParseControlValue(MinTbl.value(), Settings.MinimumValue, Settings.ControlType);
			}
			sol::optional<sol::table> MaxTbl = Params.get<sol::optional<sol::table>>("maximum");
			if (MaxTbl.has_value())
			{
				ParseControlValue(MaxTbl.value(), Settings.MaximumValue, Settings.ControlType);
			}

			Access.Ctrl->SetControlSettings(Key, Settings, false);

			// Control value setting (current, initial, offset, shape transforms)
			sol::optional<sol::table> OffsetTransform = Params.get<sol::optional<sol::table>>("offset_transform");
			if (OffsetTransform.has_value())
			{
				sol::table OT = OffsetTransform.value();
				FTransform T = FTransform::Identity;
				sol::optional<sol::table> Loc = OT.get<sol::optional<sol::table>>("location");
				if (Loc.has_value()) T.SetLocation(FVector(Loc.value().get_or("x", 0.0), Loc.value().get_or("y", 0.0), Loc.value().get_or("z", 0.0)));
				sol::optional<sol::table> Rot = OT.get<sol::optional<sol::table>>("rotation");
				if (Rot.has_value()) T.SetRotation(FRotator(Rot.value().get_or("pitch", 0.0), Rot.value().get_or("yaw", 0.0), Rot.value().get_or("roll", 0.0)).Quaternion());
				sol::optional<sol::table> Scl = OT.get<sol::optional<sol::table>>("scale");
				if (Scl.has_value()) T.SetScale3D(FVector(Scl.value().get_or("x", 1.0), Scl.value().get_or("y", 1.0), Scl.value().get_or("z", 1.0)));
				Access.Hierarchy->SetControlOffsetTransform(Key, T, true, true, false);
			}

			sol::optional<sol::table> ShapeTransform = Params.get<sol::optional<sol::table>>("shape_transform");
			if (ShapeTransform.has_value())
			{
				sol::table ST = ShapeTransform.value();
				FTransform T = FTransform::Identity;
				sol::optional<sol::table> Loc = ST.get<sol::optional<sol::table>>("location");
				if (Loc.has_value()) T.SetLocation(FVector(Loc.value().get_or("x", 0.0), Loc.value().get_or("y", 0.0), Loc.value().get_or("z", 0.0)));
				sol::optional<sol::table> Rot = ST.get<sol::optional<sol::table>>("rotation");
				if (Rot.has_value()) T.SetRotation(FRotator(Rot.value().get_or("pitch", 0.0), Rot.value().get_or("yaw", 0.0), Rot.value().get_or("roll", 0.0)).Quaternion());
				sol::optional<sol::table> Scl = ST.get<sol::optional<sol::table>>("scale");
				if (Scl.has_value()) T.SetScale3D(FVector(Scl.value().get_or("x", 1.0), Scl.value().get_or("y", 1.0), Scl.value().get_or("z", 1.0)));
				Access.Hierarchy->SetControlShapeTransform(Key, T, true, false);
			}

			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] configure(\"control\", \"%s\")"), *FId));
			return sol::make_object(Lua, true);
		});

		// ---- list(type?, filter?) ----
		AssetObj.set_function("list", [WeakBP, FPath, &Session](sol::table self,
			sol::optional<std::string> TypeOpt, sol::optional<std::string> FilterOpt, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FType = TypeOpt.has_value() ? NeoLuaStr::ToFStringOpt(TypeOpt) : TEXT("all");

			if (FType.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				// Return every element in the hierarchy
				FType = TEXT("all_elements");
			}

			// --- list("variables") ---
			if (FType.Equals(TEXT("variables"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lua.create_table();
				TArray<FRigVMGraphVariableDescription> Vars = Access.BP->GetMemberVariables();
				int32 Idx = 1;
				for (const FRigVMGraphVariableDescription& V : Vars)
				{
					sol::table Entry = Lua.create_table();
					Entry["name"] = TCHAR_TO_UTF8(*V.Name.ToString());
					Entry["type"] = TCHAR_TO_UTF8(*V.CPPType);
					Entry["default_value"] = TCHAR_TO_UTF8(*V.DefaultValue);
					Result[Idx++] = Entry;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"variables\") -> %d"), Idx - 1));
				return Result;
			}

			// --- list("selection") ---
			if (FType.Equals(TEXT("selection"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lua.create_table();
				TArray<FRigElementKey> Selected = Access.Hierarchy->GetSelectedKeys(ERigElementType::All);
				int32 Idx = 1;
				for (const FRigElementKey& Key : Selected)
				{
					sol::table Entry = Lua.create_table();
					Entry["name"] = TCHAR_TO_UTF8(*Key.Name.ToString());
					Entry["type"] = TCHAR_TO_UTF8(*ElemTypeStr(Key.Type));
					Result[Idx++] = Entry;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"selection\") -> %d"), Idx - 1));
				return Result;
			}

			// --- list("metadata", optional element_name) ---
			if (FType.Equals(TEXT("metadata"), ESearchCase::IgnoreCase))
			{
				TArray<FRigElementKey> Keys;
				if (FilterOpt.has_value() && !FilterOpt.value().empty())
				{
					FString FilterName = NeoLuaStr::ToFStringOpt(FilterOpt);
					FRigElementKey Key = FindElemKey(Access.Hierarchy, FilterName);
					if (Key.IsValid()) Keys.Add(Key);
				}
				else
				{
					Keys = Access.Hierarchy->GetAllKeys(false, ERigElementType::All);
				}

				sol::table Result = Lua.create_table();
				int32 Idx = 1;
				for (const FRigElementKey& Key : Keys)
				{
					TArray<FName> MetaNames = Access.Hierarchy->GetMetadataNames(Key);
					for (const FName& MetaName : MetaNames)
					{
						ERigMetadataType MetaType = Access.Hierarchy->GetMetadataType(Key, MetaName);
						FString Value;
						auto KeyToStr = [](const FRigElementKey& K) {
							return FString::Printf(TEXT("%s:%s"), *K.Name.ToString(), *NeoLuaEnum::ToString(K.Type));
						};
						auto JoinArr = [](const auto& Arr, const TCHAR* Sep, auto&& Stringify) {
							TArray<FString> Parts; Parts.Reserve(Arr.Num());
							for (const auto& Elt : Arr) Parts.Add(Stringify(Elt));
							return FString::Join(Parts, Sep);
						};

						switch (MetaType)
						{
						case ERigMetadataType::Bool: Value = Access.Hierarchy->GetBoolMetadata(Key, MetaName, false) ? TEXT("true") : TEXT("false"); break;
						case ERigMetadataType::Float: Value = FString::SanitizeFloat(Access.Hierarchy->GetFloatMetadata(Key, MetaName, 0.0f)); break;
						case ERigMetadataType::Int32: Value = FString::FromInt(Access.Hierarchy->GetInt32Metadata(Key, MetaName, 0)); break;
						case ERigMetadataType::Name: Value = Access.Hierarchy->GetNameMetadata(Key, MetaName, NAME_None).ToString(); break;
						case ERigMetadataType::Vector: Value = Access.Hierarchy->GetVectorMetadata(Key, MetaName, FVector::ZeroVector).ToString(); break;
						case ERigMetadataType::Rotator: Value = Access.Hierarchy->GetRotatorMetadata(Key, MetaName, FRotator::ZeroRotator).ToString(); break;
						case ERigMetadataType::Quat: Value = Access.Hierarchy->GetQuatMetadata(Key, MetaName, FQuat::Identity).ToString(); break;
						case ERigMetadataType::Transform: Value = Access.Hierarchy->GetTransformMetadata(Key, MetaName, FTransform::Identity).ToString(); break;
						case ERigMetadataType::LinearColor: Value = Access.Hierarchy->GetLinearColorMetadata(Key, MetaName, FLinearColor::Transparent).ToString(); break;
						case ERigMetadataType::RigElementKey: Value = KeyToStr(Access.Hierarchy->GetRigElementKeyMetadata(Key, MetaName, FRigElementKey())); break;

						case ERigMetadataType::BoolArray:
							Value = JoinArr(Access.Hierarchy->GetBoolArrayMetadata(Key, MetaName), TEXT(","), [](bool B){ return FString(B ? TEXT("true") : TEXT("false")); }); break;
						case ERigMetadataType::Int32Array:
							Value = JoinArr(Access.Hierarchy->GetInt32ArrayMetadata(Key, MetaName), TEXT(","), [](int32 I){ return FString::FromInt(I); }); break;
						case ERigMetadataType::FloatArray:
							Value = JoinArr(Access.Hierarchy->GetFloatArrayMetadata(Key, MetaName), TEXT(","), [](float F){ return FString::SanitizeFloat(F); }); break;
						case ERigMetadataType::NameArray:
							Value = JoinArr(Access.Hierarchy->GetNameArrayMetadata(Key, MetaName), TEXT(","), [](const FName& N){ return N.ToString(); }); break;
						case ERigMetadataType::VectorArray:
							Value = JoinArr(Access.Hierarchy->GetVectorArrayMetadata(Key, MetaName), TEXT("|"), [](const FVector& V){ return V.ToString(); }); break;
						case ERigMetadataType::RotatorArray:
							Value = JoinArr(Access.Hierarchy->GetRotatorArrayMetadata(Key, MetaName), TEXT("|"), [](const FRotator& R){ return R.ToString(); }); break;
						case ERigMetadataType::QuatArray:
							Value = JoinArr(Access.Hierarchy->GetQuatArrayMetadata(Key, MetaName), TEXT("|"), [](const FQuat& Q){ return Q.ToString(); }); break;
						case ERigMetadataType::TransformArray:
							Value = JoinArr(Access.Hierarchy->GetTransformArrayMetadata(Key, MetaName), TEXT("|"), [](const FTransform& T){ return T.ToString(); }); break;
						case ERigMetadataType::LinearColorArray:
							Value = JoinArr(Access.Hierarchy->GetLinearColorArrayMetadata(Key, MetaName), TEXT("|"), [](const FLinearColor& C){ return C.ToString(); }); break;
						case ERigMetadataType::RigElementKeyArray:
							Value = JoinArr(Access.Hierarchy->GetRigElementKeyArrayMetadata(Key, MetaName), TEXT(","), KeyToStr); break;

						default: Value = TEXT("<unsupported>"); break;
						}

						sol::table Entry = Lua.create_table();
						Entry["element"] = TCHAR_TO_UTF8(*Key.Name.ToString());
						Entry["element_type"] = TCHAR_TO_UTF8(*ElemTypeStr(Key.Type));
						Entry["name"] = TCHAR_TO_UTF8(*MetaName.ToString());
						Entry["type"] = TCHAR_TO_UTF8(*MetadataTypeStr(MetaType));
						Entry["value"] = TCHAR_TO_UTF8(*Value);
						Result[Idx++] = Entry;
					}
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"metadata\") -> %d"), Idx - 1));
				return Result;
			}

			// --- list("unit_types", optional filter) ---
			if (FType.Equals(TEXT("unit_types"), ESearchCase::IgnoreCase))
			{
				FString Filter = FilterOpt.has_value() ? NeoLuaStr::ToFStringOpt(FilterOpt) : TEXT("");
				sol::table Result = Lua.create_table();
				int32 Idx = 1;
				for (TObjectIterator<UScriptStruct> It; It; ++It)
				{
					UScriptStruct* Struct = *It;
					if (!Struct || !Struct->IsChildOf(FRigUnit::StaticStruct())) continue;
					if (Struct->HasMetaData(TEXT("Abstract")) || Struct->HasMetaData(TEXT("Deprecated"))) continue;
					FString StructName = Struct->GetName();
					if (!Filter.IsEmpty() && !StructName.Contains(Filter, ESearchCase::IgnoreCase)) continue;

					sol::table Entry = Lua.create_table();
					FString ShortName = StructName;
					ShortName.RemoveFromStart(TEXT("RigUnit_"));
					Entry["name"] = TCHAR_TO_UTF8(*ShortName);
					Entry["full_name"] = TCHAR_TO_UTF8(*StructName);
					const FString Tooltip = GetRigUnitTooltip(Struct);
					if (!Tooltip.IsEmpty())
					{
						Entry["tooltip"] = TCHAR_TO_UTF8(*Tooltip);
					}
					Result[Idx++] = Entry;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"unit_types\") -> %d"), Idx - 1));
				return Result;
			}

			// --- list("tags", optional element_name) ---
			if (FType.Equals(TEXT("tags"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lua.create_table();
				int32 Idx = 1;

				if (FilterOpt.has_value() && !FilterOpt.value().empty())
				{
					FString FilterName = NeoLuaStr::ToFStringOpt(FilterOpt);
					FRigElementKey Key = FindElemKey(Access.Hierarchy, FilterName);
					if (Key.IsValid())
					{
						TArray<FName> Tags = Access.Hierarchy->GetTags(Key);
						for (const FName& Tag : Tags)
						{
							sol::table Entry = Lua.create_table();
							Entry["element"] = TCHAR_TO_UTF8(*Key.Name.ToString());
							Entry["element_type"] = TCHAR_TO_UTF8(*ElemTypeStr(Key.Type));
							Entry["tag"] = TCHAR_TO_UTF8(*Tag.ToString());
							Result[Idx++] = Entry;
						}
					}
				}
				else
				{
					TArray<FRigElementKey> AllKeys = Access.Hierarchy->GetAllKeys(false, ERigElementType::All);
					for (const FRigElementKey& Key : AllKeys)
					{
						TArray<FName> Tags = Access.Hierarchy->GetTags(Key);
						for (const FName& Tag : Tags)
						{
							sol::table Entry = Lua.create_table();
							Entry["element"] = TCHAR_TO_UTF8(*Key.Name.ToString());
							Entry["element_type"] = TCHAR_TO_UTF8(*ElemTypeStr(Key.Type));
							Entry["tag"] = TCHAR_TO_UTF8(*Tag.ToString());
							Result[Idx++] = Entry;
						}
					}
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"tags\") -> %d"), Idx - 1));
				return Result;
			}

			// --- Filter by element type ---
			ERigElementType FilterType = ERigElementType::None;
			if (FType.Contains(TEXT("bone"), ESearchCase::IgnoreCase)) FilterType = ERigElementType::Bone;
			else if (FType.Contains(TEXT("control"), ESearchCase::IgnoreCase)) FilterType = ERigElementType::Control;
			else if (FType.Contains(TEXT("null"), ESearchCase::IgnoreCase)) FilterType = ERigElementType::Null;
			else if (FType.Contains(TEXT("curve"), ESearchCase::IgnoreCase)) FilterType = ERigElementType::Curve;
			else if (FType.Contains(TEXT("connector"), ESearchCase::IgnoreCase)) FilterType = ERigElementType::Connector;
			else if (FType.Contains(TEXT("socket"), ESearchCase::IgnoreCase)) FilterType = ERigElementType::Socket;

			sol::table Result = Lua.create_table();
			int32 Idx = 1;

			Access.Hierarchy->ForEach<FRigBaseElement>([&](FRigBaseElement* Elem) -> bool
			{
				if (FilterType != ERigElementType::None && Elem->GetKey().Type != FilterType)
					return true;

				sol::table Entry = Lua.create_table();
				Entry["name"] = TCHAR_TO_UTF8(*Elem->GetKey().Name.ToString());
				Entry["type"] = TCHAR_TO_UTF8(*ElemTypeStr(Elem->GetKey().Type));

				// Parent info
				FRigElementKey ParentKey = Access.Hierarchy->GetFirstParent(Elem->GetKey());
				if (ParentKey.IsValid())
					Entry["parent"] = TCHAR_TO_UTF8(*ParentKey.Name.ToString());

				// Control-specific info
				if (Elem->GetKey().Type == ERigElementType::Control)
				{
					FRigControlElement* CE = Cast<FRigControlElement>(Elem);
					if (CE)
					{
						Entry["control_type"] = TCHAR_TO_UTF8(*CtrlTypeStr(CE->Settings.ControlType));
						if (!CE->Settings.DisplayName.IsNone())
							Entry["display_name"] = TCHAR_TO_UTF8(*CE->Settings.DisplayName.ToString());
						Entry["shape_visible"] = CE->Settings.bShapeVisible;
						if (!CE->Settings.ShapeName.IsNone())
							Entry["shape_name"] = TCHAR_TO_UTF8(*CE->Settings.ShapeName.ToString());

						// Limits
						bool bHasLimits = false;
						for (const FRigControlLimitEnabled& LE : CE->Settings.LimitEnabled)
						{
							if (LE.IsOn()) { bHasLimits = true; break; }
						}
						Entry["has_limits"] = bHasLimits;

						}
				}

				Result[Idx++] = Entry;
				return true;
			});

			Session.Log(FString::Printf(TEXT("[OK] list(\"%s\") -> %d"), *FType, Idx - 1));
			return Result;
		});

		// ---- reparent(name, new_parent) ----
		AssetObj.set_function("reparent", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Name, const std::string& NewParent, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FElemName = NeoLuaStr::ToFString(Name);
			FRigElementKey Key = FindElemKey(Access.Hierarchy, FElemName);
			if (!Key.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] reparent -> '%s' not found"), *FElemName)); return sol::lua_nil; }

			FRigElementKey NewParentKey;
			if (!NewParent.empty())
			{
				NewParentKey = FindElemKey(Access.Hierarchy, NeoLuaStr::ToFString(NewParent));
				if (!NewParentKey.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] reparent -> parent '%s' not found"), *NeoLuaStr::ToFString(NewParent))); return sol::lua_nil; }
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ReparentCR", "Reparent ControlRig Element"));
			static_cast<UObject*>(Access.BP)->Modify();
			bool bOK = Access.Ctrl->SetParent(Key, NewParentKey, true, false);
			if (!bOK) { Session.Log(TEXT("[FAIL] reparent -> operation failed")); return sol::lua_nil; }
			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] reparent(\"%s\", \"%s\")"), *FElemName, *NeoLuaStr::ToFString(NewParent)));
			return sol::make_object(Lua, true);
		});

		// ---- rename(name, new_name) ----
		AssetObj.set_function("rename", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Name, const std::string& NewName, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FElemName = NeoLuaStr::ToFString(Name);
			FRigElementKey Key = FindElemKey(Access.Hierarchy, FElemName);
			if (!Key.IsValid()) { Session.Log(FString::Printf(TEXT("[FAIL] rename -> '%s' not found"), *FElemName)); return sol::lua_nil; }

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RenameCR", "Rename ControlRig Element"));
			static_cast<UObject*>(Access.BP)->Modify();
			FRigElementKey NewKey = Access.Ctrl->RenameElement(Key, FName(*FString(NeoLuaStr::ToFString(NewName))), false);
			if (!NewKey.IsValid()) { Session.Log(TEXT("[FAIL] rename -> operation failed")); return sol::lua_nil; }
			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] rename(\"%s\", \"%s\")"), *FElemName, *NeoLuaStr::ToFString(NewName)));
			return sol::make_object(Lua, true);
		});

		// ---- import_hierarchy(params) ----
		AssetObj.set_function("import_hierarchy", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			std::string SkelPath = Params.get_or<std::string>("skeleton", "");
			std::string MeshPath = Params.get_or<std::string>("skeletal_mesh", "");

			if (SkelPath.empty() && MeshPath.empty())
			{
				Session.Log(TEXT("[FAIL] import_hierarchy -> skeleton or skeletal_mesh required"));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ImportCR", "Import ControlRig Hierarchy"));
			static_cast<UObject*>(Access.BP)->Modify();

			int32 Before = Access.Hierarchy->Num();
			bool bOK = false;

			if (!SkelPath.empty())
			{
				FString FSkel = NeoLuaStr::ToFString(SkelPath);
				USkeleton* Skel = NeoLuaAsset::Resolve<USkeleton>(FSkel);
				if (Skel)
				{
					TArray<FRigElementKey> Keys = Access.Ctrl->ImportBones(Skel->GetReferenceSkeleton(), FName(), false, false, false, false);
					bOK = Keys.Num() > 0;
				}
			}
			else
			{
				FString FMesh = NeoLuaStr::ToFString(MeshPath);
				USkeletalMesh* Mesh = NeoLuaAsset::Resolve<USkeletalMesh>(FMesh);
				if (Mesh)
				{
					TArray<FRigElementKey> Keys = Access.Ctrl->ImportBones(Mesh->GetRefSkeleton(), FName(), false, false, false, false);
					bOK = Keys.Num() > 0;
				}
			}

			if (!bOK) { Session.Log(TEXT("[FAIL] import_hierarchy -> import failed")); return sol::lua_nil; }
			int32 Added = Access.Hierarchy->Num() - Before;
			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] import_hierarchy -> %d elements added"), Added));
			return sol::make_object(Lua, true);
		});

		// ---- import_curves(params) ---- Imports curves from a skeleton or skeletal mesh
		AssetObj.set_function("import_curves", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::optional<sol::table> OptParams, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			std::string SkelPath;
			std::string MeshPath;
			std::string Namespace;

			if (OptParams.has_value())
			{
				sol::table Params = OptParams.value();
				SkelPath = Params.get_or<std::string>("skeleton", "");
				MeshPath = Params.get_or<std::string>("skeletal_mesh", "");
				Namespace = Params.get_or<std::string>("namespace", "");
			}

			if (SkelPath.empty() && MeshPath.empty())
			{
				Session.Log(TEXT("[FAIL] import_curves -> skeleton or skeletal_mesh required"));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ImportCRCurves", "Import ControlRig Curves"));
			static_cast<UObject*>(Access.BP)->Modify();

			FName FNamespace = Namespace.empty() ? NAME_None : FName(NeoLuaStr::ToFString(Namespace));
			TArray<FRigElementKey> Keys;

			if (!MeshPath.empty())
			{
				FString FMesh = NeoLuaStr::ToFString(MeshPath);
				USkeletalMesh* Mesh = NeoLuaAsset::Resolve<USkeletalMesh>(FMesh);
				if (!Mesh) { Session.Log(FString::Printf(TEXT("[FAIL] import_curves -> could not load skeletal mesh '%s'"), *FMesh)); return sol::lua_nil; }
	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
				Keys = Access.Ctrl->ImportCurvesFromSkeletalMesh(Mesh, FNamespace, false, false);
#else
				Session.Log(TEXT("[FAIL] import_curves from skeletal mesh requires UE 5.5+")); return sol::lua_nil;
#endif
			}
			else
			{
				FString FSkel = NeoLuaStr::ToFString(SkelPath);
				USkeleton* Skel = NeoLuaAsset::Resolve<USkeleton>(FSkel);
				if (!Skel) { Session.Log(FString::Printf(TEXT("[FAIL] import_curves -> could not load skeleton '%s'"), *FSkel)); return sol::lua_nil; }
				Keys = Access.Ctrl->ImportCurves(Skel, FNamespace, false, false);
			}

			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] import_curves -> %d curves imported"), Keys.Num()));

			sol::table Result = Lua.create_table();
			for (int32 i = 0; i < Keys.Num(); ++i)
			{
				Result[i + 1] = TCHAR_TO_UTF8(*Keys[i].Name.ToString());
			}
			return Result;
		});

		// ---- import_sockets(params) ---- Imports sockets from a skeletal mesh
		AssetObj.set_function("import_sockets", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::optional<sol::table> OptParams, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			std::string MeshPath;
			std::string Namespace;
			bool bReplaceExisting = true;
			bool bRemoveObsolete = true;

			if (OptParams.has_value())
			{
				sol::table Params = OptParams.value();
				MeshPath = Params.get_or<std::string>("skeletal_mesh", "");
				Namespace = Params.get_or<std::string>("namespace", "");
				bReplaceExisting = Params.get_or("replace_existing", true);
				bRemoveObsolete = Params.get_or("remove_obsolete", true);
			}

			if (MeshPath.empty())
			{
				Session.Log(TEXT("[FAIL] import_sockets -> skeletal_mesh required (sockets only exist on meshes)"));
				return sol::lua_nil;
			}

			FString FMesh = NeoLuaStr::ToFString(MeshPath);
			USkeletalMesh* Mesh = NeoLuaAsset::Resolve<USkeletalMesh>(FMesh);
			if (!Mesh)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] import_sockets -> could not load skeletal mesh '%s'"), *FMesh));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ImportCRSockets", "Import ControlRig Sockets"));
			static_cast<UObject*>(Access.BP)->Modify();

			FName FNamespace = Namespace.empty() ? NAME_None : FName(NeoLuaStr::ToFString(Namespace));
	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			TArray<FRigElementKey> Keys = Access.Ctrl->ImportSocketsFromSkeletalMesh(Mesh, FNamespace, bReplaceExisting, bRemoveObsolete, false, false);
#else
			TArray<FRigElementKey> Keys;
			Session.Log(TEXT("[FAIL] import_sockets -> requires UE 5.6+"));
#endif

			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] import_sockets -> %d sockets imported"), Keys.Num()));

			sol::table Result = Lua.create_table();
			for (int32 i = 0; i < Keys.Num(); ++i)
			{
				Result[i + 1] = TCHAR_TO_UTF8(*Keys[i].Name.ToString());
			}
			return Result;
		});

		// ---- duplicate(names_array) ----
		AssetObj.set_function("duplicate", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::table Names, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			TArray<FRigElementKey> Keys;
			for (const auto& Pair : Names)
			{
				if (Pair.second.is<std::string>())
				{
					FString Name = NeoLuaStr::ToFString(Pair.second.as<std::string>());
					FRigElementKey Key = FindElemKey(Access.Hierarchy, Name);
					if (Key.IsValid())
					{
						Keys.Add(Key);
					}
					else
					{
						Session.Log(FString::Printf(TEXT("[FAIL] duplicate -> '%s' not found"), *Name));
					}
				}
			}

			if (Keys.Num() == 0)
			{
				Session.Log(TEXT("[FAIL] duplicate -> no valid elements"));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "DupCR", "Duplicate ControlRig Elements"));
			static_cast<UObject*>(Access.BP)->Modify();
			TArray<FRigElementKey> NewKeys = Access.Ctrl->DuplicateElements(Keys, false, false);
			if (NewKeys.Num() == 0)
			{
				Session.Log(TEXT("[FAIL] duplicate -> operation failed"));
				return sol::lua_nil;
			}

			Access.BP->MarkPackageDirty();
			sol::table Result = Lua.create_table();
			int32 Idx = 1;
			for (const FRigElementKey& K : NewKeys)
			{
				Result[Idx++] = TCHAR_TO_UTF8(*K.Name.ToString());
			}
			Session.Log(FString::Printf(TEXT("[OK] duplicate -> %d elements duplicated"), NewKeys.Num()));
			return Result;
		});

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		// ---- mirror(params) — 5.6+ ----
		AssetObj.set_function("mirror", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl)
			{
				return sol::lua_nil;
			}

			sol::optional<sol::table> ElemsOpt = Params.get<sol::optional<sol::table>>("elements");
			if (!ElemsOpt.has_value())
			{
				Session.Log(TEXT("[FAIL] mirror -> elements array required"));
				return sol::lua_nil;
			}

			TArray<FRigElementKey> Keys;
			sol::table Elems = ElemsOpt.value();
			for (const auto& Pair : Elems)
			{
				if (Pair.second.is<std::string>())
				{
					FString Name = NeoLuaStr::ToFString(Pair.second.as<std::string>());
					FRigElementKey Key = FindElemKey(Access.Hierarchy, Name);
					if (Key.IsValid()) Keys.Add(Key);
					else Session.Log(FString::Printf(TEXT("[FAIL] mirror -> '%s' not found"), *Name));
				}
			}

			if (Keys.Num() == 0)
			{
				Session.Log(TEXT("[FAIL] mirror -> no valid elements"));
				return sol::lua_nil;
			}

			std::string SearchStr = Params.get_or<std::string>("search", "");
			std::string ReplaceStr = Params.get_or<std::string>("replace", "");
			std::string MirrorAxisStr = Params.get_or<std::string>("mirror_axis", "");
			std::string AxisToFlipStr = Params.get_or<std::string>("axis_to_flip", "");

			FRigVMMirrorSettings MirrorSettings;
			MirrorSettings.SearchString = NeoLuaStr::ToFString(SearchStr);
			MirrorSettings.ReplaceString = NeoLuaStr::ToFString(ReplaceStr);
			if (!MirrorAxisStr.empty())
				MirrorSettings.MirrorAxis = ParseAxis(NeoLuaStr::ToFString(MirrorAxisStr));
			if (!AxisToFlipStr.empty())
				MirrorSettings.AxisToFlip = ParseAxis(NeoLuaStr::ToFString(AxisToFlipStr));

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "MirrorCR", "Mirror ControlRig Elements"));
			static_cast<UObject*>(Access.BP)->Modify();
			TArray<FRigElementKey> NewKeys = Access.Ctrl->MirrorElements(Keys, MirrorSettings, false, false);
			if (NewKeys.Num() == 0)
			{
				Session.Log(TEXT("[FAIL] mirror -> operation failed"));
				return sol::lua_nil;
			}

			Access.BP->MarkPackageDirty();
			sol::table Result = Lua.create_table();
			int32 Idx = 1;
			for (const FRigElementKey& K : NewKeys)
			{
				Result[Idx++] = TCHAR_TO_UTF8(*K.Name.ToString());
			}
			Session.Log(FString::Printf(TEXT("[OK] mirror -> %d elements mirrored"), NewKeys.Num()));
			return Result;
		});

#endif // ENGINE_MINOR_VERSION >= 6 (mirror)

		// ---- reorder(name, new_index) ----
		AssetObj.set_function("reorder", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Name, int NewIndex, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FElemName = NeoLuaStr::ToFString(Name);
			FRigElementKey Key = FindElemKey(Access.Hierarchy, FElemName);
			if (!Key.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] reorder -> '%s' not found"), *FElemName));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ReorderCR", "Reorder ControlRig Element"));
			static_cast<UObject*>(Access.BP)->Modify();
			if (Access.Ctrl->ReorderElement(Key, static_cast<int32>(NewIndex), false))
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] reorder(\"%s\", %d)"), *FElemName, NewIndex));
				return sol::make_object(Lua, true);
			}
			// Engine returns false for no-op (element already at target index).
			// Since we verified the element exists, treat this as success.
			Session.Log(FString::Printf(TEXT("[OK] reorder(\"%s\", %d) — already at target index"), *FElemName, NewIndex));
			return sol::make_object(Lua, true);
		});

		// ---- set_spaces(control_name, params) ----
		AssetObj.set_function("set_spaces", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& ControlName, sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FCtrlName = NeoLuaStr::ToFString(ControlName);
			FRigElementKey ControlKey = FindElemKey(Access.Hierarchy, FCtrlName);
			if (!ControlKey.IsValid() || ControlKey.Type != ERigElementType::Control)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_spaces -> control '%s' not found"), *FCtrlName));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "SetSpacesCR", "Set ControlRig Spaces"));
			static_cast<UObject*>(Access.BP)->Modify();

			int32 Count = 0;

			// Clear existing spaces if requested
			bool bClear = Params.get_or("clear", false);
			if (bClear)
			{
				FRigControlElement* ControlElement = Cast<FRigControlElement>(Access.Hierarchy->Find(ControlKey));
				if (ControlElement)
				{
					const TArray<FRigElementKey> ExistingSpaces = GetAvailableSpaceKeys(ControlElement);
					for (const FRigElementKey& Existing : ExistingSpaces)
					{
	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
						if (Access.Ctrl->RemoveAvailableSpace(ControlKey, Existing, false, false)) Count++;
#endif
					}
				}
			}

			// Add spaces
			sol::optional<sol::table> SpacesOpt = Params.get<sol::optional<sol::table>>("spaces");
			if (SpacesOpt.has_value())
			{
				sol::table Spaces = SpacesOpt.value();
				for (const auto& Pair : Spaces)
				{
					FString SpaceName;
					if (Pair.second.is<std::string>())
					{
						SpaceName = NeoLuaStr::ToFString(Pair.second.as<std::string>());
					}
					else if (Pair.second.is<sol::table>())
					{
						sol::table SpaceEntry = Pair.second.as<sol::table>();
						std::string ElemStr = SpaceEntry.get_or<std::string>("element", "");
						SpaceName = NeoLuaStr::ToFString(ElemStr);
					}

					if (SpaceName.IsEmpty()) continue;
					FRigElementKey SpaceKey = FindElemKey(Access.Hierarchy, SpaceName);
					if (!SpaceKey.IsValid())
					{
						Session.Log(FString::Printf(TEXT("[FAIL] set_spaces -> space '%s' not found"), *SpaceName));
						continue;
					}

					if (AddAvailableSpaceCompat(Access.Ctrl, ControlKey, SpaceKey)) Count++;
				}
			}

			// Activate specific space
			std::string ActiveStr = Params.get_or<std::string>("active_space", "");
			if (!ActiveStr.empty())
			{
				FString FActive = NeoLuaStr::ToFString(ActiveStr);
				FRigElementKey ActiveKey = FindElemKey(Access.Hierarchy, FActive);
				if (ActiveKey.IsValid())
				{
					if (Access.Hierarchy->SwitchToParent(ControlKey, ActiveKey, false, true)) Count++;
				}
				else
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_spaces -> active_space '%s' not found"), *FActive));
				}
			}

			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] set_spaces(\"%s\") -> %d changes"), *FCtrlName, Count));
			return sol::make_object(Lua, true);
		});

		// ---- set_metadata(element, name, type, value) ----
		AssetObj.set_function("set_metadata", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Element, const std::string& MetaNameStr,
			const std::string& MetaType, const std::string& MetaValue, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Hierarchy) return sol::lua_nil;

			// Local alias for the metadata case ladder and its nested lambdas. Lifetime ends
			// with this call, so it doesn't violate the no-raw-capture safety pattern.
			URigHierarchy* Hierarchy = Access.Hierarchy;

			FString FElem = NeoLuaStr::ToFString(Element);
			FString FMetaName = NeoLuaStr::ToFString(MetaNameStr);
			FString FMetaType = NeoLuaStr::ToFString(MetaType); // Parse is case-insensitive
			FString FMetaValue = NeoLuaStr::ToFString(MetaValue);

			FRigElementKey Key = FindElemKey(Access.Hierarchy, FElem);
			if (!Key.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> element '%s' not found"), *FElem));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "SetMetaCR", "Set ControlRig Metadata"));
			static_cast<UObject*>(Access.BP)->Modify();

			const FName MetaFName(*FMetaName);
			bool bSuccess = false;

			// Legacy lowercase / snake_case aliases from the original ladder, plus
			// short-forms callers may use.
			static const TMap<FString, ERigMetadataType> MetaAliases = {
				{ TEXT("int"),                 ERigMetadataType::Int32 },
				{ TEXT("string"),              ERigMetadataType::Name },
				{ TEXT("fname"),               ERigMetadataType::Name },
				{ TEXT("color"),               ERigMetadataType::LinearColor },
				{ TEXT("linearcolor"),         ERigMetadataType::LinearColor },
				{ TEXT("rig_element_key"),     ERigMetadataType::RigElementKey },
				{ TEXT("rig_element_key_array"), ERigMetadataType::RigElementKeyArray },
				{ TEXT("linear_color"),        ERigMetadataType::LinearColor },
				{ TEXT("linear_color_array"),  ERigMetadataType::LinearColorArray },
			};

			TOptional<ERigMetadataType> MetaTypeOpt = NeoLuaEnum::Parse<ERigMetadataType>(FMetaType, &MetaAliases);
			if (!MetaTypeOpt.IsSet())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> unsupported type '%s'. Valid: %s"),
					*FMetaType, *NeoLuaEnum::JoinValueNames<ERigMetadataType>()));
				return sol::lua_nil;
			}
			const ERigMetadataType MetaTypeEnum = *MetaTypeOpt;

			// Parse a single FRigElementKey from "Name" or "Name:Type" form.
			auto ParseRefKey = [Hierarchy](const FString& In) -> FRigElementKey
			{
				FString N, T;
				if (In.Split(TEXT(":"), &N, &T))
				{
					return FRigElementKey(FName(*N), ParseElemType(T));
				}
				return FindElemKey(Hierarchy, In);
			};

			// Split a string by Sep, trimming whitespace and dropping empties.
			auto SplitTrim = [](const FString& In, const TCHAR* Sep) -> TArray<FString>
			{
				TArray<FString> Parts;
				In.ParseIntoArray(Parts, Sep, true);
				for (FString& P : Parts) { P.TrimStartAndEndInline(); }
				return Parts;
			};

			auto HasRequestedMetadataType = [&Hierarchy, &Key, &MetaFName, MetaTypeEnum]() -> bool
			{
				return Hierarchy->GetMetadataType(Key, MetaFName) == MetaTypeEnum;
			};

			auto TransformArraysEqual = [](const TArray<FTransform>& A, const TArray<FTransform>& B) -> bool
			{
				if (A.Num() != B.Num())
				{
					return false;
				}
				for (int32 Index = 0; Index < A.Num(); ++Index)
				{
					if (!A[Index].Equals(B[Index]))
					{
						return false;
					}
				}
				return true;
			};

			switch (MetaTypeEnum)
			{
			case ERigMetadataType::Bool:
			{
				const bool bVal = FMetaValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || FMetaValue == TEXT("1");
				bSuccess = Hierarchy->SetBoolMetadata(Key, MetaFName, bVal);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetBoolMetadata(Key, MetaFName, !bVal) == bVal;
				}
				break;
			}
			case ERigMetadataType::Int32:
			{
				const int32 Value = FCString::Atoi(*FMetaValue);
				bSuccess = Hierarchy->SetInt32Metadata(Key, MetaFName, Value);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetInt32Metadata(Key, MetaFName, Value + 1) == Value;
				}
				break;
			}
			case ERigMetadataType::Float:
			{
				const float Value = FCString::Atof(*FMetaValue);
				bSuccess = Hierarchy->SetFloatMetadata(Key, MetaFName, Value);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = FMath::IsNearlyEqual(Hierarchy->GetFloatMetadata(Key, MetaFName, Value + 1.0f), Value);
				}
				break;
			}
			case ERigMetadataType::Name:
			{
				const FName Value(*FMetaValue);
				bSuccess = Hierarchy->SetNameMetadata(Key, MetaFName, Value);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetNameMetadata(Key, MetaFName, NAME_None) == Value;
				}
				break;
			}
			case ERigMetadataType::Vector:
			{
				FVector V = FVector::ZeroVector;
				if (!V.InitFromString(FMetaValue))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> invalid Vector value '%s'"), *FMetaValue));
					return sol::lua_nil;
				}
				bSuccess = Hierarchy->SetVectorMetadata(Key, MetaFName, V);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetVectorMetadata(Key, MetaFName, FVector::OneVector).Equals(V);
				}
				break;
			}
			case ERigMetadataType::Rotator:
			{
				FRotator R = FRotator::ZeroRotator;
				if (!R.InitFromString(FMetaValue))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> invalid Rotator value '%s'"), *FMetaValue));
					return sol::lua_nil;
				}
				bSuccess = Hierarchy->SetRotatorMetadata(Key, MetaFName, R);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetRotatorMetadata(Key, MetaFName, FRotator(1.0, 1.0, 1.0)).Equals(R);
				}
				break;
			}
			case ERigMetadataType::Quat:
			{
				FQuat Q = FQuat::Identity;
				if (!Q.InitFromString(FMetaValue))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> invalid Quat value '%s'"), *FMetaValue));
					return sol::lua_nil;
				}
				bSuccess = Hierarchy->SetQuatMetadata(Key, MetaFName, Q);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetQuatMetadata(Key, MetaFName, FQuat(1.0, 0.0, 0.0, 0.0)).Equals(Q);
				}
				break;
			}
			case ERigMetadataType::Transform:
			{
				FTransform T = FTransform::Identity;
				if (!T.InitFromString(FMetaValue))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> invalid Transform value '%s'"), *FMetaValue));
					return sol::lua_nil;
				}
				bSuccess = Hierarchy->SetTransformMetadata(Key, MetaFName, T);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetTransformMetadata(Key, MetaFName, FTransform(FVector::OneVector)).Equals(T);
				}
				break;
			}
			case ERigMetadataType::LinearColor:
			{
				FLinearColor C = FLinearColor::White;
				if (!C.InitFromString(FMetaValue))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> invalid LinearColor value '%s'"), *FMetaValue));
					return sol::lua_nil;
				}
				bSuccess = Hierarchy->SetLinearColorMetadata(Key, MetaFName, C);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetLinearColorMetadata(Key, MetaFName, FLinearColor::Transparent).Equals(C);
				}
				break;
			}
			case ERigMetadataType::RigElementKey:
			{
				const FRigElementKey Value = ParseRefKey(FMetaValue);
				bSuccess = Hierarchy->SetRigElementKeyMetadata(Key, MetaFName, Value);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetRigElementKeyMetadata(Key, MetaFName, FRigElementKey()) == Value;
				}
				break;
			}

			// ── Array variants ──
			// Primitive arrays: comma-separated. Struct arrays: '|'-separated items
			// (each item parsed by InitFromString which itself uses comma-separated fields).
			case ERigMetadataType::BoolArray:
			{
				TArray<bool> Arr;
				for (const FString& P : SplitTrim(FMetaValue, TEXT(",")))
					Arr.Add(P.Equals(TEXT("true"), ESearchCase::IgnoreCase) || P == TEXT("1"));
				bSuccess = Hierarchy->SetBoolArrayMetadata(Key, MetaFName, Arr);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetBoolArrayMetadata(Key, MetaFName) == Arr;
				}
				break;
			}
			case ERigMetadataType::Int32Array:
			{
				TArray<int32> Arr;
				for (const FString& P : SplitTrim(FMetaValue, TEXT(","))) Arr.Add(FCString::Atoi(*P));
				bSuccess = Hierarchy->SetInt32ArrayMetadata(Key, MetaFName, Arr);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetInt32ArrayMetadata(Key, MetaFName) == Arr;
				}
				break;
			}
			case ERigMetadataType::FloatArray:
			{
				TArray<float> Arr;
				for (const FString& P : SplitTrim(FMetaValue, TEXT(","))) Arr.Add(FCString::Atof(*P));
				bSuccess = Hierarchy->SetFloatArrayMetadata(Key, MetaFName, Arr);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetFloatArrayMetadata(Key, MetaFName) == Arr;
				}
				break;
			}
			case ERigMetadataType::NameArray:
			{
				TArray<FName> Arr;
				for (const FString& P : SplitTrim(FMetaValue, TEXT(","))) Arr.Add(FName(*P));
				bSuccess = Hierarchy->SetNameArrayMetadata(Key, MetaFName, Arr);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetNameArrayMetadata(Key, MetaFName) == Arr;
				}
				break;
			}
			case ERigMetadataType::VectorArray:
			{
				TArray<FVector> Arr;
				for (const FString& P : SplitTrim(FMetaValue, TEXT("|")))
				{
					FVector V = FVector::ZeroVector;
					if (!V.InitFromString(P))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> invalid VectorArray item '%s'"), *P));
						return sol::lua_nil;
					}
					Arr.Add(V);
				}
				bSuccess = Hierarchy->SetVectorArrayMetadata(Key, MetaFName, Arr);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetVectorArrayMetadata(Key, MetaFName) == Arr;
				}
				break;
			}
			case ERigMetadataType::RotatorArray:
			{
				TArray<FRotator> Arr;
				for (const FString& P : SplitTrim(FMetaValue, TEXT("|")))
				{
					FRotator R = FRotator::ZeroRotator;
					if (!R.InitFromString(P))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> invalid RotatorArray item '%s'"), *P));
						return sol::lua_nil;
					}
					Arr.Add(R);
				}
				bSuccess = Hierarchy->SetRotatorArrayMetadata(Key, MetaFName, Arr);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetRotatorArrayMetadata(Key, MetaFName) == Arr;
				}
				break;
			}
			case ERigMetadataType::QuatArray:
			{
				TArray<FQuat> Arr;
				for (const FString& P : SplitTrim(FMetaValue, TEXT("|")))
				{
					FQuat Q = FQuat::Identity;
					if (!Q.InitFromString(P))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> invalid QuatArray item '%s'"), *P));
						return sol::lua_nil;
					}
					Arr.Add(Q);
				}
				bSuccess = Hierarchy->SetQuatArrayMetadata(Key, MetaFName, Arr);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetQuatArrayMetadata(Key, MetaFName) == Arr;
				}
				break;
			}
			case ERigMetadataType::TransformArray:
			{
				TArray<FTransform> Arr;
				for (const FString& P : SplitTrim(FMetaValue, TEXT("|")))
				{
					FTransform T = FTransform::Identity;
					if (!T.InitFromString(P))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> invalid TransformArray item '%s'"), *P));
						return sol::lua_nil;
					}
					Arr.Add(T);
				}
				bSuccess = Hierarchy->SetTransformArrayMetadata(Key, MetaFName, Arr);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = TransformArraysEqual(Hierarchy->GetTransformArrayMetadata(Key, MetaFName), Arr);
				}
				break;
			}
			case ERigMetadataType::LinearColorArray:
			{
				TArray<FLinearColor> Arr;
				for (const FString& P : SplitTrim(FMetaValue, TEXT("|")))
				{
					FLinearColor C = FLinearColor::White;
					if (!C.InitFromString(P))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> invalid LinearColorArray item '%s'"), *P));
						return sol::lua_nil;
					}
					Arr.Add(C);
				}
				bSuccess = Hierarchy->SetLinearColorArrayMetadata(Key, MetaFName, Arr);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetLinearColorArrayMetadata(Key, MetaFName) == Arr;
				}
				break;
			}
			case ERigMetadataType::RigElementKeyArray:
			{
				TArray<FRigElementKey> Arr;
				for (const FString& P : SplitTrim(FMetaValue, TEXT(","))) Arr.Add(ParseRefKey(P));
				bSuccess = Hierarchy->SetRigElementKeyArrayMetadata(Key, MetaFName, Arr);
				if (!bSuccess && HasRequestedMetadataType())
				{
					bSuccess = Hierarchy->GetRigElementKeyArrayMetadata(Key, MetaFName) == Arr;
				}
				break;
			}
			default:
				Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> type '%s' resolved to %s, no handler"),
					*FMetaType, *NeoLuaEnum::ToString(MetaTypeEnum)));
				return sol::lua_nil;
			}

			if (bSuccess)
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_metadata(\"%s\", \"%s\", \"%s\")"), *FElem, *FMetaName, *FMetaType));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] set_metadata -> failed for '%s.%s'"), *FElem, *FMetaName));
			return sol::lua_nil;
		});

		// ---- remove_metadata(element, name) ----
		AssetObj.set_function("remove_metadata", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Element, const std::string& MetaNameStr, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Hierarchy) return sol::lua_nil;

			FString FElem = NeoLuaStr::ToFString(Element);
			FString FMetaName = NeoLuaStr::ToFString(MetaNameStr);

			FRigElementKey Key = FindElemKey(Access.Hierarchy, FElem);
			if (!Key.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] remove_metadata -> element '%s' not found"), *FElem));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemMetaCR", "Remove ControlRig Metadata"));
			static_cast<UObject*>(Access.BP)->Modify();
			if (Access.Hierarchy->RemoveMetadata(Key, FName(*FMetaName)))
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove_metadata(\"%s\", \"%s\")"), *FElem, *FMetaName));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] remove_metadata -> '%s.%s' not found"), *FElem, *FMetaName));
			return sol::lua_nil;
		});

		// ---- select(names_array) ----
		AssetObj.set_function("select", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::table Names, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			int32 Count = 0;
			for (const auto& Pair : Names)
			{
				if (Pair.second.is<std::string>())
				{
					FString Name = NeoLuaStr::ToFString(Pair.second.as<std::string>());
					FRigElementKey Key = FindElemKey(Access.Hierarchy, Name);
					if (!Key.IsValid()) continue;
					if (SelectElementCompat(Access.Ctrl, Key, true)) Count++;
				}
			}
			Session.Log(FString::Printf(TEXT("[OK] select -> %d elements"), Count));
			return sol::make_object(Lua, true);
		});

		// ---- deselect(names_array) ----
		AssetObj.set_function("deselect", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::table Names, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			int32 Count = 0;
			for (const auto& Pair : Names)
			{
				if (Pair.second.is<std::string>())
				{
					FString Name = NeoLuaStr::ToFString(Pair.second.as<std::string>());
					FRigElementKey Key = FindElemKey(Access.Hierarchy, Name);
					if (!Key.IsValid()) continue;
					if (SelectElementCompat(Access.Ctrl, Key, false)) Count++;
				}
			}
			Session.Log(FString::Printf(TEXT("[OK] deselect -> %d elements"), Count));
			return sol::make_object(Lua, true);
		});

		// ---- set_selection(names_array) ----
		AssetObj.set_function("set_selection", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::table Names, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			TArray<FRigElementKey> Keys;
			for (const auto& Pair : Names)
			{
				if (Pair.second.is<std::string>())
				{
					FString Name = NeoLuaStr::ToFString(Pair.second.as<std::string>());
					FRigElementKey Key = FindElemKey(Access.Hierarchy, Name);
					if (Key.IsValid()) Keys.Add(Key);
				}
			}

			if (SetSelectionCompat(Access.Ctrl, Keys))
			{
				Session.Log(FString::Printf(TEXT("[OK] set_selection -> %d elements"), Keys.Num()));
				return sol::make_object(Lua, true);
			}
			Session.Log(TEXT("[FAIL] set_selection -> operation failed"));
			return sol::lua_nil;
		});

		// ---- clear_selection() ----
		AssetObj.set_function("clear_selection", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			if (ClearSelectionCompat(Access.Ctrl))
			{
				Session.Log(TEXT("[OK] clear_selection"));
				return sol::make_object(Lua, true);
			}
			Session.Log(TEXT("[OK] clear_selection -> no selection to clear"));
			return sol::make_object(Lua, true);
		});

		// ---- add_parent(child, parent, opts?) ----
		AssetObj.set_function("add_parent", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Child, const std::string& Parent,
			sol::optional<sol::table> Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FChild = NeoLuaStr::ToFString(Child);
			FString FParent = NeoLuaStr::ToFString(Parent);

			FRigElementKey ChildKey = FindElemKey(Access.Hierarchy, FChild);
			FRigElementKey ParentKey = FindElemKey(Access.Hierarchy, FParent);
			if (!ChildKey.IsValid() || !ParentKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add_parent -> child '%s' or parent '%s' not found"), *FChild, *FParent));
				return sol::lua_nil;
			}

			float Weight = 0.f;
			bool bMaintainGlobal = true;
			FString Label;
			if (Opts.has_value())
			{
				Weight = static_cast<float>(Opts.value().get_or("weight", 0.0));
				bMaintainGlobal = Opts.value().get_or("maintain_global", true);
				std::string LabelStr = Opts.value().get_or<std::string>("label", "");
				Label = NeoLuaStr::ToFString(LabelStr);
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddParentCR", "Add ControlRig Parent"));
			static_cast<UObject*>(Access.BP)->Modify();

			const bool bOK = AddParentCompat(Access.Ctrl, ChildKey, ParentKey, Weight, bMaintainGlobal, Label);
			if (bOK)
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add_parent(\"%s\", \"%s\")"), *FChild, *FParent));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] add_parent(\"%s\", \"%s\") -> failed"), *FChild, *FParent));
			return sol::lua_nil;
		});

		// ---- remove_parent(child, parent, opts?) ----
		AssetObj.set_function("remove_parent", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Child, const std::string& Parent,
			sol::optional<sol::table> Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FChild = NeoLuaStr::ToFString(Child);
			FString FParent = NeoLuaStr::ToFString(Parent);

			FRigElementKey ChildKey = FindElemKey(Access.Hierarchy, FChild);
			FRigElementKey ParentKey = FindElemKey(Access.Hierarchy, FParent);
			if (!ChildKey.IsValid() || !ParentKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] remove_parent -> child '%s' or parent '%s' not found"), *FChild, *FParent));
				return sol::lua_nil;
			}

			bool bMaintainGlobal = true;
			if (Opts.has_value())
			{
				bMaintainGlobal = Opts.value().get_or("maintain_global", true);
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemParentCR", "Remove ControlRig Parent"));
			static_cast<UObject*>(Access.BP)->Modify();
			if (Access.Ctrl->RemoveParent(ChildKey, ParentKey, bMaintainGlobal, false, false))
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove_parent(\"%s\", \"%s\")"), *FChild, *FParent));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] remove_parent(\"%s\", \"%s\") -> failed"), *FChild, *FParent));
			return sol::lua_nil;
		});

		// ---- clear_parents(child, opts?) ----
		AssetObj.set_function("clear_parents", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Child, sol::optional<sol::table> Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FChild = NeoLuaStr::ToFString(Child);
			FRigElementKey ChildKey = FindElemKey(Access.Hierarchy, FChild);
			if (!ChildKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] clear_parents -> child '%s' not found"), *FChild));
				return sol::lua_nil;
			}

			bool bMaintainGlobal = true;
			if (Opts.has_value())
			{
				bMaintainGlobal = Opts.value().get_or("maintain_global", true);
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ClearParentsCR", "Clear ControlRig Parents"));
			static_cast<UObject*>(Access.BP)->Modify();
			if (Access.Ctrl->RemoveAllParents(ChildKey, bMaintainGlobal, false, false))
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] clear_parents(\"%s\")"), *FChild));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] clear_parents(\"%s\") -> failed"), *FChild));
			return sol::lua_nil;
		});

		// ---- add_component(element, params) ----
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		AssetObj.set_function("add_component", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Element, sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Hierarchy) return sol::lua_nil;

			FString FElem = NeoLuaStr::ToFString(Element);
			std::string CompName = Params.get_or<std::string>("name", "");
			std::string CompType = Params.get_or<std::string>("component_type", "");
			std::string Content = Params.get_or<std::string>("content", "");

			if (CompName.empty() || CompType.empty())
			{
				Session.Log(TEXT("[FAIL] add_component -> name and component_type required"));
				return sol::lua_nil;
			}

			FRigElementKey ElementKey = FindElemKey(Access.Hierarchy, FElem);
			if (!ElementKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add_component -> element '%s' not found"), *FElem));
				return sol::lua_nil;
			}

			FString FCompType = NeoLuaStr::ToFString(CompType);
			UScriptStruct* ComponentStruct = ResolveRigComponentStruct(FCompType);
			if (!ComponentStruct)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add_component -> component_type '%s' not found"), *FCompType));
				return sol::lua_nil;
			}

			FString FContent = NeoLuaStr::ToFString(Content);
			const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddCompCR", "Add ControlRig Component"));
			static_cast<UObject*>(Access.BP)->Modify();
			FRigComponentKey Added = AddControlRigComponentCompat(Access.Ctrl, ComponentStruct, NeoLuaStr::ToFString(CompName), ElementKey, FContent);
			if (Added.IsValid())
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add_component(\"%s\", \"%s\")"), *FElem, *NeoLuaStr::ToFString(CompName)));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] add_component -> failed for '%s'"), *FElem));
			return sol::lua_nil;
		});

		// ---- remove_component(element, name) ----
		AssetObj.set_function("remove_component", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Element, const std::string& CompName, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FElem = NeoLuaStr::ToFString(Element);
			FString FCompName = NeoLuaStr::ToFString(CompName);

			FRigElementKey ElementKey = FindElemKey(Access.Hierarchy, FElem);
			if (!ElementKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] remove_component -> element '%s' not found"), *FElem));
				return sol::lua_nil;
			}

			FRigComponentKey ComponentKey(ElementKey, FName(*FCompName));
			if (!Access.Hierarchy->FindComponent(ComponentKey))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] remove_component -> component '%s' not found on '%s'"), *FCompName, *FElem));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemCompCR", "Remove ControlRig Component"));
			static_cast<UObject*>(Access.BP)->Modify();
			if (RemoveControlRigComponentCompat(Access.Ctrl, ComponentKey))
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove_component(\"%s\", \"%s\")"), *FElem, *FCompName));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] remove_component -> failed for '%s' on '%s'"), *FCompName, *FElem));
			return sol::lua_nil;
		});

		// ---- rename_component(element, name, new_name) ----
		AssetObj.set_function("rename_component", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Element, const std::string& CompName, const std::string& NewName,
			sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FElem = NeoLuaStr::ToFString(Element);
			FString FCompName = NeoLuaStr::ToFString(CompName);
			FString FNewName = NeoLuaStr::ToFString(NewName);

			FRigElementKey ElementKey = FindElemKey(Access.Hierarchy, FElem);
			if (!ElementKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] rename_component -> element '%s' not found"), *FElem));
				return sol::lua_nil;
			}

			FRigComponentKey ComponentKey(ElementKey, FName(*FCompName));
			if (!Access.Hierarchy->FindComponent(ComponentKey))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] rename_component -> component '%s' not found on '%s'"), *FCompName, *FElem));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RenCompCR", "Rename ControlRig Component"));
			static_cast<UObject*>(Access.BP)->Modify();
			FRigComponentKey Renamed = RenameControlRigComponentCompat(Access.Ctrl, ComponentKey, FNewName);
			if (Renamed.IsValid())
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] rename_component(\"%s\", \"%s\", \"%s\")"), *FElem, *FCompName, *FNewName));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] rename_component -> failed for '%s' on '%s'"), *FCompName, *FElem));
			return sol::lua_nil;
		});

		// ---- reparent_component(element, name, new_element) ----
		AssetObj.set_function("reparent_component", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Element, const std::string& CompName, const std::string& NewElement,
			sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FElem = NeoLuaStr::ToFString(Element);
			FString FCompName = NeoLuaStr::ToFString(CompName);
			FString FNewElem = NeoLuaStr::ToFString(NewElement);

			FRigElementKey ElementKey = FindElemKey(Access.Hierarchy, FElem);
			FRigElementKey NewElementKey = FindElemKey(Access.Hierarchy, FNewElem);
			if (!ElementKey.IsValid() || !NewElementKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] reparent_component -> element '%s' or new_element '%s' not found"), *FElem, *FNewElem));
				return sol::lua_nil;
			}

			FRigComponentKey ComponentKey(ElementKey, FName(*FCompName));
			if (!Access.Hierarchy->FindComponent(ComponentKey))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] reparent_component -> component '%s' not found on '%s'"), *FCompName, *FElem));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ReparCompCR", "Reparent ControlRig Component"));
			static_cast<UObject*>(Access.BP)->Modify();
			FRigComponentKey Reparented = ReparentControlRigComponentCompat(Access.Ctrl, ComponentKey, NewElementKey);
			if (Reparented.IsValid())
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] reparent_component(\"%s\", \"%s\", \"%s\")"), *FElem, *FCompName, *FNewElem));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] reparent_component -> failed for '%s'"), *FCompName));
			return sol::lua_nil;
		});
#else
		AssetObj.set_function("add_component", [&Session](sol::table /*self*/, const std::string& /*Element*/, sol::table /*Params*/, sol::this_state S) -> sol::object
		{
			Session.Log(TEXT("[FAIL] add_component -> requires UE 5.6+"));
			return sol::lua_nil;
		});
		AssetObj.set_function("remove_component", [&Session](sol::table /*self*/, const std::string& /*Element*/, const std::string& /*CompName*/, sol::this_state S) -> sol::object
		{
			Session.Log(TEXT("[FAIL] remove_component -> requires UE 5.6+"));
			return sol::lua_nil;
		});
		AssetObj.set_function("rename_component", [&Session](sol::table /*self*/, const std::string& /*Element*/, const std::string& /*CompName*/, const std::string& /*NewName*/, sol::this_state S) -> sol::object
		{
			Session.Log(TEXT("[FAIL] rename_component -> requires UE 5.6+"));
			return sol::lua_nil;
		});
		AssetObj.set_function("reparent_component", [&Session](sol::table /*self*/, const std::string& /*Element*/, const std::string& /*CompName*/, const std::string& /*NewElement*/, sol::this_state S) -> sol::object
		{
			Session.Log(TEXT("[FAIL] reparent_component -> requires UE 5.6+"));
			return sol::lua_nil;
		});
#endif

		// ---- add_animation_channel({name, parent_control, control_type}) ----
		AssetObj.set_function("add_animation_channel", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			std::string Name = Params.get_or<std::string>("name", "");
			if (Name.empty()) { Session.Log(TEXT("[FAIL] add_animation_channel -> name required")); return sol::lua_nil; }

			std::string ParentControl = Params.get_or<std::string>("parent_control", "");
			if (ParentControl.empty()) { Session.Log(TEXT("[FAIL] add_animation_channel -> parent_control required")); return sol::lua_nil; }

			FString FElemName = NeoLuaStr::ToFString(Name);
			FString FParentControl = NeoLuaStr::ToFString(ParentControl);

			FRigElementKey ParentKey(FName(*FParentControl), ERigElementType::Control);
			if (!Access.Hierarchy->Contains(ParentKey))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add_animation_channel -> parent control '%s' not found"), *FParentControl));
				return sol::lua_nil;
			}

			FRigControlSettings Settings;
			Settings.AnimationType = ERigControlAnimationType::AnimationChannel;
			std::string CtrlTypeStr2 = Params.get_or<std::string>("control_type", "Float");
			Settings.ControlType = ParseCtrlType(NeoLuaStr::ToFString(CtrlTypeStr2));

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddCRAnimChannel", "Add Animation Channel"));
			static_cast<UObject*>(Access.BP)->Modify();

			FRigElementKey ResultKey = Access.Ctrl->AddAnimationChannel(FName(*FElemName), ParentKey, Settings, false, false);
			if (!ResultKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add_animation_channel -> failed to add '%s' under '%s'"), *FElemName, *FParentControl));
				return sol::lua_nil;
			}

			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] add_animation_channel(name=\"%s\", parent_control=\"%s\", type=\"%s\")"),
				*FElemName, *FParentControl, *NeoLuaStr::ToFString(CtrlTypeStr2)));
			return sol::make_object(Lua, true);
		});

		// ---- set_display_name(control, display_name, opts?) ----
		AssetObj.set_function("set_display_name", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& ControlName, const std::string& DisplayName,
			sol::optional<sol::table> Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FCtrlName = NeoLuaStr::ToFString(ControlName);
			FString FDisplayName = NeoLuaStr::ToFString(DisplayName);

			FRigElementKey ControlKey(FName(*FCtrlName), ERigElementType::Control);
			if (!Access.Hierarchy->Contains(ControlKey))
			{
				ControlKey = FindElemKey(Access.Hierarchy, FCtrlName);
			}
			if (!ControlKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_display_name -> '%s' not found"), *FCtrlName));
				return sol::lua_nil;
			}

			bool bRenameElement = false;
			if (Opts.has_value())
			{
				bRenameElement = Opts.value().get_or("rename_element", false);
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "SetDispNameCR", "Set ControlRig Display Name"));
			static_cast<UObject*>(Access.BP)->Modify();
			FName Result = Access.Ctrl->SetDisplayName(ControlKey, FName(*FDisplayName), bRenameElement, false);
			if (Result.IsNone())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_display_name -> failed for '%s'"), *FCtrlName));
				return sol::lua_nil;
			}
			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] set_display_name(\"%s\", \"%s\")"), *FCtrlName, *FDisplayName));
			return sol::make_object(Lua, std::string(TCHAR_TO_UTF8(*Result.ToString())));
		});

		// ---- export_text(names_array) ----
		AssetObj.set_function("export_text", [WeakBP, FPath, &Session](sol::table /*self*/,
			sol::table Names, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			TArray<FRigElementKey> Keys;
			for (const auto& Pair : Names)
			{
				if (Pair.second.is<std::string>())
				{
					FString Name = NeoLuaStr::ToFString(Pair.second.as<std::string>());
					FRigElementKey Key = FindElemKey(Access.Hierarchy, Name);
					if (Key.IsValid()) Keys.Add(Key);
				}
			}
			if (Keys.Num() == 0)
			{
				Session.Log(TEXT("[FAIL] export_text -> no valid elements"));
				return sol::lua_nil;
			}
			FString Text = Access.Ctrl->ExportToText(Keys);
			Session.Log(FString::Printf(TEXT("[OK] export_text -> %d elements exported (%d chars)"), Keys.Num(), Text.Len()));
			return sol::make_object(Lua, std::string(TCHAR_TO_UTF8(*Text)));
		});

		// ---- import_text(content, opts?) ----
		AssetObj.set_function("import_text", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Content, sol::optional<sol::table> Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FContent = NeoLuaStr::ToFString(Content);
			if (FContent.IsEmpty())
			{
				Session.Log(TEXT("[FAIL] import_text -> content required"));
				return sol::lua_nil;
			}

			bool bReplaceExisting = false;
			if (Opts.has_value())
			{
				bReplaceExisting = Opts.value().get_or("replace_existing", false);
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "ImportTextCR", "Import ControlRig Text"));
			static_cast<UObject*>(Access.BP)->Modify();
			TArray<FRigElementKey> NewKeys = Access.Ctrl->ImportFromText(FContent, bReplaceExisting, true, false, false);
			if (NewKeys.Num() == 0)
			{
				Session.Log(TEXT("[FAIL] import_text -> no elements imported"));
				return sol::lua_nil;
			}
			Access.BP->MarkPackageDirty();

			sol::table Result = Lua.create_table();
			for (int32 i = 0; i < NewKeys.Num(); ++i)
			{
				Result[i + 1] = TCHAR_TO_UTF8(*NewKeys[i].Name.ToString());
			}
			Session.Log(FString::Printf(TEXT("[OK] import_text -> %d elements imported"), NewKeys.Num()));
			return Result;
		});

		// ---- set_tag(element, tag) ----
		AssetObj.set_function("set_tag", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Element, const std::string& Tag, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Hierarchy) return sol::lua_nil;

			FString FElem = NeoLuaStr::ToFString(Element);
			FString FTag = NeoLuaStr::ToFString(Tag);
			FRigElementKey Key = FindElemKey(Access.Hierarchy, FElem);
			if (!Key.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_tag -> element '%s' not found"), *FElem));
				return sol::lua_nil;
			}
			const FScopedTransaction Tx(NSLOCTEXT("AIK", "SetTagCR", "Set ControlRig Tag"));
			static_cast<UObject*>(Access.BP)->Modify();
			if (Access.Hierarchy->SetTag(Key, FName(*FTag)))
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_tag(\"%s\", \"%s\")"), *FElem, *FTag));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] set_tag -> failed for '%s'"), *FElem));
			return sol::lua_nil;
		});

		// ---- remove_tag(element, tag) ----
		AssetObj.set_function("remove_tag", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Element, const std::string& Tag, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Hierarchy) return sol::lua_nil;

			FString FElem = NeoLuaStr::ToFString(Element);
			FString FTag = NeoLuaStr::ToFString(Tag);
			FRigElementKey Key = FindElemKey(Access.Hierarchy, FElem);
			if (!Key.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] remove_tag -> element '%s' not found"), *FElem));
				return sol::lua_nil;
			}
			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemTagCR", "Remove ControlRig Tag"));
			static_cast<UObject*>(Access.BP)->Modify();
			TArray<FName> Tags = Access.Hierarchy->GetTags(Key);
			if (Tags.Remove(FName(*FTag)) > 0)
			{
				Access.Hierarchy->SetNameArrayMetadata(Key, URigHierarchy::TagMetadataName, Tags);
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove_tag(\"%s\", \"%s\")"), *FElem, *FTag));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] remove_tag -> tag '%s' not found on '%s'"), *FTag, *FElem));
			return sol::lua_nil;
		});

		// ---- add_channel_host(channel, host) ----
		AssetObj.set_function("add_channel_host", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Channel, const std::string& Host, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FChannel = NeoLuaStr::ToFString(Channel);
			FString FHost = NeoLuaStr::ToFString(Host);
			FRigElementKey ChannelKey = FindElemKey(Access.Hierarchy, FChannel);
			FRigElementKey HostKey = FindElemKey(Access.Hierarchy, FHost);
			if (!ChannelKey.IsValid() || !HostKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add_channel_host -> channel '%s' or host '%s' not found"), *FChannel, *FHost));
				return sol::lua_nil;
			}
			const FScopedTransaction Tx(NSLOCTEXT("AIK", "AddChanHostCR", "Add Channel Host"));
			static_cast<UObject*>(Access.BP)->Modify();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			if (Access.Ctrl->AddChannelHost(ChannelKey, HostKey, false))
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] add_channel_host(\"%s\", \"%s\")"), *FChannel, *FHost));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] add_channel_host -> failed for '%s'"), *FChannel));
			return sol::lua_nil;
#else
			Session.Log(TEXT("[FAIL] add_channel_host requires UE 5.5+"));
			return sol::lua_nil;
#endif
		});

		// ---- remove_channel_host(channel, host) ----
		AssetObj.set_function("remove_channel_host", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Channel, const std::string& Host, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FChannel = NeoLuaStr::ToFString(Channel);
			FString FHost = NeoLuaStr::ToFString(Host);
			FRigElementKey ChannelKey = FindElemKey(Access.Hierarchy, FChannel);
			FRigElementKey HostKey = FindElemKey(Access.Hierarchy, FHost);
			if (!ChannelKey.IsValid() || !HostKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] remove_channel_host -> channel '%s' or host '%s' not found"), *FChannel, *FHost));
				return sol::lua_nil;
			}
			const FScopedTransaction Tx(NSLOCTEXT("AIK", "RemChanHostCR", "Remove Channel Host"));
			static_cast<UObject*>(Access.BP)->Modify();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			if (Access.Ctrl->RemoveChannelHost(ChannelKey, HostKey, false))
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] remove_channel_host(\"%s\", \"%s\")"), *FChannel, *FHost));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] remove_channel_host -> failed for '%s'"), *FChannel));
			return sol::lua_nil;
#else
			Session.Log(TEXT("[FAIL] remove_channel_host requires UE 5.5+"));
			return sol::lua_nil;
#endif
		});

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		// ---- set_component_content(element, name, content) ----
		AssetObj.set_function("set_component_content", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& Element, const std::string& CompName, const std::string& Content,
			sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Ctrl) return sol::lua_nil;

			FString FElem = NeoLuaStr::ToFString(Element);
			FString FCompName = NeoLuaStr::ToFString(CompName);
			FString FContent = NeoLuaStr::ToFString(Content);

			FRigElementKey ElementKey = FindElemKey(Access.Hierarchy, FElem);
			if (!ElementKey.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_component_content -> element '%s' not found"), *FElem));
				return sol::lua_nil;
			}
			FRigComponentKey ComponentKey(ElementKey, FName(*FCompName));
			if (!Access.Hierarchy->FindComponent(ComponentKey))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_component_content -> component '%s' not found on '%s'"), *FCompName, *FElem));
				return sol::lua_nil;
			}
			const FScopedTransaction Tx(NSLOCTEXT("AIK", "SetCompContentCR", "Set ControlRig Component Content"));
			static_cast<UObject*>(Access.BP)->Modify();
			if (Access.Ctrl->SetComponentContent(ComponentKey, FContent, false))
			{
				Access.BP->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_component_content(\"%s\", \"%s\")"), *FElem, *FCompName));
				return sol::make_object(Lua, true);
			}
			Session.Log(FString::Printf(TEXT("[FAIL] set_component_content -> failed for '%s' on '%s'"), *FCompName, *FElem));
			return sol::lua_nil;
		});
#else
		AssetObj.set_function("set_component_content", [&Session](sol::table /*self*/, const std::string& /*Element*/, const std::string& /*CompName*/, const std::string& /*Content*/, sol::this_state S) -> sol::object
		{
			Session.Log(TEXT("[FAIL] set_component_content -> requires UE 5.6+"));
			return sol::lua_nil;
		});
#endif

		// ---- get_control_value(control, opts?) ----
		AssetObj.set_function("get_control_value", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& ControlName, sol::optional<sol::table> Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Hierarchy) return sol::lua_nil;

			FString FCtrlName = NeoLuaStr::ToFString(ControlName);
			FRigElementKey ControlKey(FName(*FCtrlName), ERigElementType::Control);
			if (!Access.Hierarchy->Contains(ControlKey))
			{
				ControlKey = FindElemKey(Access.Hierarchy, FCtrlName);
				if (!ControlKey.IsValid() || ControlKey.Type != ERigElementType::Control)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] get_control_value -> control '%s' not found"), *FCtrlName));
					return sol::lua_nil;
				}
			}

			ERigControlValueType ValueType = ERigControlValueType::Current;
			if (Opts.has_value())
			{
				std::string VTStr = Opts.value().get_or<std::string>("value_type", "current");
				ValueType = NeoLuaEnum::ParseOr<ERigControlValueType>(NeoLuaStr::ToFString(VTStr), ERigControlValueType::Current);
			}

			FRigControlElement* Elem = Cast<FRigControlElement>(Access.Hierarchy->Find(ControlKey));
			if (!Elem)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] get_control_value -> element cast failed for '%s'"), *FCtrlName));
				return sol::lua_nil;
			}

			FRigControlValue Val = Access.Hierarchy->GetControlValue(ControlKey, ValueType);
			sol::table Result = Lua.create_table();
			Result["control_type"] = TCHAR_TO_UTF8(*CtrlTypeStr(Elem->Settings.ControlType));

			switch (Elem->Settings.ControlType)
			{
			case ERigControlType::Bool:
				Result["value"] = Val.Get<bool>();
				break;
			case ERigControlType::Float:
			case ERigControlType::ScaleFloat:
				Result["x"] = Val.Get<float>();
				break;
			case ERigControlType::Integer:
				Result["x"] = Val.Get<int32>();
				break;
			case ERigControlType::Vector2D:
			case ERigControlType::Position:
			case ERigControlType::Scale:
			case ERigControlType::Rotator:
			{
				FVector3f V = Val.Get<FVector3f>();
				Result["x"] = V.X;
				Result["y"] = V.Y;
				Result["z"] = V.Z;
				break;
			}
			case ERigControlType::EulerTransform:
			{
				const auto& ET = Val.GetRef<FRigControlValue::FEulerTransform_Float>();
				Result["tx"] = ET.TranslationX;
				Result["ty"] = ET.TranslationY;
				Result["tz"] = ET.TranslationZ;
				Result["pitch"] = ET.RotationPitch;
				Result["yaw"] = ET.RotationYaw;
				Result["roll"] = ET.RotationRoll;
				Result["sx"] = ET.ScaleX;
				Result["sy"] = ET.ScaleY;
				Result["sz"] = ET.ScaleZ;
				break;
			}
			case ERigControlType::Transform:
			{
				const auto& Transform = Val.GetRef<FRigControlValue::FTransform_Float>();
				const FRotator Rotation = Transform.GetRotation().Rotator();
				Result["tx"] = Transform.TranslationX;
				Result["ty"] = Transform.TranslationY;
				Result["tz"] = Transform.TranslationZ;
				Result["pitch"] = Rotation.Pitch;
				Result["yaw"] = Rotation.Yaw;
				Result["roll"] = Rotation.Roll;
				Result["sx"] = Transform.ScaleX;
				Result["sy"] = Transform.ScaleY;
				Result["sz"] = Transform.ScaleZ;
				break;
			}
			case ERigControlType::TransformNoScale:
			{
				const auto& Transform = Val.GetRef<FRigControlValue::FTransformNoScale_Float>();
				const FRotator Rotation = Transform.GetRotation().Rotator();
				Result["tx"] = Transform.TranslationX;
				Result["ty"] = Transform.TranslationY;
				Result["tz"] = Transform.TranslationZ;
				Result["pitch"] = Rotation.Pitch;
				Result["yaw"] = Rotation.Yaw;
				Result["roll"] = Rotation.Roll;
				break;
			}
			default:
				break;
			}
			Session.Log(FString::Printf(TEXT("[OK] get_control_value(\"%s\")"), *FCtrlName));
			return Result;
		});

		// ---- set_control_value(control, value_table, opts?) ----
		AssetObj.set_function("set_control_value", [WeakBP, FPath, &Session](sol::table /*self*/,
			const std::string& ControlName, sol::table ValueTbl, sol::optional<sol::table> Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Hierarchy) return sol::lua_nil;

			FString FCtrlName = NeoLuaStr::ToFString(ControlName);
			FRigElementKey ControlKey(FName(*FCtrlName), ERigElementType::Control);
			if (!Access.Hierarchy->Contains(ControlKey))
			{
				ControlKey = FindElemKey(Access.Hierarchy, FCtrlName);
				if (!ControlKey.IsValid() || ControlKey.Type != ERigElementType::Control)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_control_value -> control '%s' not found"), *FCtrlName));
					return sol::lua_nil;
				}
			}

			ERigControlValueType ValueType = ERigControlValueType::Current;
			if (Opts.has_value())
			{
				std::string VTStr = Opts.value().get_or<std::string>("value_type", "current");
				ValueType = NeoLuaEnum::ParseOr<ERigControlValueType>(NeoLuaStr::ToFString(VTStr), ERigControlValueType::Current);
			}

			FRigControlElement* Elem = Cast<FRigControlElement>(Access.Hierarchy->Find(ControlKey));
			if (!Elem)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_control_value -> element cast failed for '%s'"), *FCtrlName));
				return sol::lua_nil;
			}

			FRigControlValue Val;
			switch (Elem->Settings.ControlType)
			{
			case ERigControlType::Bool:
				Val.Set<bool>(ValueTbl.get_or("value", false));
				break;
			case ERigControlType::Float:
			case ERigControlType::ScaleFloat:
				Val.Set<float>((float)ValueTbl.get_or("x", 0.0));
				break;
			case ERigControlType::Integer:
				Val.Set<int32>((int32)ValueTbl.get_or("x", 0.0));
				break;
			case ERigControlType::Vector2D:
				Val.Set<FVector3f>(FVector3f(
					(float)ValueTbl.get_or("x", 0.0),
					(float)ValueTbl.get_or("y", 0.0),
					0.f));
				break;
			case ERigControlType::Position:
			case ERigControlType::Scale:
			case ERigControlType::Rotator:
				Val.Set<FVector3f>(FVector3f(
					(float)ValueTbl.get_or("x", 0.0),
					(float)ValueTbl.get_or("y", 0.0),
					(float)ValueTbl.get_or("z", 0.0)));
				break;
			case ERigControlType::EulerTransform:
			{
				FEulerTransform ZeroET;
				FRigControlValue::FEulerTransform_Float ET(ZeroET);
				// Accept both tx/ty/tz and x/y/z (fallback) for translation
				ET.TranslationX = (float)ValueTbl.get_or("tx", ValueTbl.get_or("x", 0.0));
				ET.TranslationY = (float)ValueTbl.get_or("ty", ValueTbl.get_or("y", 0.0));
				ET.TranslationZ = (float)ValueTbl.get_or("tz", ValueTbl.get_or("z", 0.0));
				ET.RotationPitch = (float)ValueTbl.get_or("pitch", 0.0);
				ET.RotationYaw = (float)ValueTbl.get_or("yaw", 0.0);
				ET.RotationRoll = (float)ValueTbl.get_or("roll", 0.0);
				ET.ScaleX = (float)ValueTbl.get_or("sx", 1.0);
				ET.ScaleY = (float)ValueTbl.get_or("sy", 1.0);
				ET.ScaleZ = (float)ValueTbl.get_or("sz", 1.0);
				Val.Set<FRigControlValue::FEulerTransform_Float>(ET);
				break;
			}
			case ERigControlType::Transform:
				Val.Set<FRigControlValue::FTransform_Float>(ParseControlValueTransform(ValueTbl));
				break;
			case ERigControlType::TransformNoScale:
				if (HasControlValueScaleFields(ValueTbl))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_control_value -> TransformNoScale control '%s' does not accept scale fields"), *FCtrlName));
					return sol::lua_nil;
				}
				Val.Set<FRigControlValue::FTransformNoScale_Float>(ParseControlValueTransformNoScale(ValueTbl));
				break;
			default:
				Session.Log(FString::Printf(TEXT("[FAIL] set_control_value -> unsupported control type for '%s'"), *FCtrlName));
				return sol::lua_nil;
			}

			const FScopedTransaction Tx(NSLOCTEXT("AIK", "SetCtrlValCR", "Set ControlRig Control Value"));
			static_cast<UObject*>(Access.BP)->Modify();
			Access.Hierarchy->SetControlValue(ControlKey, Val, ValueType, false);
			Access.BP->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] set_control_value(\"%s\")"), *FCtrlName));
			return sol::make_object(Lua, true);
		});

		// ---- info() ----
		AssetObj.set_function("info", [WeakBP, FPath, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);

			auto Access = ResolveControlRigAccess(WeakBP, FPath, Session);
			if (!Access.Hierarchy) return sol::lua_nil;

			sol::table Result = Lua.create_table();

			int32 Bones = 0, Controls = 0, Nulls = 0, Curves = 0, Other = 0;
			Access.Hierarchy->ForEach<FRigBaseElement>([&](FRigBaseElement* Elem) -> bool
			{
				switch (Elem->GetKey().Type)
				{
				case ERigElementType::Bone: Bones++; break;
				case ERigElementType::Control: Controls++; break;
				case ERigElementType::Null: Nulls++; break;
				case ERigElementType::Curve: Curves++; break;
				default: Other++; break;
				}
				return true;
			});

			const int32 TotalCount = Access.Hierarchy->Num();
			Result["bones"] = Bones;
			Result["controls"] = Controls;
			Result["nulls"] = Nulls;
			Result["curves"] = Curves;
			Result["other"] = Other;
			Result["total"] = TotalCount;
			Result["bone_count"] = Bones;
			Result["control_count"] = Controls;
			Result["null_count"] = Nulls;
			Result["curve_count"] = Curves;
			Result["other_count"] = Other;
			Result["total_count"] = TotalCount;

			TArray<FRigVMGraphVariableDescription> Vars = Access.BP->GetMemberVariables();
			Result["variables"] = Vars.Num();
			Result["variable_count"] = Vars.Num();

			Session.Log(FString::Printf(TEXT("[OK] info() -> %d bones, %d controls, %d nulls, %d curves, %d vars"),
				Bones, Controls, Nulls, Curves, Vars.Num()));
			return Result;
		});
	});
}

namespace NSAIControlRigExtension
{
	const TArray<FLuaFunctionDoc>& GetControlRigLuaDocs()
	{
		return ControlRigDocs;
	}

	void BindControlRigLua(sol::state& Lua, FLuaSessionData& Session)
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("ControlRig")))
		{
			Session.Log(TEXT("[WARN] ControlRig plugin is not loaded. Enable it in Edit > Plugins to use this feature."));
			return;
		}
		BindControlRig(Lua, Session);
	}
}
