// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReadGuardTypes.generated.h"

/** How bad a finding is. The verdict, and the process exit code behind the gate, are decided from these. */
UENUM(BlueprintType)
enum class EReadSeverity : uint8
{
	/** Worth knowing, changes nothing. Never fails a gate. */
	Info		UMETA(DisplayName = "Info"),

	/** Probably wrong. Fails the gate with 1. */
	Warning		UMETA(DisplayName = "Warning"),

	/** Wrong. Fails the gate with 2. */
	Error		UMETA(DisplayName = "Error"),
};

/** The three answers the gate can give, and the three numbers it exits with. */
UENUM(BlueprintType)
enum class EReadVerdict : uint8
{
	/** Nothing above Info. Exit code 0. */
	Ok		UMETA(DisplayName = "Ok"),

	/** At least one Warning and no Error. Exit code 1. */
	Warn	UMETA(DisplayName = "Warn"),

	/** At least one Error. Exit code 2. */
	Fail	UMETA(DisplayName = "Fail"),
};

/**
 * What kind of thing is wrong with a piece of text.
 *
 * Clipped and Overflow are the same measurement with two different consequences, and they are separate
 * kinds because the fix is different. A label whose box clips it loses characters; a label whose box does
 * not clip it keeps every character and draws them over the widget next door. Folding those into one
 * "does not fit" would produce a report where half the entries need a different repair to the other half.
 */
UENUM(BlueprintType)
enum class EReadFindingKind : uint8
{
	/** The glyphs are shorter than the minimum height, converted onto the smallest supported resolution. */
	TooSmall			UMETA(DisplayName = "Too Small"),

	/** The text wants more room than it was allotted, and something in the chain clips it away. */
	Clipped				UMETA(DisplayName = "Clipped"),

	/** The text wants more room than it was allotted, and nothing clips it - so it draws outside its box. */
	Overflow			UMETA(DisplayName = "Overflow"),

	/** The contrast against the measured background is under the threshold for text of this size. */
	LowContrast			UMETA(DisplayName = "Low Contrast"),

	/** Fine at 100 percent, not fine at the large-text scale. The line an accessibility review reads. */
	BreaksAtLargeScale	UMETA(DisplayName = "Breaks At Large Scale"),
};

/**
 * One text block that is wrong, with everything needed to find it again and everything needed to fix it.
 *
 * Every finding carries a name and a number. "3 labels are too small" is a statistic nobody can act on;
 * "WBP_PlayerHUD.AmmoCounter measures 9.2 px at 1280x720, the floor is 14" is a repair order.
 */
USTRUCT(BlueprintType)
struct READGUARD_API FReadGuardFinding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	EReadFindingKind Kind = EReadFindingKind::TooSmall;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	EReadSeverity Severity = EReadSeverity::Error;

	/** The UMG widget's name, e.g. AmmoCounter. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FName WidgetName;

	/** The screen it lives on, e.g. WBP_PlayerHUD - the outermost user widget in its ownership chain. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FName ScreenName;

	/** Screen.Widget, which is what the report prints and what somebody types into the UMG search box. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FString WidgetPath;

	/**
	 * The start of the text, truncated.
	 *
	 * There to find the place again. Two labels called TextBlock_23 are indistinguishable in a report;
	 * "Press E to open the..." is the one on the door.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FString TextPreview;

	/** What was measured: pixels for TooSmall, the ratio for LowContrast, overflow in pixels otherwise. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float MeasuredValue = 0.0f;

	/** What it was measured against. Always carried with the measurement so a screenshot explains itself. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float Threshold = 0.0f;

	/** The scale the measurement was taken at, in percent: 100 for the normal pass, 200 for the large one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float ScalePercent = 100.0f;

	/** True when this came out of the large-text pass rather than the normal one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bAtLargeScale = false;

	/**
	 * True when the pixels behind this text were not one colour.
	 *
	 * A contrast finding with this flag is an advisory and never an error, and MeasuredValue is the worst
	 * ratio found rather than an average. See FReadGuardScanner::SampleBackground and the documentation
	 * section "Why contrast over a moving background is a hint and not an error".
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bBackgroundVaries = false;

	/**
	 * True when the exemption list in Project Settings took the teeth out of this finding.
	 *
	 * An excluded finding is measured, produced, forced down to Info, counted separately as "excluded by
	 * settings" and still printed. An exemption list able to hide its own effect would be a way to turn a
	 * report green by editing a settings page.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bExcluded = false;

	/** One sentence saying what to do about it. Filled in by UReadGuardStatics::Explain. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FString Detail;
};

/**
 * One screen that was looked at, by name.
 *
 * This exists so that "no findings" can never be read as "nothing was checked". A report that lists
 * WBP_PlayerHUD and WBP_PauseMenu and says 47 text blocks has told you what its green means; a report
 * that only says "0 errors" has not.
 */
USTRUCT(BlueprintType)
struct READGUARD_API FReadGuardScreen
{
	GENERATED_BODY()

	/** The user widget class, without the _C the Blueprint compiler adds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FName Name;

	/** Visible text blocks measured on this screen. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 TextBlocks = 0;

	/** Findings raised on this screen, at any severity. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 Findings = 0;
};

/**
 * One measured text block, before any rule has judged it.
 *
 * Kept because the report is more useful than its findings: the smallest text on a clean screen is still
 * worth knowing, and a build script that wants to plot the distribution needs the measurements and not
 * only the failures.
 */
USTRUCT(BlueprintType)
struct READGUARD_API FReadGuardMeasurement
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FName WidgetName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FName ScreenName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FString WidgetPath;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FString TextPreview;

	/** The glyph height in device pixels as it is on screen right now, at the current resolution and scale. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float PixelHeightNow = 0.0f;

	/** The same height converted onto the target resolution through the project's DPI curve. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float PixelHeightAtTarget = 0.0f;

	/** The font size as authored, for the report line that has to name the number somebody typed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float FontSize = 0.0f;

	/** The accumulated layout scale between the widget and the viewport, DPI curve included. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float LayoutScale = 1.0f;

	/** True when the typeface name says bold. Large-text thresholds start earlier for bold text. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bBold = false;

	/** How far the text wants to be wider than its box, in local pixels. Zero or less means it fits. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float OverflowX = 0.0f;

	/** How far the text wants to be taller than its box, in local pixels. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float OverflowY = 0.0f;

	/** True when this widget, or something above it, clips to its bounds - so an overflow loses characters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bClips = false;

	/** True when the text block wraps automatically, which turns a width overflow into a height one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bAutoWrap = false;

	/** False when there was no captured frame, or too few background pixels left to measure honestly. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bContrastMeasured = false;

	/** True when the sampled background was not one colour. Turns the contrast finding into an advisory. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bBackgroundVaries = false;

	/** Contrast against the dominant background colour under the box. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float Contrast = 0.0f;

	/** The lowest contrast measured against any significant band of background under the box. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float WorstContrast = 0.0f;

	/** What ReadGuard took the text colour to be, alpha and render opacity folded in. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FLinearColor TextColor = FLinearColor::White;

	/** The dominant background colour measured under the box. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FLinearColor BackgroundColor = FLinearColor::Black;

	/** How many pixels were actually sampled after the glyph pixels were discarded. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 BackgroundSamples = 0;
};

/**
 * The thresholds and switches a scan runs with, flattened out of the settings.
 *
 * The rules never read a UDeveloperSettings. A test builds one of these in four lines, which is the only
 * reason the arithmetic in this plugin is testable at all.
 */
USTRUCT(BlueprintType)
struct READGUARD_API FReadGuardThresholds
{
	GENERATED_BODY()

	/** The smallest resolution the project supports. Every pixel height in the report is converted onto it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FIntPoint TargetResolution = FIntPoint(1280, 720);

	/** The floor, in device pixels at the target resolution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float MinimumPixelHeight = 14.0f;

	/** The contrast ratio normal text has to clear. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float NormalContrast = 4.5f;

	/** The contrast ratio large text has to clear. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float LargeContrast = 3.0f;

	/** At and above this many pixels, text counts as large. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float LargeTextPixels = 24.0f;

	/** Bold text counts as large earlier, because a heavier stroke carries at a smaller size. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float LargeTextBoldPixels = 18.66f;

	/** How many pixels a text block may want beyond its box before that counts as not fitting. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float OverflowTolerance = 1.0f;

	/** Luminance spread across the sampled background above which the contrast line says "background varies". */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float BackgroundVariesSpread = 0.15f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	EReadSeverity TooSmallSeverity = EReadSeverity::Error;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	EReadSeverity ClippedSeverity = EReadSeverity::Error;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	EReadSeverity OverflowSeverity = EReadSeverity::Warning;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	EReadSeverity LowContrastSeverity = EReadSeverity::Error;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	EReadSeverity BreaksAtLargeScaleSeverity = EReadSeverity::Warning;

	/** Master switch for the exemption list, so a tester can see both answers without editing the list. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bUseExemptions = true;

	/** Widgets allowed to break the rules, by name. What is exempted is still counted and still printed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	TArray<FName> ExemptWidgets;
};

/**
 * The whole answer for one pass: what was measured, what is wrong, and what was looked at.
 *
 * ChecksRun and ScreensVisited are not decoration. They are the half of the report that makes a green
 * verdict mean something, and they are written into the JSON for exactly that reason.
 */
USTRUCT(BlueprintType)
struct READGUARD_API FReadGuardReport
{
	GENERATED_BODY()

	/** Errors first, then warnings, then info; stable within a severity so a screenshot means one thing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	TArray<FReadGuardFinding> Findings;

	/** Every visible text block that was measured, findings or not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	TArray<FReadGuardMeasurement> Measurements;

	/** The screens that were on screen, by name, with their text block counts. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	TArray<FReadGuardScreen> ScreensVisited;

	/** How many text blocks were measured. The number that stops green from meaning "did not look". */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 TextBlocksVisited = 0;

	/** How many UMG widgets of any kind were walked to find them. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 WidgetsWalked = 0;

	/** Text blocks that were skipped because they were not visible or had no geometry yet. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 TextBlocksSkipped = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 ErrorCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 WarningCount = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 InfoCount = 0;

	/** How many findings the exemption list took the teeth out of. Always shown, never hidden. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 ExcludedCount = 0;

	/** The smallest glyph height measured, converted onto the target resolution. -1 when nothing was seen. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float SmallestPixelHeight = -1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FString SmallestPixelHeightWidget;

	/** The lowest contrast ratio measured. -1 when no contrast could be measured at all. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float WorstContrast = -1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FString WorstContrastWidget;

	/** True when the worst contrast came from a box whose background was not one colour. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bWorstContrastVaries = false;

	/** The thresholds this pass was judged against, carried so the report can explain its own numbers. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FReadGuardThresholds Thresholds;

	/** The viewport size the measurements were taken at. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	FIntPoint ViewportResolution = FIntPoint::ZeroValue;

	/** The DPI scale the project's curve gives for that viewport size. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float ViewportDPIScale = 1.0f;

	/** The DPI scale the project's curve gives for the target resolution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float TargetDPIScale = 1.0f;

	/** The application scale this pass ran at, in percent. 100 normally, 200 for the large-text pass. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float ScalePercent = 100.0f;

	/** True once a large-text pass has been merged into this report. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bLargeScaleTested = false;

	/** The scale the large-text pass ran at, in percent. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float LargeScalePercent = 200.0f;

	/** How many things broke only at the large scale. The number no other tool in the engine produces. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 LargeScaleBreakCount = 0;

	/**
	 * False when no frame was captured, so no contrast was measured at all.
	 *
	 * The report says so in words rather than reporting every box as passing. Contrast that was not
	 * measured is not contrast that was fine.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bContrastMeasured = false;

	/** How many text blocks got a real contrast measurement. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 ContrastMeasuredCount = 0;

	/** How many of those had a background that was not one colour, and so were reported as advisories. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	int32 BackgroundVariesCount = 0;

	/** Measured, wall clock, over the walk and the rules. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	float ScanMilliseconds = 0.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	EReadVerdict Verdict = EReadVerdict::Ok;

	/**
	 * The checks that actually ran, in words.
	 *
	 * When there is nothing to report, ReadGuard says what it looked for instead of saying nothing.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	TArray<FString> ChecksRun;

	/** True once a scan has actually run. A default-constructed report is not a clean report. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ReadGuard")
	bool bHasRun = false;
};
