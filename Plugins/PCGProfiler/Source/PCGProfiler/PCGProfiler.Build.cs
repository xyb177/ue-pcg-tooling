using UnrealBuildTool;

public class PCGProfiler : ModuleRules
{
    public PCGProfiler(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Json",
                "JsonUtilities",
                "Projects",
                "PCG"
            });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "ToolMenus",
                    "UnrealEd",
                    "InputCore",
                    "Slate",
                    "SlateCore"
                });
        }
    }
}
