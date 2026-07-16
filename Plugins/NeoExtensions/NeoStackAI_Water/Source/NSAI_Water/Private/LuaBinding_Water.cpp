// Copyright 2025-2026 Betide Studio. All Rights Reserved.

#include "Lua/LuaBindingRegistry.h"
#include "Lua/LuaAssetResolver.h"
#include "Lua/LuaStr.h"
#include "Lua/LuaSubsystem.h"
#include "Lua/LuaPropertyHelper.h"

#include "WaterBodyActor.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyLakeActor.h"
#include "WaterBodyRiverActor.h"
#include "WaterBodyCustomActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanComponent.h"
#include "WaterBodyLakeComponent.h"
#include "WaterBodyRiverComponent.h"
#include "WaterBodyCustomComponent.h"
#include "WaterSplineComponent.h"
#include "WaterSplineMetadata.h"
#include "GerstnerWaterWaves.h"
#include "WaterWaves.h"
#include "WaterMeshComponent.h"
#include "WaterSubsystem.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
#include "WaterTerrainComponent.h"
#endif
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "BakedShallowWaterSimulationComponent.h"
#endif
#include "WaterBodyIslandActor.h"
#include "WaterBodyExclusionVolume.h"
#include "WaterBodyManager.h"
#include "BuoyancyComponent.h"
#include "BuoyancyTypes.h"
#include "WaterZoneActor.h"
#include "WaterBodyHeightmapSettings.h"
#include "WaterBodyWeightmapSettings.h"
#include "WaterBrushEffects.h"
#include "WaterFalloffSettings.h"
#include "WaterRuntimeSettings.h"
#include "WaterEditorSettings.h"
#include "Landscape.h"
#include "LandscapeEditLayer.h"
#include "LandscapeSettings.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Components/SplineComponent.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "AI/Navigation/NavAreaBase.h"
#include "HAL/IConsoleManager.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

// ============================================================================
// HELPERS
// ============================================================================

static UWorld* GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7 && ENGINE_MINOR_VERSION < 8
static bool BakedSimCoversSampleLocation(const FShallowWaterSimulationGrid& SimulationData, const FVector& WorldLocation)
{
	if (!SimulationData.IsValid())
	{
		return false;
	}

	const FVector2D FloatIndex = SimulationData.WorldToFloatIndex(WorldLocation);
	return FloatIndex.X > 0.0
		&& FloatIndex.X < static_cast<double>(SimulationData.NumCells.X - 1)
		&& FloatIndex.Y > 0.0
		&& FloatIndex.Y < static_cast<double>(SimulationData.NumCells.Y - 1);
}
#elif ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
static const UShallowWaterSimulationDataBase* GetBakedShallowWaterData(const UBakedShallowWaterSimulationComponent* Baked)
{
	return Baked ? Baked->BakedSimulationData.Get() : nullptr;
}

static bool BakedSimCoversSampleLocation(const UShallowWaterSimulationDataBase* SimulationData, const FVector& WorldLocation)
{
	if (!SimulationData || !SimulationData->HasValidData())
	{
		return false;
	}

	const FVector2D FloatIndex = SimulationData->WorldToFloatIndex(WorldLocation);
	return FloatIndex.X > 0.0
		&& FloatIndex.X < static_cast<double>(SimulationData->NumCells.X - 1)
		&& FloatIndex.Y > 0.0
		&& FloatIndex.Y < static_cast<double>(SimulationData->NumCells.Y - 1);
}
#endif

static FIntPoint GetWaterZoneRenderTargetResolutionCompat(const AWaterZone* WaterZone)
{
	if (!WaterZone)
	{
		return FIntPoint::ZeroValue;
	}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
	FIntPoint Resolution = FIntPoint::ZeroValue;
	if (const FStructProperty* ResolutionProperty = FindFProperty<FStructProperty>(WaterZone->GetClass(), TEXT("RenderTargetResolution")))
	{
		if (ResolutionProperty->Struct == TBaseStructure<FIntPoint>::Get())
		{
			Resolution = *ResolutionProperty->ContainerPtrToValuePtr<FIntPoint>(WaterZone);
		}
	}

	if (IConsoleVariable* MaxResolutionCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Water.WaterInfo.RenderTargetResolutionMax")))
	{
		const int32 MaxResolution = MaxResolutionCVar->GetInt();
		if (MaxResolution > 0)
		{
			Resolution.X = FMath::Clamp(Resolution.X, 0, MaxResolution);
			Resolution.Y = FMath::Clamp(Resolution.Y, 0, MaxResolution);
		}
	}

	return Resolution;
#else
	return WaterZone->GetRenderTargetResolution();
#endif
}

class FScopedSuppressLandscapeAutoLayerDialog
{
public:
	FScopedSuppressLandscapeAutoLayerDialog()
		: Settings(GetMutableDefault<ULandscapeSettings>())
		, bPreviousValue(Settings ? Settings->bShowDialogForAutomaticLayerCreation : false)
	{
		if (Settings)
		{
			Settings->bShowDialogForAutomaticLayerCreation = false;
		}
	}

	~FScopedSuppressLandscapeAutoLayerDialog()
	{
		if (Settings)
		{
			Settings->bShowDialogForAutomaticLayerCreation = bPreviousValue;
		}
	}

	FScopedSuppressLandscapeAutoLayerDialog(const FScopedSuppressLandscapeAutoLayerDialog&) = delete;
	FScopedSuppressLandscapeAutoLayerDialog& operator=(const FScopedSuppressLandscapeAutoLayerDialog&) = delete;

private:
	ULandscapeSettings* Settings = nullptr;
	bool bPreviousValue = false;
};

static bool TryGetStringOption(const sol::table& Params, const char* PrimaryKey, const char* AliasKey, FString& OutValue)
{
	if (sol::optional<std::string> Value = Params.get<sol::optional<std::string>>(PrimaryKey))
	{
		OutValue = NeoLuaStr::ToFStringOpt(Value);
		return !OutValue.IsEmpty();
	}

	if (AliasKey)
	{
		if (sol::optional<std::string> Value = Params.get<sol::optional<std::string>>(AliasKey))
		{
			OutValue = NeoLuaStr::ToFStringOpt(Value);
			return !OutValue.IsEmpty();
		}
	}

	return false;
}

static bool ResolveWaterLayerDestinationIndex(ALandscape* Landscape, const FString& Placement, int32& OutDestinationIndex, FString& OutError)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	const int32 LayerCount = Landscape ? Landscape->GetEditLayersConst().Num() : 0;
	if (!Landscape || LayerCount <= 0)
	{
		OutError = TEXT("landscape has no edit layers");
		return false;
	}

	FString Normalized = Placement;
	Normalized.TrimStartAndEndInline();
	if (Normalized.IsEmpty())
	{
		OutError = TEXT("empty placement");
		return false;
	}

	if (Normalized.Equals(TEXT("top"), ESearchCase::IgnoreCase))
	{
		OutDestinationIndex = LayerCount - 1;
		return true;
	}

	if (Normalized.Equals(TEXT("bottom"), ESearchCase::IgnoreCase))
	{
		OutDestinationIndex = 0;
		return true;
	}

	if (Normalized.StartsWith(TEXT("index:"), ESearchCase::IgnoreCase))
	{
		int32 OneBasedIndex = 0;
		if (LexTryParseString(OneBasedIndex, *Normalized.RightChop(6)) && OneBasedIndex >= 1)
		{
			OutDestinationIndex = FMath::Clamp(OneBasedIndex - 1, 0, LayerCount - 1);
			return true;
		}

		OutError = FString::Printf(TEXT("invalid index placement '%s'"), *Placement);
		return false;
	}

	auto ResolveRelative = [&](const TCHAR* Prefix, int32 Offset) -> bool
	{
		const int32 PrefixLen = FCString::Strlen(Prefix);
		if (!Normalized.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return false;
		}

		FString ReferenceName = Normalized.RightChop(PrefixLen);
		ReferenceName.TrimStartAndEndInline();
		if (ReferenceName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("missing reference layer in placement '%s'"), *Placement);
			return true;
		}

		const TArray<const ULandscapeEditLayerBase*> EditLayers = Landscape->GetEditLayersConst();
		for (int32 LayerIndex = 0; LayerIndex < EditLayers.Num(); ++LayerIndex)
		{
			if (EditLayers[LayerIndex] && EditLayers[LayerIndex]->GetName().ToString().Equals(ReferenceName, ESearchCase::IgnoreCase))
			{
				OutDestinationIndex = FMath::Clamp(LayerIndex + Offset, 0, LayerCount - 1);
				return true;
			}
		}

		OutError = FString::Printf(TEXT("reference layer '%s' not found"), *ReferenceName);
		return true;
	};

	if (ResolveRelative(TEXT("above:"), 1))
	{
		return OutError.IsEmpty();
	}

	if (ResolveRelative(TEXT("below:"), 0))
	{
		return OutError.IsEmpty();
	}

	OutError = FString::Printf(TEXT("unsupported placement '%s' (use top, bottom, index:N, above:LayerName, below:LayerName)"), *Placement);
	return false;
#else
	// Pre-5.7: ALandscape::GetEditLayersConst() and ULandscapeEditLayerBase don't exist.
	// Landscape layer placement isn't supported on older engines.
	(void)Landscape; (void)Placement; (void)OutDestinationIndex;
	OutError = TEXT("landscape_layer_position requires UE 5.7+");
	return false;
#endif
}

static void ApplyWaterLayerPlacement(UWorld* World, const sol::table& Params, FLuaSessionData& Session, const TCHAR* Context)
{
	FString Placement;
	if (!TryGetStringOption(Params, "landscape_layer_position", "water_layer_position", Placement))
	{
		return;
	}

	if (!World)
	{
		Session.Log(FString::Printf(TEXT("[WARN] %s -> landscape_layer_position ignored: no editor world"), Context));
		return;
	}

	int32 LandscapesAdjusted = 0;
	const FName WaterLayerName(TEXT("Water"));
	for (ALandscape* Landscape : TActorRange<ALandscape>(World))
	{
		if (!Landscape)
		{
			continue;
		}

		const int32 WaterLayerIndex = Landscape->GetLayerIndex(WaterLayerName);
		if (WaterLayerIndex == INDEX_NONE)
		{
			continue;
		}

		int32 DestinationIndex = WaterLayerIndex;
		FString Error;
		if (!ResolveWaterLayerDestinationIndex(Landscape, Placement, DestinationIndex, Error))
		{
			Session.Log(FString::Printf(TEXT("[WARN] %s -> landscape_layer_position '%s' ignored for '%s': %s"), Context, *Placement, *Landscape->GetActorNameOrLabel(), *Error));
			continue;
		}

		if (DestinationIndex != WaterLayerIndex)
		{
			Landscape->ReorderLayer(WaterLayerIndex, DestinationIndex);
		}
		++LandscapesAdjusted;
	}

	if (LandscapesAdjusted == 0)
	{
		Session.Log(FString::Printf(TEXT("[WARN] %s -> landscape_layer_position '%s' requested but no landscape with a Water edit layer was found"), Context, *Placement));
	}
	else
	{
		Session.Log(FString::Printf(TEXT("[OK] %s -> Water landscape layer placement '%s' applied to %d landscape(s)"), Context, *Placement, LandscapesAdjusted));
	}
}

static FVector TableToVector(const sol::table& T)
{
	float X = T["x"].valid() ? T["x"].get<float>() : (T[1].valid() ? T[1].get<float>() : 0.f);
	float Y = T["y"].valid() ? T["y"].get<float>() : (T[2].valid() ? T[2].get<float>() : 0.f);
	float Z = T["z"].valid() ? T["z"].get<float>() : (T[3].valid() ? T[3].get<float>() : 0.f);
	return FVector(X, Y, Z);
}

static sol::table VectorToTable(sol::state_view& Lua, const FVector& V)
{
	sol::table T = Lua.create_table();
	T["x"] = V.X;
	T["y"] = V.Y;
	T["z"] = V.Z;
	return T;
}

static const char* WaterBodyTypeToString(EWaterBodyType Type)
{
	switch (Type)
	{
	case EWaterBodyType::River: return "River";
	case EWaterBodyType::Lake: return "Lake";
	case EWaterBodyType::Ocean: return "Ocean";
	case EWaterBodyType::Transition: return "Custom";
	default: return "Unknown";
	}
}

static EWaterBodyType StringToWaterBodyType(const std::string& Str)
{
	FString S = NeoLuaStr::ToFString(Str);
	if (S.Equals(TEXT("river"), ESearchCase::IgnoreCase)) return EWaterBodyType::River;
	if (S.Equals(TEXT("lake"), ESearchCase::IgnoreCase)) return EWaterBodyType::Lake;
	if (S.Equals(TEXT("ocean"), ESearchCase::IgnoreCase)) return EWaterBodyType::Ocean;
	if (S.Equals(TEXT("custom"), ESearchCase::IgnoreCase)) return EWaterBodyType::Transition;
	return EWaterBodyType::Num; // invalid sentinel
}

static AWaterBody* FindWaterBodyByName(UWorld* World, const FString& NameOrLabel)
{
	if (!World) return nullptr;
	for (TActorIterator<AWaterBody> It(World); It; ++It)
	{
		AWaterBody* WB = *It;
		if (WB->GetActorLabel() == NameOrLabel || WB->GetName() == NameOrLabel || WB->GetActorNameOrLabel() == NameOrLabel)
		{
			return WB;
		}
	}
	return nullptr;
}

static AActor* FindActorWithBuoyancyByName(UWorld* World, const FString& NameOrLabel)
{
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		UBuoyancyComponent* Buoy = Actor->FindComponentByClass<UBuoyancyComponent>();
		if (!Buoy) continue;
		if (Actor->GetActorLabel() == NameOrLabel || Actor->GetName() == NameOrLabel || Actor->GetActorNameOrLabel() == NameOrLabel)
		{
			return Actor;
		}
	}
	return nullptr;
}

static AWaterZone* FindWaterZoneByName(UWorld* World, const FString& NameOrLabel)
{
	if (!World) return nullptr;
	for (TActorIterator<AWaterZone> It(World); It; ++It)
	{
		AWaterZone* WZ = *It;
		if (WZ->GetActorLabel() == NameOrLabel || WZ->GetName() == NameOrLabel || WZ->GetActorNameOrLabel() == NameOrLabel)
		{
			return WZ;
		}
	}
	return nullptr;
}

// ============================================================================
// DOCS
// ============================================================================

static TArray<FLuaFunctionDoc> WaterDocs = {
	{ TEXT("water_list_bodies(type?)"), TEXT("List water body actors — optional type filter: ocean, lake, river, custom"), TEXT("table[]") },
	{ TEXT("water_list_zones()"), TEXT("List all water zones in the editor world"), TEXT("table[]") },
	{ TEXT("water_get_body(name_or_label)"), TEXT("Get detailed info for a specific water body (materials, waves, transition mats, mesh override, spline points, exclusion volumes)"), TEXT("table or nil") },
	{ TEXT("water_spawn(type, location, params?)"), TEXT("Spawn a water body — types: ocean, lake, river, custom. Mirrors UE editor factory: applies WaterSplineDefaults override, default ResetSpline points if no `points` table passed, lake/ocean default WaterWaves duplication, ocean SetCollisionExtents+FillWaterZoneWithOcean, custom SetWaterMeshOverride. params: {label?, points[]?, landscape_layer_position?='top'|'bottom'|'index:N'|'above:Layer'|'below:Layer'}"), TEXT("string or nil") },
	{ TEXT("water_set_waves(actor_name, params)"), TEXT("Configure wave generation on ocean/lake bodies. params: {generator='simple'|'spectrum' (default 'simple'), waves_asset?=path (skips generator construction and assigns asset directly via SetWaterWaves). simple: num_waves, seed, min_wavelength, max_wavelength, min_amplitude, max_amplitude, wind_angle (deg), small_wave_steepness, large_wave_steepness, steepness_falloff, direction_spread (deg), wavelength_falloff, amplitude_falloff, randomness. spectrum: spectrum_type='phillips'|'pierson_moskowitz'|'jonswap', octaves[]={num_waves, amplitude_scale, main_direction, spread_angle, uniform_spread}}"), TEXT("bool") },
	{ TEXT("water_set_material(actor_name, slot, material_path)"), TEXT("Set water material — slots: water, underwater, static_mesh, info, hlod"), TEXT("bool") },
	{ TEXT("water_set_underwater_post_process(actor_name, params)"), TEXT("Configure FUnderwaterPostProcessSettings on a water body. params: {enabled?, priority?, blend_radius?, blend_weight? (0..1)}"), TEXT("bool") },
	{ TEXT("water_set_mesh_override(actor_name, mesh_path)"), TEXT("Set the static mesh override on a water body component (used by Custom water bodies). Pass empty string to clear."), TEXT("bool") },
	{ TEXT("water_set_landscape(actor_name, params)"), TEXT("Configure landscape carving settings for a water body. params: {affects_landscape?, channel_depth?, shape_dilation?, landscape_layer_position?='top'|'bottom'|'index:N'|'above:Layer'|'below:Layer'}"), TEXT("bool") },
	{ TEXT("water_river_add_point(actor_name, location, width?, depth?, velocity?, audio?)"), TEXT("Add a spline point to a river water body (location is world-space, width/depth/velocity/audio per discrete point)"), TEXT("int or nil") },
	{ TEXT("water_river_set_point(actor_name, index, params)"), TEXT("Modify a river spline point by 1-based discrete point index. params: {location?, width?, depth?, velocity?, audio?}"), TEXT("bool") },
	{ TEXT("water_river_get_points(actor_name)"), TEXT("Get all discrete spline points with width/depth/velocity/audio metadata for a river"), TEXT("table[] or nil") },
	{ TEXT("water_river_at_key(actor_name, key)"), TEXT("Read river width and depth at a continuous spline input key (UWaterBodyRiverComponent::GetRiver*AtSplineInputKey)"), TEXT("table {width,depth} or nil") },
	{ TEXT("water_river_set_at_key(actor_name, key, params)"), TEXT("Set width/depth/velocity/audio_intensity at a continuous spline input key. params: {width?, depth?, velocity?, audio?}. Operates via Set*AtSplineInputKey on river/water-body component, not by point index."), TEXT("bool") },
	{ TEXT("water_river_set_transition_material(actor_name, slot, material_path)"), TEXT("Set river-to-lake/ocean transition materials post-spawn. slot: 'lake', 'ocean', or 'both' (in 'both' mode pass material_path as 'lake_path|ocean_path')"), TEXT("bool") },
	{ TEXT("water_zone_set_extent(zone_name, width, height)"), TEXT("Set the extent of a water zone"), TEXT("bool") },
	{ TEXT("water_zone_set_resolution(zone_name, width, height)"), TEXT("Set render target resolution of a water zone"), TEXT("bool") },
	{ TEXT("water_zone_set_far_mesh_material(zone_name, material_path)"), TEXT("Set the far-distance mesh material on a water zone (AWaterZone::SetFarMeshMaterial)"), TEXT("bool") },
	{ TEXT("water_zone_rebuild(zone_name?)"), TEXT("Rebuild water zone(s) — rebuilds all if no name given"), TEXT("bool") },
	{ TEXT("water_ocean_set_flood(height)"), TEXT("Set the ocean flood height via the water subsystem"), TEXT("bool") },
	{ TEXT("water_ocean_get_height()"), TEXT("Get the ocean base height via the water subsystem"), TEXT("number or nil") },
	{ TEXT("water_remove_body(name_or_label)"), TEXT("Remove a water body actor from the level"), TEXT("bool") },
	{ TEXT("water_get_buoyancy(actor_name)"), TEXT("Get buoyancy settings + runtime telemetry (current_water_bodies[], last_surface_info{plane_location,plane_normal,surface_position,water_depth,water_velocity})"), TEXT("table or nil") },
	{ TEXT("water_set_buoyancy(actor_name, params)"), TEXT("Configure buoyancy on an actor's UBuoyancyComponent"), TEXT("bool") },
	{ TEXT("water_add_pontoon(actor_name, params)"), TEXT("Add a pontoon to an actor's buoyancy component"), TEXT("int or nil") },
	{ TEXT("water_remove_pontoon(actor_name, index)"), TEXT("Remove a pontoon by 1-based index from buoyancy component"), TEXT("bool") },
	{ TEXT("water_list_buoyancy()"), TEXT("List all actors with a UBuoyancyComponent"), TEXT("table[]") },
	{ TEXT("water_query_surface(location)"), TEXT("Query water surface info at a world location {x,y,z}"), TEXT("table or nil") },
	{ TEXT("water_create_island(params)"), TEXT("Spawn a water body island with spline points and heightmap/weightmap settings. params: {location, points[], label?, landscape_layer_position?, heightmap={blend_mode, falloff_width, edge_offset, z_offset}, layers={name={falloff_width, edge_offset, opacity, texture_tiling, texture_influence, midpoint}}}"), TEXT("string or nil") },
	{ TEXT("water_create_zone(params)"), TEXT("Spawn a water zone. params: {location, width, height, resolution_x?, resolution_y?, tile_size?, label?}"), TEXT("string or nil") },
	{ TEXT("water_set_landscape_painting(actor_name, params)"), TEXT("Set layer weightmap painting settings on a water body. params: {layers={name={falloff_width, edge_offset, opacity, texture_tiling, texture_influence, midpoint}}}"), TEXT("bool") },
	{ TEXT("water_set_brush_effects(actor_name, params)"), TEXT("Set brush effects on a water body's heightmap. params: {blend_mode?, falloff_width?, edge_offset?, z_offset?, blurring={enabled, radius}, curl_noise={amount1, amount2, tiling1, tiling2}, displacement={height, tiling, midpoint}, terracing={alpha, spacing, smoothness}, smooth_blending={inner, outer}}"), TEXT("bool") },
	{ TEXT("water_ocean_fill_zone(actor_name)"), TEXT("Fill the owning water zone with the ocean mesh (calls FillWaterZoneWithOcean on ocean component)"), TEXT("bool") },
	{ TEXT("water_configure_body(actor_name, params)"), TEXT("Configure water body component knobs. params: {overlap_material_priority? (-8192..8191), target_wave_mask_depth?, max_wave_height_offset?, collision_height_offset?, shape_dilation?, affects_landscape?, landscape_layer_position?='top'|'bottom'|'index:N'|'above:Layer'|'below:Layer', fixed_water_depth?, generate_overlap_events?}"), TEXT("bool") },
	{ TEXT("water_get_zone(name_or_label)"), TEXT("Get detailed info for a specific water zone (extent, resolution, tile size, far mesh material, body list)"), TEXT("table or nil") },
	{ TEXT("water_set_zone_override(actor_name, zone_name_or_nil)"), TEXT("Override which water zone a water body belongs to (nil to clear override)"), TEXT("bool") },
	{ TEXT("water_ocean_get_total_height()"), TEXT("Get the total ocean height (base + flood) via the water subsystem"), TEXT("number or nil") },
	{ TEXT("water_add_exclusion_volume(actor_name, volume_name, mode?)"), TEXT("Authoritative add: appends the body to AWaterBodyExclusionVolume::WaterBodies and recomputes overlaps. mode defaults to 'add_to_exclusion'; pass 'remove_from_exclusion' for inverse-list behavior"), TEXT("bool") },
	{ TEXT("water_remove_exclusion_volume(actor_name, volume_name)"), TEXT("Authoritative remove: drops the body from AWaterBodyExclusionVolume::WaterBodies and recomputes overlaps"), TEXT("bool") },
	{ TEXT("water_list_exclusion_volumes(actor_name)"), TEXT("List exclusion volumes overlapping the body. Each entry: {name, location, mode, water_body_count, authored_water_bodies[]}"), TEXT("table[] or nil") },

	// Island lifecycle (matches water_create_island)
	{ TEXT("water_list_islands()"), TEXT("List AWaterBodyIsland actors in the editor world"), TEXT("table[]") },
	{ TEXT("water_get_island(name_or_label)"), TEXT("Get detailed info for a specific water body island"), TEXT("table or nil") },
	{ TEXT("water_island_configure(name, params)"), TEXT("Re-configure an existing water body island post-spawn. params: {heightmap?, layers?, points?} same shape as water_create_island"), TEXT("bool") },
	{ TEXT("water_remove_island(name_or_label)"), TEXT("Remove a water body island actor from the level"), TEXT("bool") },
	{ TEXT("water_body_add_island(actor_name, island_name)"), TEXT("Register an island with a water body component (UWaterBodyComponent::AddIsland)"), TEXT("bool") },
	{ TEXT("water_body_remove_island(actor_name, island_name)"), TEXT("Unregister an island from a water body component"), TEXT("bool") },
	{ TEXT("water_body_list_islands(actor_name)"), TEXT("List islands registered with a water body component (UWaterBodyComponent::GetIslands)"), TEXT("table[] or nil") },

	// Zone configure consolidated
	{ TEXT("water_zone_configure(zone_name, params)"), TEXT("Configure water zone properties. params: {overlap_priority?, velocity_blur_radius?, capture_z_offset?, half_precision_texture?, auto_include_landscapes_as_terrain?, enable_local_only_tessellation?, local_tessellation_extent?={x,y,z}, water_mesh?={tile_size?, tessellation_factor?, lod_scale?, force_collapse_density_level?, far_distance_mesh_extent?, use_far_mesh_without_ocean?, far_distance_mesh_height_without_ocean?}}"), TEXT("bool") },

	// Nav + spline-key helpers
	{ TEXT("water_set_nav_area(actor_name, class_path)"), TEXT("Set the WaterNavAreaClass on a water body component. class_path is a UClass path to a UNavAreaBase subclass; pass empty string to clear."), TEXT("bool") },
	{ TEXT("water_is_in_exclusion(actor_name, location)"), TEXT("Returns true if the world location is inside one of the body's exclusion volumes"), TEXT("bool") },
	{ TEXT("water_find_spline_key(actor_name, location)"), TEXT("Returns the continuous spline input key closest to the given world location (FindInputKeyClosestToWorldLocation)"), TEXT("number or nil") },

	// Subsystem
	{ TEXT("water_subsystem_info()"), TEXT("Diagnostic info about the water subsystem: shallow_water_enabled, underwater_pp_enabled, water_rendering_enabled, camera_underwater_depth, ocean_base_height, ocean_flood_height, ocean_total_height, water_time_seconds, smoothed_world_time_seconds, underwater_collision_trace_distance, underwater_precise_trace_distance, shallow_water_max_dynamic_forces, shallow_water_max_impulse_forces, shallow_water_render_target_size"), TEXT("table") },
	{ TEXT("water_subsystem_set_wave_time(params)"), TEXT("Override or pause wave time. params: {smoothed_seconds?, override_seconds?, should_override?, pause_wave_time?}"), TEXT("bool") },

	// Baked sim queries
	{ TEXT("water_baked_sim_query(location)"), TEXT("Sample baked shallow water simulation at a world location. Searches all water bodies with a bound UBakedShallowWaterSimulationComponent. Returns {water_height, water_depth, water_velocity={x,y,z}, body_name} or nil if no body has a baked sim covering the location"), TEXT("table or nil") },
	{ TEXT("water_baked_sim_normal(location)"), TEXT("Compute the baked sim normal at a world location"), TEXT("table {x,y,z} or nil") },

	// Terrain inspection (UWaterTerrainComponent — spawn via add_component, configure via configure_component)
	{ TEXT("water_terrain_list()"), TEXT("List actors with a UWaterTerrainComponent"), TEXT("table[]") },
	{ TEXT("water_terrain_get_info(actor_name)"), TEXT("Get terrain bounds + zone override + primitive count for a UWaterTerrainComponent on the actor"), TEXT("table or nil") },
};

// ============================================================================
// BINDING
// ============================================================================

static void BindWater(sol::state& Lua, FLuaSessionData& Session)
{
	// ================================================================
	// water_list_bodies(type?)
	// ================================================================
	Lua.set_function("water_list_bodies", [&Session](sol::optional<std::string> TypeFilterOpt, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_list_bodies -> No editor world"));
			return sol::lua_nil;
		}

		EWaterBodyType FilterType = EWaterBodyType::Num;
		if (TypeFilterOpt.has_value())
		{
			FilterType = StringToWaterBodyType(TypeFilterOpt.value());
			if (FilterType == EWaterBodyType::Num)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_list_bodies -> Unknown type filter '%s'"), UTF8_TO_TCHAR(TypeFilterOpt.value().c_str())));
				return sol::lua_nil;
			}
		}

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			AWaterBody* WB = *It;
			if (!WB) continue;

			EWaterBodyType BodyType = WB->GetWaterBodyType();
			if (FilterType != EWaterBodyType::Num && BodyType != FilterType) continue;

			UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
			UWaterSplineComponent* Spline = WB->GetWaterSpline();

			sol::table Entry = LuaView.create_table();
			Entry["name"] = TCHAR_TO_UTF8(*WB->GetActorNameOrLabel());
			Entry["type"] = WaterBodyTypeToString(BodyType);
			Entry["location"] = VectorToTable(LuaView, WB->GetActorLocation());
			Entry["spline_point_count"] = Spline ? Spline->GetNumberOfSplinePoints() : 0;
			Entry["has_waves"] = Comp ? Comp->HasWaves() : false;
			Entry["water_depth"] = Comp ? static_cast<float>(Comp->GetConstantDepth()) : 0.f;
			Entry["affects_landscape"] = Comp ? Comp->bAffectsLandscape : false;

			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] water_list_bodies -> %d bodies found"), Idx - 1));
		return Result;
	});

	// ================================================================
	// water_list_zones()
	// ================================================================
	Lua.set_function("water_list_zones", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_list_zones -> No editor world"));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (TActorIterator<AWaterZone> It(World); It; ++It)
		{
			AWaterZone* WZ = *It;
			if (!WZ) continue;

			FVector2D Extent = WZ->GetZoneExtent();
			const UWaterMeshComponent* WaterMesh = WZ->GetWaterMeshComponent();

			int32 BodyCount = 0;
			WZ->ForEachWaterBodyComponent([&BodyCount](UWaterBodyComponent*) -> bool
			{
				BodyCount++;
				return true;
			});

			sol::table Entry = LuaView.create_table();
			Entry["name"] = TCHAR_TO_UTF8(*WZ->GetActorNameOrLabel());
			Entry["location"] = VectorToTable(LuaView, WZ->GetActorLocation());

			sol::table ExtentTable = LuaView.create_table();
			ExtentTable["x"] = Extent.X;
			ExtentTable["y"] = Extent.Y;
			Entry["extent"] = ExtentTable;

			Entry["body_count"] = BodyCount;
			Entry["tile_size"] = WaterMesh ? WaterMesh->GetTileSize() : 0.f;
			Entry["tessellation_factor"] = WaterMesh ? WaterMesh->GetTessellationFactor() : 0;

			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] water_list_zones -> %d zones found"), Idx - 1));
		return Result;
	});

	// ================================================================
	// water_get_body(name_or_label)
	// ================================================================
	Lua.set_function("water_get_body", [&Session](const std::string& NameStr, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_get_body -> No editor world"));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_get_body -> Could not find water body '%s'"), *Name));
			return sol::lua_nil;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		UWaterSplineComponent* Spline = WB->GetWaterSpline();
		UWaterSplineMetadata* Meta = WB->GetWaterSplineMetadata();

		sol::table Result = LuaView.create_table();
		Result["name"] = TCHAR_TO_UTF8(*WB->GetActorNameOrLabel());
		Result["type"] = WaterBodyTypeToString(WB->GetWaterBodyType());
		Result["location"] = VectorToTable(LuaView, WB->GetActorLocation());

		// Spline points with metadata
		if (Spline)
		{
			int32 NumPoints = Spline->GetNumberOfSplinePoints();
			sol::table SplinePoints = LuaView.create_table();
			for (int32 i = 0; i < NumPoints; i++)
			{
				sol::table Pt = LuaView.create_table();
				FVector Loc = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
				Pt["x"] = Loc.X;
				Pt["y"] = Loc.Y;
				Pt["z"] = Loc.Z;

				if (Meta)
				{
					Pt["width"] = (Meta->RiverWidth.Points.IsValidIndex(i)) ? Meta->RiverWidth.Points[i].OutVal : 0.f;
					Pt["depth"] = (Meta->Depth.Points.IsValidIndex(i)) ? Meta->Depth.Points[i].OutVal : 0.f;
					Pt["velocity"] = (Meta->WaterVelocityScalar.Points.IsValidIndex(i)) ? Meta->WaterVelocityScalar.Points[i].OutVal : 0.f;
					Pt["audio"] = (Meta->AudioIntensity.Points.IsValidIndex(i)) ? Meta->AudioIntensity.Points[i].OutVal : 0.f;
				}

				SplinePoints[i + 1] = Pt;
			}
			Result["spline_points"] = SplinePoints;
		}

		// Wave info
		{
			sol::table WaveInfo = LuaView.create_table();
			WaveInfo["supported"] = Comp ? Comp->IsWaveSupported() : false;
			WaveInfo["has_waves"] = Comp ? Comp->HasWaves() : false;
			WaveInfo["max_wave_height"] = Comp ? Comp->GetMaxWaveHeight() : 0.f;

			UWaterWavesBase* WavesBase = WB->GetWaterWaves();
			if (WavesBase)
			{
				WaveInfo["asset_path"] = TCHAR_TO_UTF8(*WavesBase->GetPathName());

				// Direct cast handles in-place UGerstnerWaterWaves; if the body holds a
				// UWaterWavesAssetReference (which wraps a UWaterWavesAsset that contains
				// the actual UWaterWaves), follow the indirection so we don't mis-classify
				// asset-referenced Gerstner waves as "custom".
				UGerstnerWaterWaves* Gerstner = Cast<UGerstnerWaterWaves>(WavesBase);
				if (!Gerstner)
				{
					if (UWaterWaves* Inner = WavesBase->GetWaterWaves())
					{
						Gerstner = Cast<UGerstnerWaterWaves>(Inner);
						if (Gerstner)
						{
							WaveInfo["asset_reference"] = true;
						}
					}
				}
				if (Gerstner)
				{
					WaveInfo["type"] = "gerstner";
					WaveInfo["wave_count"] = Gerstner->GetGerstnerWaves().Num();

					if (Gerstner->GerstnerWaveGenerator)
					{
						UGerstnerWaterWaveGeneratorSimple* SimpleGen = Cast<UGerstnerWaterWaveGeneratorSimple>(Gerstner->GerstnerWaveGenerator);
						if (SimpleGen)
						{
							WaveInfo["generator"] = "simple";
							WaveInfo["num_waves"] = SimpleGen->NumWaves;
							WaveInfo["seed"] = SimpleGen->Seed;
							WaveInfo["min_wavelength"] = SimpleGen->MinWavelength;
							WaveInfo["max_wavelength"] = SimpleGen->MaxWavelength;
							WaveInfo["min_amplitude"] = SimpleGen->MinAmplitude;
							WaveInfo["max_amplitude"] = SimpleGen->MaxAmplitude;
							WaveInfo["wind_angle"] = SimpleGen->WindAngleDeg;
							WaveInfo["small_wave_steepness"] = SimpleGen->SmallWaveSteepness;
							WaveInfo["large_wave_steepness"] = SimpleGen->LargeWaveSteepness;
							WaveInfo["steepness_falloff"] = SimpleGen->SteepnessFalloff;
							WaveInfo["direction_spread"] = SimpleGen->DirectionAngularSpreadDeg;
							WaveInfo["wavelength_falloff"] = SimpleGen->WavelengthFalloff;
							WaveInfo["amplitude_falloff"] = SimpleGen->AmplitudeFalloff;
							WaveInfo["randomness"] = SimpleGen->Randomness;
						}
						else
						{
							UGerstnerWaterWaveGeneratorSpectrum* SpectrumGen = Cast<UGerstnerWaterWaveGeneratorSpectrum>(Gerstner->GerstnerWaveGenerator);
							if (SpectrumGen)
							{
								WaveInfo["generator"] = "spectrum";
								WaveInfo["octave_count"] = SpectrumGen->Octaves.Num();

								const char* SpecTypeStr = "phillips";
								if (SpectrumGen->SpectrumType == EWaveSpectrumType::PiersonMoskowitz) SpecTypeStr = "pierson_moskowitz";
								else if (SpectrumGen->SpectrumType == EWaveSpectrumType::JONSWAP) SpecTypeStr = "jonswap";
								WaveInfo["spectrum_type"] = SpecTypeStr;

								sol::table OctavesTable = LuaView.create_table();
								for (int32 OctIdx = 0; OctIdx < SpectrumGen->Octaves.Num(); ++OctIdx)
								{
									const FGerstnerWaveOctave& Oct = SpectrumGen->Octaves[OctIdx];
									sol::table OctEntry = LuaView.create_table();
									OctEntry["num_waves"] = Oct.NumWaves;
									OctEntry["amplitude_scale"] = Oct.AmplitudeScale;
									OctEntry["main_direction"] = Oct.MainDirection;
									OctEntry["spread_angle"] = Oct.SpreadAngle;
									OctEntry["uniform_spread"] = Oct.bUniformSpread;
									OctavesTable[OctIdx + 1] = OctEntry;
								}
								WaveInfo["octaves"] = OctavesTable;
							}
							else
							{
								// Some other UGerstnerWaterWaveGeneratorBase subclass (e.g. a BP-derived generator)
								WaveInfo["generator"] = "custom";
								WaveInfo["generator_class"] = TCHAR_TO_UTF8(*Gerstner->GerstnerWaveGenerator->GetClass()->GetPathName());
							}
						}
					}
				}
				else
				{
					WaveInfo["type"] = "custom";
					WaveInfo["asset_class"] = TCHAR_TO_UTF8(*WavesBase->GetClass()->GetPathName());
				}
			}
			Result["wave_info"] = WaveInfo;
		}

		// Materials
		if (Comp)
		{
			sol::table Mats = LuaView.create_table();
			Mats["water"] = Comp->WaterMaterial ? TCHAR_TO_UTF8(*Comp->WaterMaterial->GetPathName()) : "";
			Mats["underwater"] = Comp->UnderwaterPostProcessMaterial ? TCHAR_TO_UTF8(*Comp->UnderwaterPostProcessMaterial->GetPathName()) : "";
			Mats["static_mesh"] = Comp->WaterStaticMeshMaterial ? TCHAR_TO_UTF8(*Comp->WaterStaticMeshMaterial->GetPathName()) : "";
			Mats["info"] = Comp->WaterInfoMaterial ? TCHAR_TO_UTF8(*Comp->WaterInfoMaterial->GetPathName()) : "";
			Mats["hlod"] = Comp->WaterHLODMaterial ? TCHAR_TO_UTF8(*Comp->WaterHLODMaterial->GetPathName()) : "";
			Result["materials"] = Mats;

			// River-specific transition materials (visible to all body types; empty for non-rivers)
			if (UWaterBodyRiverComponent* RiverComp = Cast<UWaterBodyRiverComponent>(Comp))
			{
				sol::table Trans = LuaView.create_table();
				UMaterialInterface* Lake = RiverComp->GetRiverToLakeTransitionMaterial();
				UMaterialInterface* Ocean = RiverComp->GetRiverToOceanTransitionMaterial();
				Trans["lake"] = Lake ? TCHAR_TO_UTF8(*Lake->GetPathName()) : "";
				Trans["ocean"] = Ocean ? TCHAR_TO_UTF8(*Ocean->GetPathName()) : "";
				Result["transition_materials"] = Trans;
			}

			// Static mesh override (used by Custom water bodies; empty for others unless user set one)
			UStaticMesh* MeshOverride = Comp->GetWaterMeshOverride();
			Result["mesh_override"] = MeshOverride ? TCHAR_TO_UTF8(*MeshOverride->GetPathName()) : "";

			Result["affects_landscape"] = Comp->bAffectsLandscape;
			Result["overlap_priority"] = Comp->GetOverlapMaterialPriority();
			Result["water_depth"] = static_cast<float>(Comp->GetConstantDepth());
			Result["shape_dilation"] = Comp->ShapeDilation;
			Result["target_wave_mask_depth"] = Comp->TargetWaveMaskDepth;
			Result["max_wave_height_offset"] = Comp->MaxWaveHeightOffset;
			Result["collision_height_offset"] = Comp->CollisionHeightOffset;
			Result["generate_overlap_events"] = Comp->GetGenerateOverlapEvents();
			Result["water_body_index"] = Comp->GetWaterBodyIndex();
			Result["channel_depth"] = Comp->GetChannelDepth();
			Result["max_wave_height"] = Comp->GetMaxWaveHeight();

			// Owning water zone (resolved name; empty if none)
			if (AWaterZone* OwningZone = Comp->GetWaterZone())
			{
				Result["owning_water_zone"] = TCHAR_TO_UTF8(*OwningZone->GetActorNameOrLabel());
			}
			else
			{
				Result["owning_water_zone"] = "";
			}

			// Underwater post-process settings (the FUnderwaterPostProcessSettings struct)
			sol::table Underwater = LuaView.create_table();
			Underwater["enabled"] = Comp->UnderwaterPostProcessSettings.bEnabled;
			Underwater["priority"] = Comp->UnderwaterPostProcessSettings.Priority;
			Underwater["blend_radius"] = Comp->UnderwaterPostProcessSettings.BlendRadius;
			Underwater["blend_weight"] = Comp->UnderwaterPostProcessSettings.BlendWeight;
			Result["underwater_pp_settings"] = Underwater;

			// Curve / heightmap / weightmap settings — round-trip read of what
			// water_set_landscape / water_set_brush_effects / water_set_landscape_painting write.
			sol::table CurveOut = LuaView.create_table();
			CurveOut["channel_depth"] = Comp->CurveSettings.ChannelDepth;
			CurveOut["channel_edge_offset"] = Comp->CurveSettings.ChannelEdgeOffset;
			CurveOut["use_curve_channel"] = Comp->CurveSettings.bUseCurveChannel;
			CurveOut["curve_ramp_width"] = Comp->CurveSettings.CurveRampWidth;
			Result["curve_settings"] = CurveOut;

			sol::table HMOut = LuaView.create_table();
			const FWaterBodyHeightmapSettings& HM = Comp->WaterHeightmapSettings;
			const char* BlendModeStr = "alpha_blend";
			switch (HM.BlendMode)
			{
			case EWaterBrushBlendType::Min: BlendModeStr = "min"; break;
			case EWaterBrushBlendType::Max: BlendModeStr = "max"; break;
			case EWaterBrushBlendType::Additive: BlendModeStr = "additive"; break;
			default: break;
			}
			HMOut["blend_mode"] = BlendModeStr;
			HMOut["falloff_width"] = HM.FalloffSettings.FalloffWidth;
			HMOut["edge_offset"] = HM.FalloffSettings.EdgeOffset;
			HMOut["z_offset"] = HM.FalloffSettings.ZOffset;
			Result["heightmap_settings"] = HMOut;

			sol::table LayersOut = LuaView.create_table();
			for (const auto& KV : Comp->LayerWeightmapSettings)
			{
				sol::table Layer = LuaView.create_table();
				const FWaterBodyWeightmapSettings& WS = KV.Value;
				Layer["falloff_width"] = WS.FalloffWidth;
				Layer["edge_offset"] = WS.EdgeOffset;
				Layer["opacity"] = WS.FinalOpacity;
				Layer["texture_tiling"] = WS.TextureTiling;
				Layer["texture_influence"] = WS.TextureInfluence;
				Layer["midpoint"] = WS.Midpoint;
				LayersOut[TCHAR_TO_UTF8(*KV.Key.ToString())] = Layer;
			}
			Result["layer_weightmap_settings"] = LayersOut;

			// Nav area class — protected UPROPERTY, reflection read
			Result["nav_area_class"] = "";
			if (FProperty* NavProp = Comp->GetClass()->FindPropertyByName(TEXT("WaterNavAreaClass")))
			{
				if (FClassProperty* ClassProp = CastField<FClassProperty>(NavProp))
				{
					UObject* RawClass = ClassProp->GetObjectPropertyValue_InContainer(Comp);
					if (UClass* NavClass = Cast<UClass>(RawClass))
					{
						Result["nav_area_class"] = TCHAR_TO_UTF8(*NavClass->GetPathName());
					}
				}
			}

			// Baked shallow water sim binding info
			sol::table BakedSim = LuaView.create_table();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			UBakedShallowWaterSimulationComponent* Baked = Comp->GetBakedShallowWaterSimulation();
			BakedSim["bound"] = (Baked != nullptr);
			BakedSim["use_for_queries"] = Comp->UseBakedSimulationForQueriesAndPhysics();
			if (Baked)
			{
				BakedSim["component"] = TCHAR_TO_UTF8(*Baked->GetPathName());
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
				const UShallowWaterSimulationDataBase* SimulationData = GetBakedShallowWaterData(Baked);
				BakedSim["valid"] = SimulationData && SimulationData->HasValidData();
				if (SimulationData && SimulationData->HasValidData())
				{
					BakedSim["num_cells_x"] = SimulationData->NumCells.X;
					BakedSim["num_cells_y"] = SimulationData->NumCells.Y;
				}
#else
				BakedSim["valid"] = Baked->SimulationData.IsValid();
				if (Baked->SimulationData.IsValid())
				{
					BakedSim["num_cells_x"] = Baked->SimulationData.NumCells.X;
					BakedSim["num_cells_y"] = Baked->SimulationData.NumCells.Y;
				}
#endif
			}
#else
			BakedSim["bound"] = false;
			BakedSim["use_for_queries"] = false;
#endif
			Result["baked_sim"] = BakedSim;

			// FixedWaterDepth (protected UPROPERTY — reflection read)
			if (FProperty* FWDProp = Comp->GetClass()->FindPropertyByName(TEXT("FixedWaterDepth")))
			{
				const double* FWD = FWDProp->ContainerPtrToValuePtr<double>(Comp);
				Result["fixed_water_depth"] = FWD ? *FWD : 0.0;
			}
		}

		Session.Log(FString::Printf(TEXT("[OK] water_get_body -> '%s' (%s)"), *Name, UTF8_TO_TCHAR(WaterBodyTypeToString(WB->GetWaterBodyType()))));
		return Result;
	});

	// ================================================================
	// water_spawn(type, location, params?)
	// ================================================================
	Lua.set_function("water_spawn", [&Session](const std::string& TypeStr, const sol::table& LocationTable, sol::optional<sol::table> ParamsOpt, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_spawn -> No editor world. Open a level in the editor first."));
			return sol::lua_nil;
		}

		EWaterBodyType BodyType = StringToWaterBodyType(TypeStr);
		if (BodyType == EWaterBodyType::Num)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_spawn -> Invalid type '%s'. Use one of: ocean, lake, river, custom."), UTF8_TO_TCHAR(TypeStr.c_str())));
			return sol::lua_nil;
		}

		FVector Location = TableToVector(LocationTable);
		FTransform SpawnTransform(FRotator::ZeroRotator, Location);

		UClass* SpawnClass = nullptr;
		switch (BodyType)
		{
		case EWaterBodyType::Ocean: SpawnClass = AWaterBodyOcean::StaticClass(); break;
		case EWaterBodyType::Lake: SpawnClass = AWaterBodyLake::StaticClass(); break;
		case EWaterBodyType::River: SpawnClass = AWaterBodyRiver::StaticClass(); break;
		case EWaterBodyType::Transition: SpawnClass = AWaterBodyCustom::StaticClass(); break;
		default: break;
		}

		if (!SpawnClass)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_spawn -> Could not determine spawn class for type '%s'. Use one of: ocean, lake, river, custom."), UTF8_TO_TCHAR(TypeStr.c_str())));
			return sol::lua_nil;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Spawn Water Body")));
		FScopedSuppressLandscapeAutoLayerDialog LandscapeLayerDialogGuard;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AWaterBody* NewActor = World->SpawnActor<AWaterBody>(SpawnClass, SpawnTransform, SpawnParams);
		if (!NewActor)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_spawn -> SpawnActor returned null for class '%s' in the editor world. Verify the editor world is active and the type maps to a valid water actor class."), *SpawnClass->GetName()));
			return sol::lua_nil;
		}

		sol::table P = ParamsOpt.has_value() ? ParamsOpt.value() : LuaView.create_table();

		// Set label if provided
		sol::optional<std::string> LabelOpt = P.get<sol::optional<std::string>>("label");
		if (LabelOpt.has_value())
		{
			NewActor->SetActorLabel(NeoLuaStr::ToFStringOpt(LabelOpt));
		}

		const UWaterEditorSettings* EditorSettings = GetDefault<UWaterEditorSettings>();
		UWaterBodyComponent* Comp = NewActor->GetWaterBodyComponent();
		UWaterSplineComponent* Spline = NewActor->GetWaterSpline();

		// Lookup defaults block for this body type (matches UWaterBodyActorFactory subclass overrides)
		const FWaterBodyDefaults* WaterBodyDefaults = nullptr;
		const FWaterBrushActorDefaults* BrushDefaults = nullptr;
		switch (BodyType)
		{
		case EWaterBodyType::River:
			WaterBodyDefaults = &EditorSettings->WaterBodyRiverDefaults;
			BrushDefaults = &EditorSettings->WaterBodyRiverDefaults.BrushDefaults;
			break;
		case EWaterBodyType::Lake:
			WaterBodyDefaults = &EditorSettings->WaterBodyLakeDefaults;
			BrushDefaults = &EditorSettings->WaterBodyLakeDefaults.BrushDefaults;
			break;
		case EWaterBodyType::Ocean:
			WaterBodyDefaults = &EditorSettings->WaterBodyOceanDefaults;
			BrushDefaults = &EditorSettings->WaterBodyOceanDefaults.BrushDefaults;
			break;
		case EWaterBodyType::Transition:
			WaterBodyDefaults = &EditorSettings->WaterBodyCustomDefaults;
			break;
		default: break;
		}

		// Brush defaults — mirror UWaterBodyActorFactory::PostSpawnActor (line 37-40)
		if (Comp && BrushDefaults)
		{
			Comp->CurveSettings = BrushDefaults->CurveSettings;
			Comp->WaterHeightmapSettings = BrushDefaults->HeightmapSettings;
			Comp->LayerWeightmapSettings = BrushDefaults->LayerWeightmapSettings;
		}

		// Water body defaults — materials + WaterSplineDefaults override
		// (mirrors UWaterBodyActorFactory::PostSpawnActor line 42-56). Skip the
		// WaterSplineDefaults override when the owning class is a BP-generated
		// subclass — see ShouldOverrideWaterSplineDefaults at WaterBodyActorFactory.cpp:69.
		if (Comp && WaterBodyDefaults)
		{
			Comp->SetWaterMaterial(WaterBodyDefaults->GetWaterMaterial());
			Comp->SetWaterStaticMeshMaterial(WaterBodyDefaults->GetWaterStaticMeshMaterial());
			Comp->SetHLODMaterial(WaterBodyDefaults->GetWaterHLODMaterial());
			Comp->SetUnderwaterPostProcessMaterial(WaterBodyDefaults->GetUnderwaterPostProcessMaterial());

			if (Spline && NewActor->GetClass()->ClassGeneratedBy == nullptr)
			{
				Spline->WaterSplineDefaults = WaterBodyDefaults->SplineDefaults;
			}
		}

		// Water Info Material comes from runtime settings (critical for Water Info Texture masking)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		if (Comp)
		{
			Comp->SetWaterInfoMaterial(GetDefault<UWaterRuntimeSettings>()->GetDefaultWaterInfoMaterial());
		}
#endif

		// River-specific transition materials (mirrors UWaterBodyRiverActorFactory line 102-103)
		if (BodyType == EWaterBodyType::River && Comp)
		{
			if (UWaterBodyRiverComponent* RiverComp = Cast<UWaterBodyRiverComponent>(Comp))
			{
				RiverComp->SetLakeTransitionMaterial(EditorSettings->WaterBodyRiverDefaults.GetRiverToLakeTransitionMaterial());
				RiverComp->SetOceanTransitionMaterial(EditorSettings->WaterBodyRiverDefaults.GetRiverToOceanTransitionMaterial());
			}
		}

		// Custom-only: SetWaterMeshOverride from defaults (mirrors UWaterBodyCustomActorFactory line 215)
		if (BodyType == EWaterBodyType::Transition && Comp)
		{
			Comp->SetWaterMeshOverride(EditorSettings->WaterBodyCustomDefaults.GetWaterMesh());
		}

		// Spline points — caller-supplied or factory default
		// Engine factories call ResetSpline with type-specific defaults (river: 3 pts,
		// lake: 3 pts, ocean: 4 corners, custom: single origin point). When the caller
		// passes `points`, honor that exactly; otherwise reproduce the factory defaults.
		sol::optional<sol::table> PointsOpt = P.get<sol::optional<sol::table>>("points");
		if (Spline)
		{
			if (PointsOpt.has_value())
			{
				sol::table Points = PointsOpt.value();
				Spline->ClearSplinePoints(false);
				for (auto& Kv : Points)
				{
					sol::optional<sol::table> PtOpt = Kv.second.as<sol::optional<sol::table>>();
					if (PtOpt.has_value())
					{
						FVector PtLoc = TableToVector(PtOpt.value());
						FVector LocalPt = NewActor->GetActorTransform().InverseTransformPosition(PtLoc);
						Spline->AddSplinePoint(LocalPt, ESplineCoordinateSpace::Local, false);
					}
				}
				Spline->UpdateSpline();
				Spline->K2_SynchronizeAndBroadcastDataChange();
			}
			else
			{
#if WITH_EDITOR
				// Match UE editor factory ResetSpline calls (WaterBodyActorFactory.cpp lines 106, 144, 192, 218)
				switch (BodyType)
				{
				case EWaterBodyType::River:
					Spline->ResetSpline({ FVector(0, 0, 0), FVector(5000, 0, 0), FVector(10000, 5000, 0) });
					break;
				case EWaterBodyType::Lake:
					Spline->ResetSpline({ FVector(0, 0, 0), FVector(7000, -3000, 0), FVector(6500, 6500, 0) });
					break;
				case EWaterBodyType::Ocean:
					Spline->ResetSpline({ FVector(10000, -10000, 0), FVector(10000, 10000, 0), FVector(-10000, 10000, 0), FVector(-10000, -10000, 0) });
					break;
				case EWaterBodyType::Transition:
					Spline->ResetSpline({ FVector(0, 0, 0) });
					break;
				default: break;
				}
#endif
			}
		}

		if (Comp)
		{
			// Lake/Ocean: duplicate the default WaterWaves asset onto the actor so the
			// new body has waves out of the gate (mirrors WaterBodyOceanActorFactory line 137-141
			// and WaterBodyLakeActorFactory line 185-189). Skip if the caller will set
			// waves explicitly via water_set_waves; this just provides a non-empty default.
			if (BodyType == EWaterBodyType::Ocean)
			{
				if (const UWaterWavesBase* DefaultWaves = EditorSettings->WaterBodyOceanDefaults.WaterWaves)
				{
					UWaterWavesBase* DupWaves = DuplicateObject(DefaultWaves, NewActor, MakeUniqueObjectName(NewActor, DefaultWaves->GetClass(), TEXT("OceanWaterWaves")));
					NewActor->SetWaterWaves(DupWaves);
				}
			}
			else if (BodyType == EWaterBodyType::Lake)
			{
				if (const UWaterWavesBase* DefaultWaves = EditorSettings->WaterBodyLakeDefaults.WaterWaves)
				{
					UWaterWavesBase* DupWaves = DuplicateObject(DefaultWaves, NewActor, MakeUniqueObjectName(NewActor, DefaultWaves->GetClass(), TEXT("LakeWaterWaves")));
					NewActor->SetWaterWaves(DupWaves);
				}
			}

			// If spawned into a zone with local-only tessellation, enable static meshes
			if (const AWaterZone* WaterZone = Comp->GetWaterZone())
			{
				if (WaterZone->IsLocalOnlyTessellationEnabled())
				{
					Comp->SetWaterBodyStaticMeshEnabled(true);
				}
			}

			// Ocean: size collision extents to the owning zone and fill it
			// (mirrors WaterBodyOceanActorFactory line 146-154)
#if WITH_EDITOR
			if (BodyType == EWaterBodyType::Ocean)
			{
				if (UWaterBodyOceanComponent* OceanComp = Cast<UWaterBodyOceanComponent>(Comp))
				{
					if (const AWaterZone* OwningZone = Comp->GetWaterZone())
					{
						const double ExistingZ = OceanComp->GetCollisionExtents().Z;
						OceanComp->SetCollisionExtents(FVector(OwningZone->GetZoneExtent() / 2.0, ExistingZ));
						OceanComp->FillWaterZoneWithOcean();
					}
				}
			}
#endif

			// Register with water zones, build render data, and trigger full rebuild
			Comp->UpdateWaterZones();

			FOnWaterBodyChangedParams Params;
			Params.bShapeOrPositionChanged = true;
			Params.bWeightmapSettingsChanged = true;
			Params.bUserTriggered = true;
			Comp->UpdateAll(Params);

			// Create/update info mesh components (renders water body shape into Water Info Texture)
			Comp->UpdateWaterBodyRenderData();

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif
		}
		ApplyWaterLayerPlacement(World, P, Session, TEXT("water_spawn"));

		FString ActorName = NewActor->GetActorNameOrLabel();
		Session.Log(FString::Printf(TEXT("[OK] water_spawn -> Spawned %s water body '%s'"), UTF8_TO_TCHAR(WaterBodyTypeToString(BodyType)), *ActorName));
		return sol::make_object(LuaView, std::string(TCHAR_TO_UTF8(*ActorName)));
	});

	// ================================================================
	// water_set_waves(actor_name, params)
	// ================================================================
	Lua.set_function("water_set_waves", [&Session](const std::string& NameStr, const sol::table& Params, sol::this_state S) -> bool
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_set_waves -> No editor world. Open a level in the editor first."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_waves -> Could not find water body '%s'. List candidates with water_list_bodies()."), *Name));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp || !Comp->IsWaveSupported())
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_waves -> Water body '%s' does not support waves (only ocean and lake bodies do)"), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Waves")));
		WB->Modify();

		// Path A: caller passed an existing wave asset path — assign directly, skipping
		// in-place generator construction. Engine SetWaterWaves accepts any UWaterWavesBase
		// (WaterBodyActor.cpp:219), but Content Browser assets are usually stored as
		// UWaterWavesAsset (NOT a UWaterWavesBase subclass — see WaterWaves.h:54-92).
		// Try the direct subclass first, then fall back to wrapping a UWaterWavesAsset
		// in a fresh UWaterWavesAssetReference so the body owns the bridge object.
		sol::optional<std::string> AssetOpt = Params.get<sol::optional<std::string>>("waves_asset");
		if (AssetOpt.has_value() && !AssetOpt.value().empty())
		{
			FString AssetPath = NeoLuaStr::ToFStringOpt(AssetOpt);
			UWaterWavesBase* WavesToAssign = NeoLuaAsset::Resolve<UWaterWavesBase>(AssetPath);
			if (!WavesToAssign)
			{
				if (UWaterWavesAsset* AssetWrapper = NeoLuaAsset::Resolve<UWaterWavesAsset>(AssetPath))
				{
					UWaterWavesAssetReference* RefObject = NewObject<UWaterWavesAssetReference>(WB, NAME_None, RF_Transactional);
					RefObject->SetWaterWavesAsset(AssetWrapper);
					WavesToAssign = RefObject;
				}
			}
			if (!WavesToAssign)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_set_waves -> Could not load wave asset '%s' (tried UWaterWavesBase and UWaterWavesAsset). Check the path or use create_asset()."), *AssetPath));
				return false;
			}
			WB->SetWaterWaves(WavesToAssign);

			FOnWaterBodyChangedParams AssetChanged;
			AssetChanged.bUserTriggered = true;
			Comp->UpdateAll(AssetChanged);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif
			Session.Log(FString::Printf(TEXT("[OK] water_set_waves -> Assigned waves asset '%s' to '%s'"), *AssetPath, *Name));
			return true;
		}

		// Path B/C: build/configure UGerstnerWaterWaves in-place. Generator type defaults to "simple".
		UGerstnerWaterWaves* Waves = Cast<UGerstnerWaterWaves>(WB->GetWaterWaves());
		if (!Waves)
		{
			Waves = NewObject<UGerstnerWaterWaves>(WB, NAME_None, RF_Transactional);
			WB->SetWaterWaves(Waves);
		}
		Waves->Modify();

		FString GeneratorStr = TEXT("simple");
		sol::optional<std::string> GenOpt = Params.get<sol::optional<std::string>>("generator");
		if (GenOpt.has_value()) GeneratorStr = NeoLuaStr::ToFStringOpt(GenOpt);

		if (GeneratorStr.Equals(TEXT("spectrum"), ESearchCase::IgnoreCase))
		{
			// Path C: spectrum generator
			UGerstnerWaterWaveGeneratorSpectrum* SpecGen = Cast<UGerstnerWaterWaveGeneratorSpectrum>(Waves->GerstnerWaveGenerator);
			if (!SpecGen)
			{
				SpecGen = NewObject<UGerstnerWaterWaveGeneratorSpectrum>(Waves, NAME_None, RF_Transactional);
				Waves->GerstnerWaveGenerator = SpecGen;
			}
			SpecGen->Modify();

			sol::optional<std::string> SpecTypeOpt = Params.get<sol::optional<std::string>>("spectrum_type");
			if (SpecTypeOpt.has_value())
			{
				FString SpecType = NeoLuaStr::ToFStringOpt(SpecTypeOpt);
				if (SpecType.Equals(TEXT("phillips"), ESearchCase::IgnoreCase))                SpecGen->SpectrumType = EWaveSpectrumType::Phillips;
				else if (SpecType.Equals(TEXT("pierson_moskowitz"), ESearchCase::IgnoreCase)) SpecGen->SpectrumType = EWaveSpectrumType::PiersonMoskowitz;
				else if (SpecType.Equals(TEXT("jonswap"), ESearchCase::IgnoreCase))            SpecGen->SpectrumType = EWaveSpectrumType::JONSWAP;
				else
				{
					Session.Log(FString::Printf(TEXT("[FAIL] water_set_waves -> Unknown spectrum_type '%s'. Use 'phillips', 'pierson_moskowitz', or 'jonswap'."), *SpecType));
					return false;
				}
			}

			sol::optional<sol::table> OctavesOpt = Params.get<sol::optional<sol::table>>("octaves");
			if (OctavesOpt.has_value())
			{
				SpecGen->Octaves.Reset();
				for (auto& Kv : OctavesOpt.value())
				{
					sol::optional<sol::table> OctOpt = Kv.second.as<sol::optional<sol::table>>();
					if (!OctOpt.has_value()) continue;
					sol::table O = OctOpt.value();
					FGerstnerWaveOctave Oct;
					Oct.NumWaves = static_cast<int32>(O.get_or("num_waves", Oct.NumWaves));
					Oct.AmplitudeScale = static_cast<float>(O.get_or("amplitude_scale", static_cast<double>(Oct.AmplitudeScale)));
					Oct.MainDirection = static_cast<float>(O.get_or("main_direction", static_cast<double>(Oct.MainDirection)));
					Oct.SpreadAngle = static_cast<float>(O.get_or("spread_angle", static_cast<double>(Oct.SpreadAngle)));
					sol::optional<bool> UniformOpt = O.get<sol::optional<bool>>("uniform_spread");
					if (UniformOpt.has_value()) Oct.bUniformSpread = UniformOpt.value();
					SpecGen->Octaves.Add(Oct);
				}
			}

			Waves->RecomputeWaves(true);

			FOnWaterBodyChangedParams ChangedParams;
			ChangedParams.bUserTriggered = true;
			Comp->UpdateAll(ChangedParams);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif

			Session.Log(FString::Printf(TEXT("[OK] water_set_waves -> Configured spectrum generator (%d octaves) on '%s'"), SpecGen->Octaves.Num(), *Name));
			return true;
		}

		if (!GeneratorStr.Equals(TEXT("simple"), ESearchCase::IgnoreCase))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_waves -> Unknown generator '%s'. Use 'simple', 'spectrum', or pass waves_asset=path."), *GeneratorStr));
			return false;
		}

		// Path B: simple generator (default)
		UGerstnerWaterWaveGeneratorSimple* Gen = Cast<UGerstnerWaterWaveGeneratorSimple>(Waves->GerstnerWaveGenerator);
		if (!Gen)
		{
			Gen = NewObject<UGerstnerWaterWaveGeneratorSimple>(Waves, NAME_None, RF_Transactional);
			Waves->GerstnerWaveGenerator = Gen;
		}

		Gen->Modify();

		Gen->NumWaves = static_cast<int32>(Params.get_or("num_waves", Gen->NumWaves));
		Gen->Seed = static_cast<int32>(Params.get_or("seed", Gen->Seed));
		Gen->MinWavelength = static_cast<float>(Params.get_or("min_wavelength", static_cast<double>(Gen->MinWavelength)));
		Gen->MaxWavelength = static_cast<float>(Params.get_or("max_wavelength", static_cast<double>(Gen->MaxWavelength)));
		Gen->MinAmplitude = static_cast<float>(Params.get_or("min_amplitude", static_cast<double>(Gen->MinAmplitude)));
		Gen->MaxAmplitude = static_cast<float>(Params.get_or("max_amplitude", static_cast<double>(Gen->MaxAmplitude)));
		Gen->WindAngleDeg = static_cast<float>(Params.get_or("wind_angle", static_cast<double>(Gen->WindAngleDeg)));
		Gen->SmallWaveSteepness = static_cast<float>(Params.get_or("small_wave_steepness", static_cast<double>(Gen->SmallWaveSteepness)));
		Gen->LargeWaveSteepness = static_cast<float>(Params.get_or("large_wave_steepness", static_cast<double>(Gen->LargeWaveSteepness)));
		Gen->SteepnessFalloff = static_cast<float>(Params.get_or("steepness_falloff", static_cast<double>(Gen->SteepnessFalloff)));
		Gen->DirectionAngularSpreadDeg = static_cast<float>(Params.get_or("direction_spread", static_cast<double>(Gen->DirectionAngularSpreadDeg)));
		Gen->WavelengthFalloff = static_cast<float>(Params.get_or("wavelength_falloff", static_cast<double>(Gen->WavelengthFalloff)));
		Gen->AmplitudeFalloff = static_cast<float>(Params.get_or("amplitude_falloff", static_cast<double>(Gen->AmplitudeFalloff)));
		Gen->Randomness = static_cast<float>(Params.get_or("randomness", static_cast<double>(Gen->Randomness)));

		Waves->RecomputeWaves(true);

		FOnWaterBodyChangedParams ChangedParams;
		ChangedParams.bUserTriggered = true;
		Comp->UpdateAll(ChangedParams);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif

		Session.Log(FString::Printf(TEXT("[OK] water_set_waves -> Configured %d simple waves on '%s'"), Gen->NumWaves, *Name));
		return true;
	});

	// ================================================================
	// water_set_material(actor_name, slot, material_path)
	// ================================================================
	Lua.set_function("water_set_material", [&Session](const std::string& NameStr, const std::string& SlotStr, const std::string& MatPathStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_set_material -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_material -> Could not find water body '%s'"), *Name));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_material -> Water body '%s' has no component"), *Name));
			return false;
		}

		FString MatPath = NeoLuaStr::ToFString(MatPathStr);
		UMaterialInterface* Material = NeoLuaAsset::Resolve<UMaterialInterface>(MatPath);
		if (!Material)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_material -> Could not load material '%s'"), *MatPath));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Material")));
		Comp->Modify();

		FString Slot = NeoLuaStr::ToFString(SlotStr);
		if (Slot.Equals(TEXT("water"), ESearchCase::IgnoreCase))
		{
			Comp->SetWaterMaterial(Material);
		}
		else if (Slot.Equals(TEXT("underwater"), ESearchCase::IgnoreCase))
		{
			Comp->SetUnderwaterPostProcessMaterial(Material);
		}
		else if (Slot.Equals(TEXT("static_mesh"), ESearchCase::IgnoreCase))
		{
			Comp->SetWaterStaticMeshMaterial(Material);
		}
		else if (Slot.Equals(TEXT("info"), ESearchCase::IgnoreCase))
		{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			Comp->SetWaterInfoMaterial(Material);
#else
			Session.Log(TEXT("[FAIL] water_set_material -> 'info' slot requires UE 5.6+"));
			return false;
#endif
		}
		else if (Slot.Equals(TEXT("hlod"), ESearchCase::IgnoreCase))
		{
			Comp->SetHLODMaterial(Material);
		}
		else
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_material -> Unknown slot '%s'. Use 'water','underwater','static_mesh','info','hlod'."), *Slot));
			return false;
		}

		// Rebuild render data (info mesh components, static mesh components) with the new material
		Comp->UpdateWaterBodyRenderData();

		// Mark the zone for a full rebuild (water info texture + water mesh) so the new material takes effect
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif

		Session.Log(FString::Printf(TEXT("[OK] water_set_material -> Set '%s' material on '%s'"), *Slot, *Name));
		return true;
	});

	// ================================================================
	// water_set_landscape(actor_name, params)
	// ================================================================
	Lua.set_function("water_set_landscape", [&Session](const std::string& NameStr, const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_set_landscape -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_landscape -> Could not find water body '%s'"), *Name));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_landscape -> Water body '%s' has no component"), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Landscape Settings")));
		FScopedSuppressLandscapeAutoLayerDialog LandscapeLayerDialogGuard;
		Comp->Modify();

		sol::optional<bool> AffectsOpt = Params.get<sol::optional<bool>>("affects_landscape");
		if (AffectsOpt.has_value())
		{
			Comp->bAffectsLandscape = AffectsOpt.value();
		}

		sol::optional<double> ChannelDepthOpt = Params.get<sol::optional<double>>("channel_depth");
		if (ChannelDepthOpt.has_value())
		{
			Comp->CurveSettings.ChannelDepth = static_cast<float>(ChannelDepthOpt.value());
		}

		sol::optional<double> ShapeDilationOpt = Params.get<sol::optional<double>>("shape_dilation");
		if (ShapeDilationOpt.has_value())
		{
			Comp->ShapeDilation = static_cast<float>(ShapeDilationOpt.value());
		}

		FOnWaterBodyChangedParams ChangedParams;
		ChangedParams.bShapeOrPositionChanged = true;
		ChangedParams.bWeightmapSettingsChanged = true;
		ChangedParams.bUserTriggered = true;
		Comp->UpdateAll(ChangedParams);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif
		ApplyWaterLayerPlacement(World, Params, Session, TEXT("water_set_landscape"));

		Session.Log(FString::Printf(TEXT("[OK] water_set_landscape -> Updated landscape settings on '%s'"), *Name));
		return true;
	});

	// ================================================================
	// water_river_add_point(actor_name, location, width?, depth?, velocity?)
	// ================================================================
	Lua.set_function("water_river_add_point", [&Session](const std::string& NameStr, const sol::table& LocationTable, sol::optional<double> WidthOpt, sol::optional<double> DepthOpt, sol::optional<double> VelocityOpt, sol::optional<double> AudioOpt, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_river_add_point -> No editor world"));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_add_point -> Could not find water body '%s'"), *Name));
			return sol::lua_nil;
		}

		if (WB->GetWaterBodyType() != EWaterBodyType::River)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_add_point -> '%s' is not a river"), *Name));
			return sol::lua_nil;
		}

		UWaterSplineComponent* Spline = WB->GetWaterSpline();
		UWaterSplineMetadata* Meta = WB->GetWaterSplineMetadata();
		if (!Spline)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_add_point -> '%s' has no spline"), *Name));
			return sol::lua_nil;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Add River Spline Point")));
		WB->Modify();
		Spline->Modify();

		FVector WorldLoc = TableToVector(LocationTable);
		FVector LocalLoc = WB->GetActorTransform().InverseTransformPosition(WorldLoc);
		Spline->AddSplinePoint(LocalLoc, ESplineCoordinateSpace::Local, true);

		int32 NewIndex = Spline->GetNumberOfSplinePoints() - 1;

		// Set metadata if provided
		if (Meta)
		{
			Meta->Modify();
			if (WidthOpt.has_value() && Meta->RiverWidth.Points.IsValidIndex(NewIndex))
			{
				Meta->RiverWidth.Points[NewIndex].OutVal = static_cast<float>(WidthOpt.value());
			}
			if (DepthOpt.has_value() && Meta->Depth.Points.IsValidIndex(NewIndex))
			{
				Meta->Depth.Points[NewIndex].OutVal = static_cast<float>(DepthOpt.value());
			}
			if (VelocityOpt.has_value() && Meta->WaterVelocityScalar.Points.IsValidIndex(NewIndex))
			{
				Meta->WaterVelocityScalar.Points[NewIndex].OutVal = static_cast<float>(VelocityOpt.value());
			}
			if (AudioOpt.has_value() && Meta->AudioIntensity.Points.IsValidIndex(NewIndex))
			{
				Meta->AudioIntensity.Points[NewIndex].OutVal = static_cast<float>(AudioOpt.value());
			}
		}

		Spline->UpdateSpline();
		Spline->K2_SynchronizeAndBroadcastDataChange();

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (Comp)
		{
			FOnWaterBodyChangedParams Params;
			Params.bShapeOrPositionChanged = true;
			Params.bUserTriggered = true;
			Comp->UpdateAll(Params);
	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif
		}

		int32 LuaIndex = NewIndex + 1; // 1-based
		Session.Log(FString::Printf(TEXT("[OK] water_river_add_point -> Added point %d to '%s'"), LuaIndex, *Name));
		return sol::make_object(LuaView, LuaIndex);
	});

	// ================================================================
	// water_river_set_point(actor_name, index, params)
	// ================================================================
	Lua.set_function("water_river_set_point", [&Session](const std::string& NameStr, int LuaIndex, const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_river_set_point -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_point -> Could not find water body '%s'"), *Name));
			return false;
		}

		if (WB->GetWaterBodyType() != EWaterBodyType::River)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_point -> '%s' is not a river"), *Name));
			return false;
		}

		UWaterSplineComponent* Spline = WB->GetWaterSpline();
		UWaterSplineMetadata* Meta = WB->GetWaterSplineMetadata();
		if (!Spline)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_point -> '%s' has no spline"), *Name));
			return false;
		}

		int32 Index = LuaIndex - 1; // Convert from 1-based to 0-based
		if (Index < 0 || Index >= Spline->GetNumberOfSplinePoints())
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_point -> Index %d out of range [1..%d]"), LuaIndex, Spline->GetNumberOfSplinePoints()));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set River Spline Point")));
		WB->Modify();
		Spline->Modify();

		// Set location if provided
		sol::optional<sol::table> LocOpt = Params.get<sol::optional<sol::table>>("location");
		if (LocOpt.has_value())
		{
			FVector WorldLoc = TableToVector(LocOpt.value());
			FVector LocalLoc = WB->GetActorTransform().InverseTransformPosition(WorldLoc);
			Spline->SetLocationAtSplinePoint(Index, LocalLoc, ESplineCoordinateSpace::Local, true);
		}

		// Set metadata
		if (Meta)
		{
			Meta->Modify();
			sol::optional<double> WidthOpt = Params.get<sol::optional<double>>("width");
			if (WidthOpt.has_value() && Meta->RiverWidth.Points.IsValidIndex(Index))
			{
				Meta->RiverWidth.Points[Index].OutVal = static_cast<float>(WidthOpt.value());
			}

			sol::optional<double> DepthOpt = Params.get<sol::optional<double>>("depth");
			if (DepthOpt.has_value() && Meta->Depth.Points.IsValidIndex(Index))
			{
				Meta->Depth.Points[Index].OutVal = static_cast<float>(DepthOpt.value());
			}

			sol::optional<double> VelocityOpt = Params.get<sol::optional<double>>("velocity");
			if (VelocityOpt.has_value() && Meta->WaterVelocityScalar.Points.IsValidIndex(Index))
			{
				Meta->WaterVelocityScalar.Points[Index].OutVal = static_cast<float>(VelocityOpt.value());
			}

			sol::optional<double> AudioOpt = Params.get<sol::optional<double>>("audio");
			if (AudioOpt.has_value() && Meta->AudioIntensity.Points.IsValidIndex(Index))
			{
				Meta->AudioIntensity.Points[Index].OutVal = static_cast<float>(AudioOpt.value());
			}
		}

		Spline->UpdateSpline();
		Spline->K2_SynchronizeAndBroadcastDataChange();

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (Comp)
		{
			FOnWaterBodyChangedParams ChangedParams;
			ChangedParams.bShapeOrPositionChanged = true;
			ChangedParams.bUserTriggered = true;
			Comp->UpdateAll(ChangedParams);
	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif
		}

		Session.Log(FString::Printf(TEXT("[OK] water_river_set_point -> Updated point %d on '%s'"), LuaIndex, *Name));
		return true;
	});

	// ================================================================
	// water_river_get_points(actor_name)
	// ================================================================
	Lua.set_function("water_river_get_points", [&Session](const std::string& NameStr, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_river_get_points -> No editor world"));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_get_points -> Could not find water body '%s'"), *Name));
			return sol::lua_nil;
		}

		if (WB->GetWaterBodyType() != EWaterBodyType::River)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_get_points -> '%s' is not a river"), *Name));
			return sol::lua_nil;
		}

		UWaterSplineComponent* Spline = WB->GetWaterSpline();
		UWaterSplineMetadata* Meta = WB->GetWaterSplineMetadata();
		if (!Spline)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_get_points -> '%s' has no spline"), *Name));
			return sol::lua_nil;
		}

		int32 NumPoints = Spline->GetNumberOfSplinePoints();
		sol::table Result = LuaView.create_table();
		for (int32 i = 0; i < NumPoints; i++)
		{
			sol::table Pt = LuaView.create_table();
			Pt["index"] = i + 1; // 1-based

			FVector Loc = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
			Pt["location"] = VectorToTable(LuaView, Loc);

			if (Meta)
			{
				Pt["width"] = (Meta->RiverWidth.Points.IsValidIndex(i)) ? Meta->RiverWidth.Points[i].OutVal : 0.f;
				Pt["depth"] = (Meta->Depth.Points.IsValidIndex(i)) ? Meta->Depth.Points[i].OutVal : 0.f;
				Pt["velocity"] = (Meta->WaterVelocityScalar.Points.IsValidIndex(i)) ? Meta->WaterVelocityScalar.Points[i].OutVal : 0.f;
				Pt["audio"] = (Meta->AudioIntensity.Points.IsValidIndex(i)) ? Meta->AudioIntensity.Points[i].OutVal : 0.f;
			}

			Result[i + 1] = Pt;
		}

		Session.Log(FString::Printf(TEXT("[OK] water_river_get_points -> %d points on '%s'"), NumPoints, *Name));
		return Result;
	});

	// ================================================================
	// water_zone_set_extent(zone_name, width, height)
	// ================================================================
	Lua.set_function("water_zone_set_extent", [&Session](const std::string& NameStr, double Width, double Height, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_zone_set_extent -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterZone* WZ = FindWaterZoneByName(World, Name);
		if (!WZ)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_zone_set_extent -> Could not find water zone '%s'"), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Zone Extent")));
		WZ->Modify();

		WZ->SetZoneExtent(FVector2D(Width, Height));
		WZ->MarkForRebuild(EWaterZoneRebuildFlags::All);

		Session.Log(FString::Printf(TEXT("[OK] water_zone_set_extent -> Set extent (%.0f, %.0f) on '%s'"), Width, Height, *Name));
		return true;
	});

	// ================================================================
	// water_zone_set_resolution(zone_name, width, height)
	// ================================================================
	Lua.set_function("water_zone_set_resolution", [&Session](const std::string& NameStr, int Width, int Height, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_zone_set_resolution -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterZone* WZ = FindWaterZoneByName(World, Name);
		if (!WZ)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_zone_set_resolution -> Could not find water zone '%s'"), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Zone Resolution")));
		WZ->Modify();

		WZ->SetRenderTargetResolution(FIntPoint(Width, Height));
		WZ->MarkForRebuild(EWaterZoneRebuildFlags::All);

		Session.Log(FString::Printf(TEXT("[OK] water_zone_set_resolution -> Set resolution (%d, %d) on '%s'"), Width, Height, *Name));
		return true;
	});

	// ================================================================
	// water_zone_rebuild(zone_name?)
	// ================================================================
	Lua.set_function("water_zone_rebuild", [&Session](sol::optional<std::string> NameOpt, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_zone_rebuild -> No editor world"));
			return false;
		}

		if (NameOpt.has_value())
		{
			FString Name = NeoLuaStr::ToFStringOpt(NameOpt);
			AWaterZone* WZ = FindWaterZoneByName(World, Name);
			if (!WZ)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_zone_rebuild -> Could not find water zone '%s'"), *Name));
				return false;
			}

			WZ->MarkForRebuild(EWaterZoneRebuildFlags::All);
			Session.Log(FString::Printf(TEXT("[OK] water_zone_rebuild -> Rebuilt zone '%s'"), *Name));
		}
		else
		{
			UWaterSubsystem* Subsystem = UWaterSubsystem::GetWaterSubsystem(World);
			if (!Subsystem)
			{
				Session.Log(TEXT("[FAIL] water_zone_rebuild -> No water subsystem"));
				return false;
			}

			Subsystem->MarkAllWaterZonesForRebuild(EWaterZoneRebuildFlags::All);
			Session.Log(TEXT("[OK] water_zone_rebuild -> Rebuilt all water zones"));
		}

		return true;
	});

	// ================================================================
	// water_ocean_set_flood(height)
	// ================================================================
	Lua.set_function("water_ocean_set_flood", [&Session](double Height, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_ocean_set_flood -> No editor world"));
			return false;
		}

		UWaterSubsystem* Subsystem = UWaterSubsystem::GetWaterSubsystem(World);
		if (!Subsystem)
		{
			Session.Log(TEXT("[FAIL] water_ocean_set_flood -> No water subsystem"));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Ocean Flood Height")));
		Subsystem->SetOceanFloodHeight(static_cast<float>(Height));

		Session.Log(FString::Printf(TEXT("[OK] water_ocean_set_flood -> Set flood height to %.2f"), Height));
		return true;
	});

	// ================================================================
	// water_ocean_get_height()
	// ================================================================
	Lua.set_function("water_ocean_get_height", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_ocean_get_height -> No editor world"));
			return sol::lua_nil;
		}

		UWaterSubsystem* Subsystem = UWaterSubsystem::GetWaterSubsystem(World);
		if (!Subsystem)
		{
			Session.Log(TEXT("[FAIL] water_ocean_get_height -> No water subsystem"));
			return sol::lua_nil;
		}

		float Height = Subsystem->GetOceanBaseHeight();
		Session.Log(FString::Printf(TEXT("[OK] water_ocean_get_height -> %.2f"), Height));
		return sol::make_object(LuaView, Height);
	});

	// ================================================================
	// water_remove_body(name_or_label)
	// ================================================================
	Lua.set_function("water_remove_body", [&Session](const std::string& NameStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_remove_body -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_remove_body -> Could not find water body '%s'"), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Remove Water Body")));

		UEditorActorSubsystem* ActorSub = NeoLuaSubsystem::GetEditor<UEditorActorSubsystem>();
		if (ActorSub && ActorSub->DestroyActor(WB))
		{
			Session.Log(FString::Printf(TEXT("[OK] water_remove_body -> Removed '%s'"), *Name));
			return true;
		}

		Session.Log(FString::Printf(TEXT("[FAIL] water_remove_body -> Failed to destroy '%s'. Make sure it is an editor-world actor (not PIE/runtime-only) and the current level allows editor deletion."), *Name));
		return false;
	});

	// ================================================================
	// water_list_buoyancy()
	// ================================================================
	Lua.set_function("water_list_buoyancy", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_list_buoyancy -> No editor world"));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;

			UBuoyancyComponent* Buoy = Actor->FindComponentByClass<UBuoyancyComponent>();
			if (!Buoy) continue;

			sol::table Entry = LuaView.create_table();
			Entry["name"] = TCHAR_TO_UTF8(*Actor->GetActorNameOrLabel());
			Entry["class"] = TCHAR_TO_UTF8(*Actor->GetClass()->GetName());
			Entry["pontoon_count"] = Buoy->BuoyancyData.Pontoons.Num();
			Entry["buoyancy_coefficient"] = Buoy->BuoyancyData.BuoyancyCoefficient;
			Entry["in_water"] = Buoy->IsOverlappingWaterBody();
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] water_list_buoyancy -> %d actors with buoyancy"), Idx - 1));
		return Result;
	});

	// ================================================================
	// water_get_buoyancy(actor_name)
	// ================================================================
	Lua.set_function("water_get_buoyancy", [&Session](const std::string& NameStr, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_get_buoyancy -> No editor world"));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AActor* Actor = FindActorWithBuoyancyByName(World, Name);
		if (!Actor)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_get_buoyancy -> No actor with buoyancy component named '%s'"), *Name));
			return sol::lua_nil;
		}

		UBuoyancyComponent* Buoy = Actor->FindComponentByClass<UBuoyancyComponent>();
		const FBuoyancyData& Data = Buoy->BuoyancyData;

		sol::table Result = LuaView.create_table();
		Result["actor_name"] = TCHAR_TO_UTF8(*Actor->GetActorNameOrLabel());

		// Core buoyancy
		Result["buoyancy_coefficient"] = Data.BuoyancyCoefficient;
		Result["buoyancy_damp"] = Data.BuoyancyDamp;
		Result["buoyancy_damp2"] = Data.BuoyancyDamp2;
		Result["ramp_min_velocity"] = Data.BuoyancyRampMinVelocity;
		Result["ramp_max_velocity"] = Data.BuoyancyRampMaxVelocity;
		Result["ramp_max"] = Data.BuoyancyRampMax;
		Result["max_buoyant_force"] = Data.MaxBuoyantForce;
		Result["center_on_com"] = Data.bCenterPontoonsOnCOM;

		// Drag
		Result["apply_drag"] = Data.bApplyDragForcesInWater;
		Result["drag_coefficient"] = Data.DragCoefficient;
		Result["drag_coefficient2"] = Data.DragCoefficient2;
		Result["angular_drag_coefficient"] = Data.AngularDragCoefficient;
		Result["max_drag_speed"] = Data.MaxDragSpeed;

		// Pontoons (full FSphericalPontoon surface — runtime fields are BlueprintReadOnly per BuoyancyTypes.h:38-90)
		sol::table PontoonsTable = LuaView.create_table();
		for (int32 i = 0; i < Data.Pontoons.Num(); i++)
		{
			const FSphericalPontoon& P = Data.Pontoons[i];
			sol::table Pt = LuaView.create_table();
			Pt["index"] = i + 1;
			Pt["socket"] = TCHAR_TO_UTF8(*P.CenterSocket.ToString());
			Pt["location"] = VectorToTable(LuaView, P.RelativeLocation);
			Pt["radius"] = P.Radius;
			Pt["fx_enabled"] = P.bFXEnabled;
			Pt["is_in_water"] = P.bIsInWater;

			// Runtime simulation fields
			Pt["local_force"] = VectorToTable(LuaView, P.LocalForce);
			Pt["center_location"] = VectorToTable(LuaView, P.CenterLocation);
			sol::table SocketRot = LuaView.create_table();
			const FRotator R = P.SocketRotation.Rotator();
			SocketRot["pitch"] = R.Pitch;
			SocketRot["yaw"] = R.Yaw;
			SocketRot["roll"] = R.Roll;
			Pt["socket_rotation"] = SocketRot;
			Pt["offset"] = VectorToTable(LuaView, P.Offset);
			Pt["water_height"] = P.WaterHeight;
			Pt["water_depth"] = P.WaterDepth;
			Pt["immersion_depth"] = P.ImmersionDepth;
			Pt["water_plane_location"] = VectorToTable(LuaView, P.WaterPlaneLocation);
			Pt["water_plane_normal"] = VectorToTable(LuaView, P.WaterPlaneNormal);
			Pt["water_surface_position"] = VectorToTable(LuaView, P.WaterSurfacePosition);
			Pt["water_velocity"] = VectorToTable(LuaView, P.WaterVelocity);
			Pt["water_body_index"] = P.WaterBodyIndex;
			if (P.CurrentWaterBodyComponent)
			{
				if (AWaterBody* CurBody = P.CurrentWaterBodyComponent->GetWaterBodyActor())
				{
					Pt["current_water_body"] = TCHAR_TO_UTF8(*CurBody->GetActorNameOrLabel());
				}
			}

			PontoonsTable[i + 1] = Pt;
		}
		Result["pontoons"] = PontoonsTable;

		// River behavior
		sol::table River = LuaView.create_table();
		River["apply_forces"] = Data.bApplyRiverForces;
		River["pontoon_index"] = Data.RiverPontoonIndex + 1; // 1-based
		River["shore_push_factor"] = Data.WaterShorePushFactor;
		River["path_width"] = Data.RiverTraversalPathWidth;
		River["max_shore_force"] = Data.MaxShorePushForce;
		River["velocity_strength"] = Data.WaterVelocityStrength;
		River["max_water_force"] = Data.MaxWaterForce;
		River["always_lateral_push"] = Data.bAlwaysAllowLateralPush;
		River["allow_upstream_current"] = Data.bAllowCurrentWhenMovingFastUpstream;
		River["apply_downstream_rotation"] = Data.bApplyDownstreamAngularRotation;
		River["downstream_axis"] = VectorToTable(LuaView, Data.DownstreamAxisOfRotation);
		River["downstream_rotation_strength"] = Data.DownstreamRotationStrength;
		River["downstream_rotation_stiffness"] = Data.DownstreamRotationStiffness;
		River["downstream_angular_damping"] = Data.DownstreamRotationAngularDamping;
		River["downstream_max_acceleration"] = Data.DownstreamMaxAcceleration;
		Result["river"] = River;

		// Runtime state
		Result["is_overlapping_water"] = Buoy->IsOverlappingWaterBody();
		Result["is_in_water"] = Buoy->IsInWaterBody();

		// Current overlapping water body components (UBuoyancyComponent::GetCurrentWaterBodyComponents)
		sol::table CurrentBodies = LuaView.create_table();
		int32 CBIdx = 1;
		for (UWaterBodyComponent* WBC : Buoy->GetCurrentWaterBodyComponents())
		{
			if (!WBC) continue;
			if (AWaterBody* OwnerBody = WBC->GetWaterBodyActor())
			{
				CurrentBodies[CBIdx++] = TCHAR_TO_UTF8(*OwnerBody->GetActorNameOrLabel());
			}
		}
		Result["current_water_bodies"] = CurrentBodies;

		// Cached last surface query (UBuoyancyComponent::GetLastWaterSurfaceInfo)
		FVector LastPlaneLoc = FVector::ZeroVector;
		FVector LastPlaneNormal = FVector::UpVector;
		FVector LastSurfacePos = FVector::ZeroVector;
		float LastDepth = 0.f;
		int32 LastBodyIdx = INDEX_NONE;
		FVector LastVelocity = FVector::ZeroVector;
		Buoy->GetLastWaterSurfaceInfo(LastPlaneLoc, LastPlaneNormal, LastSurfacePos, LastDepth, LastBodyIdx, LastVelocity);

		sol::table LastSurface = LuaView.create_table();
		LastSurface["plane_location"] = VectorToTable(LuaView, LastPlaneLoc);
		LastSurface["plane_normal"] = VectorToTable(LuaView, LastPlaneNormal);
		LastSurface["surface_position"] = VectorToTable(LuaView, LastSurfacePos);
		LastSurface["water_depth"] = LastDepth;
		LastSurface["water_body_index"] = LastBodyIdx;
		LastSurface["water_velocity"] = VectorToTable(LuaView, LastVelocity);
		Result["last_surface_info"] = LastSurface;

		Session.Log(FString::Printf(TEXT("[OK] water_get_buoyancy -> '%s' (%d pontoons, %d current bodies)"), *Name, Data.Pontoons.Num(), CBIdx - 1));
		return Result;
	});

	// ================================================================
	// water_set_buoyancy(actor_name, params)
	// ================================================================
	Lua.set_function("water_set_buoyancy", [&Session](const std::string& NameStr, const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_set_buoyancy -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AActor* Actor = FindActorWithBuoyancyByName(World, Name);
		if (!Actor)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_buoyancy -> No actor with buoyancy component named '%s'"), *Name));
			return false;
		}

		UBuoyancyComponent* Buoy = Actor->FindComponentByClass<UBuoyancyComponent>();
		if (!Buoy)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_buoyancy -> actor '%s' has no BuoyancyComponent"), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Buoyancy")));
		Buoy->Modify();

		FBuoyancyData& Data = Buoy->BuoyancyData;

		// Core buoyancy
		Data.BuoyancyCoefficient = static_cast<float>(Params.get_or("buoyancy_coefficient", static_cast<double>(Data.BuoyancyCoefficient)));
		Data.BuoyancyDamp = static_cast<float>(Params.get_or("buoyancy_damp", static_cast<double>(Data.BuoyancyDamp)));
		Data.BuoyancyDamp2 = static_cast<float>(Params.get_or("buoyancy_damp2", static_cast<double>(Data.BuoyancyDamp2)));
		Data.BuoyancyRampMinVelocity = static_cast<float>(Params.get_or("ramp_min_velocity", static_cast<double>(Data.BuoyancyRampMinVelocity)));
		Data.BuoyancyRampMaxVelocity = static_cast<float>(Params.get_or("ramp_max_velocity", static_cast<double>(Data.BuoyancyRampMaxVelocity)));
		Data.BuoyancyRampMax = static_cast<float>(Params.get_or("ramp_max", static_cast<double>(Data.BuoyancyRampMax)));
		Data.MaxBuoyantForce = static_cast<float>(Params.get_or("max_buoyant_force", static_cast<double>(Data.MaxBuoyantForce)));

		sol::optional<bool> CenterOnCOMOpt = Params.get<sol::optional<bool>>("center_on_com");
		if (CenterOnCOMOpt.has_value()) Data.bCenterPontoonsOnCOM = CenterOnCOMOpt.value();

		// Drag
		sol::optional<bool> ApplyDragOpt = Params.get<sol::optional<bool>>("apply_drag");
		if (ApplyDragOpt.has_value()) Data.bApplyDragForcesInWater = ApplyDragOpt.value();

		Data.DragCoefficient = static_cast<float>(Params.get_or("drag_coefficient", static_cast<double>(Data.DragCoefficient)));
		Data.DragCoefficient2 = static_cast<float>(Params.get_or("drag_coefficient2", static_cast<double>(Data.DragCoefficient2)));
		Data.AngularDragCoefficient = static_cast<float>(Params.get_or("angular_drag_coefficient", static_cast<double>(Data.AngularDragCoefficient)));
		Data.MaxDragSpeed = static_cast<float>(Params.get_or("max_drag_speed", static_cast<double>(Data.MaxDragSpeed)));

		// River behavior (nested table)
		sol::optional<sol::table> RiverOpt = Params.get<sol::optional<sol::table>>("river");
		if (RiverOpt.has_value())
		{
			sol::table R = RiverOpt.value();

			sol::optional<bool> ApplyRiverOpt = R.get<sol::optional<bool>>("apply_forces");
			if (ApplyRiverOpt.has_value()) Data.bApplyRiverForces = ApplyRiverOpt.value();

			sol::optional<int> PontoonIdxOpt = R.get<sol::optional<int>>("pontoon_index");
			if (PontoonIdxOpt.has_value()) Data.RiverPontoonIndex = PontoonIdxOpt.value() - 1; // 1-based to 0-based

			Data.WaterShorePushFactor = static_cast<float>(R.get_or("shore_push_factor", static_cast<double>(Data.WaterShorePushFactor)));
			Data.RiverTraversalPathWidth = static_cast<float>(R.get_or("path_width", static_cast<double>(Data.RiverTraversalPathWidth)));
			Data.MaxShorePushForce = static_cast<float>(R.get_or("max_shore_force", static_cast<double>(Data.MaxShorePushForce)));
			Data.WaterVelocityStrength = static_cast<float>(R.get_or("velocity_strength", static_cast<double>(Data.WaterVelocityStrength)));
			Data.MaxWaterForce = static_cast<float>(R.get_or("max_water_force", static_cast<double>(Data.MaxWaterForce)));

			sol::optional<bool> LateralOpt = R.get<sol::optional<bool>>("always_lateral_push");
			if (LateralOpt.has_value()) Data.bAlwaysAllowLateralPush = LateralOpt.value();

			sol::optional<bool> UpstreamOpt = R.get<sol::optional<bool>>("allow_upstream_current");
			if (UpstreamOpt.has_value()) Data.bAllowCurrentWhenMovingFastUpstream = UpstreamOpt.value();

			sol::optional<bool> DownstreamRotOpt = R.get<sol::optional<bool>>("apply_downstream_rotation");
			if (DownstreamRotOpt.has_value()) Data.bApplyDownstreamAngularRotation = DownstreamRotOpt.value();

			sol::optional<sol::table> AxisOpt = R.get<sol::optional<sol::table>>("downstream_axis");
			if (AxisOpt.has_value()) Data.DownstreamAxisOfRotation = TableToVector(AxisOpt.value());

			Data.DownstreamRotationStrength = static_cast<float>(R.get_or("downstream_rotation_strength", static_cast<double>(Data.DownstreamRotationStrength)));
			Data.DownstreamRotationStiffness = static_cast<float>(R.get_or("downstream_rotation_stiffness", static_cast<double>(Data.DownstreamRotationStiffness)));
			Data.DownstreamRotationAngularDamping = static_cast<float>(R.get_or("downstream_angular_damping", static_cast<double>(Data.DownstreamRotationAngularDamping)));
			Data.DownstreamMaxAcceleration = static_cast<float>(R.get_or("downstream_max_acceleration", static_cast<double>(Data.DownstreamMaxAcceleration)));
		}

		Buoy->UpdatePontoonCoefficients();

		Session.Log(FString::Printf(TEXT("[OK] water_set_buoyancy -> Configured buoyancy on '%s'"), *Name));
		return true;
	});

	// ================================================================
	// water_add_pontoon(actor_name, params)
	// ================================================================
	Lua.set_function("water_add_pontoon", [&Session](const std::string& NameStr, const sol::table& Params, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_add_pontoon -> No editor world"));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AActor* Actor = FindActorWithBuoyancyByName(World, Name);
		if (!Actor)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_add_pontoon -> No actor with buoyancy component named '%s'"), *Name));
			return sol::lua_nil;
		}

		UBuoyancyComponent* Buoy = Actor->FindComponentByClass<UBuoyancyComponent>();

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Add Pontoon")));
		Buoy->Modify();

		float Radius = static_cast<float>(Params.get_or("radius", 100.0));

		sol::optional<std::string> SocketOpt = Params.get<sol::optional<std::string>>("socket");
		if (SocketOpt.has_value())
		{
			Buoy->AddCustomPontoon(Radius, FName(UTF8_TO_TCHAR(SocketOpt.value().c_str())));
		}
		else
		{
			FVector RelLoc = FVector::ZeroVector;
			sol::optional<sol::table> LocOpt = Params.get<sol::optional<sol::table>>("location");
			if (LocOpt.has_value())
			{
				RelLoc = TableToVector(LocOpt.value());
			}
			Buoy->AddCustomPontoon(Radius, RelLoc);
		}

		int32 NewIdx = Buoy->BuoyancyData.Pontoons.Num(); // already added, 1-based = Num()

		// Apply fx_enabled if specified
		sol::optional<bool> FxOpt = Params.get<sol::optional<bool>>("fx_enabled");
		if (FxOpt.has_value() && Buoy->BuoyancyData.Pontoons.IsValidIndex(NewIdx - 1))
		{
			Buoy->BuoyancyData.Pontoons[NewIdx - 1].bFXEnabled = FxOpt.value();
		}

		Buoy->UpdatePontoonCoefficients();

		Session.Log(FString::Printf(TEXT("[OK] water_add_pontoon -> Added pontoon %d to '%s' (radius=%.1f)"), NewIdx, *Name, Radius));
		return sol::make_object(LuaView, NewIdx);
	});

	// ================================================================
	// water_remove_pontoon(actor_name, index)
	// ================================================================
	Lua.set_function("water_remove_pontoon", [&Session](const std::string& NameStr, int LuaIndex, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_remove_pontoon -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AActor* Actor = FindActorWithBuoyancyByName(World, Name);
		if (!Actor)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_remove_pontoon -> No actor with buoyancy component named '%s'"), *Name));
			return false;
		}

		UBuoyancyComponent* Buoy = Actor->FindComponentByClass<UBuoyancyComponent>();

		int32 Index = LuaIndex - 1; // 1-based to 0-based
		if (Index < 0 || Index >= Buoy->BuoyancyData.Pontoons.Num())
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_remove_pontoon -> Index %d out of range [1..%d]"), LuaIndex, Buoy->BuoyancyData.Pontoons.Num()));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Remove Pontoon")));
		Buoy->Modify();

		Buoy->BuoyancyData.Pontoons.RemoveAt(Index);
		Buoy->UpdatePontoonCoefficients();

		Session.Log(FString::Printf(TEXT("[OK] water_remove_pontoon -> Removed pontoon %d from '%s' (%d remaining)"), LuaIndex, *Name, Buoy->BuoyancyData.Pontoons.Num()));
		return true;
	});

	// ================================================================
	// water_query_surface(location)
	// ================================================================
	Lua.set_function("water_query_surface", [&Session](const sol::table& LocationTable, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_query_surface -> No editor world"));
			return sol::lua_nil;
		}

		FVector QueryLoc = TableToVector(LocationTable);

		// Find the closest water body using spline-based distance (not actor origin)
		AWaterBody* ClosestBody = nullptr;
		float ClosestDist = TNumericLimits<float>::Max();

		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			AWaterBody* WB = *It;
			if (!WB || !WB->GetWaterBodyComponent()) continue;

			UWaterBodyComponent* WBComp = WB->GetWaterBodyComponent();
			float SplineKey = WBComp->FindInputKeyClosestToWorldLocation(QueryLoc);
			UWaterSplineComponent* WBSpline = WB->GetWaterSpline();
			float Dist;
			if (WBSpline)
			{
				FVector ClosestPoint = WBSpline->GetLocationAtSplineInputKey(SplineKey, ESplineCoordinateSpace::World);
				Dist = static_cast<float>(FVector::Dist(ClosestPoint, QueryLoc));
			}
			else
			{
				Dist = static_cast<float>(FVector::Dist(WB->GetActorLocation(), QueryLoc));
			}
			if (Dist < ClosestDist)
			{
				ClosestDist = Dist;
				ClosestBody = WB;
			}
		}

		if (!ClosestBody)
		{
			Session.Log(TEXT("[FAIL] water_query_surface -> No water bodies in the world. Spawn one with water_spawn() first."));
			return sol::lua_nil;
		}

		UWaterBodyComponent* Comp = ClosestBody->GetWaterBodyComponent();
		FVector SurfaceLocation;
		FVector SurfaceNormal;
		FVector Velocity;
		float Depth = 0.f;

	
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		bool bSuccess = Comp->GetWaterSurfaceInfoAtLocation(QueryLoc, SurfaceLocation, SurfaceNormal, Velocity, Depth, true);
#else
		Comp->GetWaterSurfaceInfoAtLocation(QueryLoc, SurfaceLocation, SurfaceNormal, Velocity, Depth, true);
		bool bSuccess = true;
#endif
		if (!bSuccess)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_query_surface -> Query failed on '%s' at location (%.0f, %.0f, %.0f). Try a point inside the water body's spline footprint or inspect the target body first with water_get_body()."),
				*ClosestBody->GetActorNameOrLabel(), QueryLoc.X, QueryLoc.Y, QueryLoc.Z));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();
		Result["surface_location"] = VectorToTable(LuaView, SurfaceLocation);
		Result["surface_normal"] = VectorToTable(LuaView, SurfaceNormal);
		Result["velocity"] = VectorToTable(LuaView, Velocity);
		Result["depth"] = Depth;
		Result["water_body"] = TCHAR_TO_UTF8(*ClosestBody->GetActorNameOrLabel());

		Session.Log(FString::Printf(TEXT("[OK] water_query_surface -> Queried '%s' at (%.0f, %.0f, %.0f)"), *ClosestBody->GetActorNameOrLabel(), QueryLoc.X, QueryLoc.Y, QueryLoc.Z));
		return Result;
	});

	// ================================================================
	// water_create_island(params)
	// ================================================================
	Lua.set_function("water_create_island", [&Session](const sol::table& Params, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_create_island -> No editor world"));
			return sol::lua_nil;
		}

		// Location
		FVector Location = FVector::ZeroVector;
		sol::optional<sol::table> LocOpt = Params.get<sol::optional<sol::table>>("location");
		if (LocOpt.has_value())
		{
			Location = TableToVector(LocOpt.value());
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Create Water Body Island")));
		FScopedSuppressLandscapeAutoLayerDialog LandscapeLayerDialogGuard;

		FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AWaterBodyIsland* Island = World->SpawnActor<AWaterBodyIsland>(AWaterBodyIsland::StaticClass(), SpawnTransform, SpawnParams);
		if (!Island)
		{
			Session.Log(TEXT("[FAIL] water_create_island -> SpawnActor returned null for AWaterBodyIsland in the editor world. Verify the editor world is active and the Water plugin is loaded."));
			return sol::lua_nil;
		}

		// Label
		sol::optional<std::string> LabelOpt = Params.get<sol::optional<std::string>>("label");
		if (LabelOpt.has_value())
		{
			Island->SetActorLabel(NeoLuaStr::ToFStringOpt(LabelOpt));
		}

		// Apply editor defaults (mirrors UWaterBodyIslandActorFactory::PostSpawnActor)
		const UWaterEditorSettings* EditorSettings = GetDefault<UWaterEditorSettings>();
		const FWaterBrushActorDefaults& IslandDefaults = EditorSettings->WaterBodyIslandDefaults.BrushDefaults;
		Island->WaterCurveSettings = IslandDefaults.CurveSettings;
		Island->WaterHeightmapSettings = IslandDefaults.HeightmapSettings;
		Island->WaterWeightmapSettings = IslandDefaults.LayerWeightmapSettings;

		// Spline points
		sol::optional<sol::table> PointsOpt = Params.get<sol::optional<sol::table>>("points");
		UWaterSplineComponent* SplineComp = Island->GetWaterSpline();
		if (PointsOpt.has_value() && SplineComp)
		{
			SplineComp->ClearSplinePoints(false);
			for (auto& Kv : PointsOpt.value())
			{
				sol::optional<sol::table> PtOpt = Kv.second.as<sol::optional<sol::table>>();
				if (PtOpt.has_value())
				{
					FVector WorldPt = TableToVector(PtOpt.value());
					FVector LocalPt = Island->GetActorTransform().InverseTransformPosition(WorldPt);
					SplineComp->AddSplinePoint(LocalPt, ESplineCoordinateSpace::Local, false);
				}
			}
			SplineComp->UpdateSpline();
		}

		// Heightmap settings (user overrides applied on top of defaults)
		sol::optional<sol::table> HeightmapOpt = Params.get<sol::optional<sol::table>>("heightmap");
		if (HeightmapOpt.has_value())
		{
			sol::table HM = HeightmapOpt.value();
			FWaterBodyHeightmapSettings& Settings = Island->WaterHeightmapSettings;

			sol::optional<std::string> BlendModeOpt = HM.get<sol::optional<std::string>>("blend_mode");
			if (BlendModeOpt.has_value())
			{
				FString BM = NeoLuaStr::ToFStringOpt(BlendModeOpt);
				if (BM.Equals(TEXT("alpha_blend"), ESearchCase::IgnoreCase)) Settings.BlendMode = EWaterBrushBlendType::AlphaBlend;
				else if (BM.Equals(TEXT("min"), ESearchCase::IgnoreCase)) Settings.BlendMode = EWaterBrushBlendType::Min;
				else if (BM.Equals(TEXT("max"), ESearchCase::IgnoreCase)) Settings.BlendMode = EWaterBrushBlendType::Max;
				else if (BM.Equals(TEXT("additive"), ESearchCase::IgnoreCase)) Settings.BlendMode = EWaterBrushBlendType::Additive;
			}

			Settings.FalloffSettings.FalloffWidth = static_cast<float>(HM.get_or("falloff_width", static_cast<double>(Settings.FalloffSettings.FalloffWidth)));
			Settings.FalloffSettings.EdgeOffset = static_cast<float>(HM.get_or("edge_offset", static_cast<double>(Settings.FalloffSettings.EdgeOffset)));
			Settings.FalloffSettings.ZOffset = static_cast<float>(HM.get_or("z_offset", static_cast<double>(Settings.FalloffSettings.ZOffset)));
		}

		// Weightmap layer settings
		sol::optional<sol::table> LayersOpt = Params.get<sol::optional<sol::table>>("layers");
		if (LayersOpt.has_value())
		{
			Island->WaterWeightmapSettings.Empty();
			for (auto& Kv : LayersOpt.value())
			{
				sol::optional<std::string> LayerNameOpt = Kv.first.as<sol::optional<std::string>>();
				sol::optional<sol::table> LayerDataOpt = Kv.second.as<sol::optional<sol::table>>();
				if (!LayerNameOpt.has_value() || !LayerDataOpt.has_value()) continue;

				FName LayerName = FName(UTF8_TO_TCHAR(LayerNameOpt.value().c_str()));
				sol::table LD = LayerDataOpt.value();

				FWaterBodyWeightmapSettings WS;
				WS.FalloffWidth = static_cast<float>(LD.get_or("falloff_width", static_cast<double>(WS.FalloffWidth)));
				WS.EdgeOffset = static_cast<float>(LD.get_or("edge_offset", static_cast<double>(WS.EdgeOffset)));
				WS.FinalOpacity = static_cast<float>(LD.get_or("opacity", static_cast<double>(WS.FinalOpacity)));
				WS.TextureTiling = static_cast<float>(LD.get_or("texture_tiling", static_cast<double>(WS.TextureTiling)));
				WS.TextureInfluence = static_cast<float>(LD.get_or("texture_influence", static_cast<double>(WS.TextureInfluence)));
				WS.Midpoint = static_cast<float>(LD.get_or("midpoint", static_cast<double>(WS.Midpoint)));

				Island->WaterWeightmapSettings.Add(LayerName, WS);
			}
		}

#if WITH_EDITOR
		Island->UpdateOverlappingWaterBodyComponents();
#endif
		ApplyWaterLayerPlacement(World, Params, Session, TEXT("water_create_island"));

		FString ActorName = Island->GetActorNameOrLabel();
		Session.Log(FString::Printf(TEXT("[OK] water_create_island -> Spawned island '%s' with %d spline points"), *ActorName, SplineComp ? SplineComp->GetNumberOfSplinePoints() : 0));
		return sol::make_object(LuaView, std::string(TCHAR_TO_UTF8(*ActorName)));
	});

	// ================================================================
	// water_create_zone(params)
	// ================================================================
	Lua.set_function("water_create_zone", [&Session](const sol::table& Params, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_create_zone -> No editor world"));
			return sol::lua_nil;
		}

		FVector Location = FVector::ZeroVector;
		sol::optional<sol::table> LocOpt = Params.get<sol::optional<sol::table>>("location");
		if (LocOpt.has_value())
		{
			Location = TableToVector(LocOpt.value());
		}

		double Width = Params.get_or("width", 51200.0);
		double Height = Params.get_or("height", 51200.0);

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Create Water Zone")));

		FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AWaterZone* Zone = World->SpawnActor<AWaterZone>(AWaterZone::StaticClass(), SpawnTransform, SpawnParams);
		if (!Zone)
		{
			Session.Log(TEXT("[FAIL] water_create_zone -> SpawnActor returned null for AWaterZone in the editor world. Verify the editor world is active and the Water plugin is loaded."));
			return sol::lua_nil;
		}

		// Label
		sol::optional<std::string> LabelOpt = Params.get<sol::optional<std::string>>("label");
		if (LabelOpt.has_value())
		{
			Zone->SetActorLabel(NeoLuaStr::ToFStringOpt(LabelOpt));
		}

		Zone->SetZoneExtent(FVector2D(Width, Height));

		// Resolution
		sol::optional<int> ResXOpt = Params.get<sol::optional<int>>("resolution_x");
		sol::optional<int> ResYOpt = Params.get<sol::optional<int>>("resolution_y");
		if (ResXOpt.has_value() || ResYOpt.has_value())
		{
			FIntPoint CurrentRes = GetWaterZoneRenderTargetResolutionCompat(Zone);
			int32 ResX = ResXOpt.has_value() ? ResXOpt.value() : CurrentRes.X;
			int32 ResY = ResYOpt.has_value() ? ResYOpt.value() : CurrentRes.Y;
			Zone->SetRenderTargetResolution(FIntPoint(ResX, ResY));
		}

		// Tile size
		sol::optional<double> TileSizeOpt = Params.get<sol::optional<double>>("tile_size");
		if (TileSizeOpt.has_value())
		{
			UWaterMeshComponent* WaterMesh = Zone->GetWaterMeshComponent();
			if (WaterMesh)
			{
				WaterMesh->SetTileSize(static_cast<float>(TileSizeOpt.value()));
			}
		}

		Zone->MarkForRebuild(EWaterZoneRebuildFlags::All);

		FString ActorName = Zone->GetActorNameOrLabel();
		Session.Log(FString::Printf(TEXT("[OK] water_create_zone -> Spawned zone '%s' (%.0f x %.0f)"), *ActorName, Width, Height));
		return sol::make_object(LuaView, std::string(TCHAR_TO_UTF8(*ActorName)));
	});

	// ================================================================
	// water_set_landscape_painting(actor_name, params)
	// ================================================================
	Lua.set_function("water_set_landscape_painting", [&Session](const std::string& NameStr, const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_set_landscape_painting -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_landscape_painting -> Could not find water body '%s'"), *Name));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_landscape_painting -> Water body '%s' has no component"), *Name));
			return false;
		}

		sol::optional<sol::table> LayersOpt = Params.get<sol::optional<sol::table>>("layers");
		if (!LayersOpt.has_value())
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_landscape_painting -> No 'layers' table provided")));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Landscape Painting")));
		Comp->Modify();

		Comp->LayerWeightmapSettings.Empty();
		int32 LayerCount = 0;

		for (auto& Kv : LayersOpt.value())
		{
			sol::optional<std::string> LayerNameOpt = Kv.first.as<sol::optional<std::string>>();
			sol::optional<sol::table> LayerDataOpt = Kv.second.as<sol::optional<sol::table>>();
			if (!LayerNameOpt.has_value() || !LayerDataOpt.has_value()) continue;

			FName LayerName = FName(UTF8_TO_TCHAR(LayerNameOpt.value().c_str()));
			sol::table LD = LayerDataOpt.value();

			FWaterBodyWeightmapSettings WS;
			WS.FalloffWidth = static_cast<float>(LD.get_or("falloff_width", static_cast<double>(WS.FalloffWidth)));
			WS.EdgeOffset = static_cast<float>(LD.get_or("edge_offset", static_cast<double>(WS.EdgeOffset)));
			WS.FinalOpacity = static_cast<float>(LD.get_or("opacity", static_cast<double>(WS.FinalOpacity)));
			WS.TextureTiling = static_cast<float>(LD.get_or("texture_tiling", static_cast<double>(WS.TextureTiling)));
			WS.TextureInfluence = static_cast<float>(LD.get_or("texture_influence", static_cast<double>(WS.TextureInfluence)));
			WS.Midpoint = static_cast<float>(LD.get_or("midpoint", static_cast<double>(WS.Midpoint)));

			Comp->LayerWeightmapSettings.Add(LayerName, WS);
			LayerCount++;
		}

		FOnWaterBodyChangedParams ChangedParams;
		ChangedParams.bWeightmapSettingsChanged = true;
		ChangedParams.bUserTriggered = true;
		Comp->UpdateAll(ChangedParams);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif

		Session.Log(FString::Printf(TEXT("[OK] water_set_landscape_painting -> Set %d layer(s) on '%s'"), LayerCount, *Name));
		return true;
	});

	// ================================================================
	// water_set_brush_effects(actor_name, params)
	// ================================================================
	Lua.set_function("water_set_brush_effects", [&Session](const std::string& NameStr, const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_set_brush_effects -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_brush_effects -> Could not find water body '%s'"), *Name));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_brush_effects -> Water body '%s' has no component"), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Brush Effects")));
		Comp->Modify();

		FWaterBodyHeightmapSettings& HM = Comp->WaterHeightmapSettings;

		// Blend mode
		sol::optional<std::string> BlendModeOpt = Params.get<sol::optional<std::string>>("blend_mode");
		if (BlendModeOpt.has_value())
		{
			FString BM = NeoLuaStr::ToFStringOpt(BlendModeOpt);
			if (BM.Equals(TEXT("alpha_blend"), ESearchCase::IgnoreCase)) HM.BlendMode = EWaterBrushBlendType::AlphaBlend;
			else if (BM.Equals(TEXT("min"), ESearchCase::IgnoreCase)) HM.BlendMode = EWaterBrushBlendType::Min;
			else if (BM.Equals(TEXT("max"), ESearchCase::IgnoreCase)) HM.BlendMode = EWaterBrushBlendType::Max;
			else if (BM.Equals(TEXT("additive"), ESearchCase::IgnoreCase)) HM.BlendMode = EWaterBrushBlendType::Additive;
		}

		// Falloff settings
		HM.FalloffSettings.FalloffWidth = static_cast<float>(Params.get_or("falloff_width", static_cast<double>(HM.FalloffSettings.FalloffWidth)));
		HM.FalloffSettings.EdgeOffset = static_cast<float>(Params.get_or("edge_offset", static_cast<double>(HM.FalloffSettings.EdgeOffset)));
		HM.FalloffSettings.ZOffset = static_cast<float>(Params.get_or("z_offset", static_cast<double>(HM.FalloffSettings.ZOffset)));

		// Blurring
		sol::optional<sol::table> BlurOpt = Params.get<sol::optional<sol::table>>("blurring");
		if (BlurOpt.has_value())
		{
			sol::table B = BlurOpt.value();
			sol::optional<bool> EnabledOpt = B.get<sol::optional<bool>>("enabled");
			if (EnabledOpt.has_value()) HM.Effects.Blurring.bBlurShape = EnabledOpt.value();
			HM.Effects.Blurring.Radius = static_cast<int32>(B.get_or("radius", HM.Effects.Blurring.Radius));
		}

		// Curl noise
		sol::optional<sol::table> CurlOpt = Params.get<sol::optional<sol::table>>("curl_noise");
		if (CurlOpt.has_value())
		{
			sol::table C = CurlOpt.value();
			HM.Effects.CurlNoise.Curl1Amount = static_cast<float>(C.get_or("amount1", static_cast<double>(HM.Effects.CurlNoise.Curl1Amount)));
			HM.Effects.CurlNoise.Curl2Amount = static_cast<float>(C.get_or("amount2", static_cast<double>(HM.Effects.CurlNoise.Curl2Amount)));
			HM.Effects.CurlNoise.Curl1Tiling = static_cast<float>(C.get_or("tiling1", static_cast<double>(HM.Effects.CurlNoise.Curl1Tiling)));
			HM.Effects.CurlNoise.Curl2Tiling = static_cast<float>(C.get_or("tiling2", static_cast<double>(HM.Effects.CurlNoise.Curl2Tiling)));
		}

		// Displacement
		sol::optional<sol::table> DispOpt = Params.get<sol::optional<sol::table>>("displacement");
		if (DispOpt.has_value())
		{
			sol::table D = DispOpt.value();
			HM.Effects.Displacement.DisplacementHeight = static_cast<float>(D.get_or("height", static_cast<double>(HM.Effects.Displacement.DisplacementHeight)));
			HM.Effects.Displacement.DisplacementTiling = static_cast<float>(D.get_or("tiling", static_cast<double>(HM.Effects.Displacement.DisplacementTiling)));
			HM.Effects.Displacement.Midpoint = static_cast<float>(D.get_or("midpoint", static_cast<double>(HM.Effects.Displacement.Midpoint)));
		}

		// Terracing
		sol::optional<sol::table> TerrOpt = Params.get<sol::optional<sol::table>>("terracing");
		if (TerrOpt.has_value())
		{
			sol::table T = TerrOpt.value();
			HM.Effects.Terracing.TerraceAlpha = static_cast<float>(T.get_or("alpha", static_cast<double>(HM.Effects.Terracing.TerraceAlpha)));
			HM.Effects.Terracing.TerraceSpacing = static_cast<float>(T.get_or("spacing", static_cast<double>(HM.Effects.Terracing.TerraceSpacing)));
			HM.Effects.Terracing.TerraceSmoothness = static_cast<float>(T.get_or("smoothness", static_cast<double>(HM.Effects.Terracing.TerraceSmoothness)));
		}

		// Smooth blending
		sol::optional<sol::table> SmoothOpt = Params.get<sol::optional<sol::table>>("smooth_blending");
		if (SmoothOpt.has_value())
		{
			sol::table SB = SmoothOpt.value();
			HM.Effects.SmoothBlending.InnerSmoothDistance = static_cast<float>(SB.get_or("inner", static_cast<double>(HM.Effects.SmoothBlending.InnerSmoothDistance)));
			HM.Effects.SmoothBlending.OuterSmoothDistance = static_cast<float>(SB.get_or("outer", static_cast<double>(HM.Effects.SmoothBlending.OuterSmoothDistance)));
		}

		FOnWaterBodyChangedParams ChangedParams;
		ChangedParams.bShapeOrPositionChanged = true;
		ChangedParams.bUserTriggered = true;
		Comp->UpdateAll(ChangedParams);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif

		Session.Log(FString::Printf(TEXT("[OK] water_set_brush_effects -> Configured brush effects on '%s'"), *Name));
		return true;
	});

	// ================================================================
	// water_ocean_fill_zone(actor_name)
	// ================================================================
	Lua.set_function("water_ocean_fill_zone", [&Session](const std::string& NameStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_ocean_fill_zone -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_ocean_fill_zone -> Could not find water body '%s'"), *Name));
			return false;
		}

		AWaterBodyOcean* Ocean = Cast<AWaterBodyOcean>(WB);
		if (!Ocean)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_ocean_fill_zone -> '%s' is not an ocean water body"), *Name));
			return false;
		}

		UWaterBodyOceanComponent* OceanComp = Cast<UWaterBodyOceanComponent>(Ocean->GetWaterBodyComponent());
		if (!OceanComp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_ocean_fill_zone -> '%s' has no ocean component"), *Name));
			return false;
		}

#if WITH_EDITOR
		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Ocean Fill Zone")));
		Ocean->Modify();

		OceanComp->FillWaterZoneWithOcean();

		FOnWaterBodyChangedParams ChangedParams;
		ChangedParams.bShapeOrPositionChanged = true;
		ChangedParams.bUserTriggered = true;
		OceanComp->UpdateAll(ChangedParams);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		OceanComp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif

		Session.Log(FString::Printf(TEXT("[OK] water_ocean_fill_zone -> Filled zone with ocean '%s'"), *Name));
		return true;
#else
		Session.Log(TEXT("[FAIL] water_ocean_fill_zone -> Only available in editor builds"));
		return false;
#endif
	});

	// ================================================================
	// water_configure_body(actor_name, params)
	// ================================================================
	Lua.set_function("water_configure_body", [&Session](const std::string& NameStr, const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_configure_body -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_configure_body -> Could not find water body '%s'"), *Name));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_configure_body -> Water body '%s' has no component"), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Configure Water Body")));
		FScopedSuppressLandscapeAutoLayerDialog LandscapeLayerDialogGuard;
		Comp->Modify();

		TArray<FString> Applied;

		// Overlap material priority — protected UPROPERTY, route through the shared
		// reflection helper so PreEditChange/PostEditChange fire (otherwise editor
		// state can go stale; see LuaPropertyHelper.h:64 commentary).
		sol::optional<int> PriorityOpt = Params.get<sol::optional<int>>("overlap_material_priority");
		if (PriorityOpt.has_value())
		{
			const int32 Val = FMath::Clamp(PriorityOpt.value(), -8192, 8191);
			NeoLuaProperty::FPropertyValueInput Input;
			Input.NumberValue = static_cast<double>(Val);
			Input.bIsNumber = true;
			FString Error;
			if (NeoLuaProperty::SetNamedPropertyValueWithEditChange(Comp, TEXT("OverlapMaterialPriority"), Input, Error))
			{
				Applied.Add(TEXT("overlap_material_priority"));
			}
			else
			{
				Session.Log(FString::Printf(TEXT("[WARN] water_configure_body -> overlap_material_priority failed: %s"), *Error));
			}
		}

		// Target wave mask depth
		sol::optional<double> WaveMaskOpt = Params.get<sol::optional<double>>("target_wave_mask_depth");
		if (WaveMaskOpt.has_value())
		{
			Comp->TargetWaveMaskDepth = static_cast<float>(WaveMaskOpt.value());
			Applied.Add(TEXT("target_wave_mask_depth"));
		}

		// Max wave height offset
		sol::optional<double> WaveHeightOffsetOpt = Params.get<sol::optional<double>>("max_wave_height_offset");
		if (WaveHeightOffsetOpt.has_value())
		{
			Comp->MaxWaveHeightOffset = static_cast<float>(WaveHeightOffsetOpt.value());
			Applied.Add(TEXT("max_wave_height_offset"));
		}

		// Collision height offset
		sol::optional<double> CollisionOffsetOpt = Params.get<sol::optional<double>>("collision_height_offset");
		if (CollisionOffsetOpt.has_value())
		{
			Comp->CollisionHeightOffset = static_cast<float>(CollisionOffsetOpt.value());
			Applied.Add(TEXT("collision_height_offset"));
		}

		// Shape dilation
		sol::optional<double> ShapeDilationOpt = Params.get<sol::optional<double>>("shape_dilation");
		if (ShapeDilationOpt.has_value())
		{
			Comp->ShapeDilation = static_cast<float>(ShapeDilationOpt.value());
			Applied.Add(TEXT("shape_dilation"));
		}

		// Affects landscape
		sol::optional<bool> AffectsLandscapeOpt = Params.get<sol::optional<bool>>("affects_landscape");
		if (AffectsLandscapeOpt.has_value())
		{
			Comp->bAffectsLandscape = AffectsLandscapeOpt.value();
			Applied.Add(TEXT("affects_landscape"));
		}

		FString RequestedLayerPlacement;
		if (TryGetStringOption(Params, "landscape_layer_position", "water_layer_position", RequestedLayerPlacement))
		{
			Applied.Add(TEXT("landscape_layer_position"));
		}

		// Fixed water depth (used when the assigned water material has Fixed Depth enabled)
		sol::optional<double> FixedDepthOpt = Params.get<sol::optional<double>>("fixed_water_depth");
		if (FixedDepthOpt.has_value())
		{
			NeoLuaProperty::FPropertyValueInput Input;
			Input.NumberValue = FixedDepthOpt.value();
			Input.bIsNumber = true;
			FString Error;
			if (NeoLuaProperty::SetNamedPropertyValueWithEditChange(Comp, TEXT("FixedWaterDepth"), Input, Error))
			{
				Applied.Add(TEXT("fixed_water_depth"));
			}
			else
			{
				Session.Log(FString::Printf(TEXT("[WARN] water_configure_body -> fixed_water_depth failed: %s"), *Error));
			}
		}

		// Generate overlap events (UPrimitiveComponent setter — needed to gate underwater post-process)
		sol::optional<bool> OverlapEvtOpt = Params.get<sol::optional<bool>>("generate_overlap_events");
		if (OverlapEvtOpt.has_value())
		{
			Comp->SetGenerateOverlapEvents(OverlapEvtOpt.value());
			Applied.Add(TEXT("generate_overlap_events"));
		}

		FOnWaterBodyChangedParams ChangedParams;
		ChangedParams.bShapeOrPositionChanged = true;
		ChangedParams.bWeightmapSettingsChanged = true;
		ChangedParams.bUserTriggered = true;
		Comp->UpdateAll(ChangedParams);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif
		ApplyWaterLayerPlacement(World, Params, Session, TEXT("water_configure_body"));

		if (Applied.Num() == 0)
		{
			Session.Log(FString::Printf(TEXT("[WARN] water_configure_body -> '%s' -> no params applied (recognized keys: overlap_material_priority, target_wave_mask_depth, max_wave_height_offset, collision_height_offset, shape_dilation, affects_landscape, landscape_layer_position, fixed_water_depth, generate_overlap_events)"), *Name));
			return false;
		}

		Session.Log(FString::Printf(TEXT("[OK] water_configure_body -> '%s' -> %s"), *Name, *FString::Join(Applied, TEXT(", "))));
		return true;
	});

	// ================================================================
	// water_get_zone(name_or_label)
	// ================================================================
	Lua.set_function("water_get_zone", [&Session](const std::string& NameStr, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_get_zone -> No editor world"));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterZone* WZ = FindWaterZoneByName(World, Name);
		if (!WZ)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_get_zone -> Could not find water zone '%s'"), *Name));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();
		Result["name"] = TCHAR_TO_UTF8(*WZ->GetActorNameOrLabel());
		Result["location"] = VectorToTable(LuaView, WZ->GetActorLocation());

		FVector2D Extent = WZ->GetZoneExtent();
		sol::table ExtentTable = LuaView.create_table();
		ExtentTable["x"] = Extent.X;
		ExtentTable["y"] = Extent.Y;
		Result["extent"] = ExtentTable;

		FIntPoint Resolution = GetWaterZoneRenderTargetResolutionCompat(WZ);
		sol::table ResTable = LuaView.create_table();
		ResTable["x"] = Resolution.X;
		ResTable["y"] = Resolution.Y;
		Result["resolution"] = ResTable;

		Result["overlap_priority"] = WZ->GetOverlapPriority();
		Result["local_tessellation"] = WZ->IsLocalOnlyTessellationEnabled();
		Result["water_zone_index"] = WZ->GetWaterZoneIndex();
		Result["velocity_blur_radius"] = static_cast<int>(WZ->GetVelocityBlurRadius());

		const UWaterMeshComponent* WaterMesh = WZ->GetWaterMeshComponent();
		if (WaterMesh)
		{
			Result["tile_size"] = WaterMesh->GetTileSize();
			Result["tessellation_factor"] = WaterMesh->GetTessellationFactor();
			Result["far_mesh_material"] = WaterMesh->FarDistanceMaterial ? TCHAR_TO_UTF8(*WaterMesh->FarDistanceMaterial->GetPathName()) : "";
		}

		// Count water bodies
		int32 BodyCount = 0;
		sol::table Bodies = LuaView.create_table();
		WZ->ForEachWaterBodyComponent([&](UWaterBodyComponent* WBComp) -> bool
		{
			BodyCount++;
			AWaterBody* WBActor = WBComp->GetWaterBodyActor();
			if (WBActor)
			{
				Bodies[BodyCount] = TCHAR_TO_UTF8(*WBActor->GetActorNameOrLabel());
			}
			return true;
		});
		Result["body_count"] = BodyCount;
		Result["water_bodies"] = Bodies;

		Session.Log(FString::Printf(TEXT("[OK] water_get_zone -> '%s' (%d bodies)"), *Name, BodyCount));
		return Result;
	});

	// ================================================================
	// water_set_zone_override(actor_name, zone_name_or_nil)
	// ================================================================
	Lua.set_function("water_set_zone_override", [&Session](const std::string& NameStr, sol::optional<std::string> ZoneNameOpt, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_set_zone_override -> No editor world"));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_zone_override -> Could not find water body '%s'"), *Name));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_zone_override -> Water body '%s' has no component"), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Zone Override")));
		Comp->Modify();

		if (ZoneNameOpt.has_value())
		{
			FString ZoneName = NeoLuaStr::ToFStringOpt(ZoneNameOpt);
			AWaterZone* Zone = FindWaterZoneByName(World, ZoneName);
			if (!Zone)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_set_zone_override -> Could not find water zone '%s'"), *ZoneName));
				return false;
			}

			Comp->SetWaterZoneOverride(TSoftObjectPtr<AWaterZone>(Zone));
			Comp->UpdateWaterZones();

			Session.Log(FString::Printf(TEXT("[OK] water_set_zone_override -> Set '%s' to zone '%s'"), *Name, *ZoneName));
		}
		else
		{
			// Clear override
			Comp->SetWaterZoneOverride(TSoftObjectPtr<AWaterZone>());
			Comp->UpdateWaterZones();

			Session.Log(FString::Printf(TEXT("[OK] water_set_zone_override -> Cleared zone override on '%s'"), *Name));
		}

		return true;
	});

	// ================================================================
	// water_ocean_get_total_height()
	// ================================================================
	Lua.set_function("water_ocean_get_total_height", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_ocean_get_total_height -> No editor world"));
			return sol::lua_nil;
		}

		UWaterSubsystem* Subsystem = UWaterSubsystem::GetWaterSubsystem(World);
		if (!Subsystem)
		{
			Session.Log(TEXT("[FAIL] water_ocean_get_total_height -> No water subsystem"));
			return sol::lua_nil;
		}

		float TotalHeight = Subsystem->GetOceanTotalHeight();
		Session.Log(FString::Printf(TEXT("[OK] water_ocean_get_total_height -> %.2f (base=%.2f, flood=%.2f)"),
			TotalHeight, Subsystem->GetOceanBaseHeight(), Subsystem->GetOceanFloodHeight()));
		return sol::make_object(LuaView, TotalHeight);
	});

	// ================================================================
	// water_add_exclusion_volume(actor_name, volume_name, mode?)
	//
	// Mutates the authoritative AWaterBodyExclusionVolume::WaterBodies list
	// (NOT the derived UWaterBodyComponent::WaterBodyExclusionVolumes cache),
	// then triggers the standard recompute so component-side membership is
	// rebuilt from the authored state. The component-cache-only path used by
	// AddExclusionVolume() is wiped on the next PostEditMove/PostEditUndo/
	// PostRegisterAllComponents/PostEditChangeProperty (see WaterBodyExclusionVolume.cpp).
	//
	// `mode` (optional): "add_to_exclusion" or "remove_from_exclusion" — sets
	// EWaterExclusionMode on the volume. When omitted, this entry point defaults
	// to AddWaterBodiesListToExclusion so adding a body makes it part of the
	// exclusion instead of inheriting UE's volume-wide default inverse-list mode.
	// ================================================================
	Lua.set_function("water_add_exclusion_volume", [&Session](const std::string& NameStr, const std::string& VolumeNameStr, sol::optional<std::string> ModeOpt, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_add_exclusion_volume -> No editor world. Open a level in the editor first."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_add_exclusion_volume -> Could not find water body '%s'. List candidates with water_list_bodies()."), *Name));
			return false;
		}

		FString VolumeName = NeoLuaStr::ToFString(VolumeNameStr);
		AWaterBodyExclusionVolume* Volume = nullptr;
		for (TActorIterator<AWaterBodyExclusionVolume> It(World); It; ++It)
		{
			if ((*It)->GetActorLabel() == VolumeName || (*It)->GetName() == VolumeName || (*It)->GetActorNameOrLabel() == VolumeName)
			{
				Volume = *It;
				break;
			}
		}

		if (!Volume)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_add_exclusion_volume -> Could not find exclusion volume '%s'. Spawn one with create_asset/find_assets first."), *VolumeName));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Add Water Body to Exclusion Volume")));
		Volume->Modify();

		// Default omitted mode to "add this listed body to the exclusion"; UE's
		// class default is the inverse-list mode, where listed bodies are removed.
		if (ModeOpt.has_value())
		{
			FString ModeStr = NeoLuaStr::ToFStringOpt(ModeOpt);
			if (ModeStr.Equals(TEXT("add_to_exclusion"), ESearchCase::IgnoreCase))
			{
				Volume->ExclusionMode = EWaterExclusionMode::AddWaterBodiesListToExclusion;
			}
			else if (ModeStr.Equals(TEXT("remove_from_exclusion"), ESearchCase::IgnoreCase))
			{
				Volume->ExclusionMode = EWaterExclusionMode::RemoveWaterBodiesListFromExclusion;
			}
			else
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_add_exclusion_volume -> Unknown mode '%s'. Use 'add_to_exclusion' or 'remove_from_exclusion'."), *ModeStr));
				return false;
			}
		}
		else
		{
			Volume->ExclusionMode = EWaterExclusionMode::AddWaterBodiesListToExclusion;
		}

		// Append to authored list — engine recompute reads from here, not from component cache
		TSoftObjectPtr<AWaterBody> SoftBody(WB);
		Volume->WaterBodies.AddUnique(SoftBody);

		FWaterExclusionVolumeChangedParams Params;
		Params.bUserTriggered = true;
		Volume->UpdateOverlappingWaterBodies(Params);
		// AWaterBodyExclusionVolume::UpdateAffectedWaterBodyCollisions is declared
		// `protected` in UE 5.7 (WaterBodyExclusionVolume.h:70), so extensions can't
		// call it directly. Replicate the body (WaterBodyExclusionVolume.cpp:125-141)
		// using public engine APIs — FWaterBodyManager::ForEachWaterBodyComponent
		// and UWaterBodyComponent::OnWaterBodyChanged are both WATER_API-exported.
		{
			TSoftObjectPtr<AWaterBodyExclusionVolume> SoftVolume(Volume);
			FOnWaterBodyChangedParams ChangedParams(Params.PropertyChangedEvent);
			ChangedParams.bUserTriggered = Params.bUserTriggered;
			ChangedParams.bShapeOrPositionChanged = false;
			ChangedParams.bWeightmapSettingsChanged = false;
			FWaterBodyManager::ForEachWaterBodyComponent(Volume->GetWorld(),
				[&SoftVolume, &ChangedParams](UWaterBodyComponent* Comp)
			{
				if (Comp && Comp->ContainsExclusionVolume(SoftVolume))
				{
					Comp->OnWaterBodyChanged(ChangedParams);
				}
				return true;
			});
		}

		Session.Log(FString::Printf(TEXT("[OK] water_add_exclusion_volume -> Added '%s' to '%s' (mode=%s, %d total)"),
			*Name, *VolumeName,
			(Volume->ExclusionMode == EWaterExclusionMode::AddWaterBodiesListToExclusion) ? TEXT("add_to_exclusion") : TEXT("remove_from_exclusion"),
			Volume->WaterBodies.Num()));
		return true;
	});

	// ================================================================
	// water_remove_exclusion_volume(actor_name, volume_name)
	//
	// Mirror of water_add_exclusion_volume — mutates the authoritative
	// AWaterBodyExclusionVolume::WaterBodies list and triggers recompute.
	// ================================================================
	Lua.set_function("water_remove_exclusion_volume", [&Session](const std::string& NameStr, const std::string& VolumeNameStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_remove_exclusion_volume -> No editor world. Open a level in the editor first."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_remove_exclusion_volume -> Could not find water body '%s'. List candidates with water_list_bodies()."), *Name));
			return false;
		}

		FString VolumeName = NeoLuaStr::ToFString(VolumeNameStr);
		AWaterBodyExclusionVolume* Volume = nullptr;
		for (TActorIterator<AWaterBodyExclusionVolume> It(World); It; ++It)
		{
			if ((*It)->GetActorLabel() == VolumeName || (*It)->GetName() == VolumeName || (*It)->GetActorNameOrLabel() == VolumeName)
			{
				Volume = *It;
				break;
			}
		}

		if (!Volume)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_remove_exclusion_volume -> Could not find exclusion volume '%s'."), *VolumeName));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Remove Water Body from Exclusion Volume")));
		Volume->Modify();

		const TSoftObjectPtr<AWaterBody> SoftBody(WB);
		const int32 RemovedCount = Volume->WaterBodies.RemoveAll([&SoftBody](const TSoftObjectPtr<AWaterBody>& Entry)
		{
			return Entry == SoftBody;
		});

		FWaterExclusionVolumeChangedParams Params;
		Params.bUserTriggered = true;
		Volume->UpdateOverlappingWaterBodies(Params);
		// AWaterBodyExclusionVolume::UpdateAffectedWaterBodyCollisions is declared
		// `protected` in UE 5.7 (WaterBodyExclusionVolume.h:70), so extensions can't
		// call it directly. Replicate the body (WaterBodyExclusionVolume.cpp:125-141)
		// using public engine APIs — FWaterBodyManager::ForEachWaterBodyComponent
		// and UWaterBodyComponent::OnWaterBodyChanged are both WATER_API-exported.
		{
			TSoftObjectPtr<AWaterBodyExclusionVolume> SoftVolume(Volume);
			FOnWaterBodyChangedParams ChangedParams(Params.PropertyChangedEvent);
			ChangedParams.bUserTriggered = Params.bUserTriggered;
			ChangedParams.bShapeOrPositionChanged = false;
			ChangedParams.bWeightmapSettingsChanged = false;
			FWaterBodyManager::ForEachWaterBodyComponent(Volume->GetWorld(),
				[&SoftVolume, &ChangedParams](UWaterBodyComponent* Comp)
			{
				if (Comp && Comp->ContainsExclusionVolume(SoftVolume))
				{
					Comp->OnWaterBodyChanged(ChangedParams);
				}
				return true;
			});
		}

		Session.Log(FString::Printf(TEXT("[OK] water_remove_exclusion_volume -> Removed %d entry(ies) of '%s' from '%s' (%d remaining)"),
			RemovedCount, *Name, *VolumeName, Volume->WaterBodies.Num()));
		return true;
	});

	// ================================================================
	// water_list_exclusion_volumes(actor_name)
	//
	// Returns volumes whose component-side cache currently lists this water body
	// (i.e. the volumes the engine has decided overlap-exclude this body), and
	// for each one also surfaces the authored Volume->WaterBodies list so callers
	// can verify edits persisted.
	// ================================================================
	Lua.set_function("water_list_exclusion_volumes", [&Session](const std::string& NameStr, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_list_exclusion_volumes -> No editor world. Open a level in the editor first."));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_list_exclusion_volumes -> Could not find water body '%s'."), *Name));
			return sol::lua_nil;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_list_exclusion_volumes -> Water body '%s' has no component."), *Name));
			return sol::lua_nil;
		}

		// Union of (a) volumes whose derived cache references this body and
		// (b) volumes whose authored list references this body — covers the case
		// where a recompute hasn't yet pushed a new entry into the cache.
		TSet<AWaterBodyExclusionVolume*> Visible;
		for (AWaterBodyExclusionVolume* Vol : Comp->GetExclusionVolumes())
		{
			if (Vol) Visible.Add(Vol);
		}
		for (TActorIterator<AWaterBodyExclusionVolume> It(World); It; ++It)
		{
			AWaterBodyExclusionVolume* Vol = *It;
			if (!Vol) continue;
			for (const TSoftObjectPtr<AWaterBody>& Entry : Vol->WaterBodies)
			{
				if (Entry.Get() == WB)
				{
					Visible.Add(Vol);
					break;
				}
			}
		}

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (AWaterBodyExclusionVolume* Vol : Visible)
		{
			sol::table Entry = LuaView.create_table();
			Entry["name"] = TCHAR_TO_UTF8(*Vol->GetActorNameOrLabel());
			Entry["location"] = VectorToTable(LuaView, Vol->GetActorLocation());

			const char* ModeStr = "remove_from_exclusion";
			if (Vol->ExclusionMode == EWaterExclusionMode::AddWaterBodiesListToExclusion)
			{
				ModeStr = "add_to_exclusion";
			}
			Entry["mode"] = ModeStr;
			Entry["water_body_count"] = Vol->WaterBodies.Num();

			// Surface the AUTHORED list so a round-trip read-after-write confirms persistence.
			sol::table Authored = LuaView.create_table();
			int32 ABIdx = 1;
			for (const TSoftObjectPtr<AWaterBody>& AuthoredBody : Vol->WaterBodies)
			{
				if (AWaterBody* Resolved = AuthoredBody.Get())
				{
					Authored[ABIdx++] = TCHAR_TO_UTF8(*Resolved->GetActorNameOrLabel());
				}
			}
			Entry["authored_water_bodies"] = Authored;

			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] water_list_exclusion_volumes -> %d volume(s) reference '%s'"), Idx - 1, *Name));
		return Result;
	});

	// ================================================================
	// water_river_set_transition_material(actor_name, slot, material_path)
	//
	// Post-spawn override of the river-to-lake/ocean transition materials
	// (UWaterBodyRiverComponent::SetLakeTransitionMaterial / SetOceanTransitionMaterial /
	// SetLakeAndOceanTransitionMaterials). water_spawn applies factory defaults at
	// creation time; this Lua entry point exposes the after-spawn setters.
	//
	// slot: "lake", "ocean", or "both". For "both", pass material_path as
	// "lake_path|ocean_path" (pipe-delimited).
	// ================================================================
	Lua.set_function("water_river_set_transition_material", [&Session](const std::string& NameStr, const std::string& SlotStr, const std::string& MatPathStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_river_set_transition_material -> No editor world. Open a level in the editor first."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_transition_material -> Could not find water body '%s'. List candidates with water_list_bodies('river')."), *Name));
			return false;
		}

		UWaterBodyRiverComponent* RiverComp = Cast<UWaterBodyRiverComponent>(WB->GetWaterBodyComponent());
		if (!RiverComp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_transition_material -> '%s' is not a river (transition materials are river-only)"), *Name));
			return false;
		}

		FString Slot = NeoLuaStr::ToFString(SlotStr);
		FString MatPath = NeoLuaStr::ToFString(MatPathStr);

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set River Transition Material")));
		RiverComp->Modify();

		if (Slot.Equals(TEXT("lake"), ESearchCase::IgnoreCase))
		{
			UMaterialInterface* Mat = NeoLuaAsset::Resolve<UMaterialInterface>(MatPath);
			if (!Mat)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_transition_material -> Could not load material '%s' for lake slot"), *MatPath));
				return false;
			}
			RiverComp->SetLakeTransitionMaterial(Mat);
		}
		else if (Slot.Equals(TEXT("ocean"), ESearchCase::IgnoreCase))
		{
			UMaterialInterface* Mat = NeoLuaAsset::Resolve<UMaterialInterface>(MatPath);
			if (!Mat)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_transition_material -> Could not load material '%s' for ocean slot"), *MatPath));
				return false;
			}
			RiverComp->SetOceanTransitionMaterial(Mat);
		}
		else if (Slot.Equals(TEXT("both"), ESearchCase::IgnoreCase))
		{
			TArray<FString> Parts;
			MatPath.ParseIntoArray(Parts, TEXT("|"), true);
			if (Parts.Num() != 2)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_transition_material -> 'both' slot requires 'lake_path|ocean_path' format, got '%s'"), *MatPath));
				return false;
			}
			UMaterialInterface* LakeMat = NeoLuaAsset::Resolve<UMaterialInterface>(Parts[0]);
			UMaterialInterface* OceanMat = NeoLuaAsset::Resolve<UMaterialInterface>(Parts[1]);
			if (!LakeMat || !OceanMat)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_transition_material -> Could not load materials (lake='%s' valid=%d, ocean='%s' valid=%d)"),
					*Parts[0], LakeMat != nullptr, *Parts[1], OceanMat != nullptr));
				return false;
			}
			RiverComp->SetLakeAndOceanTransitionMaterials(LakeMat, OceanMat);
		}
		else
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_transition_material -> Unknown slot '%s'. Use 'lake', 'ocean', or 'both'."), *Slot));
			return false;
		}

		FOnWaterBodyChangedParams Params;
		Params.bUserTriggered = true;
		RiverComp->UpdateAll(Params);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		RiverComp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif

		Session.Log(FString::Printf(TEXT("[OK] water_river_set_transition_material -> Set %s transition on '%s'"), *Slot, *Name));
		return true;
	});

	// ================================================================
	// water_river_at_key(actor_name, key) -> {width, depth} or nil
	//
	// Continuous-spline-key reads — different from water_river_get_points,
	// which returns one entry per discrete control point. Engine APIs:
	// UWaterBodyRiverComponent::GetRiverWidthAtSplineInputKey /
	// GetRiverDepthAtSplineInputKey.
	// ================================================================
	Lua.set_function("water_river_at_key", [&Session](const std::string& NameStr, double Key, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_river_at_key -> No editor world. Open a level in the editor first."));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_at_key -> Could not find water body '%s'."), *Name));
			return sol::lua_nil;
		}

		UWaterBodyRiverComponent* RiverComp = Cast<UWaterBodyRiverComponent>(WB->GetWaterBodyComponent());
		if (!RiverComp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_at_key -> '%s' is not a river"), *Name));
			return sol::lua_nil;
		}

		const float K = static_cast<float>(Key);
		sol::table Result = LuaView.create_table();
		Result["key"] = K;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		Result["width"] = RiverComp->GetRiverWidthAtSplineInputKey(K);
		Result["depth"] = RiverComp->GetRiverDepthAtSplineInputKey(K);

		Session.Log(FString::Printf(TEXT("[OK] water_river_at_key -> '%s' @ key=%.3f -> width=%.2f depth=%.2f"),
			*Name, K, Result["width"].get<float>(), Result["depth"].get<float>()));
		return Result;
#else
		(void)RiverComp;
		Session.Log(FString::Printf(TEXT("[FAIL] water_river_at_key -> '%s' requires UE 5.7+ (GetRiver{Width,Depth}AtSplineInputKey)"), *Name));
		return sol::lua_nil;
#endif
	});

	// ================================================================
	// water_river_set_at_key(actor_name, key, params)
	//
	// Continuous-spline-key writes for width/depth/velocity/audio. Operates via
	// UWaterBodyRiverComponent::SetRiver{Width,Depth}AtSplineInputKey and
	// UWaterBodyComponent::Set{WaterVelocity,AudioIntensity}AtSplineInputKey.
	// Different from water_river_set_point, which writes the discrete control
	// point at a 1-based index — input keys are continuous (0..NumPoints-1
	// fractional values).
	// ================================================================
	Lua.set_function("water_river_set_at_key", [&Session](const std::string& NameStr, double Key, const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_river_set_at_key -> No editor world. Open a level in the editor first."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_at_key -> Could not find water body '%s'."), *Name));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_at_key -> Water body '%s' has no component."), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set River Spline Key Values")));
		WB->Modify();
		Comp->Modify();

		const float K = static_cast<float>(Key);
		TArray<FString> Applied;

		// width / depth — river-only
		UWaterBodyRiverComponent* RiverComp = Cast<UWaterBodyRiverComponent>(Comp);
		sol::optional<double> WidthOpt = Params.get<sol::optional<double>>("width");
		sol::optional<double> DepthOpt = Params.get<sol::optional<double>>("depth");
		if ((WidthOpt.has_value() || DepthOpt.has_value()) && !RiverComp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_at_key -> 'width'/'depth' params require a river body, '%s' is not a river"), *Name));
			return false;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		if (WidthOpt.has_value())
		{
			RiverComp->SetRiverWidthAtSplineInputKey(K, static_cast<float>(WidthOpt.value()));
			Applied.Add(TEXT("width"));
		}
		if (DepthOpt.has_value())
		{
			RiverComp->SetRiverDepthAtSplineInputKey(K, static_cast<float>(DepthOpt.value()));
			Applied.Add(TEXT("depth"));
		}

		// velocity / audio — available on any UWaterBodyComponent
		sol::optional<double> VelocityOpt = Params.get<sol::optional<double>>("velocity");
		if (VelocityOpt.has_value())
		{
			Comp->SetWaterVelocityAtSplineInputKey(K, static_cast<float>(VelocityOpt.value()));
			Applied.Add(TEXT("velocity"));
		}
		sol::optional<double> AudioOpt = Params.get<sol::optional<double>>("audio");
		if (AudioOpt.has_value())
		{
			Comp->SetAudioIntensityAtSplineInputKey(K, static_cast<float>(AudioOpt.value()));
			Applied.Add(TEXT("audio"));
		}

		if (Applied.Num() == 0)
		{
			Session.Log(FString::Printf(TEXT("[WARN] water_river_set_at_key -> '%s' @ key=%.3f -> no params applied (pass width/depth/velocity/audio)"), *Name, K));
			return false;
		}

		FOnWaterBodyChangedParams ChangedParams;
		ChangedParams.bShapeOrPositionChanged = true;
		ChangedParams.bUserTriggered = true;
		Comp->UpdateAll(ChangedParams);
		Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);

		Session.Log(FString::Printf(TEXT("[OK] water_river_set_at_key -> '%s' @ key=%.3f -> %s"), *Name, K, *FString::Join(Applied, TEXT(", "))));
		return true;
#else
		(void)WidthOpt; (void)DepthOpt; (void)RiverComp; (void)Comp; (void)K; (void)Applied;
		Session.Log(FString::Printf(TEXT("[FAIL] water_river_set_at_key -> '%s' requires UE 5.7+ (SetRiver{Width,Depth}/SetWaterVelocity/SetAudioIntensity AtSplineInputKey)"), *Name));
		return false;
#endif
	});

	// ================================================================
	// water_set_mesh_override(actor_name, mesh_path)
	//
	// Sets the static-mesh override on a water body component
	// (UWaterBodyComponent::SetWaterMeshOverride). Pass an empty string to clear.
	// Used primarily by Custom water bodies, but legal on any type.
	// ================================================================
	Lua.set_function("water_set_mesh_override", [&Session](const std::string& NameStr, const std::string& MeshPathStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_set_mesh_override -> No editor world. Open a level in the editor first."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_mesh_override -> Could not find water body '%s'."), *Name));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_mesh_override -> Water body '%s' has no component."), *Name));
			return false;
		}

		FString MeshPath = NeoLuaStr::ToFString(MeshPathStr);
		UStaticMesh* Mesh = nullptr;
		if (!MeshPath.IsEmpty())
		{
			Mesh = NeoLuaAsset::Resolve<UStaticMesh>(MeshPath);
			if (!Mesh)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_set_mesh_override -> Could not load static mesh '%s'. Pass empty string to clear the override."), *MeshPath));
				return false;
			}
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Mesh Override")));
		Comp->Modify();
		Comp->SetWaterMeshOverride(Mesh);

		FOnWaterBodyChangedParams ChangedParams;
		ChangedParams.bShapeOrPositionChanged = true;
		ChangedParams.bUserTriggered = true;
		Comp->UpdateAll(ChangedParams);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		Comp->MarkOwningWaterZoneForRebuild(EWaterZoneRebuildFlags::All);
#endif

		Session.Log(FString::Printf(TEXT("[OK] water_set_mesh_override -> '%s' -> '%s'"), *Name, Mesh ? *Mesh->GetPathName() : TEXT("(cleared)")));
		return true;
	});

	// ================================================================
	// water_zone_set_far_mesh_material(zone_name, material_path)
	//
	// AWaterZone::SetFarMeshMaterial — controls the material used by the
	// far-distance mesh outside the local-tessellation radius.
	// ================================================================
	Lua.set_function("water_zone_set_far_mesh_material", [&Session](const std::string& NameStr, const std::string& MatPathStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_zone_set_far_mesh_material -> No editor world. Open a level in the editor first."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterZone* WZ = FindWaterZoneByName(World, Name);
		if (!WZ)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_zone_set_far_mesh_material -> Could not find water zone '%s'. List candidates with water_list_zones()."), *Name));
			return false;
		}

		FString MatPath = NeoLuaStr::ToFString(MatPathStr);
		UMaterialInterface* Material = NeoLuaAsset::Resolve<UMaterialInterface>(MatPath);
		if (!Material)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_zone_set_far_mesh_material -> Could not load material '%s'."), *MatPath));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Zone Far Mesh Material")));
		WZ->Modify();
		WZ->SetFarMeshMaterial(Material);
		WZ->MarkForRebuild(EWaterZoneRebuildFlags::All);

		Session.Log(FString::Printf(TEXT("[OK] water_zone_set_far_mesh_material -> '%s' -> '%s'"), *Name, *Material->GetPathName()));
		return true;
	});

	// ================================================================
	// water_set_underwater_post_process(actor_name, params)
	//
	// FUnderwaterPostProcessSettings (WaterBodyComponent.h:42-75) — bEnabled,
	// Priority, BlendRadius, BlendWeight. PostProcessSettings struct itself is
	// huge (200+ fields) and best edited via configure_component reflection.
	// ================================================================
	Lua.set_function("water_set_underwater_post_process", [&Session](const std::string& NameStr, const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_set_underwater_post_process -> No editor world."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_underwater_post_process -> Could not find water body '%s'."), *Name));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_underwater_post_process -> '%s' has no component."), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Underwater Post Process")));
		Comp->Modify();

		FUnderwaterPostProcessSettings& UPP = Comp->UnderwaterPostProcessSettings;
		TArray<FString> Applied;

		sol::optional<bool> EnabledOpt = Params.get<sol::optional<bool>>("enabled");
		if (EnabledOpt.has_value()) { UPP.bEnabled = EnabledOpt.value(); Applied.Add(TEXT("enabled")); }

		if (auto V = Params.get<sol::optional<double>>("priority"))    { UPP.Priority    = static_cast<float>(V.value()); Applied.Add(TEXT("priority")); }
		if (auto V = Params.get<sol::optional<double>>("blend_radius")){ UPP.BlendRadius = static_cast<float>(V.value()); Applied.Add(TEXT("blend_radius")); }
		if (auto V = Params.get<sol::optional<double>>("blend_weight")){ UPP.BlendWeight = FMath::Clamp(static_cast<float>(V.value()), 0.f, 1.f); Applied.Add(TEXT("blend_weight")); }

		if (Applied.Num() == 0)
		{
			Session.Log(FString::Printf(TEXT("[WARN] water_set_underwater_post_process -> '%s' -> no params applied (enabled, priority, blend_radius, blend_weight)"), *Name));
			return false;
		}

		FOnWaterBodyChangedParams ChangedParams;
		ChangedParams.bUserTriggered = true;
		Comp->UpdateAll(ChangedParams);

		Session.Log(FString::Printf(TEXT("[OK] water_set_underwater_post_process -> '%s' -> %s"), *Name, *FString::Join(Applied, TEXT(", "))));
		return true;
	});

	// ================================================================
	// Island lifecycle
	// ================================================================
	auto FindWaterIslandByName = [](UWorld* World, const FString& Id) -> AWaterBodyIsland*
	{
		if (!World) return nullptr;
		for (TActorIterator<AWaterBodyIsland> It(World); It; ++It)
		{
			AWaterBodyIsland* Island = *It;
			if (!Island) continue;
			if (Island->GetActorLabel() == Id || Island->GetName() == Id || Island->GetActorNameOrLabel() == Id)
			{
				return Island;
			}
		}
		return nullptr;
	};

	Lua.set_function("water_list_islands", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_list_islands -> No editor world."));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (TActorIterator<AWaterBodyIsland> It(World); It; ++It)
		{
			AWaterBodyIsland* Island = *It;
			if (!Island) continue;
			sol::table Entry = LuaView.create_table();
			Entry["name"] = TCHAR_TO_UTF8(*Island->GetActorNameOrLabel());
			Entry["location"] = VectorToTable(LuaView, Island->GetActorLocation());
			UWaterSplineComponent* Spline = Island->GetWaterSpline();
			Entry["spline_point_count"] = Spline ? Spline->GetNumberOfSplinePoints() : 0;
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] water_list_islands -> %d islands"), Idx - 1));
		return Result;
	});

	Lua.set_function("water_get_island", [&Session, FindWaterIslandByName](const std::string& NameStr, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_get_island -> No editor world."));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBodyIsland* Island = FindWaterIslandByName(World, Name);
		if (!Island)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_get_island -> Could not find island '%s'."), *Name));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();
		Result["name"] = TCHAR_TO_UTF8(*Island->GetActorNameOrLabel());
		Result["location"] = VectorToTable(LuaView, Island->GetActorLocation());

		// Spline points
		if (UWaterSplineComponent* Spline = Island->GetWaterSpline())
		{
			sol::table Points = LuaView.create_table();
			int32 NumPoints = Spline->GetNumberOfSplinePoints();
			for (int32 i = 0; i < NumPoints; i++)
			{
				sol::table Pt = LuaView.create_table();
				FVector Loc = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
				Pt["x"] = Loc.X;
				Pt["y"] = Loc.Y;
				Pt["z"] = Loc.Z;
				Points[i + 1] = Pt;
			}
			Result["spline_points"] = Points;
		}

		// Curve / heightmap / weightmap
		sol::table Curve = LuaView.create_table();
		Curve["channel_depth"] = Island->WaterCurveSettings.ChannelDepth;
		Curve["channel_edge_offset"] = Island->WaterCurveSettings.ChannelEdgeOffset;
		Curve["use_curve_channel"] = Island->WaterCurveSettings.bUseCurveChannel;
		Result["curve_settings"] = Curve;

		sol::table HMOut = LuaView.create_table();
		const FWaterBodyHeightmapSettings& HM = Island->WaterHeightmapSettings;
		const char* BlendModeStr = "alpha_blend";
		switch (HM.BlendMode)
		{
		case EWaterBrushBlendType::Min: BlendModeStr = "min"; break;
		case EWaterBrushBlendType::Max: BlendModeStr = "max"; break;
		case EWaterBrushBlendType::Additive: BlendModeStr = "additive"; break;
		default: break;
		}
		HMOut["blend_mode"] = BlendModeStr;
		HMOut["falloff_width"] = HM.FalloffSettings.FalloffWidth;
		HMOut["edge_offset"] = HM.FalloffSettings.EdgeOffset;
		HMOut["z_offset"] = HM.FalloffSettings.ZOffset;
		Result["heightmap_settings"] = HMOut;

		sol::table LayersOut = LuaView.create_table();
		for (const auto& KV : Island->WaterWeightmapSettings)
		{
			sol::table Layer = LuaView.create_table();
			Layer["falloff_width"] = KV.Value.FalloffWidth;
			Layer["edge_offset"] = KV.Value.EdgeOffset;
			Layer["opacity"] = KV.Value.FinalOpacity;
			Layer["texture_tiling"] = KV.Value.TextureTiling;
			Layer["texture_influence"] = KV.Value.TextureInfluence;
			Layer["midpoint"] = KV.Value.Midpoint;
			LayersOut[TCHAR_TO_UTF8(*KV.Key.ToString())] = Layer;
		}
		Result["layer_weightmap_settings"] = LayersOut;

		Session.Log(FString::Printf(TEXT("[OK] water_get_island -> '%s'"), *Name));
		return Result;
	});

	Lua.set_function("water_island_configure", [&Session, FindWaterIslandByName](const std::string& NameStr, const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_island_configure -> No editor world."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBodyIsland* Island = FindWaterIslandByName(World, Name);
		if (!Island)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_island_configure -> Could not find island '%s'."), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Configure Water Body Island")));
		Island->Modify();
		TArray<FString> Applied;

		// Heightmap settings
		if (auto HMOpt = Params.get<sol::optional<sol::table>>("heightmap"))
		{
			sol::table HM = HMOpt.value();
			FWaterBodyHeightmapSettings& Settings = Island->WaterHeightmapSettings;

			if (auto BMOpt = HM.get<sol::optional<std::string>>("blend_mode"))
			{
				FString BM = NeoLuaStr::ToFStringOpt(BMOpt);
				if (BM.Equals(TEXT("alpha_blend"), ESearchCase::IgnoreCase)) Settings.BlendMode = EWaterBrushBlendType::AlphaBlend;
				else if (BM.Equals(TEXT("min"), ESearchCase::IgnoreCase)) Settings.BlendMode = EWaterBrushBlendType::Min;
				else if (BM.Equals(TEXT("max"), ESearchCase::IgnoreCase)) Settings.BlendMode = EWaterBrushBlendType::Max;
				else if (BM.Equals(TEXT("additive"), ESearchCase::IgnoreCase)) Settings.BlendMode = EWaterBrushBlendType::Additive;
			}
			Settings.FalloffSettings.FalloffWidth = static_cast<float>(HM.get_or("falloff_width", static_cast<double>(Settings.FalloffSettings.FalloffWidth)));
			Settings.FalloffSettings.EdgeOffset = static_cast<float>(HM.get_or("edge_offset", static_cast<double>(Settings.FalloffSettings.EdgeOffset)));
			Settings.FalloffSettings.ZOffset = static_cast<float>(HM.get_or("z_offset", static_cast<double>(Settings.FalloffSettings.ZOffset)));
			Applied.Add(TEXT("heightmap"));
		}

		// Layer weightmap settings (replaces all layers)
		if (auto LayersOpt = Params.get<sol::optional<sol::table>>("layers"))
		{
			Island->WaterWeightmapSettings.Empty();
			for (auto& Kv : LayersOpt.value())
			{
				auto LayerNameOpt = Kv.first.as<sol::optional<std::string>>();
				auto LayerDataOpt = Kv.second.as<sol::optional<sol::table>>();
				if (!LayerNameOpt.has_value() || !LayerDataOpt.has_value()) continue;

				FName LayerName = FName(UTF8_TO_TCHAR(LayerNameOpt.value().c_str()));
				sol::table LD = LayerDataOpt.value();
				FWaterBodyWeightmapSettings WS;
				WS.FalloffWidth = static_cast<float>(LD.get_or("falloff_width", static_cast<double>(WS.FalloffWidth)));
				WS.EdgeOffset = static_cast<float>(LD.get_or("edge_offset", static_cast<double>(WS.EdgeOffset)));
				WS.FinalOpacity = static_cast<float>(LD.get_or("opacity", static_cast<double>(WS.FinalOpacity)));
				WS.TextureTiling = static_cast<float>(LD.get_or("texture_tiling", static_cast<double>(WS.TextureTiling)));
				WS.TextureInfluence = static_cast<float>(LD.get_or("texture_influence", static_cast<double>(WS.TextureInfluence)));
				WS.Midpoint = static_cast<float>(LD.get_or("midpoint", static_cast<double>(WS.Midpoint)));
				Island->WaterWeightmapSettings.Add(LayerName, WS);
			}
			Applied.Add(TEXT("layers"));
		}

		// Spline points (replaces all)
		if (auto PointsOpt = Params.get<sol::optional<sol::table>>("points"))
		{
			if (UWaterSplineComponent* Spline = Island->GetWaterSpline())
			{
				Spline->Modify();
				Spline->ClearSplinePoints(false);
				for (auto& Kv : PointsOpt.value())
				{
					if (auto PtOpt = Kv.second.as<sol::optional<sol::table>>())
					{
						FVector WorldPt = TableToVector(PtOpt.value());
						FVector LocalPt = Island->GetActorTransform().InverseTransformPosition(WorldPt);
						Spline->AddSplinePoint(LocalPt, ESplineCoordinateSpace::Local, false);
					}
				}
				Spline->UpdateSpline();
				Applied.Add(TEXT("points"));
			}
		}

		Island->UpdateOverlappingWaterBodyComponents();

		if (Applied.Num() == 0)
		{
			Session.Log(FString::Printf(TEXT("[WARN] water_island_configure -> '%s' -> no params applied"), *Name));
			return false;
		}
		Session.Log(FString::Printf(TEXT("[OK] water_island_configure -> '%s' -> %s"), *Name, *FString::Join(Applied, TEXT(", "))));
		return true;
	});

	Lua.set_function("water_remove_island", [&Session, FindWaterIslandByName](const std::string& NameStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_remove_island -> No editor world."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBodyIsland* Island = FindWaterIslandByName(World, Name);
		if (!Island)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_remove_island -> Could not find island '%s'."), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Remove Water Body Island")));
		UEditorActorSubsystem* ActorSub = NeoLuaSubsystem::GetEditor<UEditorActorSubsystem>();
		if (ActorSub && ActorSub->DestroyActor(Island))
		{
			Session.Log(FString::Printf(TEXT("[OK] water_remove_island -> Removed '%s'"), *Name));
			return true;
		}
		Session.Log(FString::Printf(TEXT("[FAIL] water_remove_island -> Failed to destroy '%s'. Make sure it is an editor-world actor."), *Name));
		return false;
	});

	Lua.set_function("water_body_add_island", [&Session, FindWaterIslandByName](const std::string& BodyNameStr, const std::string& IslandNameStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_body_add_island -> No editor world."));
			return false;
		}

		FString BodyName = NeoLuaStr::ToFString(BodyNameStr);
		AWaterBody* WB = FindWaterBodyByName(World, BodyName);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_body_add_island -> Could not find water body '%s'."), *BodyName));
			return false;
		}

		FString IslandName = NeoLuaStr::ToFString(IslandNameStr);
		AWaterBodyIsland* Island = FindWaterIslandByName(World, IslandName);
		if (!Island)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_body_add_island -> Could not find island '%s'."), *IslandName));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_body_add_island -> '%s' has no component."), *BodyName));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Add Island to Water Body")));
		Comp->Modify();
		Comp->AddIsland(Island);

		Session.Log(FString::Printf(TEXT("[OK] water_body_add_island -> Added '%s' to '%s'"), *IslandName, *BodyName));
		return true;
	});

	Lua.set_function("water_body_remove_island", [&Session, FindWaterIslandByName](const std::string& BodyNameStr, const std::string& IslandNameStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_body_remove_island -> No editor world."));
			return false;
		}

		FString BodyName = NeoLuaStr::ToFString(BodyNameStr);
		AWaterBody* WB = FindWaterBodyByName(World, BodyName);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_body_remove_island -> Could not find water body '%s'."), *BodyName));
			return false;
		}

		FString IslandName = NeoLuaStr::ToFString(IslandNameStr);
		AWaterBodyIsland* Island = FindWaterIslandByName(World, IslandName);
		if (!Island)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_body_remove_island -> Could not find island '%s'."), *IslandName));
			return false;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_body_remove_island -> '%s' has no component."), *BodyName));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Remove Island from Water Body")));
		Comp->Modify();
		Comp->RemoveIsland(Island);

		Session.Log(FString::Printf(TEXT("[OK] water_body_remove_island -> Removed '%s' from '%s'"), *IslandName, *BodyName));
		return true;
	});

	Lua.set_function("water_body_list_islands", [&Session](const std::string& NameStr, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_body_list_islands -> No editor world."));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_body_list_islands -> Could not find water body '%s'."), *Name));
			return sol::lua_nil;
		}

		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_body_list_islands -> '%s' has no component."), *Name));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (AWaterBodyIsland* Island : Comp->GetIslands())
		{
			if (!Island) continue;
			sol::table Entry = LuaView.create_table();
			Entry["name"] = TCHAR_TO_UTF8(*Island->GetActorNameOrLabel());
			Entry["location"] = VectorToTable(LuaView, Island->GetActorLocation());
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] water_body_list_islands -> %d islands on '%s'"), Idx - 1, *Name));
		return Result;
	});

	// ================================================================
	// water_zone_configure(zone_name, params)
	// ================================================================
	Lua.set_function("water_zone_configure", [&Session](const std::string& NameStr, const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_zone_configure -> No editor world."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterZone* WZ = FindWaterZoneByName(World, Name);
		if (!WZ)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_zone_configure -> Could not find water zone '%s'."), *Name));
			return false;
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Configure Water Zone")));
		WZ->Modify();
		TArray<FString> Applied;

		// All AWaterZone authoring fields are private (AllowPrivateAccess = "true").
		// Route them through the reflection helper so PreEditChange / PostEditChange fire.
		auto SetZoneNumber = [&](const TCHAR* PropName, double Value, const TCHAR* AppliedKey)
		{
			NeoLuaProperty::FPropertyValueInput Input;
			Input.NumberValue = Value;
			Input.bIsNumber = true;
			FString Error;
			if (NeoLuaProperty::SetNamedPropertyValueWithEditChange(WZ, PropName, Input, Error))
			{
				Applied.Add(AppliedKey);
			}
			else
			{
				Session.Log(FString::Printf(TEXT("[WARN] water_zone_configure -> %s failed: %s"), AppliedKey, *Error));
			}
		};

		auto SetZoneBool = [&](const TCHAR* PropName, bool Value, const TCHAR* AppliedKey)
		{
			NeoLuaProperty::FPropertyValueInput Input;
			Input.BoolValue = Value;
			Input.bIsBool = true;
			FString Error;
			if (NeoLuaProperty::SetNamedPropertyValueWithEditChange(WZ, PropName, Input, Error))
			{
				Applied.Add(AppliedKey);
			}
			else
			{
				Session.Log(FString::Printf(TEXT("[WARN] water_zone_configure -> %s failed: %s"), AppliedKey, *Error));
			}
		};

		if (auto V = Params.get<sol::optional<int>>("overlap_priority"))      SetZoneNumber(TEXT("OverlapPriority"), V.value(), TEXT("overlap_priority"));
		if (auto V = Params.get<sol::optional<int>>("velocity_blur_radius")) SetZoneNumber(TEXT("VelocityBlurRadius"), V.value(), TEXT("velocity_blur_radius"));
		if (auto V = Params.get<sol::optional<double>>("capture_z_offset"))  SetZoneNumber(TEXT("CaptureZOffset"), V.value(), TEXT("capture_z_offset"));
		if (auto V = Params.get<sol::optional<bool>>("half_precision_texture"))             SetZoneBool(TEXT("bHalfPrecisionTexture"), V.value(), TEXT("half_precision_texture"));
		if (auto V = Params.get<sol::optional<bool>>("auto_include_landscapes_as_terrain")) SetZoneBool(TEXT("bAutoIncludeLandscapesAsTerrain"), V.value(), TEXT("auto_include_landscapes_as_terrain"));
		if (auto V = Params.get<sol::optional<bool>>("enable_local_only_tessellation"))     SetZoneBool(TEXT("bEnableLocalOnlyTessellation"), V.value(), TEXT("enable_local_only_tessellation"));

		// LocalTessellationExtent — vector
		if (auto V = Params.get<sol::optional<sol::table>>("local_tessellation_extent"))
		{
			FVector Extent = TableToVector(V.value());
			if (FProperty* Prop = WZ->GetClass()->FindPropertyByName(TEXT("LocalTessellationExtent")))
			{
				if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
				{
					WZ->PreEditChange(Prop);
					FVector* ValuePtr = Prop->ContainerPtrToValuePtr<FVector>(WZ);
					*ValuePtr = Extent;
					FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
					// AWaterZone::PostEditChangeProperty is declared `private` in
					// UE 5.7 (WaterZoneActor.h:155 inside the private: section at
					// :114). Route through UObject* so we call the public base
					// entry point; virtual dispatch still lands on AWaterZone's
					// override.
					static_cast<UObject*>(WZ)->PostEditChangeProperty(Evt);
					Applied.Add(TEXT("local_tessellation_extent"));
				}
			}
		}

		// Nested water_mesh table — write directly (UWaterMeshComponent fields are public)
		if (auto MeshOpt = Params.get<sol::optional<sol::table>>("water_mesh"))
		{
			UWaterMeshComponent* Mesh = WZ->GetWaterMeshComponent();
			if (!Mesh)
			{
				Session.Log(TEXT("[WARN] water_zone_configure -> water_mesh subtable provided but zone has no WaterMeshComponent"));
			}
			else
			{
				sol::table M = MeshOpt.value();
				Mesh->Modify();
				if (auto V = M.get<sol::optional<double>>("tile_size"))                              { Mesh->SetTileSize(static_cast<float>(V.value())); Applied.Add(TEXT("water_mesh.tile_size")); }
				if (auto V = M.get<sol::optional<int>>("tessellation_factor"))
				{
					NeoLuaProperty::FPropertyValueInput In; In.NumberValue = V.value(); In.bIsNumber = true; FString Err;
					if (NeoLuaProperty::SetNamedPropertyValueWithEditChange(Mesh, TEXT("TessellationFactor"), In, Err)) Applied.Add(TEXT("water_mesh.tessellation_factor"));
				}
				if (auto V = M.get<sol::optional<double>>("lod_scale"))
				{
					NeoLuaProperty::FPropertyValueInput In; In.NumberValue = V.value(); In.bIsNumber = true; FString Err;
					if (NeoLuaProperty::SetNamedPropertyValueWithEditChange(Mesh, TEXT("LODScale"), In, Err)) Applied.Add(TEXT("water_mesh.lod_scale"));
				}
				if (auto V = M.get<sol::optional<int>>("force_collapse_density_level"))
				{
					NeoLuaProperty::FPropertyValueInput In; In.NumberValue = V.value(); In.bIsNumber = true; FString Err;
					if (NeoLuaProperty::SetNamedPropertyValueWithEditChange(Mesh, TEXT("ForceCollapseDensityLevel"), In, Err)) Applied.Add(TEXT("water_mesh.force_collapse_density_level"));
				}
				if (auto V = M.get<sol::optional<double>>("far_distance_mesh_extent"))               { Mesh->FarDistanceMeshExtent = static_cast<float>(V.value()); Applied.Add(TEXT("water_mesh.far_distance_mesh_extent")); }
				if (auto V = M.get<sol::optional<bool>>("use_far_mesh_without_ocean"))
				{
					NeoLuaProperty::FPropertyValueInput In; In.BoolValue = V.value(); In.bIsBool = true; FString Err;
					if (NeoLuaProperty::SetNamedPropertyValueWithEditChange(Mesh, TEXT("bUseFarMeshWithoutOcean"), In, Err)) Applied.Add(TEXT("water_mesh.use_far_mesh_without_ocean"));
				}
				if (auto V = M.get<sol::optional<double>>("far_distance_mesh_height_without_ocean"))
				{
					NeoLuaProperty::FPropertyValueInput In; In.NumberValue = V.value(); In.bIsNumber = true; FString Err;
					if (NeoLuaProperty::SetNamedPropertyValueWithEditChange(Mesh, TEXT("FarDistanceMeshHeightWithoutOcean"), In, Err)) Applied.Add(TEXT("water_mesh.far_distance_mesh_height_without_ocean"));
				}
				Mesh->MarkWaterMeshGridDirty();
			}
		}

		WZ->MarkForRebuild(EWaterZoneRebuildFlags::All);

		if (Applied.Num() == 0)
		{
			Session.Log(FString::Printf(TEXT("[WARN] water_zone_configure -> '%s' -> no params applied"), *Name));
			return false;
		}
		Session.Log(FString::Printf(TEXT("[OK] water_zone_configure -> '%s' -> %s"), *Name, *FString::Join(Applied, TEXT(", "))));
		return true;
	});

	// ================================================================
	// water_set_nav_area, water_is_in_exclusion, water_find_spline_key
	// ================================================================
	Lua.set_function("water_set_nav_area", [&Session](const std::string& NameStr, const std::string& ClassPathStr, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_set_nav_area -> No editor world."));
			return false;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_nav_area -> Could not find water body '%s'."), *Name));
			return false;
		}
		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_set_nav_area -> '%s' has no component."), *Name));
			return false;
		}

		FString ClassPath = NeoLuaStr::ToFString(ClassPathStr);
		UClass* NavClass = nullptr;
		if (!ClassPath.IsEmpty())
		{
			NavClass = LoadClass<UNavAreaBase>(nullptr, *ClassPath);
			if (!NavClass)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] water_set_nav_area -> Could not load nav area class '%s' (must be a UClass path to a UNavAreaBase subclass)."), *ClassPath));
				return false;
			}
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("Lua: Set Water Nav Area")));
		Comp->Modify();

		// SetNavAreaClass is public in UE 5.7; the protected UPROPERTY lookup is
		// only used to send the same editor change notification as a details edit.
		if (FProperty* Prop = Comp->GetClass()->FindPropertyByName(TEXT("WaterNavAreaClass")))
		{
			if (CastField<FClassProperty>(Prop))
			{
				Comp->PreEditChange(Prop);
				Comp->SetNavAreaClass(NavClass);
				FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
				// UWaterBodyComponent::PostEditChangeProperty is declared
				// `protected` in UE 5.7 (WaterBodyComponent.h:553 inside the
				// protected: section at :455). Route through UObject* so we
				// call the public base entry point; virtual dispatch still lands
				// on the component's override.
				static_cast<UObject*>(Comp)->PostEditChangeProperty(Evt);
				Session.Log(FString::Printf(TEXT("[OK] water_set_nav_area -> '%s' -> %s"), *Name, NavClass ? *NavClass->GetPathName() : TEXT("(cleared)")));
				return true;
			}
		}
		Session.Log(TEXT("[FAIL] water_set_nav_area -> WaterNavAreaClass property not found on component"));
		return false;
	});

	Lua.set_function("water_is_in_exclusion", [&Session](const std::string& NameStr, const sol::table& LocationTable, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_is_in_exclusion -> No editor world."));
			return false;
		}
		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_is_in_exclusion -> Could not find water body '%s'."), *Name));
			return false;
		}
		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_is_in_exclusion -> '%s' has no component."), *Name));
			return false;
		}
		const FVector Loc = TableToVector(LocationTable);
		const bool bInside = Comp->IsWorldLocationInExclusionVolume(Loc);
		Session.Log(FString::Printf(TEXT("[OK] water_is_in_exclusion -> '%s' @ (%.0f,%.0f,%.0f) = %s"), *Name, Loc.X, Loc.Y, Loc.Z, bInside ? TEXT("true") : TEXT("false")));
		return bInside;
	});

	Lua.set_function("water_find_spline_key", [&Session](const std::string& NameStr, const sol::table& LocationTable, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_find_spline_key -> No editor world."));
			return sol::lua_nil;
		}
		FString Name = NeoLuaStr::ToFString(NameStr);
		AWaterBody* WB = FindWaterBodyByName(World, Name);
		if (!WB)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_find_spline_key -> Could not find water body '%s'."), *Name));
			return sol::lua_nil;
		}
		UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
		if (!Comp)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_find_spline_key -> '%s' has no component."), *Name));
			return sol::lua_nil;
		}
		const FVector Loc = TableToVector(LocationTable);
		const float Key = Comp->FindInputKeyClosestToWorldLocation(Loc);
		Session.Log(FString::Printf(TEXT("[OK] water_find_spline_key -> '%s' @ (%.0f,%.0f,%.0f) = %.4f"), *Name, Loc.X, Loc.Y, Loc.Z, Key));
		return sol::make_object(LuaView, Key);
	});

	// ================================================================
	// water_subsystem_info + water_subsystem_set_wave_time
	// ================================================================
	Lua.set_function("water_subsystem_info", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_subsystem_info -> No editor world."));
			return sol::lua_nil;
		}
		UWaterSubsystem* Sub = UWaterSubsystem::GetWaterSubsystem(World);
		if (!Sub)
		{
			Session.Log(TEXT("[FAIL] water_subsystem_info -> No water subsystem"));
			return sol::lua_nil;
		}

		sol::table R = LuaView.create_table();
		R["shallow_water_enabled"] = Sub->IsShallowWaterSimulationEnabled();
		R["underwater_pp_enabled"] = Sub->IsUnderwaterPostProcessEnabled();
		R["water_rendering_enabled"] = Sub->IsWaterRenderingEnabled();
		R["camera_underwater_depth"] = Sub->GetCameraUnderwaterDepth();
		R["ocean_base_height"] = Sub->GetOceanBaseHeight();
		R["ocean_flood_height"] = Sub->GetOceanFloodHeight();
		R["ocean_total_height"] = Sub->GetOceanTotalHeight();
		R["water_time_seconds"] = Sub->GetWaterTimeSeconds();
		R["smoothed_world_time_seconds"] = Sub->GetSmoothedWorldTimeSeconds();
		R["override_smoothed_seconds"] = Sub->GetOverrideSmoothedWorldTimeSeconds();
		R["should_override_smoothed"] = Sub->GetShouldOverrideSmoothedWorldTimeSeconds();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		R["underwater_collision_trace_distance"] = UWaterSubsystem::GetUnderwaterCollisionTraceDistance();
		R["underwater_precise_trace_distance"] = UWaterSubsystem::GetUnderwaterPreciseTraceDistance();
#endif
		R["shallow_water_max_dynamic_forces"] = UWaterSubsystem::GetShallowWaterMaxDynamicForces();
		R["shallow_water_max_impulse_forces"] = UWaterSubsystem::GetShallowWaterMaxImpulseForces();
		R["shallow_water_render_target_size"] = UWaterSubsystem::GetShallowWaterSimulationRenderTargetSize();

		Session.Log(TEXT("[OK] water_subsystem_info"));
		return R;
	});

	Lua.set_function("water_subsystem_set_wave_time", [&Session](const sol::table& Params, sol::this_state S) -> bool
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_subsystem_set_wave_time -> No editor world."));
			return false;
		}
		UWaterSubsystem* Sub = UWaterSubsystem::GetWaterSubsystem(World);
		if (!Sub)
		{
			Session.Log(TEXT("[FAIL] water_subsystem_set_wave_time -> No water subsystem"));
			return false;
		}

		TArray<FString> Applied;
		if (auto V = Params.get<sol::optional<double>>("smoothed_seconds")) { Sub->SetSmoothedWorldTimeSeconds(static_cast<float>(V.value())); Applied.Add(TEXT("smoothed_seconds")); }
		if (auto V = Params.get<sol::optional<double>>("override_seconds")) { Sub->SetOverrideSmoothedWorldTimeSeconds(static_cast<float>(V.value())); Applied.Add(TEXT("override_seconds")); }
		if (auto V = Params.get<sol::optional<bool>>("should_override"))    { Sub->SetShouldOverrideSmoothedWorldTimeSeconds(V.value()); Applied.Add(TEXT("should_override")); }
		if (auto V = Params.get<sol::optional<bool>>("pause_wave_time"))    { Sub->SetShouldPauseWaveTime(V.value()); Applied.Add(TEXT("pause_wave_time")); }

		if (Applied.Num() == 0)
		{
			Session.Log(TEXT("[WARN] water_subsystem_set_wave_time -> no params applied (smoothed_seconds, override_seconds, should_override, pause_wave_time)"));
			return false;
		}
		Session.Log(FString::Printf(TEXT("[OK] water_subsystem_set_wave_time -> %s"), *FString::Join(Applied, TEXT(", "))));
		return true;
	});

	// ================================================================
	// water_baked_sim_query + water_baked_sim_normal
	// ================================================================
	Lua.set_function("water_baked_sim_query", [&Session](const sol::table& LocationTable, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_baked_sim_query -> No editor world."));
			return sol::lua_nil;
		}
		const FVector Loc = TableToVector(LocationTable);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		// Search every water body's bound baked-sim component for a domain that contains the location.
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			AWaterBody* WB = *It;
			if (!WB) continue;
			UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
			if (!Comp) continue;
			if (!Comp->UseBakedSimulationForQueriesAndPhysics()) continue;
			UBakedShallowWaterSimulationComponent* Baked = Comp->GetBakedShallowWaterSimulation();
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			const UShallowWaterSimulationDataBase* SimulationData = GetBakedShallowWaterData(Baked);
			if (!BakedSimCoversSampleLocation(SimulationData, Loc)) continue;
#else
			if (!Baked || !BakedSimCoversSampleLocation(Baked->SimulationData, Loc)) continue;
#endif

			FVector Velocity;
			float Height = 0.f;
			float Depth = 0.f;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			SimulationData->SampleShallowWaterSimulationAtPosition(Loc, Velocity, Height, Depth);
#else
			Baked->SimulationData.SampleShallowWaterSimulationAtPosition(Loc, Velocity, Height, Depth);
#endif

			sol::table R = LuaView.create_table();
			R["water_height"] = Height;
			R["water_depth"] = Depth;
			R["water_velocity"] = VectorToTable(LuaView, Velocity);
			R["body_name"] = TCHAR_TO_UTF8(*WB->GetActorNameOrLabel());

			Session.Log(FString::Printf(TEXT("[OK] water_baked_sim_query -> '%s' -> height=%.2f depth=%.2f"), *WB->GetActorNameOrLabel(), Height, Depth));
			return R;
		}

		Session.Log(TEXT("[FAIL] water_baked_sim_query -> No water body has a valid baked simulation covering the location. Bake one via the engine's UShallowWaterRiverComponent or assign manually."));
		return sol::lua_nil;
#else
		(void)Loc;
		Session.Log(TEXT("[FAIL] water_baked_sim_query requires UE 5.7+"));
		return sol::lua_nil;
#endif
	});

	Lua.set_function("water_baked_sim_normal", [&Session](const sol::table& LocationTable, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_baked_sim_normal -> No editor world."));
			return sol::lua_nil;
		}
		const FVector Loc = TableToVector(LocationTable);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			AWaterBody* WB = *It;
			if (!WB) continue;
			UWaterBodyComponent* Comp = WB->GetWaterBodyComponent();
			if (!Comp) continue;
			if (!Comp->UseBakedSimulationForQueriesAndPhysics()) continue;
			UBakedShallowWaterSimulationComponent* Baked = Comp->GetBakedShallowWaterSimulation();
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			const UShallowWaterSimulationDataBase* SimulationData = GetBakedShallowWaterData(Baked);
			if (!BakedSimCoversSampleLocation(SimulationData, Loc)) continue;

			const FVector Normal = SimulationData->ComputeShallowWaterSimulationNormalAtPosition(Loc);
#else
			if (!Baked || !BakedSimCoversSampleLocation(Baked->SimulationData, Loc)) continue;

			const FVector Normal = Baked->SimulationData.ComputeShallowWaterSimulationNormalAtPosition(Loc);
#endif
			Session.Log(FString::Printf(TEXT("[OK] water_baked_sim_normal -> '%s' -> (%.3f,%.3f,%.3f)"), *WB->GetActorNameOrLabel(), Normal.X, Normal.Y, Normal.Z));
			return VectorToTable(LuaView, Normal);
		}

		Session.Log(TEXT("[FAIL] water_baked_sim_normal -> No water body has a valid baked simulation covering the location."));
		return sol::lua_nil;
#else
		(void)Loc;
		Session.Log(TEXT("[FAIL] water_baked_sim_normal requires UE 5.7+"));
		return sol::lua_nil;
#endif
	});

	// ================================================================
	// water_terrain_list + water_terrain_get_info
	// ================================================================
	Lua.set_function("water_terrain_list", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_terrain_list -> No editor world."));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;
			UWaterTerrainComponent* Terrain = Actor->FindComponentByClass<UWaterTerrainComponent>();
			if (!Terrain) continue;

			sol::table Entry = LuaView.create_table();
			Entry["name"] = TCHAR_TO_UTF8(*Actor->GetActorNameOrLabel());
			Entry["class"] = TCHAR_TO_UTF8(*Actor->GetClass()->GetName());
			Entry["component"] = TCHAR_TO_UTF8(*Terrain->GetName());
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] water_terrain_list -> %d actors with UWaterTerrainComponent"), Idx - 1));
		return Result;
	});

	Lua.set_function("water_terrain_get_info", [&Session](const std::string& NameStr, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] water_terrain_get_info -> No editor world."));
			return sol::lua_nil;
		}

		FString Name = NeoLuaStr::ToFString(NameStr);
		AActor* Actor = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Cand = *It;
			if (!Cand) continue;
			if (!Cand->FindComponentByClass<UWaterTerrainComponent>()) continue;
			if (Cand->GetActorLabel() == Name || Cand->GetName() == Name || Cand->GetActorNameOrLabel() == Name)
			{
				Actor = Cand;
				break;
			}
		}
		if (!Actor)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] water_terrain_get_info -> No actor with UWaterTerrainComponent named '%s'."), *Name));
			return sol::lua_nil;
		}

		UWaterTerrainComponent* Terrain = Actor->FindComponentByClass<UWaterTerrainComponent>();

		sol::table R = LuaView.create_table();
		R["actor_name"] = TCHAR_TO_UTF8(*Actor->GetActorNameOrLabel());
		R["component"] = TCHAR_TO_UTF8(*Terrain->GetName());

		const FBox2D Bounds = Terrain->GetTerrainBounds();
		sol::table BoundsTable = LuaView.create_table();
		sol::table BMin = LuaView.create_table();
		BMin["x"] = Bounds.Min.X; BMin["y"] = Bounds.Min.Y;
		sol::table BMax = LuaView.create_table();
		BMax["x"] = Bounds.Max.X; BMax["y"] = Bounds.Max.Y;
		BoundsTable["min"] = BMin;
		BoundsTable["max"] = BMax;
		BoundsTable["valid"] = Bounds.bIsValid;
		R["terrain_bounds_2d"] = BoundsTable;

		const TArray<UPrimitiveComponent*> Prims = Terrain->GetTerrainPrimitives();
		R["primitive_count"] = Prims.Num();

		// WaterZoneOverride (protected — read via reflection)
		R["water_zone_override"] = "";
		if (FProperty* Prop = Terrain->GetClass()->FindPropertyByName(TEXT("WaterZoneOverride")))
		{
			if (FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
			{
				FSoftObjectPtr* SoftPtr = SoftProp->ContainerPtrToValuePtr<FSoftObjectPtr>(Terrain);
				if (SoftPtr && !SoftPtr->ToSoftObjectPath().IsNull())
				{
					R["water_zone_override"] = TCHAR_TO_UTF8(*SoftPtr->ToSoftObjectPath().ToString());
				}
			}
		}

		Session.Log(FString::Printf(TEXT("[OK] water_terrain_get_info -> '%s' (%d primitives)"), *Name, Prims.Num()));
		return R;
	});
}

namespace NSAIWaterExtension
{
	const TArray<FLuaFunctionDoc>& GetWaterLuaDocs()
	{
		return WaterDocs;
	}

	void BindWaterLua(sol::state& Lua, FLuaSessionData& Session)
	{
		BindWater(Lua, Session);
	}
}
