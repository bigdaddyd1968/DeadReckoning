# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

DeadReckoning is an Unreal Engine 5.8 C++ project (module name `DeadReckoning`). The engine is installed locally at `C:\Program Files\Epic Games\UE_5.8`. The project is very early-stage: the only game-specific C++ so far is a single base character class, and the `Content/` tree is still the stock Epic multi-variant template content (First/Third Person plus Combat/Horror/Platforming/Shooter/SideScrolling variants) used as a starting point.

## Build commands

There is no cross-platform build script (npm/make/etc.) — this is driven by UnrealBuildTool (UBT) directly, or via Visual Studio/Rider using the generated solution.

Regenerate project files after adding/removing source files or changing `.Build.cs` / `.Target.cs`:
```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\GenerateProjectFiles.bat" -project="D:\DeadReckoning\DeadReckoning.uproject" -game
```

Build the editor target (Development config) from the command line:
```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" DeadReckoningEditor Win64 Development -Project="D:\DeadReckoning\DeadReckoning.uproject" -WaitMutex
```

Build the game target instead of the editor by substituting `DeadReckoning` (see `Source/DeadReckoning.Target.cs`) for `DeadReckoningEditor` (`Source/DeadReckoningEditor.Target.cs`). Valid configs: `DebugGame`, `Development`, `Shipping`.

There are two solution files at the repo root:
- `DeadReckoning.slnx` / `DeadReckoning.sln` — the game project + engine, for day-to-day C++ work. Open/build this in Visual Studio or Rider for normal iteration (F5 launches the editor with the game loaded).
- `Automation_DeadReckoning.slnx` — references UnrealBuildTool/AutomationTool's own C# projects (EpicGames.* libraries). Only relevant if modifying build tooling itself, not game code.

There is no unit test suite or linter configured in this repo yet.

## Architecture

- Single Runtime module, `DeadReckoning` (`Source/DeadReckoning/DeadReckoning.Build.cs`), depending on `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`. Split into the conventional `Public/` (headers) and `Private/` (implementation) trees, currently just `Characters/`.
- `ADeadReckoningCharacter_Base` (`Source/DeadReckoning/Public/Characters/DeadReckoningCharacter_Base.h`) is the project's own `ACharacter` subclass and is the intended root for future gameplay character code — it's currently just engine-default boilerplate (empty `BeginPlay`/`Tick`/`SetupPlayerInputComponent` overrides).
- Input uses Unreal's Enhanced Input system (`Content/Input/IMC_Default.uasset`, `IMC_MouseLook.uasset`, and per-action assets under `Content/Input/Actions`), not the legacy input system — new bindings should go through `UEnhancedInputComponent` / `UInputAction`, not `SetupPlayerInputComponent`'s legacy `BindAction` axis/action mappings.
- `Content/` is organized by the stock Epic sample-game layout: `FirstPerson/`, `ThirdPerson/`, and one folder per gameplay variant (`Variant_Combat`, `Variant_Horror`, `Variant_Platforming`, `Variant_Shooter`, `Variant_SideScrolling`), each with its own `Blueprints/`, `Input/`, level (`Lvl_*.umap`), and variant-specific assets (UI, VFX, Anims). `Content/Weapons/` and `Content/LevelPrototyping/` are shared across variants. World Partition external actors/objects live under `Content/__ExternalActors__` and `__ExternalObjects__` per level — don't hand-edit these.
- The default startup map is `/Engine/Maps/Templates/OpenWorld` (`Config/DefaultEngine.ini`, `[/Script/EngineSettings.GameMapsSettings]`).
- `DeadReckoning.uproject` enables a large plugin set beyond the default template — notably `GameplayAbilities`/`AbilitySystemGameFeatureActions`/`TargetingSystem` (GAS), `Mover`/`MoverIntegrations`/`ChaosMover` (movement), `MetaHuman` + `ChaosClothAsset`/`ChaosOutfitAsset` (characters/clothing), `PoseSearch`/`MotionTrajectory`/`MotionWarping`/`BlendStack`/`AnimationWarping` (animation), and `NeoStackAI` (see below). Check this list before assuming a subsystem isn't available.

### NeoStackAI editor automation

The `NeoStackAI` plugin exposes a live Unreal Editor to Claude Code through Lua (`execute_script`) and drives the skills under `.claude/skills/` and `.agents/skills/` (identical content, kept in sync by the plugin — treat `.claude/skills/` as the canonical copy when editing, `.neostack/skills-manifest.json` tracks digests). Relevant skills:
- `neostack-blueprint` — creating/editing Blueprints (variables, components, graph nodes) via `execute_script`.
- `neostack-widget` / `neostack-umg-design` — UMG Widget Blueprint construction and visual design.
- `neostack-game-testing` — driving autonomous PIE playtests.

These skills assume a running Unreal Editor instance with the NeoStackAI plugin loaded; they operate on `.uasset`/Blueprint content, not on the C++ source tree.
