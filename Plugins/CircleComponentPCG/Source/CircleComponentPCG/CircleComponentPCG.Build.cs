using UnrealBuildTool;

public class CircleComponentPCG : ModuleRules
{
	public CircleComponentPCG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"SlateCore",
				"RenderCore",
				"RHI"
			}
		);
	}
}
