// Copyright Khaos Games. All Rights Reserved.

using UnrealBuildTool;

public class KhaosDocsEditor : ModuleRules
{
	public KhaosDocsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"ContentBrowser",
				"ContentBrowserData",
				"ContentBrowserFileDataSource",
				"InputCore",
				"Projects",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd",
				"WorkspaceMenuStructure",
			}
		);
	}
}
