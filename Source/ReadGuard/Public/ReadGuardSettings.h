// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ReadGuardTypes.h"
#include "ReadGuardSettings.generated.h"

/**
 * Project-wide settings for ReadGuard, under Project Settings -> Plugins -> ReadGuard.
 *
 * Three things in here decide what the plugin is worth.
 *
 * TargetResolution is the first and it is the whole idea. Everybody builds interfaces on the monitor in
 * front of them, and the smallest screen the game will ever run on is somewhere else entirely. Every pixel
 * height in every report is converted onto this resolution through the project's own DPI curve, so the
 * question stops being "does it look right here" and becomes "how tall is it there".
 *
 * MinimumPixelHeight is the second. Fourteen pixels is a defensible floor and it is not a law; a game
 * played at arm's length on a handheld wants more, and a strategy game full of dense tables may argue for
 * less. It is a setting because the right answer depends on how far away the player is sitting, and no
 * plugin knows that.
 *
 * ExemptWidgets is the third, and it is not a convenience. Every project has a debug overlay, a version
 * stamp in the corner, a frame counter - text that is deliberately tiny and deliberately low contrast. A
 * tool that reports twenty errors on its first run is a tool that gets switched off within the hour. What
 * is exempted is still measured, still shown, and still counted as "excluded by settings", so the list can
 * never quietly make a project look clean.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "ReadGuard"))
class READGUARD_API UReadGuardSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UReadGuardSettings();

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** The settings object, never null. */
	static const UReadGuardSettings& Get();

	/** The settings flattened into the struct the rules take. Applies the exemption master switch. */
	FReadGuardThresholds MakeThresholds() const;

	//~ What is being measured against ---------------------------------------------------------------

	/**
	 * The smallest resolution the project supports.
	 *
	 * Every measured height is converted onto this through the DPI curve under Project Settings > User
	 * Interface. 1280x720 by default, because that is where text first becomes unreadable and because it
	 * is still what a handheld and a lot of consoles fall back to under load.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Target")
	FIntPoint TargetResolution = FIntPoint(1280, 720);

	/**
	 * The smallest glyph height, in device pixels at the target resolution, that counts as readable.
	 *
	 * This is measured as the font's maximum character height at the scale Slate is actually drawing it -
	 * not the font size somebody typed, which says nothing about what reaches the screen.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Target", meta = (ClampMin = "1.0", UIMax = "40.0"))
	float MinimumPixelHeight = 14.0f;

	/** The contrast ratio normal text has to clear. 4.5:1 is the usual threshold for normal text. */
	UPROPERTY(config, EditAnywhere, Category = "Target", meta = (ClampMin = "1.0", UIMax = "21.0"))
	float NormalContrastRatio = 4.5f;

	/** The contrast ratio large text has to clear. 3:1 is the usual threshold for large text. */
	UPROPERTY(config, EditAnywhere, Category = "Target", meta = (ClampMin = "1.0", UIMax = "21.0"))
	float LargeContrastRatio = 3.0f;

	/** At and above this height, in pixels at the target resolution, text counts as large. */
	UPROPERTY(config, EditAnywhere, Category = "Target", meta = (ClampMin = "1.0", UIMax = "80.0"))
	float LargeTextPixels = 24.0f;

	/** The same threshold for bold text, which carries at a smaller size and so counts as large earlier. */
	UPROPERTY(config, EditAnywhere, Category = "Target", meta = (ClampMin = "1.0", UIMax = "80.0"))
	float LargeTextBoldPixels = 18.66f;

	/**
	 * How many pixels a text block may want beyond its box before that counts as not fitting.
	 *
	 * One pixel by default. Slate's desired size and its allotted size disagree by fractions all the time
	 * through ordinary rounding, and a checker that reports every one of those is noise.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Target", meta = (ClampMin = "0.0", UIMax = "16.0"))
	float OverflowTolerance = 1.0f;

	//~ The large-text pass ---------------------------------------------------------------------------

	/**
	 * The scale the large-text pass runs at, in percent.
	 *
	 * 200 by default, because "content must remain usable at 200 percent magnification" is the line the
	 * accessibility guidance draws and therefore the line a review will draw. ReadGuard really sets the
	 * application scale, lets Slate lay the interface out again, and measures a second time.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Large Text", meta = (ClampMin = "100.0", UIMax = "400.0"))
	float LargeScalePercent = 200.0f;

	/**
	 * Frames to wait after changing the scale before measuring.
	 *
	 * Slate needs a tick to lay out again and a frame to draw. Measuring too early reports the old layout
	 * at the new scale, which is a report full of failures that are entirely the measurer's fault.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Large Text", meta = (ClampMin = "1", UIMax = "10"))
	int32 SettleFrames = 3;

	//~ Contrast ---------------------------------------------------------------------------------------

	/**
	 * Measure contrast at all.
	 *
	 * Doing so captures the rendered frame, which costs a readback and a redraw. It is on by default
	 * because contrast is half the reason to own this plugin, and it is a switch because a scan on a
	 * timer in a shipped build should not be paying for it.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Contrast")
	bool bMeasureContrast = true;

	/**
	 * How much the luminance under a text box may vary before the finding says "background varies".
	 *
	 * Above this, the contrast line becomes an advisory carrying the worst value measured, and it can no
	 * longer fail a gate. A single number pretending a moving background is one colour would be worse than
	 * no number at all.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Contrast", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BackgroundVariesSpread = 0.15f;

	/**
	 * How many pixels to sample under one text box.
	 *
	 * The box is sampled on a grid, not exhaustively. A headline across a 4K screen is a quarter of a
	 * million pixels and the answer does not get better for reading all of them.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Contrast", meta = (ClampMin = "16", UIMax = "4096"))
	int32 MaxSamplesPerBox = 512;

	//~ Severities -------------------------------------------------------------------------------------

	/** Text under the minimum height. Error by default: it is the failure this plugin exists to find. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EReadSeverity TooSmallSeverity = EReadSeverity::Error;

	/** Text a box cuts off. Error by default: characters are being lost. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EReadSeverity ClippedSeverity = EReadSeverity::Error;

	/** Text drawing outside its box. Warning by default: nothing is lost, but something is being covered. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EReadSeverity OverflowSeverity = EReadSeverity::Warning;

	/** Contrast under the threshold, on a background that does not vary. */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EReadSeverity LowContrastSeverity = EReadSeverity::Error;

	/**
	 * Something that only breaks at the large text scale.
	 *
	 * Warning by default rather than Error, because a project that has not yet promised 200 percent
	 * support should not have its build stopped by it - and because it is still the line an accessibility
	 * review will read, which is why it is reported separately instead of folded in with the rest.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Severity")
	EReadSeverity BreaksAtLargeScaleSeverity = EReadSeverity::Warning;

	//~ Exemptions -------------------------------------------------------------------------------------

	/** Use the exemption list at all. Turning it off is how you see what the list is hiding. */
	UPROPERTY(config, EditAnywhere, Category = "Exemptions")
	bool bUseExemptions = true;

	/**
	 * Widgets allowed to break the rules, by UMG widget name - VersionStamp, FpsCounter, DebugLine.
	 *
	 * Matched against the widget's own name, not its path, because that is the name that appears in the
	 * report and the name somebody will copy out of it.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Exemptions")
	TArray<FName> ExemptWidgets;

	//~ The report -------------------------------------------------------------------------------------

	/** Draw the report from the first frame. ReadGuard.Show and ReadGuard.Hide flip it. */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	bool bShowReportByDefault = true;

	/**
	 * Scan automatically a moment after the first world has begun play.
	 *
	 * The delay is not laziness: widgets are created in BeginPlay and have no geometry until Slate has
	 * ticked, so a scan in the same frame would measure an interface that is not there yet.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	bool bScanOnBeginPlay = true;

	/** How long after begin play the automatic scan waits for the interface to exist. */
	UPROPERTY(config, EditAnywhere, Category = "Report", meta = (ClampMin = "0.0", UIMax = "10.0", Units = "Seconds"))
	float AutoScanDelaySeconds = 1.5f;

	/**
	 * Scan again every N seconds. Zero, the default, means never.
	 *
	 * Off by default on purpose: ReadGuard is a measuring instrument, not a monitor, and a scan that runs
	 * behind your back on a timer is a scan that costs frames nobody asked it to spend.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Report", meta = (ClampMin = "0.0", UIMax = "120.0", Units = "Seconds"))
	float AutoScanIntervalSeconds = 0.0f;

	/** How many findings the on-screen panel lists before it says how many more there are. */
	UPROPERTY(config, EditAnywhere, Category = "Report", meta = (ClampMin = "1", UIMax = "40"))
	int32 MaxReportRows = 14;

	/**
	 * Draw the report even when the project's HUD is not an AReadGuardHUD.
	 *
	 * A project with its own HUD class does not have to reparent it: turn this on and the same panel is
	 * drawn through AHUD::OnHUDPostRender instead. The two paths know about each other and cannot stack.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	bool bAutoDrawOnAnyHUD = false;

	/** Where ReadGuard.Report and ReadGuard.Gate write, relative to the project directory. */
	UPROPERTY(config, EditAnywhere, Category = "Report")
	FString ReportPath = TEXT("Saved/ReadGuard/report.json");
};
