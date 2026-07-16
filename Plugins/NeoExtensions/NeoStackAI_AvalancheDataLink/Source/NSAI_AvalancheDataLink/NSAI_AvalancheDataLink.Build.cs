using UnrealBuildTool;

public class NSAI_AvalancheDataLink : ModuleRules
{
    public NSAI_AvalancheDataLink(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDefinitions.Add("LUA_BUILD_AS_DLL=1");

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "NeoStackAI",
            "UnrealEd",
            "Avalanche",
            "AvalancheDataLink",
            "AvalancheRemoteControl",
            "DataLink",
            "DataLinkEdGraph",
            "RemoteControl",
            "RemoteControlLogic"
        });
    }
}
