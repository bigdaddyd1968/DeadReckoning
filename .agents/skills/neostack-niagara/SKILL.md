---
name: neostack-niagara
description: How to author Niagara VFX systems through `execute_script`. Use when the user asks to create a Niagara system, add or modify emitters, set module parameters, build curve-driven scale/color, configure renderers, or expose user parameters. Niagara is enrichment-only — the methods live on the table returned by `open_asset`.
---

# Niagara

The `Niagara` domain doesn't have global functions. Everything goes through methods on the asset table:

```lua
local ns = open_asset("/Game/NS_Foo")     -- existing system
local ns = create_asset("/Game/NS_Foo", "/Script/Niagara.NiagaraSystem")   -- NEW: see below
```

Use `ns:help()` for the full method list, `ns:info()` for a structured summary.

## Creating a system

There is currently no `niagara` alias in `list_asset_types()`. Pass the full class path:

```lua
local ns = create_asset("/Game/NS_Foo", "/Script/Niagara.NiagaraSystem")
```

A blank system has no emitters. Templates supply complete pre-wired emitters.

## The element types

`add` / `remove` / `list` / `configure` / `get` operate on these element types:

| Type                  | What it is                                              |
|-----------------------|---------------------------------------------------------|
| `emitter`             | Standard (graph-based) emitter                          |
| `stateless_emitter`   | Stateless (lightweight) emitter                         |
| `module`              | A module function call inside a stage                   |
| `assignment_module`   | A "Set Variable" assignment with target attributes      |
| `stateless_module`    | A module on a stateless emitter                         |
| `renderer`            | Sprite / mesh / ribbon / light / etc.                   |
| `user_parameter`      | A user-exposed parameter on the system                  |
| `event_handler`       | An event handler on an emitter                          |
| `simulation_stage`    | A simulation stage on an emitter                        |
| `spawn_info`          | Spawn info entry on a stateless emitter                 |

## Adding emitters from templates

```lua
local templates = ns:list("emitter_templates")
-- common entries: Fountain, ConfettiBurst, DirectionalBurst, OmnidirectionalBurst,
--                 SimpleSpriteBurst, UpwardMeshBurst, …

ns:add("emitter", {
  name = "MyFountain",
  template_asset = "/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain",
})
```

`name` and `template_asset` are both required. `template_asset` must be the **full asset path** (not the friendly name). Empty emitters cannot be created — the engine requires a template.

## Stage names

Modules live inside a stage of an emitter (or directly on the system):

- `SystemSpawn`, `SystemUpdate` — system-level
- `EmitterSpawn`, `EmitterUpdate` — emitter-level (one-shot at emitter init / per-frame at emitter level)
- `ParticleSpawn`, `ParticleUpdate` — per-particle

A stage name is required for `list("modules")`, `configure("module")`, `get("module")`, etc.

## Listing modules and their inputs

```lua
ns:list("modules", {emitter="MyFountain", stage="ParticleSpawn"})
-- → [{name="InitializeParticle", index=0, enabled=true, script="/Niagara/.../InitializeParticle"}, ...]

ns:list("module_inputs", {emitter="MyFountain", stage="EmitterUpdate", module_name="SpawnRate"})
-- → [{name="SpawnRate", value=90.0, value_mode="rapid_iteration", type="NiagaraFloat", full_name="Constants.MyFountain.SpawnRate.SpawnRate"}, ...]
```

Note: the key is `module_name`, **not** `module`. Unrecognised keys produce a `[WARN] key 'X' was not consumed` line — useful for catching typos.

`value_mode` tells you where the value lives:

- `rapid_iteration` — in the script's RI param store, can be set as a literal
- `pin` — a static-switch / override-pin input (set with `{value="..."}`)
- `dynamic_input`, `linked`, `data_interface`, `curve` — graph-structural; set via `{mode="…"}`

## Setting module input values

### Literal values (rapid iteration)

```lua
ns:configure("module", "SpawnRate", {
  emitter = "MyFountain",
  stage = "EmitterUpdate",
  parameters = {
    SpawnRate = 200.0,
    ["Spawn Probability"] = 0.5,
    SpawnGroup = 5,
  },
})
```

Keys are the **input names as shown by `list("module_inputs")`** (short form, with spaces if any — quote them). Returns `true` and `[OK] configure -> N set`.

### Advanced modes

For non-literal values, pass a table with `mode`:

```lua
parameters = {
  -- Bind to a parameter
  Position    = {mode="linked", parameter="Particles.Position"},
  -- Embed HLSL — see "HLSL syntax" below; this is an EXPRESSION, no `return`.
  Speed       = {mode="hlsl", code="1.0 + sin(EngineTime)"},
  -- Replace with a dynamic input script
  Velocity    = {mode="dynamic_input", script="/Niagara/DynamicInputs/Velocity/Add.Add"},
  -- Curves: NiagaraDataInterface{Curve, ColorCurve, Vector2DCurve, VectorCurve, Vector4Curve}
  Alpha       = {mode="curve", keys={{time=0, value=1, interp="linear"}, {time=1, value=0}}},
  Color       = {mode="color_curve", keys={
                  {time=0, color={r=1,g=1,b=1,a=1}},
                  {time=1, color={r=1,g=0,b=0,a=0}}}},
  -- Reset to default (clears any override / RI override)
  WhateverInput = {mode="reset"},
}
```

Curve key interp options: `linear`, `cubic`, `constant`.

#### HLSL syntax — expression, not statement

`mode="hlsl"` takes an **expression**, not a function body. The engine wraps your code
as `Out_X = (Type)(YOUR_CODE);` (`NiagaraHlslTranslator.cpp:8920`), so a `return` keyword
ends up inside parentheses and the VM compiler fails with "unexpected RETURN".

```lua
-- ✓ Right — particle-side expression (Particles.NormalizedAge is in scope per-particle)
Color = {mode="hlsl", code="lerp(float3(1,1,1), float3(1,0,0), Particles.NormalizedAge)"}
-- ✗ Wrong — produces "unexpected RETURN"
Speed = {mode="hlsl", code="return 1.0 + sin(0.5);"}
```

If you write a `return`-prefixed snippet, the binding logs a `[WARN]` immediately so the
mistake is visible before you hit the engine's confusing error.

**HLSL variable scope is restricted.** A custom expression can only reference variables
that have been *encountered earlier* in the compiled script — not arbitrary engine
globals. Common references that DO work:

- Particle-side: `Particles.NormalizedAge`, `Particles.Lifetime`, `Particles.Velocity.x` (any attribute already on the particle)
- System-side: only attributes already written by an earlier module in the stack

`EngineTime` and `Engine.Owner.SystemAge` will fail with `Cannot use variable in custom
expression, it hasn't been encountered yet`. For time-driven math, drive a User
parameter from blueprint code (`set_user_parameter` at runtime) and reference
`User.YourParam` from the HLSL.

For multi-statement logic or things that need engine globals, write a scratch-pad script
instead — `mode="hlsl"` is for one-liners that touch already-in-scope attributes.

### Static switch / non-RI inputs

For boolean static-switch pins:

```lua
parameters = { ["Use Spawn Probability"] = {value="true"} }
```

For **enum**-typed static-switch pins (every `*_Mode` input on `InitializeParticle`,
`UpdateMeshOrientation.Orientation Method`, `EmitterState.LoopBehavior`, etc.), pass the
authored enum name as a plain string — no wrapper needed:

```lua
parameters = {
  ["Lifetime Mode"]      = "Direct Set",         -- ENiagara_LifetimeMode
  ["Color Mode"]         = "Random Range Linear",-- ENiagara_ColorInitializationMode
  ["Orientation Method"] = "Rotation Rate",      -- ENiagara_UpdateMeshOrientationMode
}
```

The binding resolves the enum name through `NeoLuaEnum::ParseRuntime` against the pin's
`UEnum`, so authored names, `EWhatever::ValueName` form, and display-name aliases all
work. If the name doesn't match anything, the error message lists the valid options.

Round-trip is symmetric: `get` returns the enum's authored name as a string. To force the
underlying int (rare), pass `{value=N}` as before.

## Reading values back

```lua
local v = ns:get("module", {emitter="MyFountain", stage="EmitterUpdate", module_name="SpawnRate"}, "SpawnRate")
-- v = {name="SpawnRate", type="NiagaraFloat", value=200.0, value_mode="rapid_iteration", full_name="…"}
```

Or get the whole module:

```lua
ns:get("module", {emitter="MyFountain", stage="EmitterUpdate", module_name="SpawnRate"})
-- → {enabled=true, name="SpawnRate", inputs=[{name=…, value=…, …}, …]}
```

## Module lifecycle

```lua
ns:enable_module({emitter="MyFountain", stage="EmitterUpdate", module_name="SpawnRate", enabled=false})
ns:move_module({emitter="MyFountain", stage="EmitterUpdate", module_name="SpawnRate", new_index=0})
```

## Emitter properties

```lua
ns:configure("emitter", "MyFountain", {
  enabled = true,
  properties = {
    SimTarget         = "GPUComputeSim",   -- or "CPUSim"
    bLocalSpace       = true,
    bDeterminism      = true,
    CalculateBoundsMode = "Fixed",
    FixedBounds       = {min={x=-100,y=-100,z=-100}, max={x=100,y=100,z=100}},
  },
})
```

Field names resolve case-insensitively with snake_case + b-prefix variants — `fixed_bounds` → `FixedBounds`, `local_space` → `bLocalSpace`. Enum values use the authored names exactly: `SortMode="ViewDepth"`, `SimTarget="GPUComputeSim"`.

Other configurable scopes: `system`, `simulation_stage`, `event_handler`, `stateless_emitter`, `stateless_module`, `spawn_info`, `renderer`.

## User parameters

```lua
ns:add("user_parameter", {name="MySpeed", type="Float", default=1.0})
ns:set_user_parameter({name="MySpeed", value=2.5})
ns:get("user_parameter", "MySpeed")    -- → 2.5
ns:rename_user_parameter({old_name="MySpeed", new_name="Speed"})  -- propagates through scripts
```

Supported types: `Float`, `Int`, `Bool`, `Vector`, `Vector2`, `Vector4`, `Color`, `Quat`, `Position`, `Matrix`, plus data interfaces.

## Renderers

```lua
ns:list("renderer_types")              -- discoverable (Sprite, Mesh, Ribbon, Light, …)
ns:add("renderer", {emitter="MyFountain", type="Mesh", properties={Mesh="/Engine/BasicShapes/Cube"}})
ns:configure("renderer", {emitter="MyFountain", index=0}, {properties={SortMode="ViewDepth"}})
```

## Persistence

```lua
ns:compile()    -- returns {success, errors, warnings}
ns:save()       -- returns true
```

Niagara has its own compile pipeline (separate from `FLuaGraphFinalizer`). The plugin auto-recompiles when needed (e.g. when RI is baked out and you change a literal). You usually don't need to call `compile()` manually unless you want to *check* for errors with `validate()` / `run_validation()` first.

## Verification gotcha

In-script reads after a mutation may show stale data because the parameter store update isn't fully published until the script ends. **Verify in a fresh script** (a separate `execute_script` call):

```lua
-- Script 1: mutate
ns:configure("module", "SpawnRate", {emitter="F", stage="EmitterUpdate", parameters={SpawnRate=200}})

-- Script 2: verify (fresh execute_script)
local inputs = ns:list("module_inputs", {emitter="F", stage="EmitterUpdate", module_name="SpawnRate"})
```

## Discovery escape hatches

- `ns:help()` — every method on the system object
- `ns:info()` — emitter/module/renderer/user-param counts
- `ns:list("emitters" | "modules" | "renderers" | "user_parameters" | "event_handlers" | "simulation_stages" | "module_inputs" | "dynamic_inputs" | "scratch_pad_scripts" | "available_modules" | "emitter_templates" | "stateless_modules" | "spawn_infos")`
- `ns:list("renderer_types")` — discover what `add("renderer", {type=…})` accepts
- `ns:list("parameter_definitions")` — system-bound parameter definitions
- `ns:validate()` / `ns:run_validation()` — engine validation report
- `report_issue("…")` — last-resort escape when something's truly missing

## Versioning (advanced)

Niagara emitters can be versioned (multiple variants of the same template):

```lua
ns:version("list",   {emitter="MyFountain"})
ns:version("add",    {emitter="MyFountain", major=2, minor=0})
ns:version("expose", {emitter="MyFountain", major=2, minor=0})
ns:version("delete", {emitter="MyFountain", version_guid="…"})
```

## Scratch-pad scripts

Per-system Niagara scripts you can author and reference from modules. Scratch-pad
scripts use **dedicated methods**, not the generic `add()` dispatcher — `add("scratch_pad_script", ...)` will fail with `unknown type`.

```lua
ns:list("scratch_pad_scripts")
ns:create_scratch_pad_script({name="MyDI", type="DynamicInput"})  -- type=, NOT usage=
ns:rename_scratch_pad_script({old_name="MyDI", new_name="VelocityDI"})
ns:delete_scratch_pad_script({name="VelocityDI"})
```

Valid `type=` values: `Module`, `DynamicInput`, `Function` (case-insensitive). Default is `Module`.

To use a scratch-pad script as a module input, target it with `{scratch_pad="<name>"}` instead of `{emitter, stage}`.
