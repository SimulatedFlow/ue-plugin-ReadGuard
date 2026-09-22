// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReadGuardTypes.h"
#include "ReadGuardStatics.generated.h"

/**
 * The arithmetic, and the Blueprint surface.
 *
 * Everything in the first half of this class is a pure function of its arguments: no world, no subsystem,
 * no widget, no viewport. That is not tidiness for its own sake. A plugin whose entire product is a
 * judgement has to be able to prove the judgement is right, and a rule you can only exercise by standing a
 * game up and looking at it is a rule nobody ever tests. These are the functions the automation tests
 * cover, and they are public so your own tooling can call them with numbers ReadGuard never saw.
 *
 * The second half - the verdict, the names, the sentences - is the part the report is built out of.
 */
UCLASS()
class READGUARD_API UReadGuardStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ The arithmetic -------------------------------------------------------------------------------

	/**
	 * Relative luminance of a colour, on the 0..1 scale the contrast formula expects.
	 *
	 * The colour must already be linear. An FColor read out of a captured frame is sRGB encoded, and
	 * constructing an FLinearColor from it applies the decode - which is exactly why the sampling path
	 * goes through FLinearColor and never through the raw bytes.
	 *
	 * Black is 0 and white is 1, by construction: the three coefficients add up to one.
	 */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Math")
	static float RelativeLuminance(const FLinearColor& Color);

	/**
	 * The contrast ratio between two colours: (lighter + 0.05) / (darker + 0.05).
	 *
	 * Symmetric by construction - the order of the arguments cannot change the answer - and bounded by 21,
	 * which is what black against white gives exactly.
	 */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Math")
	static float ContrastRatio(const FLinearColor& A, const FLinearColor& B);

	/** The same ratio when you already have the two luminances. */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Math")
	static float ContrastRatioFromLuminance(float LuminanceA, float LuminanceB);

	/**
	 * The DPI scale the project's own curve gives for a resolution.
	 *
	 * This is the curve under Project Settings > User Interface, read through the engine's own evaluation,
	 * so a project with a hand-authored curve gets its own answer and not a guess at the default one.
	 */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Math")
	static float ProjectDPIScaleForResolution(FIntPoint Resolution);

	/**
	 * Convert a measured pixel height from the resolution it was measured at onto another one.
	 *
	 * Pure arithmetic and no engine state, which is what makes it testable: 24 px measured where the DPI
	 * scale is 1.0 is 16 px where the curve says 0.6666.
	 */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Math")
	static float ScalePixelHeight(float MeasuredPixels, float SourceDPIScale, float TargetDPIScale);

	/**
	 * What a glyph height on screen becomes at the target resolution.
	 *
	 * FontPixelHeight is the height the font produces at scale 1, WidgetScale is the accumulated layout
	 * scale between the widget and the viewport, SourceDPIScale is the curve's value for the resolution
	 * the measurement was taken at, and the target resolution is put through the curve here.
	 */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Math")
	static float EffectivePixelHeight(float FontPixelHeight, float SourceDPIScale, float WidgetScale, FIntPoint TargetResolution);

	/**
	 * Does text of this height count as large?
	 *
	 * Bold counts as large earlier, because a heavier stroke carries at a smaller size - that is where the
	 * two thresholds in the accessibility guidance come from, and it is why this takes a bold flag at all.
	 */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Math")
	static bool IsLargeText(float Pixels, bool bBold, float LargeTextPixels = 24.0f, float LargeTextBoldPixels = 18.66f);

	/** The contrast ratio text of this size has to clear: the large threshold if it is large, else the normal one. */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Math")
	static float RequiredContrast(float Pixels, bool bBold, const FReadGuardThresholds& Thresholds);

	//~ The verdict ----------------------------------------------------------------------------------

	/**
	 * Fail if anything is an Error, Warn if anything is a Warning, otherwise Ok.
	 *
	 * Info can never fail a gate. That is what makes the "background varies" advisory safe to emit
	 * generously: it can inform a person without ever being able to stop a build.
	 */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Verdict")
	static EReadVerdict Judge(const TArray<FReadGuardFinding>& Findings);

	/** 0 clean, 1 warnings, 2 errors - the convention shared with the other gates in this range. */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Verdict")
	static int32 VerdictExitCode(EReadVerdict Verdict);

	UFUNCTION(BlueprintPure, Category = "ReadGuard|Verdict")
	static FString VerdictName(EReadVerdict Verdict);

	UFUNCTION(BlueprintPure, Category = "ReadGuard|Verdict")
	static FString KindName(EReadFindingKind Kind);

	UFUNCTION(BlueprintPure, Category = "ReadGuard|Verdict")
	static FString SeverityName(EReadSeverity Severity);

	/**
	 * Force every finding whose widget is on the exemption list down to Info, and count them.
	 *
	 * Returns how many were excluded. The count is the point: an exemption that could hide itself would be
	 * a way to make a project look clean by editing a settings page, so what is exempted is still measured,
	 * still produced and still printed - as "excluded by settings".
	 */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard|Verdict")
	static int32 ApplyExemptions(UPARAM(ref) TArray<FReadGuardFinding>& Findings, const TArray<FName>& ExemptWidgets);

	/** Errors first, then warnings, then info; within a severity, worst measurement first. */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard|Verdict")
	static void SortFindings(UPARAM(ref) TArray<FReadGuardFinding>& Findings);

	//~ Words ----------------------------------------------------------------------------------------

	/**
	 * One sentence saying what to do about a finding.
	 *
	 * Not what is wrong - the numbers already say that - but what to change. A report that only describes
	 * the fault makes the reader do the translation every single time.
	 */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Report")
	static FString Explain(const FReadGuardFinding& Finding);

	/** The finding as one line: severity, kind, widget, measurement against threshold. */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Report")
	static FString FormatFinding(const FReadGuardFinding& Finding);

	/**
	 * The headline: visited, errors, warnings, smallest text, worst contrast, scan cost.
	 *
	 * The visit count comes first on purpose. It is the number that says what the rest of the line is
	 * worth.
	 */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Report")
	static FString Headline(const FReadGuardReport& Report);

	/** The second line: which screens were looked at, by name. */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Report")
	static FString FormatScreens(const FReadGuardReport& Report);

	/** The first characters of a piece of text, so a finding can be found again on screen. */
	UFUNCTION(BlueprintPure, Category = "ReadGuard|Report")
	static FString TruncateText(const FString& Text, int32 MaxCharacters = 40);

	//~ Convenience for Blueprint ---------------------------------------------------------------------

	/** The last report the subsystem produced, for the world this object lives in. */
	UFUNCTION(BlueprintPure, Category = "ReadGuard", meta = (WorldContext = "WorldContextObject"))
	static FReadGuardReport GetLastReport(const UObject* WorldContextObject);

	/** Start a scan at the current scale. True when one was started. */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard", meta = (WorldContext = "WorldContextObject"))
	static bool Scan(const UObject* WorldContextObject);

	/** Start a scan at the given percentage of the normal text scale. 200 is the accessibility case. */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard", meta = (WorldContext = "WorldContextObject"))
	static bool ScanAtScale(const UObject* WorldContextObject, float ScalePercent);
};
