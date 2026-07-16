# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Dead Reckoning is a **Gothic Western Action RPG** (`Config/DefaultGame.ini`) built in Unreal Engine 5.8 by Speed Demon Games / Vegas Visual Design, LLP. The engine is installed locally at `C:\Program Files\Epic Games\UE_5.8`.

"Dead Reckoning" is the **game's title**, not the navigation/extrapolation technique — `DeadReckoning*` prefixes are project namespacing, nothing more. Nothing in this project extrapolates position from velocity; `UDeadReckoningAttributeSet`, despite the name, is an ordinary GAS attribute set holding Health and Stamina.

## Build commands

No cross-platform build script (npm/make/etc.) — builds run through UnrealBuildTool (UBT) directly, or via Visual Studio/Rider using the generated solution.

Regenerate project files after adding/removing source files or changing `.Build.cs` / `.Target.cs`:
```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\GenerateProjectFiles.bat" -project="D:\DeadReckoning\DeadReckoning.uproject" -game
```

Build the editor target (Development config):
```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" DeadReckoningEditor Win64 Development -Project="D:\DeadReckoning\DeadReckoning.uproject" -WaitMutex
```

Build the game target instead of the editor by substituting `DeadReckoning` (`Source/DeadReckoning.Target.cs`) for `DeadReckoningEditor` (`Source/DeadReckoningEditor.Target.cs`). Valid configs: `DebugGame`, `Development`, `Shipping`.

Solution files at the repo root:
- `DeadReckoning.slnx` / `DeadReckoning.sln` — game project + engine, for day-to-day C++ work. F5 launches the editor with the game loaded.
- `Automation_DeadReckoning.slnx` — UnrealBuildTool/AutomationTool's own C# projects (EpicGames.* libraries). Only for modifying build tooling, not game code.

No unit test suite or linter is configured.

## Architecture

Single Runtime module, `DeadReckoning` (`Source/DeadReckoning/DeadReckoning.Build.cs`), in the conventional `Public/` / `Private/` split. Public deps: `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`, `UMG`, `Niagara`. Private deps: `GameplayAbilities`, `GameplayTags`, `GameplayTasks`.

The C++ layer is a thin, deliberately Blueprint-extensible spine — each class has a Blueprint subclass in `Content/_DeadReckoning/` that supplies the actual asset references:

- **`ADeadReckoningCharacter_Base`** (`Public/Characters/`) — `abstract` ACharacter implementing `IAbilitySystemInterface`. Owns the `UAbilitySystemComponent` (replicated, `Mixed` mode via the `AscReplicationMode` property) and the `UDeadReckoningAttributeSet`, plus a SpringArm→Camera boom rig. Ability actor info is initialized in **both** `PossessedBy` (server) and `OnRep_PlayerState` (client) — the standard GAS pattern; keep both paths if you touch ASC init.
- **`ADeadReckoningPlayerController`** (`Public/`) — `abstract`. Owns Enhanced Input mapping contexts as an editable `DefaultMappingContexts` array and registers them in `SetupInputComponent`, guarded by `IsLocalPlayerController()`. **IMCs belong to the controller, not the character** — add new mapping contexts to that array in the Blueprint, don't register them from the character.
- **`ADeadReckoningGameMode`** (`Public/`) — `AGameModeBase` subclass, currently a stub constructor.
- **`UDeadReckoningAttributeSet`** (`Public/GameplayAbilitySystem/AttributeSets/`) — Health/MaxHealth/Stamina/MaxStamina, each `ReplicatedUsing` an `OnRep_` that calls `GAMEPLAYATTRIBUTE_REPNOTIFY`. New attributes must follow that pattern *and* be added to `GetLifetimeReplicatedProps`.

**Input routing convention:** `SetupPlayerInputComponent` binds Enhanced Input actions to thin private handlers (`Move`/`Look`) that unpack `FInputActionValue` and immediately delegate to `virtual BlueprintCallable` verbs (`DoMove`/`DoLook`/`DoJumpStart`/`DoJumpEnd`). New input should preserve this split so UI/touch and AI can drive the same verbs without synthesizing input events. Enhanced Input only — never legacy axis/action `BindAction`.

Log category `LogDeadReckoning` is declared in `Source/DeadReckoning/DeadReckoning.h`; the character also declares its own `LogDeadReckoningCharacter_Base`.

### Content layout

- **`Content/_DeadReckoning/`** is the project's own content root — the underscore sorts it to the top. It holds the Blueprint counterparts to the C++ spine (`BPC_DeadReckoningCharacter_Base`, `GM_DeadReckoningGameMode`, `PC_DeadReckoningPlayerController`) and `GameplayAbilitySystem/` split into `Abilities/` (`GA_*`), `Effects/` (`GE_*`), and `Cues/` (`GC_*`) — GhostDash is the reference vertical slice across all three. **New project content goes here**, not in the stock folders.
- Everything else is stock Epic template content (`FirstPerson/`, `ThirdPerson/`, `Variant_*`, `StarterContent/`, `LevelPrototyping/`) or third-party/Fab marketplace packs (`Fab/`, `Western_Pack/`, `Gothic_Environment/`, `ModularGothicFantasyEnvironment/`, `Grz_Archer_Pack/`, `HS_WeaponPack_03/`, the various AnimSets). Treat these as vendored assets — they're source material for the gothic-western art direction, not code to maintain.
- Startup and default map is `/Game/ThirdPerson/Lvl_ThirdPerson` (`Config/DefaultEngine.ini`).
- World Partition external actors/objects live under `Content/__ExternalActors__` and `__ExternalObjects__` per level — don't hand-edit these.

`DeadReckoning.uproject` enables ~95 plugins, far beyond the default template — notably GAS (`GameplayAbilities`, `AbilitySystemGameFeatureActions`, `TargetingSystem`), movement (`Mover`, `MoverIntegrations`, `ChaosMover`), characters/clothing (`MetaHuman`, `ChaosClothAsset`, `ChaosOutfitAsset`), animation (`PoseSearch`, `MotionTrajectory`, `MotionWarping`, `BlendStack`, `AnimationWarping`), and `CommonUI`. Check that list before assuming a subsystem isn't available.

### Conventions

New C++ files carry the project copyright header, matching `CopyrightNotice` in `Config/DefaultGame.ini`:
```cpp
// Copyright 2026 by M. Duane Forkner Jr, Speed Demon Games and Vegas Visual Design, LLP. All rights reserved.
```
This is applied consistently: every gameplay class under `Public/` and `Private/` carries it. The only files with Epic's header are the module glue (`DeadReckoning.h/.cpp`) and the UBT scripts (`DeadReckoning.Build.cs`, both `.Target.cs`) — leave those as-is; they're Epic-authored template files, not project code.

### NeoStackAI editor automation

The `NeoStackAI` plugin exposes a live Unreal Editor to Claude Code through Lua (`execute_script`) and drives the skills under `.claude/skills/` and `.agents/skills/` (identical content, kept in sync by the plugin — treat `.claude/skills/` as canonical when editing; `.neostack/skills-manifest.json` tracks digests). Skills: `neostack-blueprint` (Blueprint variables/components/graph nodes), `neostack-widget` / `neostack-umg-design` (UMG), `neostack-niagara` / `neostack-niagara-design` (VFX), `neostack-game-testing` (autonomous PIE playtests).

These skills require a running editor with the plugin loaded, and operate on `.uasset`/Blueprint content, not the C++ source tree. `Plugins/NeoExtensions/` holds ~20 `NeoStackAI_*` subsystem bridges (GameplayAbilities, Niagara, ControlRig, MetaHuman, EnhancedInput, …) that back them; `Plugins/Developer/RiderLink` is Rider's IDE integration. Both are tooling — not game code.
