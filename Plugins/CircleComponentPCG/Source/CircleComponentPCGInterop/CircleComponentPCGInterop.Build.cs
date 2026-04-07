using UnrealBuildTool;

public class CircleComponentPCGInterop : ModuleRules
{
	public CircleComponentPCGInterop(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"PCG",
				"CircleComponentPCG"
			}
		);
	}
}
