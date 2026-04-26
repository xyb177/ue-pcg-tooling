using UnrealBuildTool;

public class RuntimeSpawnBudget : ModuleRules
{
    public RuntimeSpawnBudget(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "DeveloperSettings"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Projects"
            });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "InputCore",
                    "Slate",
                    "SlateCore",
                    "UMG",
                    "UnrealEd"
                });
        }
    }
}
