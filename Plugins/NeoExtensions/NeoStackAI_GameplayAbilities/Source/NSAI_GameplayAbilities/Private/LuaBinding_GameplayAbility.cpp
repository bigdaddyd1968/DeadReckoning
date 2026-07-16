// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Lua/LuaBindingRegistry.h"
#include "Lua/LuaAssetResolver.h"
#include "Lua/LuaStr.h"
#include "Engine/Blueprint.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Modules/ModuleManager.h"
#include "GameplayAbilitiesExtensionBinding.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

// ============================================================================
// Helpers
// ============================================================================

namespace
{

// ---- Reflection helpers to access protected UPROPERTY members ----

template<typename T>
T* GetPropertyPtr(UGameplayAbility* GA, const TCHAR* PropName)
{
	FProperty* Prop = FindFProperty<FProperty>(GA->GetClass(), PropName);
	if (Prop)
	{
		return Prop->ContainerPtrToValuePtr<T>(GA);
	}
	return nullptr;
}

static FGameplayTagContainer* GetTagContainer(UGameplayAbility* GA, const TCHAR* PropName)
{
	return GetPropertyPtr<FGameplayTagContainer>(GA, PropName);
}

static FGameplayTagContainer* GetEditableTagContainer(UGameplayAbility* GA, const TCHAR* PropName)
{
	if (FCString::Strcmp(PropName, TEXT("AbilityTags")) == 0)
	{
#if WITH_EDITOR && ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		return &GA->EditorGetAssetTags();
#endif
	}
	return GetTagContainer(GA, PropName);
}

static const FGameplayTagContainer* GetReadableTagContainer(UGameplayAbility* GA, const TCHAR* PropName)
{
	if (FCString::Strcmp(PropName, TEXT("AbilityTags")) == 0)
	{
		return &GA->GetAssetTags();
	}
	return GetTagContainer(GA, PropName);
}

static TArray<FAbilityTriggerData>* GetAbilityTriggers(UGameplayAbility* GA)
{
	return GetPropertyPtr<TArray<FAbilityTriggerData>>(GA, TEXT("AbilityTriggers"));
}

static TSubclassOf<UGameplayEffect>* GetCostEffectClass(UGameplayAbility* GA)
{
	return GetPropertyPtr<TSubclassOf<UGameplayEffect>>(GA, TEXT("CostGameplayEffectClass"));
}

static TSubclassOf<UGameplayEffect>* GetCooldownEffectClass(UGameplayAbility* GA)
{
	return GetPropertyPtr<TSubclassOf<UGameplayEffect>>(GA, TEXT("CooldownGameplayEffectClass"));
}

// ---- end reflection helpers ----

// Returns true on full success. On the first invalid tag, OutError describes
// which tag failed and the destination Container is left untouched.
static bool SetTagContainerFromLuaArray(FGameplayTagContainer& Container, const sol::table& Tags, const TCHAR* FieldName, FString& OutError)
{
	FGameplayTagContainer Staging;
	for (auto& Pair : Tags)
	{
		if (!Pair.second.is<std::string>()) continue;
		const FString TagString = NeoLuaStr::ToFString(Pair.second.as<std::string>());
		FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagString), false);
		if (!Tag.IsValid())
		{
			OutError = FString::Printf(
				TEXT("tag '%s' in '%s' is not registered. Add it in Project Settings > Gameplay Tags or fix the spelling."),
				*TagString, FieldName);
			return false;
		}
		Staging.AddTag(Tag);
	}
	Container = MoveTemp(Staging);
	return true;
}

static FString FormatInstancingPolicy(TEnumAsByte<EGameplayAbilityInstancingPolicy::Type> Policy)
{
	switch (Policy.GetValue())
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	case EGameplayAbilityInstancingPolicy::NonInstanced: return TEXT("NonInstanced");
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	case EGameplayAbilityInstancingPolicy::InstancedPerActor: return TEXT("InstancedPerActor");
	case EGameplayAbilityInstancingPolicy::InstancedPerExecution: return TEXT("InstancedPerExecution");
	default: return TEXT("Unknown");
	}
}

static FString FormatNetExecutionPolicy(TEnumAsByte<EGameplayAbilityNetExecutionPolicy::Type> Policy)
{
	switch (Policy.GetValue())
	{
	case EGameplayAbilityNetExecutionPolicy::LocalPredicted: return TEXT("LocalPredicted");
	case EGameplayAbilityNetExecutionPolicy::LocalOnly: return TEXT("LocalOnly");
	case EGameplayAbilityNetExecutionPolicy::ServerInitiated: return TEXT("ServerInitiated");
	case EGameplayAbilityNetExecutionPolicy::ServerOnly: return TEXT("ServerOnly");
	default: return TEXT("Unknown");
	}
}

static FString FormatReplicationPolicy(TEnumAsByte<EGameplayAbilityReplicationPolicy::Type> Policy)
{
	switch (Policy.GetValue())
	{
	case EGameplayAbilityReplicationPolicy::ReplicateNo: return TEXT("ReplicateNo");
	case EGameplayAbilityReplicationPolicy::ReplicateYes: return TEXT("ReplicateYes");
	default: return TEXT("Unknown");
	}
}

static FString FormatNetSecurityPolicy(TEnumAsByte<EGameplayAbilityNetSecurityPolicy::Type> Policy)
{
	switch (Policy.GetValue())
	{
	case EGameplayAbilityNetSecurityPolicy::ClientOrServer: return TEXT("ClientOrServer");
	case EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution: return TEXT("ServerOnlyExecution");
	case EGameplayAbilityNetSecurityPolicy::ServerOnlyTermination: return TEXT("ServerOnlyTermination");
	case EGameplayAbilityNetSecurityPolicy::ServerOnly: return TEXT("ServerOnly");
	default: return TEXT("Unknown");
	}
}

static bool ParseInstancingPolicy(const FString& Value, TEnumAsByte<EGameplayAbilityInstancingPolicy::Type>& OutPolicy)
{
	if (Value.Equals(TEXT("NonInstanced"), ESearchCase::IgnoreCase))
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		OutPolicy = EGameplayAbilityInstancingPolicy::NonInstanced;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		return true;
	}
	if (Value.Equals(TEXT("InstancedPerActor"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
		return true;
	}
	if (Value.Equals(TEXT("InstancedPerExecution"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
		return true;
	}
	return false;
}

static bool ParseNetExecutionPolicy(const FString& Value, TEnumAsByte<EGameplayAbilityNetExecutionPolicy::Type>& OutPolicy)
{
	if (Value.Equals(TEXT("LocalPredicted"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
		return true;
	}
	if (Value.Equals(TEXT("LocalOnly"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
		return true;
	}
	if (Value.Equals(TEXT("ServerInitiated"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
		return true;
	}
	if (Value.Equals(TEXT("ServerOnly"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
		return true;
	}
	return false;
}

static bool ParseReplicationPolicy(const FString& Value, TEnumAsByte<EGameplayAbilityReplicationPolicy::Type>& OutPolicy)
{
	if (Value.Equals(TEXT("ReplicateNo"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
		return true;
	}
	if (Value.Equals(TEXT("ReplicateYes"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
		return true;
	}
	return false;
}

static bool ParseNetSecurityPolicy(const FString& Value, TEnumAsByte<EGameplayAbilityNetSecurityPolicy::Type>& OutPolicy)
{
	if (Value.Equals(TEXT("ClientOrServer"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;
		return true;
	}
	if (Value.Equals(TEXT("ServerOnlyExecution"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution;
		return true;
	}
	if (Value.Equals(TEXT("ServerOnlyTermination"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyTermination;
		return true;
	}
	if (Value.Equals(TEXT("ServerOnly"), ESearchCase::IgnoreCase))
	{
		OutPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
		return true;
	}
	return false;
}

static FString FormatTriggerSource(TEnumAsByte<EGameplayAbilityTriggerSource::Type> Source)
{
	switch (Source.GetValue())
	{
	case EGameplayAbilityTriggerSource::GameplayEvent: return TEXT("GameplayEvent");
	case EGameplayAbilityTriggerSource::OwnedTagAdded: return TEXT("OwnedTagAdded");
	case EGameplayAbilityTriggerSource::OwnedTagPresent: return TEXT("OwnedTagPresent");
	default: return TEXT("Unknown");
	}
}

static sol::table TagContainerToLuaArray(sol::state_view& Lua, const FGameplayTagContainer& Container)
{
	sol::table Result = Lua.create_table();
	int32 Idx = 1;
	for (const FGameplayTag& Tag : Container)
	{
		Result[Idx++] = TCHAR_TO_UTF8(*Tag.ToString());
	}
	return Result;
}

// Pre/Post edit helpers for CDO modification
static void PreEditGA(UGameplayAbility* GA, FProperty* Prop)
{
	GA->SetFlags(RF_Transactional);
	GA->Modify();
	if (Prop)
	{
		GA->PreEditChange(Prop);
	}
}

static void PostEditGA(UGameplayAbility* GA, FProperty* Prop)
{
	FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
	GA->PostEditChangeProperty(Evt);
	GA->MarkPackageDirty();
}

} // anonymous namespace

// ============================================================================
// Registration
// ============================================================================

static TArray<FLuaFunctionDoc> GameplayAbilityDocs = {
	{ TEXT("open_asset(path):info()"),
		TEXT("GameplayAbility: read instancing/net/replication/security policies, cost/cooldown effect classes, trigger count, and tag containers"),
		TEXT("table") },
	{ TEXT("open_asset(path):configure(\"policy\", params)"),
		TEXT("GameplayAbility: set {instancing, net_execution, replication, net_security, retrigger_instanced_ability, replicate_input_directly, server_respects_remote_ability_cancellation}"),
		TEXT("true or nil") },
	{ TEXT("open_asset(path):configure(\"cost\"|\"cooldown\", params)"),
		TEXT("GameplayAbility: set Cost/CooldownGameplayEffectClass from {effect=\"/Game/.../GE_*.GE_*\"} or clear with {clear=true}"),
		TEXT("true or nil") },
	{ TEXT("open_asset(path):configure(\"tags\", params)"),
		TEXT("GameplayAbility: set ability_tags, activation_required/blocked, cancel/block_abilities_with, activation_owned, source/target_required/blocked from arrays of tag strings"),
		TEXT("true or nil") },
	{ TEXT("open_asset(path):add(\"trigger\", {tag, type})"),
		TEXT("GameplayAbility: append a FAbilityTriggerData. type in {GameplayEvent, OwnedTagAdded, OwnedTagPresent}"),
		TEXT("1-based index or nil") },
	{ TEXT("open_asset(path):remove(\"trigger\", index)"),
		TEXT("GameplayAbility: remove AbilityTriggers entry by 1-based index"),
		TEXT("true or nil") },
	{ TEXT("open_asset(path):list(type?)"),
		TEXT("GameplayAbility: type in {triggers, tags, all} (default all)"),
		TEXT("table or nil") },
};

// Resolve the current UGameplayAbility CDO for an asset path. Must be called
// on every method invocation — the Blueprint's GeneratedClass + CDO are
// reinstanced by every compile (see Editor/UnrealEd/Private/Kismet2/
// KismetReinstanceUtilities.cpp:524-531), so captured raw pointers rot.
struct FGAHandle
{
	UBlueprint* Blueprint = nullptr;
	UGameplayAbility* GA = nullptr;
	bool IsValid() const { return Blueprint && GA; }
};

static FGAHandle ResolveGAHandle(const FString& FPath)
{
	FGAHandle H;
	H.Blueprint = NeoLuaAsset::Resolve<UBlueprint>(FPath);
	if (H.Blueprint && H.Blueprint->GeneratedClass)
	{
		H.GA = Cast<UGameplayAbility>(H.Blueprint->GeneratedClass->GetDefaultObject());
	}
	return H;
}

static void BindGameplayAbility(sol::state& Lua, FLuaSessionData& Session)
{
	Lua.set_function("_enrich_gameplay_ability", [&Session](sol::table BPObj, sol::this_state S)
	{
		sol::state_view LuaView(S);
		std::string PathStr = BPObj.get<std::string>("path");
		FString FPath = NeoLuaStr::ToFString(PathStr);

		FGAHandle Initial = ResolveGAHandle(FPath);
		if (!Initial.IsValid()) return;

		// ---- help text ----
		BPObj["_help_text"] =
			"GameplayAbility verbs (add/remove/list/configure/info/help):\n"
			"\n"
			"configure(type, params):\n"
			"  configure(\"policy\", {instancing=\"InstancedPerActor\", net_execution=\"LocalPredicted\",\n"
			"    replication=\"ReplicateYes\", net_security=\"ServerOnly\",\n"
			"    retrigger_instanced_ability=true, replicate_input_directly=false,\n"
			"    server_respects_remote_ability_cancellation=true})\n"
			"  configure(\"cost\", {effect=\"/Game/Path/To/GE_Cost.GE_Cost\"})\n"
			"  configure(\"cost\", {clear=true})  -- remove cost effect\n"
			"  configure(\"cooldown\", {effect=\"/Game/Path/To/GE_Cooldown.GE_Cooldown\"})\n"
			"  configure(\"cooldown\", {clear=true})  -- remove cooldown effect\n"
			"  configure(\"tags\", {ability_tags={\"Ability.Melee\"}, activation_required={\"Tag.One\"},\n"
			"    activation_blocked={\"Tag.Two\"}, cancel_abilities_with={...},\n"
			"    block_abilities_with={...}, activation_owned={...},\n"
			"    source_required={...}, source_blocked={...},\n"
			"    target_required={...}, target_blocked={...}})\n"
			"\n"
			"add(type, params):\n"
			"  add(\"trigger\", {tag=\"Event.Attack\", type=\"GameplayEvent\"})\n"
			"    type: GameplayEvent, OwnedTagAdded, OwnedTagPresent\n"
			"\n"
			"remove(type, id):\n"
			"  remove(\"trigger\", 1)  -- 1-based index\n"
			"\n"
			"list(type):\n"
			"  list(\"triggers\"), list(\"tags\")\n";

		// ==================================================================
		// info()
		// ==================================================================
		BPObj.set_function("info", [FPath, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FGAHandle H = ResolveGAHandle(FPath);
			if (!H.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] info() -> GameplayAbility '%s' could not be resolved. Asset may have been deleted or renamed; re-run open_asset()."), *FPath));
				return sol::lua_nil;
			}
			UBlueprint* Blueprint = H.Blueprint;
			UGameplayAbility* GA = H.GA;
			sol::table Info = Lua.create_table();

			Info["name"] = TCHAR_TO_UTF8(*Blueprint->GetName());
			Info["class_name"] = TCHAR_TO_UTF8(*GA->GetClass()->GetName());

			// Policies (these have public getters)
			Info["instancing_policy"] = TCHAR_TO_UTF8(*FormatInstancingPolicy(GA->GetInstancingPolicy()));
			Info["net_execution_policy"] = TCHAR_TO_UTF8(*FormatNetExecutionPolicy(GA->GetNetExecutionPolicy()));
			Info["replication_policy"] = TCHAR_TO_UTF8(*FormatReplicationPolicy(GA->GetReplicationPolicy()));

			// Net security policy (public getter)
			Info["net_security_policy"] = TCHAR_TO_UTF8(*FormatNetSecurityPolicy(GA->GetNetSecurityPolicy()));

			// Ability tags (public getter)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			const FGameplayTagContainer& AssetTags = GA->GetAssetTags();
			if (AssetTags.Num() > 0)
			{
				Info["ability_tags"] = TagContainerToLuaArray(Lua, AssetTags);
			}
#endif

			// Bool flags (via reflection)
			if (auto* V = GetPropertyPtr<bool>(GA, TEXT("bRetriggerInstancedAbility")))
				Info["retrigger_instanced_ability"] = *V;
			if (auto* V = GetPropertyPtr<bool>(GA, TEXT("bReplicateInputDirectly")))
				Info["replicate_input_directly"] = *V;
			if (auto* V = GetPropertyPtr<bool>(GA, TEXT("bServerRespectsRemoteAbilityCancellation")))
				Info["server_respects_remote_ability_cancellation"] = *V;

			// Cost effect (via reflection)
			TSubclassOf<UGameplayEffect>* CostClass = GetCostEffectClass(GA);
			if (CostClass && *CostClass)
			{
				Info["cost_effect"] = TCHAR_TO_UTF8(*(*CostClass)->GetName());
			}

			// Cooldown effect (via reflection)
			TSubclassOf<UGameplayEffect>* CooldownClass = GetCooldownEffectClass(GA);
			if (CooldownClass && *CooldownClass)
			{
				Info["cooldown_effect"] = TCHAR_TO_UTF8(*(*CooldownClass)->GetName());
			}

			// Triggers (via reflection)
			TArray<FAbilityTriggerData>* Triggers = GetAbilityTriggers(GA);
			Info["trigger_count"] = Triggers ? Triggers->Num() : 0;

			// Tag containers (via reflection)
			struct FTagMapping { const char* Key; const TCHAR* PropName; };
			static const FTagMapping TagMappings[] = {
				{"activation_required_tags", TEXT("ActivationRequiredTags")},
				{"activation_blocked_tags", TEXT("ActivationBlockedTags")},
				{"cancel_abilities_with_tag", TEXT("CancelAbilitiesWithTag")},
				{"block_abilities_with_tag", TEXT("BlockAbilitiesWithTag")},
				{"activation_owned_tags", TEXT("ActivationOwnedTags")},
				{"source_required_tags", TEXT("SourceRequiredTags")},
				{"source_blocked_tags", TEXT("SourceBlockedTags")},
				{"target_required_tags", TEXT("TargetRequiredTags")},
				{"target_blocked_tags", TEXT("TargetBlockedTags")},
			};

			for (const auto& Mapping : TagMappings)
			{
				FGameplayTagContainer* Tags = GetTagContainer(GA, Mapping.PropName);
				if (Tags && Tags->Num() > 0)
				{
					Info[Mapping.Key] = TagContainerToLuaArray(Lua, *Tags);
				}
			}

			int32 TriggerCount = Triggers ? Triggers->Num() : 0;
			Session.Log(FString::Printf(TEXT("[OK] info() -> %s: instancing=%s, net=%s, %d triggers"),
				*Blueprint->GetName(),
				*FormatInstancingPolicy(GA->GetInstancingPolicy()),
				*FormatNetExecutionPolicy(GA->GetNetExecutionPolicy()),
				TriggerCount));
			return Info;
		});

		// ==================================================================
		// configure(type, params)
		// ==================================================================
		BPObj.set_function("configure", [FPath, &Session](sol::table /*self*/,
			const std::string& Type, sol::object Param, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);

			FGAHandle H = ResolveGAHandle(FPath);
			if (!H.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] configure(\"%s\") -> GameplayAbility '%s' could not be resolved. Asset may have been deleted or renamed; re-run open_asset()."), *FType, *FPath));
				return sol::lua_nil;
			}
			UGameplayAbility* GA = H.GA;

			// ---- configure("policy", {instancing="...", net_execution="...", ...}) ----
			if (FType.Equals(TEXT("policy"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"policy\") -> params table required. Example: configure(\"policy\", {instancing=\"InstancedPerActor\", net_execution=\"LocalPredicted\"})."));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();

				TOptional<TEnumAsByte<EGameplayAbilityInstancingPolicy::Type>> StagedInstancing;
				TOptional<TEnumAsByte<EGameplayAbilityNetExecutionPolicy::Type>> StagedNetExecution;
				TOptional<TEnumAsByte<EGameplayAbilityReplicationPolicy::Type>> StagedReplication;
				TOptional<TEnumAsByte<EGameplayAbilityNetSecurityPolicy::Type>> StagedNetSecurity;

				sol::optional<std::string> InstOpt = P.get<sol::optional<std::string>>("instancing");
				if (InstOpt.has_value())
				{
					FString InstStr = NeoLuaStr::ToFString(InstOpt.value());
					TEnumAsByte<EGameplayAbilityInstancingPolicy::Type> ParsedPolicy;
					if (!ParseInstancingPolicy(InstStr, ParsedPolicy))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] configure(\"policy\") -> unknown instancing '%s'. Valid: NonInstanced, InstancedPerActor, InstancedPerExecution"), *InstStr));
						return sol::lua_nil;
					}
					StagedInstancing.Emplace(ParsedPolicy);
				}

				sol::optional<std::string> NetExecOpt = P.get<sol::optional<std::string>>("net_execution");
				if (NetExecOpt.has_value())
				{
					FString NEStr = NeoLuaStr::ToFString(NetExecOpt.value());
					TEnumAsByte<EGameplayAbilityNetExecutionPolicy::Type> ParsedPolicy;
					if (!ParseNetExecutionPolicy(NEStr, ParsedPolicy))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] configure(\"policy\") -> unknown net_execution '%s'. Valid: LocalPredicted, LocalOnly, ServerInitiated, ServerOnly"), *NEStr));
						return sol::lua_nil;
					}
					StagedNetExecution.Emplace(ParsedPolicy);
				}

				sol::optional<std::string> RepOpt = P.get<sol::optional<std::string>>("replication");
				if (RepOpt.has_value())
				{
					FString RepStr = NeoLuaStr::ToFString(RepOpt.value());
					TEnumAsByte<EGameplayAbilityReplicationPolicy::Type> ParsedPolicy;
					if (!ParseReplicationPolicy(RepStr, ParsedPolicy))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] configure(\"policy\") -> unknown replication '%s'. Valid: ReplicateNo, ReplicateYes"), *RepStr));
						return sol::lua_nil;
					}
					StagedReplication.Emplace(ParsedPolicy);
				}

				sol::optional<std::string> SecOpt = P.get<sol::optional<std::string>>("net_security");
				if (SecOpt.has_value())
				{
					FString SecStr = NeoLuaStr::ToFString(SecOpt.value());
					TEnumAsByte<EGameplayAbilityNetSecurityPolicy::Type> ParsedPolicy;
					if (!ParseNetSecurityPolicy(SecStr, ParsedPolicy))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] configure(\"policy\") -> unknown net_security '%s'. Valid: ClientOrServer, ServerOnlyExecution, ServerOnlyTermination, ServerOnly"), *SecStr));
						return sol::lua_nil;
					}
					StagedNetSecurity.Emplace(ParsedPolicy);
				}

				sol::optional<bool> RetriggerOpt = P.get<sol::optional<bool>>("retrigger_instanced_ability");
				sol::optional<bool> ReplicateInputOpt = P.get<sol::optional<bool>>("replicate_input_directly");
				sol::optional<bool> ServerRespectsCancellationOpt = P.get<sol::optional<bool>>("server_respects_remote_ability_cancellation");

				const FScopedTransaction Tx(NSLOCTEXT("NeoStackAI", "EditGAPolicy", "Edit Gameplay Ability Policy"));
				PreEditGA(GA, nullptr);

				if (StagedInstancing.IsSet())
				{
					if (auto* Prop = GetPropertyPtr<TEnumAsByte<EGameplayAbilityInstancingPolicy::Type>>(GA, TEXT("InstancingPolicy")))
						*Prop = StagedInstancing.GetValue();
				}
				if (StagedNetExecution.IsSet())
				{
					if (auto* Prop = GetPropertyPtr<TEnumAsByte<EGameplayAbilityNetExecutionPolicy::Type>>(GA, TEXT("NetExecutionPolicy")))
						*Prop = StagedNetExecution.GetValue();
				}
				if (StagedReplication.IsSet())
				{
					if (auto* Prop = GetPropertyPtr<TEnumAsByte<EGameplayAbilityReplicationPolicy::Type>>(GA, TEXT("ReplicationPolicy")))
						*Prop = StagedReplication.GetValue();
				}
				if (StagedNetSecurity.IsSet())
				{
					if (auto* Prop = GetPropertyPtr<TEnumAsByte<EGameplayAbilityNetSecurityPolicy::Type>>(GA, TEXT("NetSecurityPolicy")))
						*Prop = StagedNetSecurity.GetValue();
				}

				if (RetriggerOpt.has_value())
				{
					if (auto* Prop = GetPropertyPtr<bool>(GA, TEXT("bRetriggerInstancedAbility")))
						*Prop = RetriggerOpt.value();
				}
				if (ReplicateInputOpt.has_value())
				{
					if (auto* Prop = GetPropertyPtr<bool>(GA, TEXT("bReplicateInputDirectly")))
						*Prop = ReplicateInputOpt.value();
				}
				if (ServerRespectsCancellationOpt.has_value())
				{
					if (auto* Prop = GetPropertyPtr<bool>(GA, TEXT("bServerRespectsRemoteAbilityCancellation")))
						*Prop = ServerRespectsCancellationOpt.value();
				}

				PostEditGA(GA, nullptr);
				Session.Log(TEXT("[OK] configure(\"policy\")"));
				return sol::make_object(Lua, true);
			}

			// ---- configure("cost", {effect="..."}) or configure("cost", {clear=true}) ----
			if (FType.Equals(TEXT("cost"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"cost\") -> table with 'effect' or 'clear' required"));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();

				// Clear cost effect
				if (P.get_or("clear", false))
				{
					const FScopedTransaction Tx(NSLOCTEXT("NeoStackAI", "ClearGACost", "Clear Gameplay Ability Cost"));
					FProperty* CostProp = GA->GetClass()->FindPropertyByName(TEXT("CostGameplayEffectClass"));
					PreEditGA(GA, CostProp);

					TSubclassOf<UGameplayEffect>* CostClass = GetCostEffectClass(GA);
					if (CostClass)
					{
						*CostClass = nullptr;
					}

					PostEditGA(GA, CostProp);
					Session.Log(TEXT("[OK] configure(\"cost\", clear=true)"));
					return sol::make_object(Lua, true);
				}

				std::string EffectPath = P.get_or<std::string>("effect", "");
				if (EffectPath.empty())
				{
					Session.Log(TEXT("[FAIL] configure(\"cost\") -> 'effect' (asset path) or 'clear' required"));
					return sol::lua_nil;
				}

				FString FEffectPath = NeoLuaStr::ToFString(EffectPath);
				UBlueprint* EffectBP = NeoLuaAsset::Resolve<UBlueprint>(FEffectPath);
				if (!EffectBP || !EffectBP->GeneratedClass)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"cost\") -> could not load GameplayEffect Blueprint '%s'. Use a full object path like /Game/Effects/GE_Cost.GE_Cost or inspect candidates with find_assets('/Game', {class='GameplayEffect'})."), *FEffectPath));
					return sol::lua_nil;
				}

				if (!EffectBP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass()))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"cost\") -> '%s' is not a GameplayEffect blueprint (its GeneratedClass derives from '%s')."), *FEffectPath, *EffectBP->GeneratedClass->GetName()));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("NeoStackAI", "EditGACost", "Edit Gameplay Ability Cost"));
				FProperty* CostProp = GA->GetClass()->FindPropertyByName(TEXT("CostGameplayEffectClass"));
				PreEditGA(GA, CostProp);

				TSubclassOf<UGameplayEffect>* CostClass = GetCostEffectClass(GA);
				if (CostClass)
				{
					*CostClass = EffectBP->GeneratedClass;
				}

				PostEditGA(GA, CostProp);

				Session.Log(FString::Printf(TEXT("[OK] configure(\"cost\", effect=\"%s\")"), *FEffectPath));
				return sol::make_object(Lua, true);
			}

			// ---- configure("cooldown", {effect="..."}) or configure("cooldown", {clear=true}) ----
			if (FType.Equals(TEXT("cooldown"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"cooldown\") -> table with 'effect' or 'clear' required"));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();

				// Clear cooldown effect
				if (P.get_or("clear", false))
				{
					const FScopedTransaction Tx(NSLOCTEXT("NeoStackAI", "ClearGACooldown", "Clear Gameplay Ability Cooldown"));
					FProperty* CooldownProp = GA->GetClass()->FindPropertyByName(TEXT("CooldownGameplayEffectClass"));
					PreEditGA(GA, CooldownProp);

					TSubclassOf<UGameplayEffect>* CooldownClass = GetCooldownEffectClass(GA);
					if (CooldownClass)
					{
						*CooldownClass = nullptr;
					}

					PostEditGA(GA, CooldownProp);
					Session.Log(TEXT("[OK] configure(\"cooldown\", clear=true)"));
					return sol::make_object(Lua, true);
				}

				std::string EffectPath = P.get_or<std::string>("effect", "");
				if (EffectPath.empty())
				{
					Session.Log(TEXT("[FAIL] configure(\"cooldown\") -> 'effect' (asset path) or 'clear' required"));
					return sol::lua_nil;
				}

				FString FEffectPath = NeoLuaStr::ToFString(EffectPath);
				UBlueprint* EffectBP = NeoLuaAsset::Resolve<UBlueprint>(FEffectPath);
				if (!EffectBP || !EffectBP->GeneratedClass)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"cooldown\") -> could not load GameplayEffect Blueprint '%s'. Use a full object path like /Game/Effects/GE_Cooldown.GE_Cooldown or inspect candidates with find_assets('/Game', {class='GameplayEffect'})."), *FEffectPath));
					return sol::lua_nil;
				}

				if (!EffectBP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass()))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] configure(\"cooldown\") -> '%s' is not a GameplayEffect blueprint (its GeneratedClass derives from '%s')."), *FEffectPath, *EffectBP->GeneratedClass->GetName()));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("NeoStackAI", "EditGACooldown", "Edit Gameplay Ability Cooldown"));
				FProperty* CooldownProp = GA->GetClass()->FindPropertyByName(TEXT("CooldownGameplayEffectClass"));
				PreEditGA(GA, CooldownProp);

				TSubclassOf<UGameplayEffect>* CooldownClass = GetCooldownEffectClass(GA);
				if (CooldownClass)
				{
					*CooldownClass = EffectBP->GeneratedClass;
				}

				PostEditGA(GA, CooldownProp);

				Session.Log(FString::Printf(TEXT("[OK] configure(\"cooldown\", effect=\"%s\")"), *FEffectPath));
				return sol::make_object(Lua, true);
			}

			// ---- configure("tags", {activation_required={...}, ...}) ----
			if (FType.Equals(TEXT("tags"), ESearchCase::IgnoreCase))
			{
				if (!Param.is<sol::table>())
				{
					Session.Log(TEXT("[FAIL] configure(\"tags\") -> params table required. Example: configure(\"tags\", {ability_tags={\"Ability.Melee\"}})."));
					return sol::lua_nil;
				}
				sol::table P = Param.as<sol::table>();

				struct FTagSetMapping { const char* Key; const TCHAR* PropName; };
				static const FTagSetMapping TagSetMappings[] = {
					{"ability_tags", TEXT("AbilityTags")},
					{"activation_required", TEXT("ActivationRequiredTags")},
					{"activation_blocked", TEXT("ActivationBlockedTags")},
					{"cancel_abilities_with", TEXT("CancelAbilitiesWithTag")},
					{"block_abilities_with", TEXT("BlockAbilitiesWithTag")},
					{"activation_owned", TEXT("ActivationOwnedTags")},
					{"source_required", TEXT("SourceRequiredTags")},
					{"source_blocked", TEXT("SourceBlockedTags")},
					{"target_required", TEXT("TargetRequiredTags")},
					{"target_blocked", TEXT("TargetBlockedTags")},
				};

				// Validate every tag up-front (atomic semantics): stage into temporary
				// containers; on any invalid tag, surface the error and commit nothing.
				TArray<TPair<FGameplayTagContainer*, FGameplayTagContainer>> Staged;
				for (const auto& Mapping : TagSetMappings)
				{
					sol::optional<sol::table> TagsOpt = P.get<sol::optional<sol::table>>(Mapping.Key);
					if (!TagsOpt.has_value()) continue;

					FGameplayTagContainer* Container = GetEditableTagContainer(GA, Mapping.PropName);
					if (!Container)
					{
						Session.Log(FString::Printf(TEXT("[FAIL] configure(\"tags\") -> could not access UGameplayAbility::%s via reflection. Verify this asset's GeneratedClass derives from GameplayAbility and reopen it with open_asset()."), Mapping.PropName));
						return sol::lua_nil;
					}

					FGameplayTagContainer NewValue;
					FString TagErr;
					if (!SetTagContainerFromLuaArray(NewValue, TagsOpt.value(), Mapping.PropName, TagErr))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] configure(\"tags\") -> %s No tags were changed."), *TagErr));
						return sol::lua_nil;
					}
					Staged.Add(TPair<FGameplayTagContainer*, FGameplayTagContainer>(Container, MoveTemp(NewValue)));
				}

				const FScopedTransaction Tx(NSLOCTEXT("NeoStackAI", "EditGATags", "Edit Gameplay Ability Tags"));
				PreEditGA(GA, nullptr);
				for (auto& Entry : Staged)
				{
					*Entry.Key = MoveTemp(Entry.Value);
				}
				PostEditGA(GA, nullptr);

				Session.Log(FString::Printf(TEXT("[OK] configure(\"tags\") -> %d field%s updated"), Staged.Num(), Staged.Num() == 1 ? TEXT("") : TEXT("s")));
				return sol::make_object(Lua, true);
			}

			Session.Log(FString::Printf(TEXT("[FAIL] configure(\"%s\") -> unknown type. Valid: policy, cost, cooldown, tags"), *FType));
			return sol::lua_nil;
		});

		// ==================================================================
		// add(type, params)
		// ==================================================================
		BPObj.set_function("add", [FPath, &Session](sol::table /*self*/,
			const std::string& Type, sol::optional<sol::table> Params, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);

			FGAHandle H = ResolveGAHandle(FPath);
			if (!H.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> GameplayAbility '%s' could not be resolved. Asset may have been deleted or renamed; re-run open_asset()."), *FType, *FPath));
				return sol::lua_nil;
			}
			UGameplayAbility* GA = H.GA;

			// ---- add("trigger", {tag="...", type="..."}) ----
			if (FType.Equals(TEXT("trigger"), ESearchCase::IgnoreCase))
			{
				if (!Params.has_value())
				{
					Session.Log(TEXT("[FAIL] add(\"trigger\") -> params table required. Example: add(\"trigger\", {tag=\"Event.Attack\", type=\"GameplayEvent\"})."));
					return sol::lua_nil;
				}
				sol::table P = Params.value();

				std::string TagStr = P.get_or<std::string>("tag", "");
				if (TagStr.empty())
				{
					Session.Log(TEXT("[FAIL] add(\"trigger\") -> 'tag' required. Example: add(\"trigger\", {tag=\"Event.Attack\", type=\"GameplayEvent\"})."));
					return sol::lua_nil;
				}

				FString FTagStr = NeoLuaStr::ToFString(TagStr);
				FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*FTagStr), false);
				if (!Tag.IsValid())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"trigger\") -> tag '%s' not found in project registry. Add it in Project Settings > Gameplay Tags or choose an existing tag."), *FTagStr));
					return sol::lua_nil;
				}

				// Parse trigger source type
				EGameplayAbilityTriggerSource::Type TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
				std::string TypeStr = P.get_or<std::string>("type", "GameplayEvent");
				FString FTypeStr = NeoLuaStr::ToFString(TypeStr);

				if (FTypeStr.Equals(TEXT("GameplayEvent"), ESearchCase::IgnoreCase))
					TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
				else if (FTypeStr.Equals(TEXT("OwnedTagAdded"), ESearchCase::IgnoreCase))
					TriggerSource = EGameplayAbilityTriggerSource::OwnedTagAdded;
				else if (FTypeStr.Equals(TEXT("OwnedTagPresent"), ESearchCase::IgnoreCase))
					TriggerSource = EGameplayAbilityTriggerSource::OwnedTagPresent;
				else
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add(\"trigger\") -> unknown type '%s'. Valid: GameplayEvent, OwnedTagAdded, OwnedTagPresent"), *FTypeStr));
					return sol::lua_nil;
				}

				TArray<FAbilityTriggerData>* Triggers = GetAbilityTriggers(GA);
				if (!Triggers)
				{
					Session.Log(TEXT("[FAIL] add(\"trigger\") -> could not access UGameplayAbility::AbilityTriggers. Verify this asset's GeneratedClass derives from GameplayAbility and reopen it with open_asset()."));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("NeoStackAI", "AddGATrigger", "Add Gameplay Ability Trigger"));
				FProperty* TriggerProp = GA->GetClass()->FindPropertyByName(TEXT("AbilityTriggers"));
				PreEditGA(GA, TriggerProp);

				FAbilityTriggerData NewTrigger;
				NewTrigger.TriggerTag = Tag;
				NewTrigger.TriggerSource = TriggerSource;
				Triggers->Add(NewTrigger);

				PostEditGA(GA, TriggerProp);

				Session.Log(FString::Printf(TEXT("[OK] add(\"trigger\", tag=\"%s\", type=\"%s\") -> index %d"),
					*FTagStr, *FTypeStr, Triggers->Num()));
				return sol::make_object(Lua, Triggers->Num());
			}

			Session.Log(FString::Printf(TEXT("[FAIL] add(\"%s\") -> unknown type. Valid: trigger"), *FType));
			return sol::lua_nil;
		});

		// ==================================================================
		// remove(type, id)
		// ==================================================================
		BPObj.set_function("remove", [FPath, &Session](sol::table /*self*/,
			const std::string& Type, sol::object Id, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFString(Type);

			FGAHandle H = ResolveGAHandle(FPath);
			if (!H.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] remove(\"%s\") -> GameplayAbility '%s' could not be resolved. Asset may have been deleted or renamed; re-run open_asset()."), *FType, *FPath));
				return sol::lua_nil;
			}
			UGameplayAbility* GA = H.GA;

			// ---- remove("trigger", index) ----
			if (FType.Equals(TEXT("trigger"), ESearchCase::IgnoreCase))
			{
				if (!Id.is<int>() && !Id.is<double>())
				{
					Session.Log(TEXT("[FAIL] remove(\"trigger\") -> 1-based index required. Example: remove(\"trigger\", 1)."));
					return sol::lua_nil;
				}
				int32 LuaIdx = Id.is<int>() ? Id.as<int>() : (int32)Id.as<double>();
				int32 Idx = LuaIdx - 1;

				TArray<FAbilityTriggerData>* Triggers = GetAbilityTriggers(GA);
				if (!Triggers)
				{
					Session.Log(TEXT("[FAIL] remove(\"trigger\") -> could not access UGameplayAbility::AbilityTriggers. Verify this asset's GeneratedClass derives from GameplayAbility and reopen it with open_asset()."));
					return sol::lua_nil;
				}

				if (Idx < 0 || Idx >= Triggers->Num())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] remove(\"trigger\", %d) -> out of range (count=%d). Use list(\"triggers\") to inspect valid indices."), LuaIdx, Triggers->Num()));
					return sol::lua_nil;
				}

				const FScopedTransaction Tx(NSLOCTEXT("NeoStackAI", "RemoveGATrigger", "Remove Gameplay Ability Trigger"));
				FProperty* TriggerProp = GA->GetClass()->FindPropertyByName(TEXT("AbilityTriggers"));
				PreEditGA(GA, TriggerProp);
				Triggers->RemoveAt(Idx);
				PostEditGA(GA, TriggerProp);

				Session.Log(FString::Printf(TEXT("[OK] remove(\"trigger\", %d)"), LuaIdx));
				return sol::make_object(Lua, true);
			}

			Session.Log(FString::Printf(TEXT("[FAIL] remove(\"%s\") -> unknown type. Valid: trigger"), *FType));
			return sol::lua_nil;
		});

		// ==================================================================
		// list(type)
		// ==================================================================
		BPObj.set_function("list", [FPath, &Session](sol::table self,
			sol::optional<std::string> TypeOpt, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			FString FType = NeoLuaStr::ToFStringOpt(TypeOpt, TEXT("all"));

			if (FType.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				sol::protected_function InfoFn = self["info"];
				if (InfoFn.valid()) return InfoFn(self);
				return sol::lua_nil;
			}

			FGAHandle H = ResolveGAHandle(FPath);
			if (!H.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] list(\"%s\") -> GameplayAbility '%s' could not be resolved. Asset may have been deleted or renamed; re-run open_asset()."), *FType, *FPath));
				return sol::lua_nil;
			}
			UGameplayAbility* GA = H.GA;

			// ---- list("triggers") ----
			if (FType.Contains(TEXT("trigger"), ESearchCase::IgnoreCase))
			{
				TArray<FAbilityTriggerData>* Triggers = GetAbilityTriggers(GA);
				if (!Triggers)
				{
					Session.Log(TEXT("[FAIL] list(\"triggers\") -> could not access UGameplayAbility::AbilityTriggers. Verify this asset's GeneratedClass derives from GameplayAbility and reopen it with open_asset()."));
					return sol::lua_nil;
				}
				sol::table Result = Lua.create_table();
				for (int32 i = 0; i < Triggers->Num(); i++)
				{
					const FAbilityTriggerData& Trigger = (*Triggers)[i];
					sol::table E = Lua.create_table();
					E["index"] = i + 1;
					E["tag"] = TCHAR_TO_UTF8(*Trigger.TriggerTag.ToString());
					E["type"] = TCHAR_TO_UTF8(*FormatTriggerSource(Trigger.TriggerSource));
					Result[i + 1] = E;
				}
				Session.Log(FString::Printf(TEXT("[OK] list(\"triggers\") -> %d"), Triggers->Num()));
				return Result;
			}

			// ---- list("tags") ----
			if (FType.Contains(TEXT("tag"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lua.create_table();

				struct FTagListMapping { const char* Key; const TCHAR* PropName; };
				static const FTagListMapping TagListMappings[] = {
					{"ability_tags", TEXT("AbilityTags")},
					{"activation_required", TEXT("ActivationRequiredTags")},
					{"activation_blocked", TEXT("ActivationBlockedTags")},
					{"cancel_abilities_with", TEXT("CancelAbilitiesWithTag")},
					{"block_abilities_with", TEXT("BlockAbilitiesWithTag")},
					{"activation_owned", TEXT("ActivationOwnedTags")},
					{"source_required", TEXT("SourceRequiredTags")},
					{"source_blocked", TEXT("SourceBlockedTags")},
					{"target_required", TEXT("TargetRequiredTags")},
					{"target_blocked", TEXT("TargetBlockedTags")},
				};

				for (const auto& Mapping : TagListMappings)
				{
					const FGameplayTagContainer* Container = GetReadableTagContainer(GA, Mapping.PropName);
					sol::table TagList = Lua.create_table();
					if (Container)
					{
						int32 Idx = 1;
						for (const FGameplayTag& Tag : *Container)
						{
							TagList[Idx++] = TCHAR_TO_UTF8(*Tag.ToString());
						}
					}
					Result[Mapping.Key] = TagList;
				}

				Session.Log(TEXT("[OK] list(\"tags\")"));
				return Result;
			}

			Session.Log(FString::Printf(TEXT("[FAIL] list(\"%s\") -> unknown type. Valid: triggers, tags"), *FType));
			return sol::lua_nil;
		});

		// help() is handled by Blueprint's help() which reads _help_text
	});
}

namespace NSAIGameplayAbilitiesExtension
{
	const TArray<FLuaFunctionDoc>& GetGameplayAbilityLuaDocs()
	{
		return GameplayAbilityDocs;
	}

	void BindGameplayAbilityLua(sol::state& Lua, FLuaSessionData& Session)
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("GameplayAbilities")))
		{
			Session.Log(TEXT("[WARN] GameplayAbilities plugin is not loaded. Enable it in Edit > Plugins to use this feature."));
			return;
		}
		BindGameplayAbility(Lua, Session);
	}
}
