// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Lua/LuaBindingRegistry.h"
#include "Lua/LuaAssetResolver.h"
#include "Lua/LuaStr.h"
#include "Tools/NeoStackToolUtils.h"
#include "ScopedTransaction.h"

#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Rig/IKRigDefinition.h"
#include "Engine/SkeletalMesh.h"

// Per-op configuration (5.6+ headers)
#include "RetargetEditor/IKRetargeterPoseGenerator.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "Retargeter/RetargetOps/FKChainsOp.h"
#include "Retargeter/RetargetOps/IKChainsOp.h"
#include "Retargeter/RetargetOps/PelvisMotionOp.h"
#include "Retargeter/RetargetOps/RootMotionGeneratorOp.h"
#include "Retargeter/RetargetOps/PinBoneOp.h"
#include "Retargeter/RetargetOps/CopyBasePoseOp.h"
#include "Retargeter/RetargetOps/CurveRemapOp.h"
#include "Retargeter/RetargetOps/RunIKRigOp.h"
#include "Retargeter/RetargetOps/StrideWarpingOp.h"
#include "Retargeter/RetargetOps/ScaleSourceOp.h"
#include "Retargeter/RetargetOps/AlignPoleVectorOp.h"
#include "Retargeter/RetargetOps/SpeedPlantingOp.h"
#include "Retargeter/RetargetOps/RetargetPoseOp.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "Retargeter/RetargetOps/FilterBoneOp.h"
#include "Retargeter/RetargetOps/StretchChainOp.h"
#endif
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
#include "Retargeter/RetargetOps/AdditivePoseOp.h"
#include "Retargeter/RetargetOps/BlendToSourceOp.h"
#include "Retargeter/RetargetOps/OffsetGoalsOp.h"
#include "Retargeter/RetargetOps/ScaleGoalsOp.h"
#include "Retargeter/RetargetOps/WeaponGoalsOp.h"
#endif
#endif
#include "Lua/LuaDynamicTypeHelper.h"
#include "Lua/LuaPropertyTable.h"
#include "Modules/ModuleManager.h"
#include "IKRigExtensionBinding.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#define NSAI_IKRETARGET_HAS_EXPORTED_OP_SETTINGS 0
#else
#define NSAI_IKRETARGET_HAS_EXPORTED_OP_SETTINGS 1
#endif

// Settings mutations go through UE's op controllers. We copy the settings struct,
// apply Lua fields to the copy, and call the matching SetSettings() so op-specific
// runtime copy behavior is preserved instead of mutating live Settings storage.

// ============================================================================
// Helpers
// ============================================================================

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
// Backward-compatible short aliases for retarget op types (5.6+)
static const TMap<FString, FString> OpAliases = {
	{ TEXT("Pelvis"), TEXT("PelvisMotion") },
	{ TEXT("FK"), TEXT("FKChains") },
	{ TEXT("IK"), TEXT("IKChains") },
	{ TEXT("RunIK"), TEXT("RunIKRig") },
	{ TEXT("RootMotionGenerator"), TEXT("RootMotion") },
	{ TEXT("Curve"), TEXT("CurveRemap") },
	{ TEXT("Stride"), TEXT("StrideWarping") },
	{ TEXT("Floor"), TEXT("FloorConstraint") },
	{ TEXT("Pin"), TEXT("PinBone") },
	{ TEXT("Filter"), TEXT("FilterBone") },
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
	{ TEXT("RetargetPose"), TEXT("AdditivePose") },
#else
	{ TEXT("Blend"), TEXT("BlendToSource") },
	{ TEXT("Offset"), TEXT("OffsetGoals") },
	{ TEXT("Scale"), TEXT("ScaleGoals") },
	{ TEXT("Weapon"), TEXT("WeaponGoal") },
	{ TEXT("WeaponGoals"), TEXT("WeaponGoal") },
#endif
};

static const TArray<FString> OpPrefixes = { TEXT("FIKRetarget"), TEXT("IKRetarget") };
static const TArray<FString> OpSuffixes = { TEXT("Op") };

/** Resolve user-friendly op type aliases to full script struct paths */
static FString ResolveOpTypePath(const FString& TypeName)
{
	// Try alias first
	FString Resolved = TypeName;
	if (const FString* Alias = OpAliases.Find(TypeName))
		Resolved = *Alias;

	// Dynamic struct discovery
	UScriptStruct* OpStruct = LuaDynamicType::FindDerivedStruct(
		FIKRetargetOpBase::StaticStruct(), Resolved, OpPrefixes, OpSuffixes);
	if (OpStruct)
		return OpStruct->GetPathName();

	return FString();
}
#endif // ENGINE_MINOR_VERSION >= 6

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
/** Find op index by name (5.6+ retarget ops system) */
static int32 FindOpIndexByName(UIKRetargeterController* Ctrl, const FString& OpName)
{
	return Ctrl->GetIndexOfOpByName(FName(*OpName));
}
#endif

/** Parse "source" / "target" string to enum */
static bool ParseSourceOrTarget(const std::string& Str, ERetargetSourceOrTarget& OutVal)
{
	FString FStr = NeoLuaStr::ToFString(Str);
	if (FStr.Equals(TEXT("source"), ESearchCase::IgnoreCase))
	{
		OutVal = ERetargetSourceOrTarget::Source;
		return true;
	}
	if (FStr.Equals(TEXT("target"), ESearchCase::IgnoreCase))
	{
		OutVal = ERetargetSourceOrTarget::Target;
		return true;
	}
	return false;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
/** Parse FK rotation mode string to enum */
static bool ParseFKRotationMode(const std::string& Str, EFKChainRotationMode& OutVal)
{
	FString FStr = NeoLuaStr::ToFString(Str);
	if (FStr.Equals(TEXT("None"), ESearchCase::IgnoreCase))             { OutVal = EFKChainRotationMode::None; return true; }
	if (FStr.Equals(TEXT("Interpolated"), ESearchCase::IgnoreCase))     { OutVal = EFKChainRotationMode::Interpolated; return true; }
	if (FStr.Equals(TEXT("OneToOne"), ESearchCase::IgnoreCase))         { OutVal = EFKChainRotationMode::OneToOne; return true; }
	if (FStr.Equals(TEXT("OneToOneReversed"), ESearchCase::IgnoreCase)) { OutVal = EFKChainRotationMode::OneToOneReversed; return true; }
	if (FStr.Equals(TEXT("MatchChain"), ESearchCase::IgnoreCase))       { OutVal = EFKChainRotationMode::MatchChain; return true; }
	if (FStr.Equals(TEXT("MatchScaledChain"), ESearchCase::IgnoreCase)) { OutVal = EFKChainRotationMode::MatchScaledChain; return true; }
	if (FStr.Equals(TEXT("CopyLocal"), ESearchCase::IgnoreCase))        { OutVal = EFKChainRotationMode::CopyLocal; return true; }
	return false;
}

/** Parse FK translation mode string to enum */
static bool ParseFKTranslationMode(const std::string& Str, EFKChainTranslationMode& OutVal)
{
	FString FStr = NeoLuaStr::ToFString(Str);
	if (FStr.Equals(TEXT("None"), ESearchCase::IgnoreCase))                           { OutVal = EFKChainTranslationMode::None; return true; }
	if (FStr.Equals(TEXT("GloballyScaled"), ESearchCase::IgnoreCase))                 { OutVal = EFKChainTranslationMode::GloballyScaled; return true; }
	if (FStr.Equals(TEXT("Absolute"), ESearchCase::IgnoreCase))                       { OutVal = EFKChainTranslationMode::Absolute; return true; }
	if (FStr.Equals(TEXT("StretchBoneLengthUniformly"), ESearchCase::IgnoreCase))     { OutVal = EFKChainTranslationMode::StretchBoneLengthUniformly; return true; }
	if (FStr.Equals(TEXT("StretchBoneLengthNonUniformly"), ESearchCase::IgnoreCase))  { OutVal = EFKChainTranslationMode::StretchBoneLengthNonUniformly; return true; }
	if (FStr.Equals(TEXT("OrientAndScale"), ESearchCase::IgnoreCase))                 { OutVal = EFKChainTranslationMode::OrientAndScale; return true; }
	return false;
}
/** Convert FK rotation mode enum to string */
static const char* FKRotationModeToString(EFKChainRotationMode Mode)
{
	switch (Mode)
	{
	case EFKChainRotationMode::None:              return "None";
	case EFKChainRotationMode::Interpolated:      return "Interpolated";
	case EFKChainRotationMode::OneToOne:          return "OneToOne";
	case EFKChainRotationMode::OneToOneReversed:  return "OneToOneReversed";
	case EFKChainRotationMode::MatchChain:        return "MatchChain";
	case EFKChainRotationMode::MatchScaledChain:  return "MatchScaledChain";
	case EFKChainRotationMode::CopyLocal:         return "CopyLocal";
	default:                                      return "Unknown";
	}
}

/** Convert FK translation mode enum to string */
static const char* FKTranslationModeToString(EFKChainTranslationMode Mode)
{
	switch (Mode)
	{
	case EFKChainTranslationMode::None:                          return "None";
	case EFKChainTranslationMode::GloballyScaled:                return "GloballyScaled";
	case EFKChainTranslationMode::Absolute:                      return "Absolute";
	case EFKChainTranslationMode::StretchBoneLengthUniformly:    return "StretchBoneLengthUniformly";
	case EFKChainTranslationMode::StretchBoneLengthNonUniformly: return "StretchBoneLengthNonUniformly";
	case EFKChainTranslationMode::OrientAndScale:                return "OrientAndScale";
	default:                                                     return "Unknown";
	}
}
#endif // ENGINE_MINOR_VERSION >= 7

/** Parse auto map chain type string to enum */
static bool ParseAutoMapType(const std::string& Str, EAutoMapChainType& OutVal)
{
	FString FStr = NeoLuaStr::ToFString(Str);
	if (FStr.Equals(TEXT("exact"), ESearchCase::IgnoreCase))  { OutVal = EAutoMapChainType::Exact; return true; }
	if (FStr.Equals(TEXT("fuzzy"), ESearchCase::IgnoreCase))  { OutVal = EAutoMapChainType::Fuzzy; return true; }
	if (FStr.Equals(TEXT("clear"), ESearchCase::IgnoreCase))  { OutVal = EAutoMapChainType::Clear; return true; }
	return false;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
/** Get op type display name from its UScriptStruct */
static FString GetOpTypeDisplayName(const FIKRetargetOpBase* Op)
{
	if (!Op) return TEXT("Unknown");
	const UScriptStruct* OpType = Op->GetType();
	if (!OpType) return TEXT("Unknown");
	// Strip "FIKRetarget" prefix and "Op" suffix for display
	FString Name = OpType->GetName();
	Name.RemoveFromStart(TEXT("FIKRetarget"));
	Name.RemoveFromEnd(TEXT("Op"));
	return Name;
}

static bool IsRetargetOpTypeNamed(const FIKRetargetOpBase* Op, const TCHAR* StructName)
{
	const UScriptStruct* OpType = Op ? Op->GetType() : nullptr;
	if (!OpType)
	{
		return false;
	}

	const FString ActualName = OpType->GetName();
	return ActualName.Equals(StructName)
		|| ActualName.Equals(FString::Printf(TEXT("F%s"), StructName));
}

/** Build a new sol::table from Src, dropping any top-level key in ControlKeys.
 *  Used to hand a "settings-only" slice to NeoLuaProperty::ApplyTableToStruct when
 *  the caller's Lua table mixes selector keys (chain, op_name) with struct fields. */
static sol::table FilterOutControlKeys(const sol::table& Src, sol::state_view Lv,
	std::initializer_list<const TCHAR*> ControlKeys)
{
	sol::table Out = Lv.create_table();
	for (auto& KV : Src)
	{
		if (!KV.first.is<std::string>())
		{
			Out[KV.first] = KV.second;
			continue;
		}
		FString Key = UTF8_TO_TCHAR(KV.first.as<std::string>().c_str());
		bool bControl = false;
		for (const TCHAR* CK : ControlKeys)
		{
			if (Key.Equals(CK, ESearchCase::IgnoreCase)) { bControl = true; break; }
		}
		if (!bControl) Out[KV.first] = KV.second;
	}
	return Out;
}

/** Extract the struct-fields slice of a configure() Lua table. Applies the control-key
 *  filter in BOTH forms — flat (legacy: top-level fields + selector keys like `chain`,
 *  `op_name`, or bone refs) AND nested (new: everything under `properties={...}`). The
 *  filter catches the case where a user mistakenly drops a selector key inside the
 *  nested sub-table; without filtering, ApplyTableToStruct would attempt to write
 *  `chain="Spine"` into the FRetargetFKChainSettings::TargetChainName field and silently
 *  rename the chain. */
static sol::table ExtractSettingsProps(const sol::table& Src, sol::state_view Lv,
	std::initializer_list<const TCHAR*> ControlKeys)
{
	sol::optional<sol::table> Sub = Src.get<sol::optional<sol::table>>("properties");
	if (Sub.has_value()) return FilterOutControlKeys(Sub.value(), Lv, ControlKeys);
	return FilterOutControlKeys(Src, Lv, ControlKeys);
}

/** Look up a string key in the top-level Lua table, falling back to the nested
 *  `properties` sub-table if present. Used for selector keys that must be extracted
 *  before ApplyTableToStruct runs (e.g. bone-reference fields consumed by dedicated
 *  controller setters). */
static std::string GetSelectorString(const sol::table& Src, const char* Key)
{
	std::string Val = Src.get_or<std::string>(Key, "");
	if (!Val.empty()) return Val;
	sol::optional<sol::table> Sub = Src.get<sol::optional<sol::table>>("properties");
	if (Sub.has_value()) return Sub.value().get_or<std::string>(Key, "");
	return Val;
}

/** Log ApplyTableToStruct warnings as [WARN] session messages with a caller-supplied prefix. */
static void LogApplyWarnings(FLuaSessionData& Session, const FString& Prefix, const TArray<FString>& Warnings)
{
	for (const FString& W : Warnings)
	{
		Session.Log(FString::Printf(TEXT("[WARN] %s -> %s"), *Prefix, *W));
	}
}

#if NSAI_IKRETARGET_HAS_EXPORTED_OP_SETTINGS
template <typename ControllerType, typename SettingsType>
static bool TryApplyOpSettingsThroughController(
	UIKRetargetOpControllerBase* BaseCtrl,
	const UScriptStruct* ActualSettingsType,
	UIKRetargeter* Retargeter,
	const sol::table& Props,
	FString& ApplyErr,
	TArray<FString>& Warnings,
	bool& bApplied)
{
	ControllerType* TypedCtrl = Cast<ControllerType>(BaseCtrl);
	if (!TypedCtrl)
	{
		return false;
	}

	if (ActualSettingsType != SettingsType::StaticStruct())
	{
		ApplyErr = FString::Printf(TEXT("controller/settings type mismatch: controller=%s expected=%s actual=%s"),
			*ControllerType::StaticClass()->GetName(),
			*SettingsType::StaticStruct()->GetName(),
			ActualSettingsType ? *ActualSettingsType->GetName() : TEXT("<none>"));
		return true;
	}

	SettingsType SettingsCopy = TypedCtrl->GetSettings();
	bApplied = NeoLuaProperty::ApplyTableToStruct(
		SettingsType::StaticStruct(), &SettingsCopy, Retargeter,
		Props, ApplyErr, &Warnings);
	if (ApplyErr.IsEmpty())
	{
		TypedCtrl->SetSettings(SettingsCopy);
	}
	return true;
}
#endif

static bool ApplyGenericOpSettingsThroughController(
	UIKRetargetOpControllerBase* BaseCtrl,
	const UScriptStruct* ActualSettingsType,
	UIKRetargeter* Retargeter,
	const sol::table& Props,
	FString& ApplyErr,
	TArray<FString>& Warnings,
	bool& bApplied)
{
	if (!BaseCtrl)
	{
		ApplyErr = TEXT("op controller is null");
		return true;
	}

#if NSAI_IKRETARGET_HAS_EXPORTED_OP_SETTINGS
	if (TryApplyOpSettingsThroughController<UIKRetargetFKChainsController, FIKRetargetFKChainsOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetIKChainsController, FIKRetargetIKChainsOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetPelvisMotionController, FIKRetargetPelvisMotionOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetRootMotionController, FIKRetargetRootMotionOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetPinBoneController, FIKRetargetPinBoneOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (TryApplyOpSettingsThroughController<UIKRetargetFilterBoneController, FIKRetargetFilterBoneOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
#endif
	if (TryApplyOpSettingsThroughController<UIKRetargetCopyBasePoseController, FIKRetargetCopyBasePoseOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetCurveRemapController, FIKRetargetCurveRemapOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetRunIKRigController, FIKRetargetRunIKRigOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetStrideWarpingController, FIKRetargetStrideWarpingOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetScaleSourceController, FIKRetargetScaleSourceOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (TryApplyOpSettingsThroughController<UIKRetargetStretchChainController, FIKRetargetStretchChainOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
#endif
	if (TryApplyOpSettingsThroughController<UIKRetargetAlignPoleVectorController, FIKRetargetAlignPoleVectorOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetSpeedPlantingController, FIKRetargetSpeedPlantingOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (TryApplyOpSettingsThroughController<UIKRetargetAdditivePoseController, FIKRetargetAdditivePoseOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
#endif
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
	if (TryApplyOpSettingsThroughController<UIKRetargetPoseController, FIKRetargetPoseOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetBlendToSourceController, FIKRetargetBlendToSourceOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetOffsetGoalsController, FIKRetargetOffsetGoalsOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetScaleGoalsController, FIKRetargetScaleGoalsOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
	if (TryApplyOpSettingsThroughController<UIKRetargetWeaponGoalsOpController, FIKRetargetWeaponGoalsOpSettings>(
		BaseCtrl, ActualSettingsType, Retargeter, Props, ApplyErr, Warnings, bApplied)) return true;
#endif

	return false;
#else
	ApplyErr = TEXT("retarget op settings are not exported by this engine version");
	return true;
#endif
}
#endif

// ============================================================================
// Binding registration
// ============================================================================

static TArray<FLuaFunctionDoc> IKRetargeterDocs = {};

static void BindIKRetargeter(sol::state& Lua, FLuaSessionData& Session)
{
	Lua.set_function("_enrich_ik_retargeter", [&Session](sol::table AssetObj, sol::this_state S)
	{
		sol::state_view LuaView(S);
		std::string PathStr = AssetObj.get<std::string>("path");
		FString FPath = NeoLuaStr::ToFString(PathStr);
		UIKRetargeter* Retargeter = NeoLuaAsset::Resolve<UIKRetargeter>(FPath);
		if (!Retargeter) return;

		UIKRetargeterController* Ctrl = UIKRetargeterController::GetController(Retargeter);
		if (!Ctrl) return;

		// ====================================================================
		// help text
		// ====================================================================
		AssetObj["_help_text"] =
			"IK Retargeter — element types for add/remove/list/configure:\n"
			"  op            — retarget op in the stack\n"
			"  default_ops   — add standard op set (PelvisMotion, FK, IK, RunIK, RootMotion)\n"
			"  pose          — retarget pose (source or target)\n"
			"  chain_mapping — source-to-target chain mapping\n"
			"\n"
			"add(type, params):\n"
			"  add(\"op\", {type=\"FKChains\", name=\"My FK\", enabled=true})\n"
			"    Op types: dynamically discovered — FKChains, IKChains, PelvisMotion, RunIKRig, RootMotion,\n"
			"              CurveRemap, PinBone, FilterBone, CopyBasePose, StrideWarping, ScaleSource,\n"
			"              StretchChain, AlignPoleVector, SpeedPlanting, FloorConstraint, AdditivePose"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			", RetargetPose, BlendToSource, OffsetGoals, ScaleGoals, WeaponGoal (alias WeaponGoals)"
#endif
			"\n"
			"  add(\"default_ops\")  — add standard op set\n"
			"  add(\"pose\", {name=\"Custom\", for=\"target\"})\n"
			"\n"
			"remove(type, id):\n"
			"  remove(\"op\", \"FK Chains\")  — by name\n"
			"  remove(\"all_ops\")           — remove all ops\n"
			"  remove(\"pose\", {name=\"Custom\", for=\"target\"})\n"
			"\n"
			"list(type, opt_arg?):\n"
			"  list(\"ops\")            — op stack with index/name/type/enabled\n"
			"  list(\"chain_mappings\") — target->source chain pairs\n"
			"  list(\"poses\")          — source and target pose names\n"
			"  list(\"fk_chains\")      — FK chain settings (optional op_name arg)\n"
			"  list(\"fk_chains\", \"My FK Op\")  — FK settings for specific op\n"
			"  list(\"ik_chains\")      — IK chain settings (optional op_name arg)\n"
			"\n"
			"configure(type, params):\n"
			"  configure(\"source_ikrig\", \"/Game/Rigs/Source_IKRig\")\n"
			"  configure(\"target_ikrig\", \"/Game/Rigs/Target_IKRig\")\n"
			"  configure(\"preview_mesh\", {mesh=\"/Game/Mesh\", for=\"source\"})\n"
			"  configure(\"op\", {name=\"FK Chains\", enabled=false, new_name=\"...\"})\n"
			"  configure(\"chain_mapping\", {target=\"LeftArm\", source=\"LeftArm\"})\n"
			"  configure(\"fk_chain\", {chain=\"Spine\", rotation_mode=\"Interpolated\", rotation_alpha=1.0, ...})\n"
			"  configure(\"ik_chain\", {chain=\"LeftFoot\", enable_ik=true, blend_to_source=0.5, ...})\n"
			"  configure(\"pelvis\", {op_name=.., rotation_alpha=1.0, source_pelvis_bone=\"pelvis\", ...})\n"
			"  configure(\"root_motion\", {op_name=.., root_motion_source=\"CopyFromSourceRoot\", ...})\n"
			"  configure(\"op_settings\", {name=\"Pin Bones\", properties={...}})\n"
			"    — generic reflection-based path covers every op type (PinBone, FilterBone, CopyBasePose,\n"
			"      CurveRemap, StrideWarping, ScaleSource, StretchChain, AlignPoleVector, SpeedPlanting,\n"
			"      AdditivePose, RunIKRig"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			", RetargetPose, BlendToSource, OffsetGoals, ScaleGoals, WeaponGoal"
#endif
			", and the four above). 'properties' is a\n"
			"      table of struct fields — ApplyTableToStruct handles primitives/enums/vectors/rotators.\n"
			"\n"
			"  All configure(\"fk_chain\"|\"ik_chain\"|\"pelvis\"|\"root_motion\") calls also accept a nested\n"
			"  properties={...} sub-table; flat top-level fields are kept for backward compat.\n"
			"\n"
			"Action methods:\n"
			"  auto_map(mode, force?)   — \"exact\", \"fuzzy\", \"clear\". force defaults to true\n"
			"                              (remap everything); pass false to only fill unset chain mappings.\n"
			"  auto_align({for=\"target\", method=\"chain_to_chain\"})  — auto-align bones (methods: chain_to_chain, mesh_to_mesh, local_rotation, global_rotation)\n"
			"  move_op(name, to_index)  — reorder op in stack\n"
			"  set_parent_op(\"ChildOp\", \"ParentOp\") — set parent/child op relationship\n"
			"  get_parent_op(\"OpName\") — get parent op name (nil if none)\n"
			"  run_op_setup(name_or_index) — trigger initial setup on an op\n"
			"  reset_chain_settings({chain=.., op_name=..})   — reset chain in a single op\n"
			"  reset_chain_settings({chain=.., all_ops=true}) — reset chain across every op\n"
			"  get_target_ikrig_for_op(\"OpName\") — get target IK Rig path for op\n"
			"  get_all_target_ikrigs() — array of every distinct target IK Rig across all ops\n"
			"  get_pelvis_bone({for=..}) — pelvis bone name derived from the side's IK Rig\n"
			"  is_chain_at_default({chain=.., op_name=..}) — whether chain settings are still at defaults\n"
			"  is_chain_goal_connected(goal_name) — whether an IK goal is wired into a solver\n"
			"  duplicate_pose({name=.., new_name=.., for=..})\n"
			"  rename_pose({old_name=.., new_name=.., for=..})\n"
			"  set_current_pose({name=.., for=..})\n"
			"  get_retarget_pose({name=.., for=..}) — read an arbitrary (non-current) pose\n"
			"  edit_pose_bone({bone=.., rotation={p=0,y=0,r=0}, for=..})\n"
			"  get_rotation_offset({bone=.., for=..}) — read bone rotation offset\n"
			"  set_root_offset({offset={x=0,y=0,z=0}, for=..}) — true set-semantics; UE 5.7 persists Z only\n"
			"  get_root_offset({for=..}) — read root translation offset\n"
			"  get_source_chain({target=.., op_name=..}) — get mapped source chain\n"
			"  get_pelvis_bones({op_name=..}) — read pelvis bone assignments from op\n"
			"  get_root_bones({op_name=..}) — read root motion bone assignments from op\n"
			"  reset_pose({pose_name=.., bones={..}, for=..})\n"
			"  snap_to_ground({bone=.., for=..})\n"
			"\n"
			"Op-specific action verbs:\n"
			"  set_pin_pair({op=\"Pin\", from=\"source_bone\", to=\"target_bone\"}) — PinBone\n"
			"  clear_pin_pairs(\"op_name\") / get_pin_pairs(\"op_name\")          — PinBone\n"
			"  add_filter_bone({op=\"Filter\", bone=\"...\"})                    — FilterBone\n"
			"  clear_filter_bones(\"op_name\") / get_filter_bones(\"op_name\")    — FilterBone\n"
			"  configure_copy_base_pose({op=\"Copy\", copy_from_start=\"spine_01\", exclude={...}}) — CopyBasePose\n"
			"  get_copy_base_pose(\"op_name\")                                  — CopyBasePose\n"
			"\n"
			"  info()                   — asset summary (includes op stack with parent/children/can_have_children)\n";

		// ====================================================================
		// add(type, params)
		// ====================================================================
		AssetObj.set_function("add", [Ctrl, Retargeter, &Session](sol::table /*self*/,
			const std::string& Type, sol::optional<sol::table> Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			FString FType = NeoLuaStr::ToFString(Type);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			// ---- add("op", {type=.., name=.., enabled=..}) — 5.6+ retarget ops system ----
			if (FType.Equals(TEXT("op"), ESearchCase::IgnoreCase))
			{
				if (!Params.has_value())
				{
					Session.Log(TEXT("[FAIL] add(\"op\") -> params required: {type=\"FKChains\", name=.., enabled=true}"));
					return sol::lua_nil;
				}
				sol::table P = Params.value();
				std::string OpType = P.get_or<std::string>("type", "");
				if (OpType.empty())
				{
					Session.Log(TEXT("[FAIL] add(\"op\") -> 'type' required"));
					return sol::lua_nil;
				}

				FString OpPath = ResolveOpTypePath(NeoLuaStr::ToFString(OpType));
				if (OpPath.IsEmpty())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"op\") -> unknown op type '%s'. Valid: %s"),
					UTF8_TO_TCHAR(OpType.c_str()),
					*LuaDynamicType::FormatAvailableStructTypes(FIKRetargetOpBase::StaticStruct(), OpPrefixes, OpSuffixes)));
					return sol::lua_nil;
				}

				FScopedTransaction Txn(FText::FromString(TEXT("Add Retarget Op")));
				int32 Idx = Ctrl->AddRetargetOp(OpPath);
				if (Idx < 0)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"op\", type=\"%s\") -> engine rejected the op (duplicate singleton, unresolved parent, or invalid type path). Check list(\"ops\") for duplicates and verify the op type."),
						UTF8_TO_TCHAR(OpType.c_str())));
					return sol::lua_nil;
				}

				// Set name if provided
				std::string NameStr = P.get_or<std::string>("name", "");
				if (!NameStr.empty())
				{
					Ctrl->SetOpName(FName(NeoLuaStr::ToFString(NameStr)), Idx);
				}

				// Set enabled state
				sol::optional<bool> Enabled = P.get<sol::optional<bool>>("enabled");
				if (Enabled.has_value())
				{
					Ctrl->SetRetargetOpEnabled(Idx, Enabled.value());
				}

				// Assign IK rigs to the new op
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
				const UIKRigDefinition* SourceRig = Ctrl->GetIKRig(ERetargetSourceOrTarget::Source);
				if (SourceRig)
				{
					Ctrl->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, SourceRig);
				}
				const UIKRigDefinition* TargetRig = Ctrl->GetIKRig(ERetargetSourceOrTarget::Target);
				if (TargetRig)
				{
					Ctrl->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, TargetRig);
				}
#endif

				FName FinalName = Ctrl->GetOpName(Idx);
				Session.Log(FString::Printf(TEXT("[OK] add(\"op\", type=\"%s\") -> index %d, name \"%s\""),
					UTF8_TO_TCHAR(OpType.c_str()), Idx, *FinalName.ToString()));
				return sol::make_object(Lv, Idx);
			}

			// ---- add("default_ops") ----
			if (FType.Equals(TEXT("default_ops"), ESearchCase::IgnoreCase))
			{
				int32 ExistingCount = Ctrl->GetNumRetargetOps();
				if (ExistingCount > 0)
				{
					Session.Log(FString::Printf(TEXT("[OK] add(\"default_ops\") -> stack already has %d ops"), ExistingCount));
					return sol::make_object(Lv, ExistingCount);
				}
				FScopedTransaction Txn(FText::FromString(TEXT("Add Default Retarget Ops")));
				Ctrl->AddDefaultOps();

				// Assign IK rigs to all ops
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
				const UIKRigDefinition* SourceRig = Ctrl->GetIKRig(ERetargetSourceOrTarget::Source);
				if (SourceRig) Ctrl->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, SourceRig);
				const UIKRigDefinition* TargetRig = Ctrl->GetIKRig(ERetargetSourceOrTarget::Target);
				if (TargetRig) Ctrl->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, TargetRig);
#endif

				int32 NewCount = Ctrl->GetNumRetargetOps();
				Session.Log(FString::Printf(TEXT("[OK] add(\"default_ops\") -> %d ops added"), NewCount));
				return sol::make_object(Lv, NewCount);
			}
#endif // ENGINE_MINOR_VERSION >= 6 (retarget ops)

			// ---- add("pose", {name=.., for="target"}) ----
			if (FType.Equals(TEXT("pose"), ESearchCase::IgnoreCase))
			{
				if (!Params.has_value())
				{
					Session.Log(TEXT("[FAIL] add(\"pose\") -> params required: {name=.., for=\"target\"}"));
					return sol::lua_nil;
				}
				sol::table P = Params.value();
				std::string PoseName = P.get_or<std::string>("name", "");
				std::string ForStr = P.get_or<std::string>("for", "target");
				if (PoseName.empty())
				{
					Session.Log(TEXT("[FAIL] add(\"pose\") -> 'name' required"));
					return sol::lua_nil;
				}

				ERetargetSourceOrTarget Side;
				if (!ParseSourceOrTarget(ForStr, Side))
				{
					Session.Log(TEXT("[FAIL] add(\"pose\") -> 'for' must be \"source\" or \"target\""));
					return sol::lua_nil;
				}

				// Soft warning: the pose can be created without an IK rig assigned, but any
				// subsequent edit_pose_bone/auto_align/reset_pose calls against that side will
				// fail at the engine level (they dereference the side's IK Rig skeleton).
				if (!Ctrl->GetIKRig(Side))
				{
					const TCHAR* SideStr = Side == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target");
					Session.Log(FString::Printf(TEXT("[WARN] add(\"pose\", for=%s) -> %s IK Rig is not assigned yet; edit_pose_bone/auto_align on this pose will fail until you call configure(\"%s_ikrig\", ...)."),
						SideStr, SideStr, SideStr));
				}

				FScopedTransaction Txn(FText::FromString(TEXT("Create Retarget Pose")));
				FName Result = Ctrl->CreateRetargetPose(FName(NeoLuaStr::ToFString(PoseName)), Side);
				if (Result.IsNone())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"pose\", name=\"%s\", for=%s) -> CreateRetargetPose failed (name may collide with existing pose or IK rig may be unassigned). Call list(\"poses\") for existing names; configure source/target IK rigs first."),
						UTF8_TO_TCHAR(PoseName.c_str()), UTF8_TO_TCHAR(ForStr.c_str())));
					return sol::lua_nil;
				}
				Session.Log(FString::Printf(TEXT("[OK] add(\"pose\", \"%s\", for=%s)"), *Result.ToString(), UTF8_TO_TCHAR(ForStr.c_str())));
				return sol::make_object(Lv, std::string(TCHAR_TO_UTF8(*Result.ToString())));
			}

			Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> unknown type. Valid: op, default_ops, pose"), *FType));
			return sol::lua_nil;
		});

		// ====================================================================
		// remove(type, id)
		// ====================================================================
		AssetObj.set_function("remove", [Ctrl, &Session](sol::table /*self*/,
			const std::string& Type, sol::optional<sol::object> IdOpt, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			FString FType = NeoLuaStr::ToFString(Type);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			// ---- remove("op", "OpName") — 5.6+ ----
			if (FType.Equals(TEXT("op"), ESearchCase::IgnoreCase))
			{
				if (!IdOpt.has_value() || !IdOpt.value().is<std::string>())
				{
					Session.Log(TEXT("[FAIL] remove(\"op\") -> op name (string) required"));
					return sol::lua_nil;
				}
				FString OpName = NeoLuaStr::ToFString(IdOpt.value().as<std::string>());
				int32 Idx = FindOpIndexByName(Ctrl, OpName);
				if (Idx < 0)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] remove(\"op\", \"%s\") -> op not found. Call list(\"ops\") for valid names."), *OpName));
					return sol::lua_nil;
				}
				FScopedTransaction Txn(FText::FromString(TEXT("Remove Retarget Op")));
				bool bOk = Ctrl->RemoveRetargetOp(Idx);
				Session.Log(FString::Printf(TEXT("[%s] remove(\"op\", \"%s\")"), bOk ? TEXT("OK") : TEXT("FAIL"), *OpName));
				return bOk ? sol::make_object(Lv, true) : sol::lua_nil;
			}

			// ---- remove("all_ops") — 5.6+ ----
			if (FType.Equals(TEXT("all_ops"), ESearchCase::IgnoreCase))
			{
				FScopedTransaction Txn(FText::FromString(TEXT("Remove All Retarget Ops")));
				bool bOk = Ctrl->RemoveAllOps();
				Session.Log(FString::Printf(TEXT("[%s] remove(\"all_ops\")"), bOk ? TEXT("OK") : TEXT("FAIL")));
				return bOk ? sol::make_object(Lv, true) : sol::lua_nil;
			}
#endif

			// ---- remove("pose", {name=.., for=..}) ----
			if (FType.Equals(TEXT("pose"), ESearchCase::IgnoreCase))
			{
				if (!IdOpt.has_value() || !IdOpt.value().is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] remove(\"pose\") -> params table required: {name=.., for=\"target\"}"));
					return sol::lua_nil;
				}
				sol::table P = IdOpt.value().as<sol::table>();
				std::string PoseName = P.get_or<std::string>("name", "");
				std::string ForStr = P.get_or<std::string>("for", "target");
				if (PoseName.empty())
				{
					Session.Log(TEXT("[FAIL] remove(\"pose\") -> 'name' required"));
					return sol::lua_nil;
				}
				ERetargetSourceOrTarget Side;
				if (!ParseSourceOrTarget(ForStr, Side))
				{
					Session.Log(TEXT("[FAIL] remove(\"pose\") -> 'for' must be \"source\" or \"target\""));
					return sol::lua_nil;
				}
				FScopedTransaction Txn(FText::FromString(TEXT("Remove Retarget Pose")));
				bool bOk = Ctrl->RemoveRetargetPose(FName(NeoLuaStr::ToFString(PoseName)), Side);
				Session.Log(FString::Printf(TEXT("[%s] remove(\"pose\", \"%s\", for=%s)"),
					bOk ? TEXT("OK") : TEXT("FAIL"), UTF8_TO_TCHAR(PoseName.c_str()), UTF8_TO_TCHAR(ForStr.c_str())));
				return bOk ? sol::make_object(Lv, true) : sol::lua_nil;
			}

			Session.Log(FString::Printf(TEXT("[FAIL] remove(\"%s\") -> unknown type. Valid: op, all_ops, pose"), *FType));
			return sol::lua_nil;
		});

		// ====================================================================
		// list(type, opt_arg?)
		// ====================================================================
		AssetObj.set_function("list", [Ctrl, Retargeter, &Session](sol::table self,
			sol::optional<std::string> TypeOpt, sol::optional<std::string> ExtraArg, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			FString FType = NeoLuaStr::ToFStringOpt(TypeOpt, TEXT("all"));

			// ---- list("all") / list() -> info() ----
			if (FType.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				sol::protected_function InfoFn = self["info"];
				if (InfoFn.valid()) return InfoFn(self);
				return sol::lua_nil;
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			// ---- list("ops") — 5.6+ retarget ops system ----
			if (FType.Equals(TEXT("ops"), ESearchCase::IgnoreCase) || FType.Equals(TEXT("op"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lv.create_table();
				int32 Count = Ctrl->GetNumRetargetOps();
				for (int32 Idx = 0; Idx < Count; Idx++)
				{
					sol::table E = Lv.create_table();
					E["index"] = Idx;
					FName OpName = Ctrl->GetOpName(Idx);
					E["name"] = TCHAR_TO_UTF8(*OpName.ToString());
					E["enabled"] = Ctrl->GetRetargetOpEnabled(Idx);

					FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(Idx);
					E["type"] = TCHAR_TO_UTF8(*GetOpTypeDisplayName(Op));

					int32 ParentIdx = Ctrl->GetParentOpIndex(Idx);
					if (ParentIdx != INDEX_NONE)
					{
						FName ParentName = Ctrl->GetOpName(ParentIdx);
						E["parent"] = TCHAR_TO_UTF8(*ParentName.ToString());
					}

					// Parent/child hierarchy introspection (C3): emit children names + capability flag
					// so scripts can walk the op graph without iterating all ops themselves.
					E["can_have_children"] = Ctrl->GetCanOpHaveChildren(Idx);
					TArray<int32> ChildIndices = Ctrl->GetChildOpIndices(Idx);
					sol::table ChildList = Lv.create_table();
					for (int32 ChildI = 0; ChildI < ChildIndices.Num(); ++ChildI)
					{
						FName ChildName = Ctrl->GetOpName(ChildIndices[ChildI]);
						ChildList[ChildI + 1] = TCHAR_TO_UTF8(*ChildName.ToString());
					}
					E["children"] = ChildList;

					Result[Idx + 1] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"ops\") -> %d ops"), Count));
				return Result;
			}
#endif

			// ---- list("chain_mappings") ----
			if (FType.Equals(TEXT("chain_mappings"), ESearchCase::IgnoreCase) || FType.Equals(TEXT("chain_mapping"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lv.create_table();
				int32 PairCount = 0;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
				// Gather chain mappings from all ops (5.6+ retarget ops system)
				int32 NumOps = Ctrl->GetNumRetargetOps();
				for (int32 OpIdx = 0; OpIdx < NumOps; OpIdx++)
				{
					FName OpName = Ctrl->GetOpName(OpIdx);
					const FRetargetChainMapping* Mapping = Ctrl->GetChainMapping(OpName);
					if (!Mapping) continue;

					const TArray<FRetargetChainPair>& Pairs = Mapping->GetChainPairs();
					for (const FRetargetChainPair& Pair : Pairs)
					{
						sol::table E = Lv.create_table();
						E["target_chain"] = TCHAR_TO_UTF8(*Pair.TargetChainName.ToString());
						E["source_chain"] = TCHAR_TO_UTF8(*Pair.SourceChainName.ToString());
						E["op_name"] = TCHAR_TO_UTF8(*OpName.ToString());
						PairCount++;
						Result[PairCount] = E;
					}
				}
				// Fallback: try getting chain mapping without op name
				if (PairCount == 0)
				{
					const FRetargetChainMapping* Mapping = Ctrl->GetChainMapping(NAME_None);
					if (Mapping)
					{
						const TArray<FRetargetChainPair>& Pairs = Mapping->GetChainPairs();
						for (const FRetargetChainPair& Pair : Pairs)
						{
							sol::table E = Lv.create_table();
							E["target_chain"] = TCHAR_TO_UTF8(*Pair.TargetChainName.ToString());
							E["source_chain"] = TCHAR_TO_UTF8(*Pair.SourceChainName.ToString());
							PairCount++;
							Result[PairCount] = E;
						}
					}
				}
#endif // ENGINE_MINOR_VERSION >= 6
				Session.Log(FString::Printf(TEXT("[OK] list(\"chain_mappings\") -> %d pairs"), PairCount));
				return Result;
			}

			// ---- list("poses") ----
			if (FType.Equals(TEXT("poses"), ESearchCase::IgnoreCase) || FType.Equals(TEXT("pose"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lv.create_table();

				sol::table SourcePoses = Lv.create_table();
				TMap<FName, FIKRetargetPose>& SrcMap = Ctrl->GetRetargetPoses(ERetargetSourceOrTarget::Source);
				int32 Si = 1;
				for (auto& KV : SrcMap)
				{
					SourcePoses[Si++] = TCHAR_TO_UTF8(*KV.Key.ToString());
				}
				Result["source"] = SourcePoses;
				Result["source_current"] = TCHAR_TO_UTF8(*Ctrl->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Source).ToString());

				sol::table TargetPoses = Lv.create_table();
				TMap<FName, FIKRetargetPose>& TgtMap = Ctrl->GetRetargetPoses(ERetargetSourceOrTarget::Target);
				int32 Ti = 1;
				for (auto& KV : TgtMap)
				{
					TargetPoses[Ti++] = TCHAR_TO_UTF8(*KV.Key.ToString());
				}
				Result["target"] = TargetPoses;
				Result["target_current"] = TCHAR_TO_UTF8(*Ctrl->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target).ToString());

				Session.Log(FString::Printf(TEXT("[OK] list(\"poses\") -> %d source, %d target"), SrcMap.Num(), TgtMap.Num()));
				return Result;
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			// ---- list("fk_chains", op_name?) ----
			if (FType.Equals(TEXT("fk_chains"), ESearchCase::IgnoreCase) || FType.Equals(TEXT("fk_chain"), ESearchCase::IgnoreCase))
			{
				FName TargetOpName = NAME_None;
				if (ExtraArg.has_value() && !ExtraArg.value().empty())
				{
					TargetOpName = FName(NeoLuaStr::ToFStringOpt(ExtraArg));
				}

				sol::table Result = Lv.create_table();
				int32 EntryIdx = 1;
				int32 NumOps = Ctrl->GetNumRetargetOps();
				for (int32 OpIdx = 0; OpIdx < NumOps; OpIdx++)
				{
					FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(OpIdx);
					if (!IsRetargetOpTypeNamed(Op, TEXT("IKRetargetFKChainsOp"))) continue;

					FName OpName = Ctrl->GetOpName(OpIdx);
					if (!TargetOpName.IsNone() && OpName != TargetOpName) continue;

					// Read settings directly off the op to avoid the by-value GetSettings() copy
					// that would otherwise require the FIKRetargetFKChainsOpSettings vtable.
					const FIKRetargetFKChainsOp* FKOp = static_cast<const FIKRetargetFKChainsOp*>(Op);
					for (const FRetargetFKChainSettings& Chain : FKOp->Settings.ChainsToRetarget)
					{
						sol::table E = Lv.create_table();
						E["op_name"] = TCHAR_TO_UTF8(*OpName.ToString());
						E["chain"] = TCHAR_TO_UTF8(*Chain.TargetChainName.ToString());
						E["enable_fk"] = Chain.EnableFK;
						E["rotation_mode"] = FKRotationModeToString(Chain.RotationMode);
						E["rotation_alpha"] = Chain.RotationAlpha;
						E["translation_mode"] = FKTranslationModeToString(Chain.TranslationMode);
						E["translation_alpha"] = Chain.TranslationAlpha;
						Result[EntryIdx++] = E;
					}
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"fk_chains\") -> %d chains"), EntryIdx - 1));
				return Result;
			}

			// ---- list("ik_chains", op_name?) ----
			if (FType.Equals(TEXT("ik_chains"), ESearchCase::IgnoreCase) || FType.Equals(TEXT("ik_chain"), ESearchCase::IgnoreCase))
			{
				FName TargetOpName = NAME_None;
				if (ExtraArg.has_value() && !ExtraArg.value().empty())
				{
					TargetOpName = FName(NeoLuaStr::ToFStringOpt(ExtraArg));
				}

				sol::table Result = Lv.create_table();
				int32 EntryIdx = 1;
				int32 NumOps = Ctrl->GetNumRetargetOps();
				for (int32 OpIdx = 0; OpIdx < NumOps; OpIdx++)
				{
					FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(OpIdx);
					if (!IsRetargetOpTypeNamed(Op, TEXT("IKRetargetIKChainsOp"))) continue;

					FName OpName = Ctrl->GetOpName(OpIdx);
					if (!TargetOpName.IsNone() && OpName != TargetOpName) continue;

					// Direct access — avoids the by-value controller GetSettings copy that
					// would otherwise need an ODR-violating FIKRetargetIKChainsOpSettings::PostLoad stub.
					const FIKRetargetIKChainsOp* IKOp = static_cast<const FIKRetargetIKChainsOp*>(Op);
					for (const FRetargetIKChainSettings& Chain : IKOp->Settings.ChainsToRetarget)
					{
						sol::table E = Lv.create_table();
						E["op_name"] = TCHAR_TO_UTF8(*OpName.ToString());
						E["chain"] = TCHAR_TO_UTF8(*Chain.TargetChainName.ToString());
						E["enable_ik"] = Chain.EnableIK;
						E["blend_to_source"] = Chain.BlendToSource;
						E["blend_to_source_translation"] = Chain.BlendToSourceTranslation;
						E["blend_to_source_rotation"] = Chain.BlendToSourceRotation;
						E["extension"] = Chain.Extension;
						E["scale_vertical"] = Chain.ScaleVertical;
						E["apply_pelvis_offset_to_source_goals"] = Chain.ApplyPelvisOffsetToSourceGoals;

						sol::table Offset = Lv.create_table();
						Offset["x"] = Chain.StaticOffset.X;
						Offset["y"] = Chain.StaticOffset.Y;
						Offset["z"] = Chain.StaticOffset.Z;
						E["static_offset"] = Offset;

						sol::table LocalOffset = Lv.create_table();
						LocalOffset["x"] = Chain.StaticLocalOffset.X;
						LocalOffset["y"] = Chain.StaticLocalOffset.Y;
						LocalOffset["z"] = Chain.StaticLocalOffset.Z;
						E["static_local_offset"] = LocalOffset;

						sol::table RotOffset = Lv.create_table();
						RotOffset["p"] = Chain.StaticRotationOffset.Pitch;
						RotOffset["y"] = Chain.StaticRotationOffset.Yaw;
						RotOffset["r"] = Chain.StaticRotationOffset.Roll;
						E["static_rotation_offset"] = RotOffset;

						sol::table BlendWeights = Lv.create_table();
						BlendWeights["x"] = Chain.BlendToSourceWeights.X;
						BlendWeights["y"] = Chain.BlendToSourceWeights.Y;
						BlendWeights["z"] = Chain.BlendToSourceWeights.Z;
						E["blend_to_source_weights"] = BlendWeights;

						Result[EntryIdx++] = E;
					}
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"ik_chains\") -> %d chains"), EntryIdx - 1));
				return Result;
			}
#endif // ENGINE_MINOR_VERSION >= 7

			Session.Log(FString::Printf(TEXT("[FAIL] list(\"%s\") -> unknown type. Valid: ops, chain_mappings, poses, fk_chains, ik_chains"), *FType));
			return sol::lua_nil;
		});

		// ====================================================================
		// configure(type, params)
		// ====================================================================
		AssetObj.set_function("configure", [Ctrl, Retargeter, &Session](sol::table /*self*/,
			const std::string& Type, sol::object Param, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			FString FType = NeoLuaStr::ToFString(Type);

			// ---- configure("source_ikrig", path) ----
			if (FType.Equals(TEXT("source_ikrig"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<std::string>())
				{
					Session.Log(TEXT("[FAIL] configure(\"source_ikrig\") -> path string required"));
					return sol::lua_nil;
				}
				FString RigPath = NeoLuaStr::ToFString(Param.as<std::string>());
				UIKRigDefinition* IKRig = NeoLuaAsset::Resolve<UIKRigDefinition>(RigPath);
				if (!IKRig)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"source_ikrig\") -> '%s' not found"), *RigPath));
					return sol::lua_nil;
				}
				FScopedTransaction Txn(FText::FromString(TEXT("Set Source IK Rig")));
				Ctrl->SetIKRig(ERetargetSourceOrTarget::Source, IKRig);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
				Ctrl->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, IKRig);
#endif
				Session.Log(FString::Printf(TEXT("[OK] configure(\"source_ikrig\", \"%s\")"), *IKRig->GetName()));
				return sol::make_object(Lv, true);
			}

			// ---- configure("target_ikrig", path) ----
			if (FType.Equals(TEXT("target_ikrig"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<std::string>())
				{
					Session.Log(TEXT("[FAIL] configure(\"target_ikrig\") -> path string required"));
					return sol::lua_nil;
				}
				FString RigPath = NeoLuaStr::ToFString(Param.as<std::string>());
				UIKRigDefinition* IKRig = NeoLuaAsset::Resolve<UIKRigDefinition>(RigPath);
				if (!IKRig)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"target_ikrig\") -> '%s' not found"), *RigPath));
					return sol::lua_nil;
				}
				FScopedTransaction Txn(FText::FromString(TEXT("Set Target IK Rig")));
				Ctrl->SetIKRig(ERetargetSourceOrTarget::Target, IKRig);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
				Ctrl->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, IKRig);
#endif
				Session.Log(FString::Printf(TEXT("[OK] configure(\"target_ikrig\", \"%s\")"), *IKRig->GetName()));
				return sol::make_object(Lv, true);
			}

			// ---- configure("preview_mesh", {mesh=.., for=..}) ----
			if (FType.Equals(TEXT("preview_mesh"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"preview_mesh\") -> table required: {mesh=.., for=\"source\"}"));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();
				std::string MeshPath = P.get_or<std::string>("mesh", "");
				std::string ForStr = P.get_or<std::string>("for", "target");
				if (MeshPath.empty())
				{
					Session.Log(TEXT("[FAIL] configure(\"preview_mesh\") -> 'mesh' path required"));
					return sol::lua_nil;
				}
				ERetargetSourceOrTarget Side;
				if (!ParseSourceOrTarget(ForStr, Side))
				{
					Session.Log(TEXT("[FAIL] configure(\"preview_mesh\") -> 'for' must be \"source\" or \"target\""));
					return sol::lua_nil;
				}
				FString FMeshPath = NeoLuaStr::ToFString(MeshPath);
				USkeletalMesh* Mesh = NeoLuaAsset::Resolve<USkeletalMesh>(FMeshPath);
				if (!Mesh)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"preview_mesh\") -> '%s' not found"), *FMeshPath));
					return sol::lua_nil;
				}
				FScopedTransaction Txn(FText::FromString(TEXT("Set Preview Mesh")));
				Ctrl->SetPreviewMesh(Side, Mesh);
				Session.Log(FString::Printf(TEXT("[OK] configure(\"preview_mesh\", \"%s\", for=%s)"),
					*Mesh->GetName(), UTF8_TO_TCHAR(ForStr.c_str())));
				return sol::make_object(Lv, true);
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			// ---- configure("op", {name=.., enabled=..}) — 5.6+ ----
			if (FType.Equals(TEXT("op"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"op\") -> table required: {name=.., enabled=..}"));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();
				std::string OpNameStr = P.get_or<std::string>("name", "");
				if (OpNameStr.empty())
				{
					Session.Log(TEXT("[FAIL] configure(\"op\") -> 'name' required"));
					return sol::lua_nil;
				}
				FString OpName = NeoLuaStr::ToFString(OpNameStr);
				int32 Idx = FindOpIndexByName(Ctrl, OpName);
				if (Idx < 0)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"op\") -> op '%s' not found"), *OpName));
					return sol::lua_nil;
				}
				sol::optional<bool> Enabled = P.get<sol::optional<bool>>("enabled");
				if (Enabled.has_value())
				{
					Ctrl->SetRetargetOpEnabled(Idx, Enabled.value());
				}
				std::string NewName = P.get_or<std::string>("new_name", "");
				if (!NewName.empty())
				{
					Ctrl->SetOpName(FName(NeoLuaStr::ToFString(NewName)), Idx);
				}
				Session.Log(FString::Printf(TEXT("[OK] configure(\"op\", \"%s\")"), *OpName));
				return sol::make_object(Lv, true);
			}

#endif // ENGINE_MINOR_VERSION >= 6 (configure op)

			// ---- configure("chain_mapping", {target=.., source=.., op_name=..}) ----
			if (FType.Equals(TEXT("chain_mapping"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"chain_mapping\") -> table required: {target=.., source=..}"));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();
				std::string TargetChain = P.get_or<std::string>("target", "");
				std::string SourceChain = P.get_or<std::string>("source", "");
				std::string OpNameStr = P.get_or<std::string>("op_name", "");
				if (TargetChain.empty())
				{
					Session.Log(TEXT("[FAIL] configure(\"chain_mapping\") -> 'target' chain name required"));
					return sol::lua_nil;
				}
				FName OpName = OpNameStr.empty() ? NAME_None : FName(NeoLuaStr::ToFString(OpNameStr));
				FScopedTransaction Txn(FText::FromString(TEXT("Set Chain Mapping")));
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
				bool bOk = Ctrl->SetSourceChain(
					FName(NeoLuaStr::ToFString(SourceChain)),
					FName(NeoLuaStr::ToFString(TargetChain)),
					OpName);
#else
				bool bOk = Ctrl->SetSourceChain(
					FName(NeoLuaStr::ToFString(SourceChain)),
					FName(NeoLuaStr::ToFString(TargetChain)));
#endif
				Session.Log(FString::Printf(TEXT("[%s] configure(\"chain_mapping\", target=\"%s\", source=\"%s\")"),
					bOk ? TEXT("OK") : TEXT("FAIL"),
					UTF8_TO_TCHAR(TargetChain.c_str()), UTF8_TO_TCHAR(SourceChain.c_str())));
				return bOk ? sol::make_object(Lv, true) : sol::lua_nil;
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7 && NSAI_IKRETARGET_HAS_EXPORTED_OP_SETTINGS
			// ---- configure("fk_chain", {chain=.., op_name=.., ...struct fields... }) ----
			// Accepts struct fields flat at top level (legacy) or nested under "properties" (new).
			// Applied to a copied settings struct and committed through the FK op controller.
			if (FType.Equals(TEXT("fk_chain"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"fk_chain\") -> table required: {chain=.., op_name=.., <fields> | properties={...}}"));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();
				std::string ChainName = P.get_or<std::string>("chain", "");
				if (ChainName.empty())
				{
					Session.Log(TEXT("[FAIL] configure(\"fk_chain\") -> 'chain' name required"));
					return sol::lua_nil;
				}
				FName FChainName = FName(NeoLuaStr::ToFString(ChainName));

				// Find the FK op (by op_name or first FK op)
				std::string OpNameStr = P.get_or<std::string>("op_name", "");
				int32 NumOps = Ctrl->GetNumRetargetOps();
				int32 FoundOpIdx = -1;
				for (int32 OpIdx = 0; OpIdx < NumOps; OpIdx++)
				{
					FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(OpIdx);
					if (!Op || !Op->GetType()->IsChildOf(FIKRetargetFKChainsOp::StaticStruct())) continue;
					if (!OpNameStr.empty())
					{
						FName OpNm = Ctrl->GetOpName(OpIdx);
						if (!OpNm.ToString().Equals(UTF8_TO_TCHAR(OpNameStr.c_str()), ESearchCase::IgnoreCase)) continue;
					}
					FoundOpIdx = OpIdx;
					break;
				}
				if (FoundOpIdx < 0)
				{
					Session.Log(TEXT("[FAIL] configure(\"fk_chain\") -> no FK Chains op found. Call list(\"ops\") or add(\"op\", {type=\"FKChains\"}) first."));
					return sol::lua_nil;
				}

				UIKRetargetFKChainsController* FKCtrl = Cast<UIKRetargetFKChainsController>(Ctrl->GetOpController(FoundOpIdx));
				if (!FKCtrl)
				{
					Session.Log(TEXT("[FAIL] configure(\"fk_chain\") -> failed to get FK Chains controller"));
					return sol::lua_nil;
				}
				FIKRetargetFKChainsOpSettings SettingsCopy = FKCtrl->GetSettings();

				FRetargetFKChainSettings* ChainSettings = nullptr;
				for (FRetargetFKChainSettings& Chain : SettingsCopy.ChainsToRetarget)
				{
					if (Chain.TargetChainName == FChainName) { ChainSettings = &Chain; break; }
				}
				if (!ChainSettings)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"fk_chain\") -> chain '%s' not found in FK op. Call list(\"fk_chains\") for valid names."), UTF8_TO_TCHAR(ChainName.c_str())));
					return sol::lua_nil;
				}

				FScopedTransaction Txn(FText::FromString(TEXT("Configure FK Chain")));
				Retargeter->Modify();

				sol::table SettingsProps = ExtractSettingsProps(P, Lv, { TEXT("chain"), TEXT("op_name") });
				FString ApplyErr;
				TArray<FString> Warnings;
				NeoLuaProperty::ApplyTableToStruct(
					FRetargetFKChainSettings::StaticStruct(), ChainSettings, Retargeter,
					SettingsProps, ApplyErr, &Warnings);
				LogApplyWarnings(Session, FString::Printf(TEXT("configure(\"fk_chain\", \"%s\")"), *FChainName.ToString()), Warnings);
				if (!ApplyErr.IsEmpty())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"fk_chain\", \"%s\") -> %s"), *FChainName.ToString(), *ApplyErr));
					return sol::lua_nil;
				}
				FKCtrl->SetSettings(SettingsCopy);
				Retargeter->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] configure(\"fk_chain\", \"%s\")"), UTF8_TO_TCHAR(ChainName.c_str())));
				return sol::make_object(Lv, true);
			}

			// ---- configure("ik_chain", {chain=.., op_name=.., ...struct fields... }) ----
			// Same pattern as configure("fk_chain"): mutate a copied settings struct and
			// commit through the IK op controller.
			if (FType.Equals(TEXT("ik_chain"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"ik_chain\") -> table required: {chain=.., op_name=.., <fields> | properties={...}}"));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();
				std::string ChainName = P.get_or<std::string>("chain", "");
				if (ChainName.empty())
				{
					Session.Log(TEXT("[FAIL] configure(\"ik_chain\") -> 'chain' name required"));
					return sol::lua_nil;
				}
				FName FChainName = FName(NeoLuaStr::ToFString(ChainName));

				// Find the IK op
				std::string OpNameStr = P.get_or<std::string>("op_name", "");
				int32 NumOps = Ctrl->GetNumRetargetOps();
				int32 FoundOpIdx = -1;
				for (int32 OpIdx = 0; OpIdx < NumOps; OpIdx++)
				{
					FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(OpIdx);
					if (!Op || !Op->GetType()->IsChildOf(FIKRetargetIKChainsOp::StaticStruct())) continue;
					if (!OpNameStr.empty())
					{
						FName OpNm = Ctrl->GetOpName(OpIdx);
						if (!OpNm.ToString().Equals(UTF8_TO_TCHAR(OpNameStr.c_str()), ESearchCase::IgnoreCase)) continue;
					}
					FoundOpIdx = OpIdx;
					break;
				}
				if (FoundOpIdx < 0)
				{
					Session.Log(TEXT("[FAIL] configure(\"ik_chain\") -> no IK Chains op found. Call list(\"ops\") or add(\"op\", {type=\"IKChains\"}) first."));
					return sol::lua_nil;
				}

				UIKRetargetIKChainsController* IKCtrl = Cast<UIKRetargetIKChainsController>(Ctrl->GetOpController(FoundOpIdx));
				if (!IKCtrl)
				{
					Session.Log(TEXT("[FAIL] configure(\"ik_chain\") -> failed to get IK Chains controller"));
					return sol::lua_nil;
				}
				FIKRetargetIKChainsOpSettings SettingsCopy = IKCtrl->GetSettings();

				FRetargetIKChainSettings* ChainSettings = nullptr;
				for (FRetargetIKChainSettings& Chain : SettingsCopy.ChainsToRetarget)
				{
					if (Chain.TargetChainName == FChainName) { ChainSettings = &Chain; break; }
				}
				if (!ChainSettings)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"ik_chain\") -> chain '%s' not found in IK op. Call list(\"ik_chains\") for valid names."), UTF8_TO_TCHAR(ChainName.c_str())));
					return sol::lua_nil;
				}

				FScopedTransaction Txn(FText::FromString(TEXT("Configure IK Chain")));
				Retargeter->Modify();

				sol::table SettingsProps = ExtractSettingsProps(P, Lv, { TEXT("chain"), TEXT("op_name") });
				FString ApplyErr;
				TArray<FString> Warnings;
				NeoLuaProperty::ApplyTableToStruct(
					FRetargetIKChainSettings::StaticStruct(), ChainSettings, Retargeter,
					SettingsProps, ApplyErr, &Warnings);
				LogApplyWarnings(Session, FString::Printf(TEXT("configure(\"ik_chain\", \"%s\")"), *FChainName.ToString()), Warnings);
				if (!ApplyErr.IsEmpty())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"ik_chain\", \"%s\") -> %s"), *FChainName.ToString(), *ApplyErr));
					return sol::lua_nil;
				}
				IKCtrl->SetSettings(SettingsCopy);
				Retargeter->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] configure(\"ik_chain\", \"%s\")"), UTF8_TO_TCHAR(ChainName.c_str())));
				return sol::make_object(Lv, true);
			}

			// ---- configure("pelvis", {op_name=.., source_pelvis_bone=.., target_pelvis_bone=.., <struct fields>}) ----
			// Bone references go through the controller's dedicated setters. All other
			// settings fields are applied to a copy and committed through SetSettings().
			if (FType.Equals(TEXT("pelvis"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"pelvis\") -> table required"));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();

				// Find pelvis op
				std::string OpNameStr = P.get_or<std::string>("op_name", "");
				int32 NumOps = Ctrl->GetNumRetargetOps();
				int32 FoundOpIdx = -1;
				for (int32 OpIdx = 0; OpIdx < NumOps; OpIdx++)
				{
					FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(OpIdx);
					if (!Op || !Op->GetType()->IsChildOf(FIKRetargetPelvisMotionOp::StaticStruct())) continue;
					if (!OpNameStr.empty())
					{
						FName OpNm = Ctrl->GetOpName(OpIdx);
						if (!OpNm.ToString().Equals(UTF8_TO_TCHAR(OpNameStr.c_str()), ESearchCase::IgnoreCase)) continue;
					}
					FoundOpIdx = OpIdx;
					break;
				}
				if (FoundOpIdx < 0)
				{
					Session.Log(TEXT("[FAIL] configure(\"pelvis\") -> no Pelvis Motion op found. Call add(\"op\", {type=\"PelvisMotion\"}) first."));
					return sol::lua_nil;
				}

				UIKRetargetOpControllerBase* BaseCtrl = Ctrl->GetOpController(FoundOpIdx);
				UIKRetargetPelvisMotionController* PelvisCtrl = Cast<UIKRetargetPelvisMotionController>(BaseCtrl);
				if (!PelvisCtrl)
				{
					Session.Log(TEXT("[FAIL] configure(\"pelvis\") -> failed to get Pelvis controller"));
					return sol::lua_nil;
				}

				FScopedTransaction Txn(FText::FromString(TEXT("Configure Pelvis Motion")));
				Retargeter->Modify();

				// Bone references use dedicated setters (they have semantics beyond plain struct assignment).
				// Pull from either the flat top-level form or nested properties={...} — otherwise
				// a user who writes configure("pelvis", {properties={source_pelvis_bone="pelvis", ...}})
				// would silently lose the bone setter (the reflection path's FBoneReference handler
				// would still apply, but the dedicated setter carries additional engine bookkeeping).
				FIKRetargetPelvisMotionOpSettings SettingsCopy = PelvisCtrl->GetSettings();

				sol::table SettingsProps = ExtractSettingsProps(P, Lv,
					{ TEXT("op_name"), TEXT("source_pelvis_bone"), TEXT("target_pelvis_bone") });
				FString ApplyErr;
				TArray<FString> Warnings;
				NeoLuaProperty::ApplyTableToStruct(
					FIKRetargetPelvisMotionOpSettings::StaticStruct(), &SettingsCopy, Retargeter,
					SettingsProps, ApplyErr, &Warnings);
				LogApplyWarnings(Session, TEXT("configure(\"pelvis\")"), Warnings);
				if (!ApplyErr.IsEmpty())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"pelvis\") -> %s"), *ApplyErr));
					return sol::lua_nil;
				}
				PelvisCtrl->SetSettings(SettingsCopy);

				std::string SrcPelvis = GetSelectorString(P, "source_pelvis_bone");
				if (!SrcPelvis.empty()) PelvisCtrl->SetSourcePelvisBone(FName(NeoLuaStr::ToFString(SrcPelvis)));

				std::string TgtPelvis = GetSelectorString(P, "target_pelvis_bone");
				if (!TgtPelvis.empty()) PelvisCtrl->SetTargetPelvisBone(FName(NeoLuaStr::ToFString(TgtPelvis)));

				Retargeter->MarkPackageDirty();
				Session.Log(TEXT("[OK] configure(\"pelvis\")"));
				return sol::make_object(Lv, true);
			}

			// ---- configure("root_motion", {op_name=.., source_root_bone=.., target_root_bone=..,
			//       target_pelvis_bone=.., <struct fields>}) ----
			// Same pattern as configure("pelvis"): bone refs via dedicated setters, everything
			// else via a copied settings struct committed through SetSettings().
			if (FType.Equals(TEXT("root_motion"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"root_motion\") -> table required"));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();

				// Find root motion op
				std::string OpNameStr = P.get_or<std::string>("op_name", "");
				int32 NumOps = Ctrl->GetNumRetargetOps();
				int32 FoundOpIdx = -1;
				for (int32 OpIdx = 0; OpIdx < NumOps; OpIdx++)
				{
					FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(OpIdx);
					if (!Op || !Op->GetType()->IsChildOf(FIKRetargetRootMotionOp::StaticStruct())) continue;
					if (!OpNameStr.empty())
					{
						FName OpNm = Ctrl->GetOpName(OpIdx);
						if (!OpNm.ToString().Equals(UTF8_TO_TCHAR(OpNameStr.c_str()), ESearchCase::IgnoreCase)) continue;
					}
					FoundOpIdx = OpIdx;
					break;
				}
				if (FoundOpIdx < 0)
				{
					Session.Log(TEXT("[FAIL] configure(\"root_motion\") -> no Root Motion op found. Call add(\"op\", {type=\"RootMotion\"}) first."));
					return sol::lua_nil;
				}

				UIKRetargetOpControllerBase* BaseCtrl = Ctrl->GetOpController(FoundOpIdx);
				UIKRetargetRootMotionController* RootCtrl = Cast<UIKRetargetRootMotionController>(BaseCtrl);
				if (!RootCtrl)
				{
					Session.Log(TEXT("[FAIL] configure(\"root_motion\") -> failed to get Root Motion controller"));
					return sol::lua_nil;
				}

				FScopedTransaction Txn(FText::FromString(TEXT("Configure Root Motion")));
				Retargeter->Modify();

				FIKRetargetRootMotionOpSettings SettingsCopy = RootCtrl->GetSettings();

				sol::table SettingsProps = ExtractSettingsProps(P, Lv,
					{ TEXT("op_name"), TEXT("source_root_bone"), TEXT("target_root_bone"), TEXT("target_pelvis_bone") });
				FString ApplyErr;
				TArray<FString> Warnings;
				NeoLuaProperty::ApplyTableToStruct(
					FIKRetargetRootMotionOpSettings::StaticStruct(), &SettingsCopy, Retargeter,
					SettingsProps, ApplyErr, &Warnings);
				LogApplyWarnings(Session, TEXT("configure(\"root_motion\")"), Warnings);
				if (!ApplyErr.IsEmpty())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"root_motion\") -> %s"), *ApplyErr));
					return sol::lua_nil;
				}
				RootCtrl->SetSettings(SettingsCopy);

				// Bone references — dedicated setters only. Pulls from flat top-level or nested
				// properties={...} so either form works symmetrically.
				std::string SrcRoot = GetSelectorString(P, "source_root_bone");
				if (!SrcRoot.empty()) RootCtrl->SetSourceRootBone(FName(NeoLuaStr::ToFString(SrcRoot)));

				std::string TgtRoot = GetSelectorString(P, "target_root_bone");
				if (!TgtRoot.empty()) RootCtrl->SetTargetRootBone(FName(NeoLuaStr::ToFString(TgtRoot)));

				std::string TgtPelvis = GetSelectorString(P, "target_pelvis_bone");
				if (!TgtPelvis.empty()) RootCtrl->SetTargetPelvisBone(FName(NeoLuaStr::ToFString(TgtPelvis)));

				Retargeter->MarkPackageDirty();
				Session.Log(TEXT("[OK] configure(\"root_motion\")"));
				return sol::make_object(Lv, true);
			}

			// ---- configure("op_settings", {name=.., properties={...}}) ----
			// Generic reflection-based op-settings configure. The "properties" sub-table is
			// applied to a copied settings struct and committed through a matching op-specific
			// controller SetSettings() route. Ops without a safe matching controller route fail
			// instead of mutating the live settings pointer.
			if (FType.Equals(TEXT("op_settings"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"op_settings\") -> table required: {name=.., properties={...}}"));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();
				std::string OpNameStr = P.get_or<std::string>("name", "");
				if (OpNameStr.empty())
				{
					Session.Log(TEXT("[FAIL] configure(\"op_settings\") -> 'name' required"));
					return sol::lua_nil;
				}
				sol::optional<sol::table> PropsOpt = P.get<sol::optional<sol::table>>("properties");
				if (!PropsOpt.has_value())
				{
					Session.Log(TEXT("[FAIL] configure(\"op_settings\") -> 'properties' sub-table required"));
					return sol::lua_nil;
				}

				FString FOpName = NeoLuaStr::ToFString(OpNameStr);
				int32 Idx = FindOpIndexByName(Ctrl, FOpName);
				if (Idx < 0)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"op_settings\", \"%s\") -> op not found. Call list(\"ops\") for valid names."), *FOpName));
					return sol::lua_nil;
				}

				FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(Idx);
				if (!Op)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"op_settings\", \"%s\") -> op base pointer is null"), *FOpName));
					return sol::lua_nil;
				}
				const UScriptStruct* SettingsType = Op->GetSettingsType();
				if (!SettingsType)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"op_settings\", \"%s\") -> op has no settings struct (type=%s)"),
						*FOpName, *GetOpTypeDisplayName(Op)));
					return sol::lua_nil;
				}

				FScopedTransaction Txn(FText::FromString(TEXT("Configure Op Settings")));
				Retargeter->Modify();

				FString ApplyErr;
				TArray<FString> Warnings;
				bool bApplied = false;
				UIKRetargetOpControllerBase* BaseCtrl = Ctrl->GetOpController(Idx);
				const bool bRouted = ApplyGenericOpSettingsThroughController(
					BaseCtrl, SettingsType, Retargeter, PropsOpt.value(), ApplyErr, Warnings, bApplied);
				LogApplyWarnings(Session, FString::Printf(TEXT("configure(\"op_settings\", \"%s\")"), *FOpName), Warnings);
				if (!bRouted)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"op_settings\", \"%s\") -> no safe controller SetSettings route for op type %s"),
						*FOpName, *GetOpTypeDisplayName(Op)));
					return sol::lua_nil;
				}
				if (!ApplyErr.IsEmpty())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"op_settings\", \"%s\") -> %s"), *FOpName, *ApplyErr));
					return sol::lua_nil;
				}
				Retargeter->MarkPackageDirty();
				// bApplied is false when every key in properties was unknown/non-editable OR the table
				// was empty. Surface that distinctly so scripts can tell the apply was a no-op.
				if (!bApplied)
				{
					Session.Log(FString::Printf(TEXT("[OK] configure(\"op_settings\", \"%s\") -> %s: 0 properties applied (table empty or all keys unknown)"),
						*FOpName, *GetOpTypeDisplayName(Op)));
				}
				else
				{
					Session.Log(FString::Printf(TEXT("[OK] configure(\"op_settings\", \"%s\") -> %s"), *FOpName, *GetOpTypeDisplayName(Op)));
				}
				return sol::make_object(Lv, true);
			}
#endif // ENGINE_MINOR_VERSION >= 7

			Session.Log(FString::Printf(TEXT("[FAIL] configure(\"%s\") -> unknown type. Valid: source_ikrig, target_ikrig, preview_mesh, op, op_settings, chain_mapping, fk_chain, ik_chain, pelvis, root_motion. (CopyBasePose/PinBone/FilterBone bone APIs use dedicated verbs — see help.)"), *FType));
			return sol::lua_nil;
		});

		// ====================================================================
		// auto_map(mode, force_remap?, op_name?)
		// ====================================================================
		AssetObj.set_function("auto_map", [Ctrl, &Session](sol::table /*self*/,
			const std::string& Mode, sol::optional<bool> ForceRemap, sol::optional<std::string> OpName, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			EAutoMapChainType MapType;
			if (!ParseAutoMapType(Mode, MapType))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] auto_map(\"%s\") -> unknown mode. Valid: exact, fuzzy, clear"), UTF8_TO_TCHAR(Mode.c_str())));
				return sol::lua_nil;
			}
			bool bForce = ForceRemap.value_or(true);
			FName FOpName = NAME_None;
			if (OpName.has_value() && !OpName.value().empty())
			{
				FOpName = FName(NeoLuaStr::ToFStringOpt(OpName));
			}

			FScopedTransaction Txn(FText::FromString(TEXT("Auto Map Chains")));
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			Ctrl->AutoMapChains(MapType, bForce, FOpName);
#else
			Ctrl->AutoMapChains(MapType, bForce);
#endif
			Session.Log(FString::Printf(TEXT("[OK] auto_map(\"%s\", force=%s)"), UTF8_TO_TCHAR(Mode.c_str()), bForce ? TEXT("true") : TEXT("false")));
			return sol::make_object(Lv, true);
		});

		// ====================================================================
		// auto_align({for=.., bones=..?})
		// ====================================================================
		AssetObj.set_function("auto_align", [Ctrl, &Session](sol::table /*self*/,
			sol::optional<sol::table> Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string ForStr = "target";
			std::string MethodStr = "chain_to_chain";
			if (Params.has_value())
			{
				ForStr = Params.value().get_or<std::string>("for", "target");
				MethodStr = Params.value().get_or<std::string>("method", "chain_to_chain");
			}

			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] auto_align() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}

			// Parse alignment method
			ERetargetAutoAlignMethod Method = ERetargetAutoAlignMethod::ChainToChain;
			FString FMethod = NeoLuaStr::ToFString(MethodStr);
			if (FMethod.Equals(TEXT("mesh_to_mesh"), ESearchCase::IgnoreCase) || FMethod.Equals(TEXT("MeshToMesh"), ESearchCase::IgnoreCase))
				Method = ERetargetAutoAlignMethod::MeshToMesh;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			else if (FMethod.Equals(TEXT("local_rotation"), ESearchCase::IgnoreCase) || FMethod.Equals(TEXT("LocalRotationAxes"), ESearchCase::IgnoreCase))
				Method = ERetargetAutoAlignMethod::LocalRotationAxes;
			else if (FMethod.Equals(TEXT("global_rotation"), ESearchCase::IgnoreCase) || FMethod.Equals(TEXT("GlobalRotationAxes"), ESearchCase::IgnoreCase))
				Method = ERetargetAutoAlignMethod::GlobalRotationAxes;
#endif

			FScopedTransaction Txn(FText::FromString(TEXT("Auto Align Bones")));

			// Check if specific bones are requested
			if (Params.has_value())
			{
				sol::optional<sol::table> BonesTbl = Params.value().get<sol::optional<sol::table>>("bones");
				if (BonesTbl.has_value())
				{
					TArray<FName> BoneNames;
					sol::table BT = BonesTbl.value();
					for (auto& KV : BT)
					{
						if (KV.second.is<std::string>())
						{
							BoneNames.Add(FName(NeoLuaStr::ToFString(KV.second.as<std::string>())));
						}
					}
					Ctrl->AutoAlignBones(BoneNames, Method, Side);
					Session.Log(FString::Printf(TEXT("[OK] auto_align(%d bones, for=%s, method=%s)"), BoneNames.Num(), UTF8_TO_TCHAR(ForStr.c_str()), UTF8_TO_TCHAR(MethodStr.c_str())));
					return sol::make_object(Lv, true);
				}
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			Ctrl->AutoAlignAllBones(Side, Method);
#else
			Ctrl->AutoAlignAllBones(Side);
#endif
			Session.Log(FString::Printf(TEXT("[OK] auto_align(all, for=%s, method=%s)"), UTF8_TO_TCHAR(ForStr.c_str()), UTF8_TO_TCHAR(MethodStr.c_str())));
			return sol::make_object(Lv, true);
		});

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		// ====================================================================
		// move_op(name, to_index) — 5.6+
		// ====================================================================
		AssetObj.set_function("move_op", [Ctrl, &Session](sol::table /*self*/,
			const std::string& OpName, int TargetIndex, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			FString FOpName = NeoLuaStr::ToFString(OpName);
			int32 Idx = FindOpIndexByName(Ctrl, FOpName);
			if (Idx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] move_op(\"%s\", %d) -> op not found. Call list(\"ops\") for valid names; TargetIndex must be 0..num_ops-1."),
					*FOpName, TargetIndex));
				return sol::lua_nil;
			}
			FScopedTransaction Txn(FText::FromString(TEXT("Move Retarget Op")));
			bool bOk = Ctrl->MoveRetargetOpInStack(Idx, TargetIndex);
			Session.Log(FString::Printf(TEXT("[%s] move_op(\"%s\", %d)"), bOk ? TEXT("OK") : TEXT("FAIL"), *FOpName, TargetIndex));
			return bOk ? sol::make_object(Lv, true) : sol::lua_nil;
		});

		// ====================================================================
		// set_parent_op("ChildOp", "ParentOp") — 5.6+
		// ====================================================================
		AssetObj.set_function("set_parent_op", [Ctrl, &Session](sol::table /*self*/,
			const std::string& ChildOp, const std::string& ParentOp, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			FString ChildName = NeoLuaStr::ToFString(ChildOp);
			FString ParentName = NeoLuaStr::ToFString(ParentOp);
			FScopedTransaction Txn(FText::FromString(TEXT("Set Parent Op")));
			bool bSuccess = Ctrl->SetParentOpByName(FName(ChildName), FName(ParentName));
			if (bSuccess)
				Session.Log(FString::Printf(TEXT("[OK] set_parent_op(\"%s\" -> parent \"%s\")"), *ChildName, *ParentName));
			else
				Session.Log(FString::Printf(TEXT("[FAIL] set_parent_op(\"%s\" -> \"%s\") -> child or parent not found, or the parent relationship is not allowed for this op type. Call list(\"ops\") to verify names."),
					*ChildName, *ParentName));
			return sol::make_object(Lv, bSuccess);
		});

		// ====================================================================
		// get_parent_op("OpName") — 5.6+
		// ====================================================================
		AssetObj.set_function("get_parent_op", [Ctrl, &Session](sol::table /*self*/,
			const std::string& OpName, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			FString FOpName = NeoLuaStr::ToFString(OpName);
			FName ParentName = Ctrl->GetParentOpByName(FName(FOpName));
			if (ParentName != NAME_None)
				return sol::make_object(Lv, std::string(TCHAR_TO_UTF8(*ParentName.ToString())));
			return sol::lua_nil;
		});
#endif // ENGINE_MINOR_VERSION >= 6 (ops management)

		// ====================================================================
		// duplicate_pose({name=.., new_name=.., for=..})
		// ====================================================================
		AssetObj.set_function("duplicate_pose", [Ctrl, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string SrcName = Params.get_or<std::string>("name", "");
			std::string NewName = Params.get_or<std::string>("new_name", "");
			std::string ForStr = Params.get_or<std::string>("for", "target");
			if (SrcName.empty() || NewName.empty())
			{
				Session.Log(TEXT("[FAIL] duplicate_pose() -> 'name' and 'new_name' required"));
				return sol::lua_nil;
			}
			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] duplicate_pose() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}
			FScopedTransaction Txn(FText::FromString(TEXT("Duplicate Retarget Pose")));
			FName Result = Ctrl->DuplicateRetargetPose(
				FName(NeoLuaStr::ToFString(SrcName)),
				FName(NeoLuaStr::ToFString(NewName)),
				Side);
			if (Result.IsNone())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] duplicate_pose(\"%s\", for=%s) -> source pose not found. Call list(\"poses\") to see valid names."),
					UTF8_TO_TCHAR(SrcName.c_str()), UTF8_TO_TCHAR(ForStr.c_str())));
				return sol::lua_nil;
			}
			Session.Log(FString::Printf(TEXT("[OK] duplicate_pose(\"%s\" -> \"%s\")"), UTF8_TO_TCHAR(SrcName.c_str()), *Result.ToString()));
			return sol::make_object(Lv, std::string(TCHAR_TO_UTF8(*Result.ToString())));
		});

		// ====================================================================
		// rename_pose({old_name=.., new_name=.., for=..})
		// ====================================================================
		AssetObj.set_function("rename_pose", [Ctrl, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string OldName = Params.get_or<std::string>("old_name", "");
			std::string NewName = Params.get_or<std::string>("new_name", "");
			std::string ForStr = Params.get_or<std::string>("for", "target");
			if (OldName.empty() || NewName.empty())
			{
				Session.Log(TEXT("[FAIL] rename_pose() -> 'old_name' and 'new_name' required"));
				return sol::lua_nil;
			}
			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] rename_pose() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}
			FScopedTransaction Txn(FText::FromString(TEXT("Rename Retarget Pose")));
			bool bOk = Ctrl->RenameRetargetPose(
				FName(NeoLuaStr::ToFString(OldName)),
				FName(NeoLuaStr::ToFString(NewName)),
				Side);
			Session.Log(FString::Printf(TEXT("[%s] rename_pose(\"%s\" -> \"%s\")"),
				bOk ? TEXT("OK") : TEXT("FAIL"), UTF8_TO_TCHAR(OldName.c_str()), UTF8_TO_TCHAR(NewName.c_str())));
			return bOk ? sol::make_object(Lv, true) : sol::lua_nil;
		});

		// ====================================================================
		// set_current_pose({name=.., for=..})
		// ====================================================================
		AssetObj.set_function("set_current_pose", [Ctrl, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string PoseName = Params.get_or<std::string>("name", "");
			std::string ForStr = Params.get_or<std::string>("for", "target");
			if (PoseName.empty())
			{
				Session.Log(TEXT("[FAIL] set_current_pose() -> 'name' required"));
				return sol::lua_nil;
			}
			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] set_current_pose() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}
			FScopedTransaction Txn(FText::FromString(TEXT("Set Current Retarget Pose")));
			bool bOk = Ctrl->SetCurrentRetargetPose(FName(NeoLuaStr::ToFString(PoseName)), Side);
			Session.Log(FString::Printf(TEXT("[%s] set_current_pose(\"%s\", for=%s)"),
				bOk ? TEXT("OK") : TEXT("FAIL"), UTF8_TO_TCHAR(PoseName.c_str()), UTF8_TO_TCHAR(ForStr.c_str())));
			return bOk ? sol::make_object(Lv, true) : sol::lua_nil;
		});

		// ====================================================================
		// edit_pose_bone({bone=.., rotation={p,y,r}, for=..})
		// ====================================================================
		AssetObj.set_function("edit_pose_bone", [Ctrl, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string BoneName = Params.get_or<std::string>("bone", "");
			std::string ForStr = Params.get_or<std::string>("for", "target");
			if (BoneName.empty())
			{
				Session.Log(TEXT("[FAIL] edit_pose_bone() -> 'bone' required"));
				return sol::lua_nil;
			}
			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] edit_pose_bone() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}

			sol::optional<sol::table> RotTbl = Params.get<sol::optional<sol::table>>("rotation");
			if (!RotTbl.has_value())
			{
				Session.Log(TEXT("[FAIL] edit_pose_bone() -> 'rotation' table {p=.., y=.., r=..} required"));
				return sol::lua_nil;
			}
			sol::table RT = RotTbl.value();
			double Pitch = RT.get<sol::optional<double>>("p").value_or(RT.get<sol::optional<double>>("pitch").value_or(0.0));
			double Yaw = RT.get<sol::optional<double>>("y").value_or(RT.get<sol::optional<double>>("yaw").value_or(0.0));
			double Roll = RT.get<sol::optional<double>>("r").value_or(RT.get<sol::optional<double>>("roll").value_or(0.0));

			// Convert Euler (degrees) to Quaternion
			FRotator Rot(Pitch, Yaw, Roll);
			FQuat Quat = Rot.Quaternion();

			// Engine SetRotationOffsetForRetargetPoseBone dereferences GetAsset()->GetIKRig(Side)
			// at IKRetargeterController.cpp:1312-1313 without a null check, so we must guard here.
			if (!Ctrl->GetIKRig(Side))
			{
				const TCHAR* SideStr = Side == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target");
				Session.Log(FString::Printf(TEXT("[FAIL] edit_pose_bone(\"%s\") -> %s IK Rig is not assigned. Call configure(\"%s_ikrig\", \"/Game/...\") first."),
					UTF8_TO_TCHAR(BoneName.c_str()), SideStr, SideStr));
				return sol::lua_nil;
			}

			FScopedTransaction Txn(FText::FromString(TEXT("Edit Pose Bone")));
			Ctrl->SetRotationOffsetForRetargetPoseBone(FName(NeoLuaStr::ToFString(BoneName)), Quat, Side);
			Session.Log(FString::Printf(TEXT("[OK] edit_pose_bone(\"%s\", p=%.1f y=%.1f r=%.1f, for=%s)"),
				UTF8_TO_TCHAR(BoneName.c_str()), Pitch, Yaw, Roll, UTF8_TO_TCHAR(ForStr.c_str())));
			return sol::make_object(Lv, true);
		});

		// ====================================================================
		// set_root_offset({offset={x,y,z}, for=..})
		//
		// True set-semantics: writes FIKRetargetPose::RootTranslationOffset directly via
		// SetRootTranslationDelta (IKRetargeter.cpp:791). The controller's
		// SetRootOffsetInRetargetPose uses AddToRootTranslationDelta (accumulating), which
		// contradicts the "set" name documented in the Lua API.
		// ====================================================================
		AssetObj.set_function("set_root_offset", [Ctrl, Retargeter, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string ForStr = Params.get_or<std::string>("for", "target");
			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] set_root_offset() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}

			sol::optional<sol::table> OffTbl = Params.get<sol::optional<sol::table>>("offset");
			if (!OffTbl.has_value())
			{
				Session.Log(TEXT("[FAIL] set_root_offset() -> 'offset' table {x=.., y=.., z=..} required"));
				return sol::lua_nil;
			}
			sol::table OT = OffTbl.value();
			FVector Offset;
			Offset.X = OT.get<sol::optional<double>>("x").value_or(0.0);
			Offset.Y = OT.get<sol::optional<double>>("y").value_or(0.0);
			Offset.Z = OT.get<sol::optional<double>>("z").value_or(0.0);

			FScopedTransaction Txn(FText::FromString(TEXT("Set Root Offset")));
			Retargeter->Modify();
			Ctrl->GetCurrentRetargetPose(Side).SetRootTranslationDelta(Offset);
			Retargeter->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] set_root_offset(%.1f, %.1f, %.1f, for=%s)"),
				Offset.X, Offset.Y, Offset.Z, UTF8_TO_TCHAR(ForStr.c_str())));
			return sol::make_object(Lv, true);
		});

		// ====================================================================
		// reset_pose({pose_name=.., bones={..}, for=..})
		// ====================================================================
		AssetObj.set_function("reset_pose", [Ctrl, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string PoseName = Params.get_or<std::string>("pose_name", "Default");
			std::string ForStr = Params.get_or<std::string>("for", "target");
			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] reset_pose() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}

			TArray<FName> BonesToReset;
			sol::optional<sol::table> BonesTbl = Params.get<sol::optional<sol::table>>("bones");
			if (BonesTbl.has_value())
			{
				sol::table BT = BonesTbl.value();
				for (auto& KV : BT)
				{
					if (KV.second.is<std::string>())
					{
						BonesToReset.Add(FName(NeoLuaStr::ToFString(KV.second.as<std::string>())));
					}
				}
			}

			FScopedTransaction Txn(FText::FromString(TEXT("Reset Retarget Pose")));
			Ctrl->ResetRetargetPose(FName(NeoLuaStr::ToFString(PoseName)), BonesToReset, Side);
			Session.Log(FString::Printf(TEXT("[OK] reset_pose(\"%s\", %d bones, for=%s)"),
				UTF8_TO_TCHAR(PoseName.c_str()), BonesToReset.Num(), UTF8_TO_TCHAR(ForStr.c_str())));
			return sol::make_object(Lv, true);
		});

		// ====================================================================
		// snap_to_ground({bone=.., for=..})
		// ====================================================================
		AssetObj.set_function("snap_to_ground", [Ctrl, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string BoneName = Params.get_or<std::string>("bone", "");
			std::string ForStr = Params.get_or<std::string>("for", "target");
			if (BoneName.empty())
			{
				Session.Log(TEXT("[FAIL] snap_to_ground() -> 'bone' required"));
				return sol::lua_nil;
			}
			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] snap_to_ground() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}

			FScopedTransaction Txn(FText::FromString(TEXT("Snap Bone to Ground")));
			Ctrl->SnapBoneToGround(FName(NeoLuaStr::ToFString(BoneName)), Side);
			Session.Log(FString::Printf(TEXT("[OK] snap_to_ground(\"%s\", for=%s)"),
				UTF8_TO_TCHAR(BoneName.c_str()), UTF8_TO_TCHAR(ForStr.c_str())));
			return sol::make_object(Lv, true);
		});

		// ====================================================================
		// get_rotation_offset({bone=.., for=..})
		// ====================================================================
		AssetObj.set_function("get_rotation_offset", [Ctrl, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string BoneName = Params.get_or<std::string>("bone", "");
			std::string ForStr = Params.get_or<std::string>("for", "target");
			if (BoneName.empty())
			{
				Session.Log(TEXT("[FAIL] get_rotation_offset() -> 'bone' required"));
				return sol::lua_nil;
			}
			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] get_rotation_offset() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}

			FQuat Quat = Ctrl->GetRotationOffsetForRetargetPoseBone(FName(NeoLuaStr::ToFString(BoneName)), Side);
			FRotator Rot = Quat.Rotator();
			sol::table Result = Lv.create_table();
			Result["p"] = Rot.Pitch;
			Result["y"] = Rot.Yaw;
			Result["r"] = Rot.Roll;
			Session.Log(FString::Printf(TEXT("[OK] get_rotation_offset(\"%s\", for=%s) -> p=%.2f y=%.2f r=%.2f"),
				UTF8_TO_TCHAR(BoneName.c_str()), UTF8_TO_TCHAR(ForStr.c_str()), Rot.Pitch, Rot.Yaw, Rot.Roll));
			return Result;
		});

		// ====================================================================
		// get_root_offset({for=..})
		// ====================================================================
		AssetObj.set_function("get_root_offset", [Ctrl, &Session](sol::table /*self*/,
			sol::optional<sol::table> Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string ForStr = "target";
			if (Params.has_value()) ForStr = Params.value().get_or<std::string>("for", "target");

			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] get_root_offset() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}

			FVector Offset = Ctrl->GetRootOffsetInRetargetPose(Side);
			sol::table Result = Lv.create_table();
			Result["x"] = Offset.X;
			Result["y"] = Offset.Y;
			Result["z"] = Offset.Z;
			Session.Log(FString::Printf(TEXT("[OK] get_root_offset(for=%s) -> %.2f, %.2f, %.2f"),
				UTF8_TO_TCHAR(ForStr.c_str()), Offset.X, Offset.Y, Offset.Z));
			return Result;
		});

		// ====================================================================
		// get_source_chain({target=.., op_name=..})
		// ====================================================================
		AssetObj.set_function("get_source_chain", [Ctrl, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string TargetChain = Params.get_or<std::string>("target", "");
			if (TargetChain.empty())
			{
				Session.Log(TEXT("[FAIL] get_source_chain() -> 'target' chain name required"));
				return sol::lua_nil;
			}
			std::string OpNameStr = Params.get_or<std::string>("op_name", "");
			FName OpName = OpNameStr.empty() ? NAME_None : FName(NeoLuaStr::ToFString(OpNameStr));

	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			FName SourceChain = Ctrl->GetSourceChain(FName(NeoLuaStr::ToFString(TargetChain)), OpName);
#else
			FName SourceChain = Ctrl->GetSourceChain(FName(NeoLuaStr::ToFString(TargetChain)));
#endif
			if (SourceChain.IsNone())
			{
				Session.Log(FString::Printf(TEXT("[OK] get_source_chain(\"%s\") -> None (unmapped)"), UTF8_TO_TCHAR(TargetChain.c_str())));
				return sol::lua_nil;
			}
			Session.Log(FString::Printf(TEXT("[OK] get_source_chain(\"%s\") -> \"%s\""),
				UTF8_TO_TCHAR(TargetChain.c_str()), *SourceChain.ToString()));
			return sol::make_object(Lv, std::string(TCHAR_TO_UTF8(*SourceChain.ToString())));
		});

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		// ====================================================================
		// run_op_setup(name_or_index) — 5.6+
		// ====================================================================
		AssetObj.set_function("run_op_setup", [Ctrl, &Session](sol::table /*self*/,
			sol::object IdArg, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			int32 Idx = -1;
			if (IdArg.is<int>())
			{
				Idx = IdArg.as<int>();
			}
			else if (IdArg.is<std::string>())
			{
				FString OpName = NeoLuaStr::ToFString(IdArg.as<std::string>());
				Idx = FindOpIndexByName(Ctrl, OpName);
			}
			if (Idx < 0 || Idx >= Ctrl->GetNumRetargetOps())
			{
				FString InputRepr = IdArg.is<int>()
					? FString::FromInt(IdArg.as<int>())
					: (IdArg.is<std::string>() ? FString(TEXT("\"")) + NeoLuaStr::ToFString(IdArg.as<std::string>()) + TEXT("\"") : TEXT("<unknown>"));
				Session.Log(FString::Printf(TEXT("[FAIL] run_op_setup(%s) -> op not found or index out of range. Call list(\"ops\") for valid names; index must be 0..%d."),
					*InputRepr, Ctrl->GetNumRetargetOps() - 1));
				return sol::lua_nil;
			}
			FScopedTransaction Txn(FText::FromString(TEXT("Run Op Initial Setup")));
			Ctrl->RunOpInitialSetup(Idx);
			FName OpName = Ctrl->GetOpName(Idx);
			Session.Log(FString::Printf(TEXT("[OK] run_op_setup(\"%s\")"), *OpName.ToString()));
			return sol::make_object(Lv, true);
		});

		// ====================================================================
		// reset_chain_settings({chain=.., op_name=..})  — single-op reset
		// reset_chain_settings({chain=.., all_ops=true}) — UIKRetargeterController::ResetChainSettingsInAllOps
		// ====================================================================
		AssetObj.set_function("reset_chain_settings", [Ctrl, Retargeter, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string ChainName = Params.get_or<std::string>("chain", "");
			if (ChainName.empty())
			{
				Session.Log(TEXT("[FAIL] reset_chain_settings() -> 'chain' required"));
				return sol::lua_nil;
			}
			FName FChainName = FName(NeoLuaStr::ToFString(ChainName));

			bool bAllOps = Params.get_or("all_ops", false);
			if (bAllOps)
			{
				FScopedTransaction Txn(FText::FromString(TEXT("Reset Chain Settings In All Ops")));
				Retargeter->Modify();
				Ctrl->ResetChainSettingsInAllOps(FChainName);
				Retargeter->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] reset_chain_settings(\"%s\", all_ops=true)"), *FChainName.ToString()));
				return sol::make_object(Lv, true);
			}

			std::string OpNameStr = Params.get_or<std::string>("op_name", "");
			if (OpNameStr.empty())
			{
				Session.Log(TEXT("[FAIL] reset_chain_settings() -> 'op_name' required (or pass all_ops=true). Call list(\"ops\") for valid names."));
				return sol::lua_nil;
			}
			FScopedTransaction Txn(FText::FromString(TEXT("Reset Chain Settings")));
			Retargeter->Modify();
			Ctrl->ResetChainSettingsToDefault(FChainName, FName(NeoLuaStr::ToFString(OpNameStr)));
			Retargeter->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] reset_chain_settings(\"%s\", op=\"%s\")"),
				UTF8_TO_TCHAR(ChainName.c_str()), UTF8_TO_TCHAR(OpNameStr.c_str())));
			return sol::make_object(Lv, true);
		});

		// ====================================================================
		// get_target_ikrig_for_op(op_name) — 5.6+
		// ====================================================================
		AssetObj.set_function("get_target_ikrig_for_op", [Ctrl, &Session](sol::table /*self*/,
			const std::string& OpName, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			FName FOpName = FName(NeoLuaStr::ToFString(OpName));
			const UIKRigDefinition* IKRig = Ctrl->GetTargetIKRigForOp(FOpName);
			if (!IKRig)
			{
				Session.Log(FString::Printf(TEXT("[OK] get_target_ikrig_for_op(\"%s\") -> none"), UTF8_TO_TCHAR(OpName.c_str())));
				return sol::lua_nil;
			}
			std::string Path = TCHAR_TO_UTF8(*IKRig->GetPathName());
			Session.Log(FString::Printf(TEXT("[OK] get_target_ikrig_for_op(\"%s\") -> \"%s\""),
				UTF8_TO_TCHAR(OpName.c_str()), *IKRig->GetPathName()));
			return sol::make_object(Lv, Path);
		});
#endif // ENGINE_MINOR_VERSION >= 6

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		// ====================================================================
		// get_pelvis_bones({op_name=..}) — read pelvis bone assignments
		// ====================================================================
		AssetObj.set_function("get_pelvis_bones", [Ctrl, &Session](sol::table /*self*/,
			sol::optional<sol::table> Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			// Find pelvis op
			std::string OpNameStr = "";
			if (Params.has_value()) OpNameStr = Params.value().get_or<std::string>("op_name", "");

			int32 NumOps = Ctrl->GetNumRetargetOps();
			for (int32 OpIdx = 0; OpIdx < NumOps; OpIdx++)
			{
				FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(OpIdx);
				if (!IsRetargetOpTypeNamed(Op, TEXT("IKRetargetPelvisMotionOp"))) continue;
				if (!OpNameStr.empty())
				{
					FName OpNm = Ctrl->GetOpName(OpIdx);
					if (!OpNm.ToString().Equals(UTF8_TO_TCHAR(OpNameStr.c_str()), ESearchCase::IgnoreCase)) continue;
				}

				UIKRetargetOpControllerBase* BaseCtrl = Ctrl->GetOpController(OpIdx);
				UIKRetargetPelvisMotionController* PelvisCtrl = Cast<UIKRetargetPelvisMotionController>(BaseCtrl);
				if (!PelvisCtrl) continue;

				sol::table Result = Lv.create_table();
				FName SrcBone = PelvisCtrl->GetSourcePelvisBone();
				FName TgtBone = PelvisCtrl->GetTargetPelvisBone();
				Result["source_pelvis_bone"] = SrcBone.IsNone() ? "" : TCHAR_TO_UTF8(*SrcBone.ToString());
				Result["target_pelvis_bone"] = TgtBone.IsNone() ? "" : TCHAR_TO_UTF8(*TgtBone.ToString());
				Session.Log(FString::Printf(TEXT("[OK] get_pelvis_bones() -> src=\"%s\", tgt=\"%s\""),
					*SrcBone.ToString(), *TgtBone.ToString()));
				return Result;
			}
			Session.Log(TEXT("[FAIL] get_pelvis_bones() -> no Pelvis Motion op found"));
			return sol::lua_nil;
		});

		// ====================================================================
		// get_root_bones({op_name=..}) — read root motion bone assignments
		// ====================================================================
		AssetObj.set_function("get_root_bones", [Ctrl, &Session](sol::table /*self*/,
			sol::optional<sol::table> Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string OpNameStr = "";
			if (Params.has_value()) OpNameStr = Params.value().get_or<std::string>("op_name", "");

			int32 NumOps = Ctrl->GetNumRetargetOps();
			for (int32 OpIdx = 0; OpIdx < NumOps; OpIdx++)
			{
				FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(OpIdx);
				if (!IsRetargetOpTypeNamed(Op, TEXT("IKRetargetRootMotionOp"))) continue;
				if (!OpNameStr.empty())
				{
					FName OpNm = Ctrl->GetOpName(OpIdx);
					if (!OpNm.ToString().Equals(UTF8_TO_TCHAR(OpNameStr.c_str()), ESearchCase::IgnoreCase)) continue;
				}

				UIKRetargetOpControllerBase* BaseCtrl = Ctrl->GetOpController(OpIdx);
				UIKRetargetRootMotionController* RootCtrl = Cast<UIKRetargetRootMotionController>(BaseCtrl);
				if (!RootCtrl) continue;

				sol::table Result = Lv.create_table();
				FName SrcRoot = RootCtrl->GetSourceRootBone();
				FName TgtRoot = RootCtrl->GetTargetRootBone();
				FName TgtPelvis = RootCtrl->GetTargetPelvisBone();
				Result["source_root_bone"] = SrcRoot.IsNone() ? "" : TCHAR_TO_UTF8(*SrcRoot.ToString());
				Result["target_root_bone"] = TgtRoot.IsNone() ? "" : TCHAR_TO_UTF8(*TgtRoot.ToString());
				Result["target_pelvis_bone"] = TgtPelvis.IsNone() ? "" : TCHAR_TO_UTF8(*TgtPelvis.ToString());
				Session.Log(FString::Printf(TEXT("[OK] get_root_bones() -> src_root=\"%s\", tgt_root=\"%s\", tgt_pelvis=\"%s\""),
					*SrcRoot.ToString(), *TgtRoot.ToString(), *TgtPelvis.ToString()));
				return Result;
			}
			Session.Log(TEXT("[FAIL] get_root_bones() -> no Root Motion op found"));
			return sol::lua_nil;
		});

		// ====================================================================
		// get_all_target_ikrigs() — UIKRetargeterController::GetAllTargetIKRigs
		// Returns an array of path strings, one per distinct target IK Rig referenced by any op.
		// ====================================================================
		AssetObj.set_function("get_all_target_ikrigs", [Ctrl, &Session](sol::table /*self*/, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			TArray<UIKRigDefinition*> Rigs = Ctrl->GetAllTargetIKRigs();
			sol::table Result = Lv.create_table();
			int32 Emitted = 0;
			for (UIKRigDefinition* Rig : Rigs)
			{
				if (!Rig) continue;
				Result[++Emitted] = TCHAR_TO_UTF8(*Rig->GetPathName());
			}
			Session.Log(FString::Printf(TEXT("[OK] get_all_target_ikrigs() -> %d rig(s)"), Emitted));
			return Result;
		});

		// ====================================================================
		// PinBone op verbs — UIKRetargetPinBoneController bone-pair API
		// (generic settings struct reachable via configure("op_settings", ...))
		// ====================================================================
		AssetObj.set_function("set_pin_pair", [Ctrl, Retargeter, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string OpNameStr = Params.get_or<std::string>("op", "");
			std::string FromStr = Params.get_or<std::string>("from", "");
			std::string ToStr = Params.get_or<std::string>("to", "");
			if (OpNameStr.empty() || FromStr.empty() || ToStr.empty())
			{
				Session.Log(TEXT("[FAIL] set_pin_pair() -> 'op', 'from', 'to' required"));
				return sol::lua_nil;
			}
			int32 Idx = FindOpIndexByName(Ctrl, NeoLuaStr::ToFString(OpNameStr));
			if (Idx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_pin_pair(op=\"%s\") -> op not found. Call list(\"ops\") for valid names."), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			UIKRetargetPinBoneController* PinCtrl = Cast<UIKRetargetPinBoneController>(Ctrl->GetOpController(Idx));
			if (!PinCtrl)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_pin_pair(op=\"%s\") -> op is not a PinBone op"), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			FScopedTransaction Txn(FText::FromString(TEXT("Set Pin Bone Pair")));
			Retargeter->Modify();
			PinCtrl->SetBonePair(FName(NeoLuaStr::ToFString(FromStr)), FName(NeoLuaStr::ToFString(ToStr)));
			Retargeter->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] set_pin_pair(op=\"%s\", \"%s\" -> \"%s\")"),
				UTF8_TO_TCHAR(OpNameStr.c_str()), UTF8_TO_TCHAR(FromStr.c_str()), UTF8_TO_TCHAR(ToStr.c_str())));
			return sol::make_object(Lv, true);
		});

		AssetObj.set_function("clear_pin_pairs", [Ctrl, Retargeter, &Session](sol::table /*self*/,
			const std::string& OpNameStr, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			int32 Idx = FindOpIndexByName(Ctrl, NeoLuaStr::ToFString(OpNameStr));
			if (Idx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] clear_pin_pairs(\"%s\") -> op not found. Call list(\"ops\") for valid names."), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			UIKRetargetPinBoneController* PinCtrl = Cast<UIKRetargetPinBoneController>(Ctrl->GetOpController(Idx));
			if (!PinCtrl)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] clear_pin_pairs(\"%s\") -> op is not a PinBone op"), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			FScopedTransaction Txn(FText::FromString(TEXT("Clear Pin Bone Pairs")));
			Retargeter->Modify();
			PinCtrl->ClearAllBonePairs();
			Retargeter->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] clear_pin_pairs(\"%s\")"), UTF8_TO_TCHAR(OpNameStr.c_str())));
			return sol::make_object(Lv, true);
		});

		AssetObj.set_function("get_pin_pairs", [Ctrl, &Session](sol::table /*self*/,
			const std::string& OpNameStr, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			int32 Idx = FindOpIndexByName(Ctrl, NeoLuaStr::ToFString(OpNameStr));
			if (Idx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] get_pin_pairs(\"%s\") -> op not found. Call list(\"ops\") for valid names."), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			UIKRetargetPinBoneController* PinCtrl = Cast<UIKRetargetPinBoneController>(Ctrl->GetOpController(Idx));
			if (!PinCtrl)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] get_pin_pairs(\"%s\") -> op is not a PinBone op"), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			TMap<FName, FName> Pairs = PinCtrl->GetAllBonePairs();
			sol::table Result = Lv.create_table();
			int32 PairIdx = 0;
			for (auto& KV : Pairs)
			{
				sol::table Entry = Lv.create_table();
				Entry["to"] = TCHAR_TO_UTF8(*KV.Key.ToString());
				Entry["from"] = TCHAR_TO_UTF8(*KV.Value.ToString());
				Result[++PairIdx] = Entry;
			}
			Session.Log(FString::Printf(TEXT("[OK] get_pin_pairs(\"%s\") -> %d pair(s)"), UTF8_TO_TCHAR(OpNameStr.c_str()), PairIdx));
			return Result;
		});

		// ====================================================================
		// FilterBone op verbs — UIKRetargetFilterBoneController bone-filter API
		// ====================================================================
		AssetObj.set_function("add_filter_bone", [Ctrl, Retargeter, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string OpNameStr = Params.get_or<std::string>("op", "");
			std::string BoneStr = Params.get_or<std::string>("bone", "");
			if (OpNameStr.empty() || BoneStr.empty())
			{
				Session.Log(TEXT("[FAIL] add_filter_bone() -> 'op' and 'bone' required"));
				return sol::lua_nil;
			}
			int32 Idx = FindOpIndexByName(Ctrl, NeoLuaStr::ToFString(OpNameStr));
			if (Idx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add_filter_bone(op=\"%s\") -> op not found. Call list(\"ops\") for valid names."), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			UIKRetargetFilterBoneController* FilterCtrl = Cast<UIKRetargetFilterBoneController>(Ctrl->GetOpController(Idx));
			if (!FilterCtrl)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add_filter_bone(op=\"%s\") -> op is not a FilterBone op"), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			FScopedTransaction Txn(FText::FromString(TEXT("Add Bone to Filter")));
			Retargeter->Modify();
			FilterCtrl->AddBoneToFilter(FName(NeoLuaStr::ToFString(BoneStr)));
			Retargeter->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] add_filter_bone(op=\"%s\", bone=\"%s\")"),
				UTF8_TO_TCHAR(OpNameStr.c_str()), UTF8_TO_TCHAR(BoneStr.c_str())));
			return sol::make_object(Lv, true);
		});

		AssetObj.set_function("clear_filter_bones", [Ctrl, Retargeter, &Session](sol::table /*self*/,
			const std::string& OpNameStr, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			int32 Idx = FindOpIndexByName(Ctrl, NeoLuaStr::ToFString(OpNameStr));
			if (Idx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] clear_filter_bones(\"%s\") -> op not found. Call list(\"ops\") for valid names."), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			UIKRetargetFilterBoneController* FilterCtrl = Cast<UIKRetargetFilterBoneController>(Ctrl->GetOpController(Idx));
			if (!FilterCtrl)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] clear_filter_bones(\"%s\") -> op is not a FilterBone op"), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			FScopedTransaction Txn(FText::FromString(TEXT("Clear Filter Bones")));
			Retargeter->Modify();
			FilterCtrl->ClearBonesToFilter();
			Retargeter->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] clear_filter_bones(\"%s\")"), UTF8_TO_TCHAR(OpNameStr.c_str())));
			return sol::make_object(Lv, true);
		});

		AssetObj.set_function("get_filter_bones", [Ctrl, &Session](sol::table /*self*/,
			const std::string& OpNameStr, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			int32 Idx = FindOpIndexByName(Ctrl, NeoLuaStr::ToFString(OpNameStr));
			if (Idx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] get_filter_bones(\"%s\") -> op not found. Call list(\"ops\") for valid names."), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			UIKRetargetFilterBoneController* FilterCtrl = Cast<UIKRetargetFilterBoneController>(Ctrl->GetOpController(Idx));
			if (!FilterCtrl)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] get_filter_bones(\"%s\") -> op is not a FilterBone op"), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			TArray<FName> Bones = FilterCtrl->GetAllBonesToFilter();
			sol::table Result = Lv.create_table();
			for (int32 i = 0; i < Bones.Num(); ++i)
			{
				Result[i + 1] = TCHAR_TO_UTF8(*Bones[i].ToString());
			}
			Session.Log(FString::Printf(TEXT("[OK] get_filter_bones(\"%s\") -> %d bone(s)"), UTF8_TO_TCHAR(OpNameStr.c_str()), Bones.Num()));
			return Result;
		});

		// ====================================================================
		// CopyBasePose op verbs — UIKRetargetCopyBasePoseController exclude-bones + start-bone API
		// (settings struct also reachable via configure("op_settings", ...))
		// ====================================================================
		AssetObj.set_function("configure_copy_base_pose", [Ctrl, Retargeter, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string OpNameStr = Params.get_or<std::string>("op", "");
			if (OpNameStr.empty())
			{
				Session.Log(TEXT("[FAIL] configure_copy_base_pose() -> 'op' required"));
				return sol::lua_nil;
			}
			int32 Idx = FindOpIndexByName(Ctrl, NeoLuaStr::ToFString(OpNameStr));
			if (Idx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] configure_copy_base_pose(op=\"%s\") -> op not found. Call list(\"ops\") for valid names."), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			UIKRetargetCopyBasePoseController* CopyCtrl = Cast<UIKRetargetCopyBasePoseController>(Ctrl->GetOpController(Idx));
			if (!CopyCtrl)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] configure_copy_base_pose(op=\"%s\") -> op is not a CopyBasePose op"), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}

			FScopedTransaction Txn(FText::FromString(TEXT("Configure Copy Base Pose")));
			Retargeter->Modify();

			// Copy-from-start bone
			std::string CopyFromStart = Params.get_or<std::string>("copy_from_start", "");
			if (!CopyFromStart.empty())
			{
				CopyCtrl->SetCopyFromStart(FName(NeoLuaStr::ToFString(CopyFromStart)));
			}

			// Exclude-bones list (replaces existing when provided)
			sol::optional<sol::table> ExcludeTbl = Params.get<sol::optional<sol::table>>("exclude");
			if (ExcludeTbl.has_value())
			{
				CopyCtrl->ResetBonesToExclude();
				for (auto& KV : ExcludeTbl.value())
				{
					if (KV.second.is<std::string>())
					{
						CopyCtrl->AddBoneToExclude(FName(NeoLuaStr::ToFString(KV.second.as<std::string>())));
					}
				}
			}

			Retargeter->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] configure_copy_base_pose(op=\"%s\")"), UTF8_TO_TCHAR(OpNameStr.c_str())));
			return sol::make_object(Lv, true);
		});

		AssetObj.set_function("get_copy_base_pose", [Ctrl, &Session](sol::table /*self*/,
			const std::string& OpNameStr, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			int32 Idx = FindOpIndexByName(Ctrl, NeoLuaStr::ToFString(OpNameStr));
			if (Idx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] get_copy_base_pose(\"%s\") -> op not found. Call list(\"ops\") for valid names."), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			UIKRetargetCopyBasePoseController* CopyCtrl = Cast<UIKRetargetCopyBasePoseController>(Ctrl->GetOpController(Idx));
			if (!CopyCtrl)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] get_copy_base_pose(\"%s\") -> op is not a CopyBasePose op"), UTF8_TO_TCHAR(OpNameStr.c_str())));
				return sol::lua_nil;
			}
			sol::table Result = Lv.create_table();
			FName Start = CopyCtrl->GetCopyFromStart();
			Result["copy_from_start"] = Start.IsNone() ? "" : TCHAR_TO_UTF8(*Start.ToString());
			sol::table Excluded = Lv.create_table();
			TArray<FName> Ex = CopyCtrl->GetBonesToExclude();
			for (int32 i = 0; i < Ex.Num(); ++i)
			{
				Excluded[i + 1] = TCHAR_TO_UTF8(*Ex[i].ToString());
			}
			Result["exclude"] = Excluded;
			Session.Log(FString::Printf(TEXT("[OK] get_copy_base_pose(\"%s\") -> %d excluded"),
				UTF8_TO_TCHAR(OpNameStr.c_str()), Ex.Num()));
			return Result;
		});
#endif // ENGINE_MINOR_VERSION >= 7

		// ====================================================================
		// get_pelvis_bone({for=..}) — UIKRetargeterController::GetPelvisBone
		// Returns the pelvis bone name from the asset's side-IK-rig (derived from its
		// retarget root setting). Useful for pre-populating configure("pelvis", {source_pelvis_bone=...}).
		// ====================================================================
		AssetObj.set_function("get_pelvis_bone", [Ctrl, &Session](sol::table /*self*/,
			sol::optional<sol::table> Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string ForStr = "target";
			if (Params.has_value()) ForStr = Params.value().get_or<std::string>("for", "target");
			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] get_pelvis_bone() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}
			if (!Ctrl->GetIKRig(Side))
			{
				const TCHAR* SideStr = Side == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target");
				Session.Log(FString::Printf(TEXT("[OK] get_pelvis_bone(for=%s) -> none (%s IK Rig not assigned)"), SideStr, SideStr));
				return sol::lua_nil;
			}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			FName Bone = Ctrl->GetPelvisBone(Side);
#else
			// Pre-5.6: GetPelvisBone(Side) not exposed. Return None so the next branch logs
			// "none" instead of failing — callers needing exact pelvis on older engines
			// can read it via the Source/Target IKRig directly.
			(void)Side;
			FName Bone;
#endif
			if (Bone.IsNone())
			{
				Session.Log(FString::Printf(TEXT("[OK] get_pelvis_bone(for=%s) -> none"), UTF8_TO_TCHAR(ForStr.c_str())));
				return sol::lua_nil;
			}
			Session.Log(FString::Printf(TEXT("[OK] get_pelvis_bone(for=%s) -> \"%s\""), UTF8_TO_TCHAR(ForStr.c_str()), *Bone.ToString()));
			return sol::make_object(Lv, std::string(TCHAR_TO_UTF8(*Bone.ToString())));
		});

		// ====================================================================
		// get_retarget_pose({name=.., for=..}) — UIKRetargeter::GetRetargetPoseByName
		// Reads an arbitrary (non-current) pose without swapping the current pose.
		// Returns {root_offset={x,y,z}, rotations={[bone]={p,y,r}, ...}} or nil if not found.
		// ====================================================================
		AssetObj.set_function("get_retarget_pose", [Retargeter, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string PoseName = Params.get_or<std::string>("name", "");
			std::string ForStr = Params.get_or<std::string>("for", "target");
			if (PoseName.empty())
			{
				Session.Log(TEXT("[FAIL] get_retarget_pose() -> 'name' required"));
				return sol::lua_nil;
			}
			ERetargetSourceOrTarget Side;
			if (!ParseSourceOrTarget(ForStr, Side))
			{
				Session.Log(TEXT("[FAIL] get_retarget_pose() -> 'for' must be \"source\" or \"target\""));
				return sol::lua_nil;
			}
			const FIKRetargetPose* Pose = Retargeter->GetRetargetPoseByName(Side, FName(NeoLuaStr::ToFString(PoseName)));
			if (!Pose)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] get_retarget_pose(\"%s\", for=%s) -> pose not found. Call list(\"poses\") for valid names."),
					UTF8_TO_TCHAR(PoseName.c_str()), UTF8_TO_TCHAR(ForStr.c_str())));
				return sol::lua_nil;
			}

			sol::table Result = Lv.create_table();

			// UE exposes this as an FVector, but FIKRetargetPose clamps X/Y to zero.
			FVector Root = Pose->GetRootTranslationDelta();
			sol::table RootTbl = Lv.create_table();
			RootTbl["x"] = Root.X; RootTbl["y"] = Root.Y; RootTbl["z"] = Root.Z;
			Result["root_offset"] = RootTbl;

			// Per-bone rotation offsets
			sol::table Rotations = Lv.create_table();
			for (const TPair<FName, FQuat>& KV : Pose->GetAllDeltaRotations())
			{
				FRotator Rot = KV.Value.Rotator();
				sol::table RotTbl = Lv.create_table();
				RotTbl["p"] = Rot.Pitch;
				RotTbl["y"] = Rot.Yaw;
				RotTbl["r"] = Rot.Roll;
				Rotations[TCHAR_TO_UTF8(*KV.Key.ToString())] = RotTbl;
			}
			Result["rotations"] = Rotations;

			Session.Log(FString::Printf(TEXT("[OK] get_retarget_pose(\"%s\", for=%s) -> root=(%.2f,%.2f,%.2f), %d rotation(s)"),
				UTF8_TO_TCHAR(PoseName.c_str()), UTF8_TO_TCHAR(ForStr.c_str()),
				Root.X, Root.Y, Root.Z, Pose->GetAllDeltaRotations().Num()));
			return Result;
		});

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		// ====================================================================
		// is_chain_at_default({chain=.., op_name=..}) — UIKRetargeterController::AreChainSettingsAtDefault
		// ====================================================================
		AssetObj.set_function("is_chain_at_default", [Ctrl, &Session](sol::table /*self*/,
			sol::table Params, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			std::string ChainName = Params.get_or<std::string>("chain", "");
			std::string OpNameStr = Params.get_or<std::string>("op_name", "");
			if (ChainName.empty() || OpNameStr.empty())
			{
				Session.Log(TEXT("[FAIL] is_chain_at_default() -> 'chain' and 'op_name' required"));
				return sol::lua_nil;
			}
			bool bAtDefault = Ctrl->AreChainSettingsAtDefault(
				FName(NeoLuaStr::ToFString(ChainName)),
				FName(NeoLuaStr::ToFString(OpNameStr)));
			Session.Log(FString::Printf(TEXT("[OK] is_chain_at_default(\"%s\", op=\"%s\") -> %s"),
				UTF8_TO_TCHAR(ChainName.c_str()), UTF8_TO_TCHAR(OpNameStr.c_str()),
				bAtDefault ? TEXT("true") : TEXT("false")));
			return sol::make_object(Lv, bAtDefault);
		});

		// ====================================================================
		// is_chain_goal_connected(goal_name) — UIKRetargeterController::IsChainGoalConnectedToASolver
		// ====================================================================
		AssetObj.set_function("is_chain_goal_connected", [Ctrl, &Session](sol::table /*self*/,
			const std::string& GoalName, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			if (GoalName.empty())
			{
				Session.Log(TEXT("[FAIL] is_chain_goal_connected() -> goal name required"));
				return sol::lua_nil;
			}
			FName GoalFName = FName(NeoLuaStr::ToFString(GoalName));
			bool bConnected = Ctrl->IsChainGoalConnectedToASolver(GoalFName);
			Session.Log(FString::Printf(TEXT("[OK] is_chain_goal_connected(\"%s\") -> %s"),
				UTF8_TO_TCHAR(GoalName.c_str()), bConnected ? TEXT("true") : TEXT("false")));
			return sol::make_object(Lv, bConnected);
		});
#endif // ENGINE_MINOR_VERSION >= 6

		// ====================================================================
		// info() — override default
		// ====================================================================
		AssetObj.set_function("info", [Ctrl, Retargeter, &Session](sol::table /*self*/, sol::this_state St) -> sol::object
		{
			sol::state_view Lv(St);
			sol::table Result = Lv.create_table();

			// Source IK Rig
			const UIKRigDefinition* SourceRig = Ctrl->GetIKRig(ERetargetSourceOrTarget::Source);
			Result["source_ikrig"] = SourceRig ? TCHAR_TO_UTF8(*SourceRig->GetPathName()) : "none";

			// Target IK Rig
			const UIKRigDefinition* TargetRig = Ctrl->GetIKRig(ERetargetSourceOrTarget::Target);
			Result["target_ikrig"] = TargetRig ? TCHAR_TO_UTF8(*TargetRig->GetPathName()) : "none";

			// Preview meshes
			USkeletalMesh* SrcMesh = Ctrl->GetPreviewMesh(ERetargetSourceOrTarget::Source);
			Result["source_mesh"] = SrcMesh ? TCHAR_TO_UTF8(*SrcMesh->GetName()) : "none";
			USkeletalMesh* TgtMesh = Ctrl->GetPreviewMesh(ERetargetSourceOrTarget::Target);
			Result["target_mesh"] = TgtMesh ? TCHAR_TO_UTF8(*TgtMesh->GetName()) : "none";

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			// Op stack summary (5.6+)
			int32 NumOps = Ctrl->GetNumRetargetOps();
			Result["num_ops"] = NumOps;
			sol::table OpList = Lv.create_table();
			for (int32 Idx = 0; Idx < NumOps; Idx++)
			{
				sol::table E = Lv.create_table();
				E["index"] = Idx;
				FName OpName = Ctrl->GetOpName(Idx);
				E["name"] = TCHAR_TO_UTF8(*OpName.ToString());
				E["enabled"] = Ctrl->GetRetargetOpEnabled(Idx);
				FIKRetargetOpBase* Op = Ctrl->GetRetargetOpByIndex(Idx);
				E["type"] = TCHAR_TO_UTF8(*GetOpTypeDisplayName(Op));
				OpList[Idx + 1] = E;
			}
			Result["ops"] = OpList;

			// Chain mapping count
			int32 MappingCount = 0;
			for (int32 OpIdx = 0; OpIdx < NumOps; OpIdx++)
			{
				FName OpName = Ctrl->GetOpName(OpIdx);
				const FRetargetChainMapping* Mapping = Ctrl->GetChainMapping(OpName);
				if (Mapping)
				{
					MappingCount += Mapping->GetChainPairs().Num();
				}
			}
			Result["num_chain_mappings"] = MappingCount;
#else
			Result["num_ops"] = 0;
			Result["num_chain_mappings"] = 0;
#endif

			// Pose names
			sol::table SourcePoseNames = Lv.create_table();
			TMap<FName, FIKRetargetPose>& SrcPoses = Ctrl->GetRetargetPoses(ERetargetSourceOrTarget::Source);
			int32 SPi = 1;
			for (auto& KV : SrcPoses)
			{
				SourcePoseNames[SPi++] = TCHAR_TO_UTF8(*KV.Key.ToString());
			}
			Result["source_poses"] = SourcePoseNames;
			Result["source_current_pose"] = TCHAR_TO_UTF8(*Ctrl->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Source).ToString());

			sol::table TargetPoseNames = Lv.create_table();
			TMap<FName, FIKRetargetPose>& TgtPoses = Ctrl->GetRetargetPoses(ERetargetSourceOrTarget::Target);
			int32 TPi = 1;
			for (auto& KV : TgtPoses)
			{
				TargetPoseNames[TPi++] = TCHAR_TO_UTF8(*KV.Key.ToString());
			}
			Result["target_poses"] = TargetPoseNames;
			Result["target_current_pose"] = TCHAR_TO_UTF8(*Ctrl->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target).ToString());

			Session.Log(FString::Printf(TEXT("[OK] info() -> %d ops, %d chain mappings, %d src poses, %d tgt poses"),
				Result.get_or("num_ops", 0), Result.get_or("num_chain_mappings", 0), SrcPoses.Num(), TgtPoses.Num()));
			return Result;
		});
	});
}

namespace NSAIIKRigExtension
{
	const TArray<FLuaFunctionDoc>& GetIKRetargeterLuaDocs()
	{
		return IKRetargeterDocs;
	}

	void BindIKRetargeterLua(sol::state& Lua, FLuaSessionData& Session)
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("IKRig")))
		{
			Session.Log(TEXT("[WARN] IKRig plugin is not loaded. Enable it in Edit > Plugins to use this feature."));
			return;
		}
		BindIKRetargeter(Lua, Session);
	}
}
