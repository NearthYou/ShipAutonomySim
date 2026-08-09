using UnrealBuildTool;

public class ShipAutonomySim : ModuleRules
{
	public ShipAutonomySim(ReadOnlyTargetRules Target) : base(Target)
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
				"InputCore",
				"EnhancedInput",
				"RenderCore",
				"RHI",
				"ImageCore",
				"ImageWrapper",
				"Json",
				"Water"
			});
	}
}
