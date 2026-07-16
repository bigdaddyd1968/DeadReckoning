using UnrealBuildTool;
using System.IO;

public class NSAI_ControlRig : ModuleRules
{
    public NSAI_ControlRig(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Lua and sol2 are exposed by the NeoStack AI core module as public includes.
        PrivateDefinitions.Add("NSAI_CONTROLRIG_MODULE=1");
        PrivateDefinitions.Add("LUA_BUILD_AS_DLL=1");
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "NeoStackAI",
            "UnrealEd",
            "ControlRig",
            "ControlRigDeveloper",
            "RigVM",
            "RigVMDeveloper",
            "Kismet",
            "BlueprintGraph",
        });
    }
}
