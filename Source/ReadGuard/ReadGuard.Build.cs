// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class ReadGuard : ModuleRules
{
	public ReadGuard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module, and that is the whole architecture.
		//
		// Everything this plugin claims is a measurement of what is on screen at the moment somebody asks:
		// the geometry Slate actually allotted, the glyph height the font cache actually produced, the pixels
		// the renderer actually drew. None of that exists in the editor with no game standing, and all of it
		// has to keep existing in a cooked Shipping build - which is the build that gets submitted and the
		// build where a nine-pixel line is still nine pixels.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",

			// AHUD, UCanvas, UGameInstanceSubsystem, UGameViewportClient, FViewport, and
			// UUserInterfaceSettings::GetDPIScaleBasedOnSize - the DPI curve this plugin exists to recompute.
			"Engine",

			// UUserWidget, UWidgetTree, UTextBlock, URichTextBlock. The text blocks are found through UMG
			// because UMG is what carries the names a person can act on: STextBlock has geometry, but only
			// the UWidget in front of it knows it is called HealthLabel and lives in WBP_PlayerHUD.
			"UMG",

			// FSlateApplication - the application scale that the 200 percent pass really sets, and the
			// renderer whose font measure service reports the glyph height in device pixels.
			"Slate",

			// FGeometry, FSlateFontInfo, FSlateFontMeasure, FSlateRect.
			"SlateCore",

			// UReadGuardSettings is a UDeveloperSettings, so the thresholds appear under
			// Project Settings > Plugins > ReadGuard with no editor module involved.
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// GWhiteTexture - the one-pixel texture the report panel is tiled from.
			"RenderCore",

			// FReadSurfaceDataFlags, and the pixel formats behind the captured frame.
			"RHI",

			// Saved/ReadGuard/report.json. The field names in that file are a published interface that build
			// scripts grep, so they are spelled out by hand with a JSON writer rather than reflected off the
			// C++ member names by FJsonObjectConverter - renaming a member must never rename a field.
			"Json",
			"JsonUtilities",
		});

		// Deliberately NOT here:
		//   UnrealEd - there is nothing to measure without a running game, so an editor module would have
		//              nothing to do but pretend. The scan runs in PIE, in Standalone and in the packaged
		//              build, and it is the same code in all three.
	}
}
