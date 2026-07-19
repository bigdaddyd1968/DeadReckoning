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

### GAS configuration lives in ini, not C++

Two GAS settings are config-driven, and both bite silently if missed:

- **Gameplay tags** are declared in `Config/DefaultGameplayTags.ini` (`ImportTagsFromConfig=True`) as `+GameplayTagList` entries — there are no native C++ tag declarations. GhostDash's are `GameplayAbility.Movement.Dash` and `GameplayCue.Dash.Active`; its supporting effects use `Cooldown.Dash` and `Status.Stamina.Regen`. New tags go here; `WarnOnInvalidTags=True`, so typos surface as warnings rather than errors.
- **GameplayCue discovery is scoped** by `+GameplayCueNotifyPaths` in `Config/DefaultGame.ini` to `/Game/_DeadReckoning/GameplayAbilitySystem/Cues`. A `GC_*` asset outside that path **is not registered and will not fire**, with no error. The scope exists because the unset default scans all of `/Game/` — ~10 GB of vendored art. Add a path there rather than dropping cues elsewhere.

### Damage, death, and respawn (Blueprint prototype)

The damage/death loop is prototyped in the `BPC_DeadReckoningCharacter_Base` **EventGraph**, not in C++. Two Enhanced Input test bindings drive it, mapped to keys in `IMC_Default`: **`IA_Damage` (Z)** applies `GE_Status_Damage` to self, **`IA_Heal` (X)** applies `GE_Status_Heal`. Each `IA_Damage` mirrors the Health attribute into a `CurrentHealth` BP variable, and death is detected when that reads ≤ 0 (via a `PlayerDeath` function that returns `Is Player Dead`).

`IA_Damage.Triggered` is **gated on `bIsPlayerDead`** (a dead player ignores damage input) — this is the only trigger for the death sequence, so the gate makes death fire **exactly once per life**. Without it, every extra Z press after Health hit 0 re-ran the whole sequence and spawned another `WBP_PlayerDeath`; respawn only removed the last (cached) one, leaving earlier widgets stuck on screen. That bug was invisible while respawn was a level reload (which destroyed all widgets) and only surfaced once respawn became in-place — keep this guard, and if you add another death trigger, guard it the same way.

On death the graph sets `bIsPlayerDead`, ragdolls the mesh (`SetSimulatePhysics` + `SetCollisionEnabled(PhysicsOnly)` + `DisableMovement`), then shows `WBP_PlayerDeath` ("You are Dead!") via `Create Widget → Add to Viewport`, caching the instance in the `PlayerDeathWidget` variable. The `Respawn` custom event waits 5 s, calls `Remove from Parent` on `PlayerDeathWidget`, then **respawns the player in place at the level's `PlayerStart`** (it used to `Open Level`-reload `DefaultTestLevel`, but no longer). The respawn sequence: `GetActorOfClass(PlayerStart)` → un-ragdoll the mesh (`SetSimulatePhysics(false)` + `SetCollisionEnabled(QueryOnly)` + re-snap the mesh to its default relative transform `(0,0,-90)` / yaw `-90`) → `SetMovementMode(Walking)` (undoing `DisableMovement`) → `SetActorLocationAndRotation` teleport to the PlayerStart → `ApplyGameplayEffectToSelf(GE_Respawn_Reset)` → clear `bIsPlayerDead` / reset `CurrentHealth`. Because it is **no longer a level reload, per-life state must be reset explicitly** — that's what the tail of the sequence does; if you add new per-life state, reset it here too. `WBP_PlayerDeath` uses the `BloodieCurse` font under `Content/_DeadReckoning/Fonts/`. A widget `Create`d but never `Add`ed to the viewport will not render regardless of visibility — a mistake this flow previously had.

`GE_Respawn_Reset` (in the Effects folder) is the health/stamina restore: an **Instant** GE whose two `Override` modifiers set Health→MaxHealth and Stamina→MaxStamina via *attribute-based* magnitude (coefficient 1, captured from Target). This exists because a direct reset isn't reachable from Blueprint any other way: the `UDeadReckoningAttributeSet` attributes are `BlueprintReadOnly` (no `Set Health` node compiles) and `SetNumericAttributeBase` isn't Blueprint-exposed, so a GameplayEffect is the only path. `Override` gives an exact reset regardless of current value. Author GE modifiers through the **NeoStack GameplayEffect bridge** (`bp:add("modifier", {attribute="DeadReckoningAttributeSet.Health", op="Override", magnitude={type="AttributeBased", backing_attribute="DeadReckoningAttributeSet.MaxHealth", ...}})`), **not** Python `set_editor_property` — nearly every `GameplayModifierInfo` field is `EditDefaultsOnly` and the Python setter rejects it ("cannot be edited on instances").

`UDeadReckoningAttributeSet::PostGameplayEffectExecute` clamps Health to `[0, MaxHealth]` after any instant effect modifies it, so a heal (`GE_Status_Heal` is `AddBase` +20) can't push Health over the cap. It's the set's only clamp — `PreAttributeChange` isn't overridden, and the accessors (`GetHealth`/`SetHealth`/`GetMaxHealth`/`GetHealthAttribute`) come from the `ATTRIBUTE_ACCESSORS_BASIC` macro. The base virtual's parameter type is `FGameplayEffectMod`**`CallbackData`** (`Mod`, *not* `ModifierCallbackData` — that misspelling compiles as an undefined type and fails the `override`), defined in `GameplayEffectExtension.h` (include it in the `.cpp`). This is a C++ change: it needs a module rebuild (close the editor — it holds `UnrealEditor-DeadReckoning.dll` — then `Build.bat DeadReckoningEditor`), not just a Blueprint recompile.

`WBP_PlayerDeath` fades itself in: its own EventGraph sets `Render Opacity` to 0 on `Construct` and eases it toward 1 on `Tick` via `FInterp To` (`GetRenderOpacity → FInterp To(Target=1, Interp Speed=5) → SetRenderOpacity`, self as target). This is **graph-driven rather than a UMG MovieScene animation on purpose** — the NeoStack tooling can create widget animations but can't author their tracks/keyframes, and UMG's `Animations` array is protected from the Python API, so a real opacity track isn't reachable; `add_timeline` is Actor-only and doesn't apply to widget BPs. Don't burn time trying to author a MovieScene track for this — extend the Tick fade instead.

### Content layout

- **`Content/_DeadReckoning/`** is the project's own content root — the underscore sorts it to the top. It holds the Blueprint counterparts to the C++ spine (`BPC_DeadReckoningCharacter_Base`, `GM_DeadReckoningGameMode`, `PC_DeadReckoningPlayerController`), `Maps/` (`DefaultTestLevel` — the entry point), and `GameplayAbilitySystem/` split into `Abilities/` (`GA_*`), `Effects/` (`GE_*`), and `Cues/` (`GC_*`) — GhostDash is the reference vertical slice across all three. **New project content goes here**, not in the stock folders.
- Everything else is third-party/Fab marketplace art (`Fab/`, `Western_Pack/`, `Gothic_Environment/`, `ModularGothicFantasyEnvironment/`, `Grz_Archer_Pack/`, `HS_WeaponPack_03/`, `BlinkAndDashVFX/`, `WhooshSFXPackLite/`, the various AnimSets) plus `Input/`, `Characters/`, and `LevelPrototyping/`. Treat the packs as vendored assets — source material for the gothic-western art direction, not code to maintain. `Content/StarterContent/` is gitignored: stock Epic sample content, re-addable from the engine, not worth ~636 MB of LFS storage.
- **The stock Epic template content is gone** — FirstPerson, ThirdPerson, the `Variant_*` slices, and `Weapons/` were all removed once the project had its own entry point, along with their external actors. Don't reintroduce them. Several vendored packs ship their own `ThirdPerson_AnimBP`-style assets; those are self-contained pack content and unrelated to the deleted `/Game/ThirdPerson/`.
- Startup map, editor startup map, and `GlobalDefaultGameMode` all point at project-owned content (`Config/DefaultEngine.ini`): `/Game/_DeadReckoning/Maps/DefaultTestLevel` and `GM_DeadReckoningGameMode_C`. This has changed more than once — **read `DefaultEngine.ini` rather than trusting any map path quoted here**.
- `DefaultTestLevel` is a regular (non-World Partition) level, so there's currently no `Content/__ExternalActors__/__ExternalObjects__` tree — those folders existed for the deleted stock template maps. If a future level enables World Partition, its external actors/objects will live under `Content/__ExternalActors__/<MapFolder>/` and `__ExternalObjects__/`, not beside the `.umap`; don't hand-edit them, and remove that folder alongside the map if it's ever deleted.

### Git LFS

All Unreal assets are stored in **Git LFS** (~4500 files). `.gitattributes` routes `*.uasset`, `*.umap`, `*.ubulk`, `*.uexp`, and common media (`*.png`, `*.tga`, `*.fbx`, `*.wav`, …) through the LFS filter, so new assets are converted automatically on `git add` — no per-file setup. It also pins the repo's line-ending policy (`* text=auto`) so behavior doesn't depend on each machine's `core.autocrlf`.

Consequences worth knowing:
- `git show HEAD:<asset>` yields a **pointer file**, not asset bytes. To compare real content, use the working tree or `git lfs` tooling.
- If assets ever appear as ~130-byte text files starting with `version https://git-lfs.github.com/spec/v1`, the smudge filter didn't run — `git lfs checkout` restores them from the local object store.
- Binary `.uasset` merge conflicts can't be resolved by merging; pick a side, then re-`git add` so the clean filter regenerates the pointer.
- The remote is `github.com/bigdaddyd1968/DeadReckoning` (branch `master`); ordinary `git push` works and uploads LFS objects. But the full vendored art is ~10 GB — far past GitHub's free 1 GB LFS tier — and at least one texture exceeds GitHub's hard 100 MB per-file limit, so pushing *everything* would need paid LFS capacity or self-hosting. Only project-authored assets have been pushed so far; **don't assume the remote is a complete mirror**.

### Working with a running editor

The Unreal Editor holds OS locks on `.uasset`/`.umap` files it has loaded. **Git operations that write to the working tree (`git rm`, `git stash`, `git checkout`) fail with "Invalid argument" / "Permission denied" while the editor is open**, sometimes after partially applying — leaving files deleted from disk mid-operation. Close the editor before history rewrites or bulk file operations; commits are fine, since they only read.

The editor also writes config while running (`Config/DefaultEditor.ini` accumulates `AdvancedPreviewScene.SharedProfiles`), so working-tree churn there is usually incidental editor state, not authored change.

Prefer deleting or renaming assets **through the editor** rather than `git rm`: it runs reference fixup. `EditorAssetLibrary.find_package_referencers_for_asset` is a good pre-flight check.

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

The `neostack-connect` Claude Desktop extension bridges to the editor. It is **pinned to one project** via `project_dir` in `%APPDATA%\Claude\Claude Extensions Settings\local.dxt.neostack.neostack-connect.json`, which the extension maps to `NEOSTACK_PROJECT_DIR` **when the proxy process launches** — editing it requires restarting Claude Desktop to take effect. If it points at another project (this user works across several), the connector exposes only `unreal_status`/`list_unreal_projects` instead of `execute_script`. Leaving `project_dir` unset auto-connects when exactly one NeoStackAI editor is running.

The editor publishes `Saved/NeoStackAI/runtime.json` (project path, editor PID, heartbeat) and hosts its own HTTP MCP server — `execute_script` can be driven directly at that URL with JSON-RPC if the extension is misconfigured (initialize to get an `MCP-Session-Id`, send `notifications/initialized`, then `tools/call` `execute_script`). Lua `execute_script` exposes `execute_python(code)`, which unlocks Unreal's full Python API (`import unreal`) for anything the Lua surface lacks — asset deletion, reference queries, world settings. Note `print()` is required for output; a bare `return` yields nothing (Lua `return` tables aren't surfaced either — only `print`/`log` and image observes come back).

When driving `neostack-game-testing` playtests, two gotchas bite: (1) synthetic `playtest_key`/`playtest_click` events don't reach the PIE game — fire Enhanced Input actions with `playtest_input_action` (which calls `InjectInputForAction`) by asset path, e.g. `/Game/Input/Actions/IA_Damage`. (2) `playtest_observe` and console `HighResShot` capture the 3-D scene render target only, **not** the Slate/UMG layer, so on-screen widgets never appear in those screenshots; verify UMG at runtime via `execute_python` instead — `unreal.WidgetLibrary.get_all_widgets_of_class(world, cls, False)` then `is_in_viewport()` (it's `WidgetLibrary` in 5.8, not the older `WidgetBlueprintLibrary`). Many `UserWidget`/controller properties (`Player`, `WidgetTree`) and local-player subsystems are protected and unreadable from Python.
