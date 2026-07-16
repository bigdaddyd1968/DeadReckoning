// Copyright 2026 Betide Studio. All Rights Reserved.
//
// Niagara-specific struct handler registrations for NeoLuaProperty::ApplyTable / ReadAsTable.
// Adds Lua round-trip for FNiagaraBool, FNiagaraPosition, FNiagaraVariant, etc.
//
// Also exposes helpers for writing/reading FNiagaraParameterStore values via the
// same reflection path used by other bindings.

#pragma once

#include "CoreMinimal.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

struct FNiagaraTypeDefinition;
class UNiagaraDataInterface;
class UNiagaraRendererProperties;

namespace NSAINiagaraLCD
{

// Called from FNSAI_NiagaraModule::StartupModule. Idempotent.
void RegisterNiagaraStructHandlers();

// Resolve a type name string (e.g. "Float", "Vector", "Color", "SkeletalMesh") to a
// FNiagaraTypeDefinition using the engine's type registry + editor-allowed-types.
// Returns an invalid type def if not resolvable.
FNiagaraTypeDefinition ResolveTypeDefinition(const FString& TypeName);

// List the authored names of all types allowed as user parameters (for error messages
// and discoverability). Backed by FNiagaraEditorUtilities::GetAllowedUserVariableTypes.
TArray<FString> GetAllowedUserParameterTypeNames();

// Serialize a Lua value to the byte layout of a Niagara type. Output buffer is sized
// to TypeDef.GetSize() and populated according to the type's struct layout (or via
// direct bit-copy for primitive Float/Int/Bool).
bool SerializeSolValueToTypedBytes(const sol::object& Value,
	const FNiagaraTypeDefinition& TypeDef,
	TArray<uint8>& OutBytes,
	FString& OutError);

// Read bytes of a Niagara type back into a Lua value.
sol::object ReadTypedBytesAsSolValue(const uint8* Bytes,
	const FNiagaraTypeDefinition& TypeDef,
	sol::state_view Lua);

// ── Curve data interface populator ──────────────────────────────────────
//
// Given a UNiagaraDataInterface instance (any UNiagaraDataInterfaceCurveBase
// subclass — Curve / ColorCurve / VectorCurve / Vector2DCurve / Vector4Curve)
// and a Lua array of key entries, walks the DI's FRichCurve UPROPERTY fields
// via reflection and populates each according to the field's name:
//
//   Curve                     ← keys[i].value
//   XCurve / YCurve / ZCurve / WCurve
//                             ← keys[i].{x,y,z,w} (accepts upper-case too)
//   RedCurve / GreenCurve / BlueCurve / AlphaCurve
//                             ← keys[i].color.{r,g,b,a}
//                             ← or keys[i].{r,g,b,a} (flat)
//
// Calls UpdateTimeRanges() via virtual dispatch after all curves are populated.
bool ApplyKeysToCurveDI(UNiagaraDataInterface* DI,
	const sol::table& Keys,
	FString& OutError);

// ── Renderer binding refresh ────────────────────────────────────────────
//
// After a Lua table has written RootName on one or more FNiagaraVariableAttributeBinding
// UPROPERTYs of a renderer (via LCD struct handlers), call this to re-derive the
// cached fields (ParamMapVariable/DataSetName/bBindingExistsOnSource/bIsCachedParticleValue)
// and correctly set BindingSourceMode from the namespace of the new RootName.
//
// Walks:
//   - Top-level FNiagaraVariableAttributeBinding fields on the renderer (e.g. PositionBinding)
//     → calls FNiagaraVariableAttributeBinding::SetValue(RootName, OuterEmitterBase, SourceMode)
//   - FNiagaraMaterialAttributeBinding inside MaterialParameters.AttributeBindings arrays
//     → calls FNiagaraMaterialAttributeBinding::CacheValues(OuterEmitter)
void RefreshRendererBindings(UNiagaraRendererProperties* Renderer);

} // namespace NSAINiagaraLCD
