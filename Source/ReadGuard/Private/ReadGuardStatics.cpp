// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "ReadGuardStatics.h"

#include "Engine/Engine.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/StringBuilder.h"
#include "ReadGuardSubsystem.h"

namespace ReadGuardMath
{
	/**
	 * The coefficients from the accessibility guidance, and they add up to exactly one.
	 *
	 * That is not a coincidence and it is not something to round: it is the reason black measures 0 and
	 * white measures 1, which is the reason white on black measures exactly 21 and not 20.98.
	 */
	constexpr float LuminanceR = 0.2126f;
	constexpr float LuminanceG = 0.7152f;
	constexpr float LuminanceB = 0.0722f;

	/** The offset in the contrast formula. Keeps the ratio finite when one of the two colours is black. */
	constexpr float ContrastOffset = 0.05f;
}

float UReadGuardStatics::RelativeLuminance(const FLinearColor& Color)
{
	// Clamped, because a captured HDR frame or a colour somebody typed by hand can carry channels outside
	// 0..1, and a "luminance" of 1.4 would push every contrast ratio it touches into fiction.
	const float R = FMath::Clamp(Color.R, 0.0f, 1.0f);
	const float G = FMath::Clamp(Color.G, 0.0f, 1.0f);
	const float B = FMath::Clamp(Color.B, 0.0f, 1.0f);

	return ReadGuardMath::LuminanceR * R + ReadGuardMath::LuminanceG * G + ReadGuardMath::LuminanceB * B;
}

float UReadGuardStatics::ContrastRatio(const FLinearColor& A, const FLinearColor& B)
{
	return ContrastRatioFromLuminance(RelativeLuminance(A), RelativeLuminance(B));
}

float UReadGuardStatics::ContrastRatioFromLuminance(const float LuminanceA, const float LuminanceB)
{
	const float Lighter = FMath::Max(LuminanceA, LuminanceB);
	const float Darker = FMath::Min(LuminanceA, LuminanceB);

	return (Lighter + ReadGuardMath::ContrastOffset) / (Darker + ReadGuardMath::ContrastOffset);
}

float UReadGuardStatics::ProjectDPIScaleForResolution(const FIntPoint Resolution)
{
	const FIntPoint Safe(FMath::Max(Resolution.X, 1), FMath::Max(Resolution.Y, 1));

	if (const UUserInterfaceSettings* Settings = GetDefault<UUserInterfaceSettings>())
	{
		return Settings->GetDPIScaleBasedOnSize(Safe);
	}

	return 1.0f;
}

float UReadGuardStatics::ScalePixelHeight(const float MeasuredPixels, const float SourceDPIScale, const float TargetDPIScale)
{
	// A source scale of zero is not a division to guard against so much as a measurement that never
	// happened; returning the unscaled height is the least wrong thing to do with it.
	if (SourceDPIScale <= KINDA_SMALL_NUMBER)
	{
		return MeasuredPixels;
	}

	return MeasuredPixels * (TargetDPIScale / SourceDPIScale);
}

float UReadGuardStatics::EffectivePixelHeight(const float FontPixelHeight, const float SourceDPIScale, const float WidgetScale, const FIntPoint TargetResolution)
{
	return ScalePixelHeight(FontPixelHeight * WidgetScale, SourceDPIScale, ProjectDPIScaleForResolution(TargetResolution));
}

bool UReadGuardStatics::IsLargeText(const float Pixels, const bool bBold, const float LargeTextPixels, const float LargeTextBoldPixels)
{
	return Pixels >= (bBold ? LargeTextBoldPixels : LargeTextPixels);
}

float UReadGuardStatics::RequiredContrast(const float Pixels, const bool bBold, const FReadGuardThresholds& Thresholds)
{
	return IsLargeText(Pixels, bBold, Thresholds.LargeTextPixels, Thresholds.LargeTextBoldPixels)
		? Thresholds.LargeContrast
		: Thresholds.NormalContrast;
}

EReadVerdict UReadGuardStatics::Judge(const TArray<FReadGuardFinding>& Findings)
{
	EReadVerdict Verdict = EReadVerdict::Ok;

	for (const FReadGuardFinding& Finding : Findings)
	{
		if (Finding.Severity == EReadSeverity::Error)
		{
			// Nothing above Fail, so there is nothing left to learn from the rest of the list.
			return EReadVerdict::Fail;
		}

		if (Finding.Severity == EReadSeverity::Warning)
		{
			Verdict = EReadVerdict::Warn;
		}
	}

	return Verdict;
}

int32 UReadGuardStatics::VerdictExitCode(const EReadVerdict Verdict)
{
	switch (Verdict)
	{
	case EReadVerdict::Fail:	return 2;
	case EReadVerdict::Warn:	return 1;
	default:					return 0;
	}
}

FString UReadGuardStatics::VerdictName(const EReadVerdict Verdict)
{
	switch (Verdict)
	{
	case EReadVerdict::Fail:	return TEXT("fail");
	case EReadVerdict::Warn:	return TEXT("warn");
	default:					return TEXT("ok");
	}
}

FString UReadGuardStatics::KindName(const EReadFindingKind Kind)
{
	switch (Kind)
	{
	case EReadFindingKind::TooSmall:			return TEXT("too small");
	case EReadFindingKind::Clipped:				return TEXT("clipped");
	case EReadFindingKind::Overflow:			return TEXT("overflow");
	case EReadFindingKind::LowContrast:			return TEXT("low contrast");
	case EReadFindingKind::BreaksAtLargeScale:	return TEXT("breaks at large scale");
	default:									return TEXT("unknown");
	}
}

FString UReadGuardStatics::SeverityName(const EReadSeverity Severity)
{
	switch (Severity)
	{
	case EReadSeverity::Error:		return TEXT("error");
	case EReadSeverity::Warning:	return TEXT("warning");
	default:						return TEXT("info");
	}
}

int32 UReadGuardStatics::ApplyExemptions(TArray<FReadGuardFinding>& Findings, const TArray<FName>& ExemptWidgets)
{
	if (ExemptWidgets.Num() == 0)
	{
		return 0;
	}

	int32 Excluded = 0;

	for (FReadGuardFinding& Finding : Findings)
	{
		if (!ExemptWidgets.Contains(Finding.WidgetName))
		{
			continue;
		}

		// Measured, produced, printed - and then defanged. The finding does not disappear; it stops being
		// able to fail a build, and it starts being counted in a number the report always shows.
		Finding.bExcluded = true;
		Finding.Severity = EReadSeverity::Info;
		++Excluded;
	}

	return Excluded;
}

void UReadGuardStatics::SortFindings(TArray<FReadGuardFinding>& Findings)
{
	Findings.Sort([](const FReadGuardFinding& A, const FReadGuardFinding& B)
	{
		if (A.Severity != B.Severity)
		{
			return A.Severity > B.Severity;
		}

		if (A.Kind != B.Kind)
		{
			return A.Kind < B.Kind;
		}

		// Within one kind, the worst measurement first. For a height that is the smallest number and for a
		// contrast ratio it is also the smallest number, which is why one comparison covers both.
		if (!FMath::IsNearlyEqual(A.MeasuredValue, B.MeasuredValue))
		{
			const bool bSmallerIsWorse =
				A.Kind == EReadFindingKind::TooSmall || A.Kind == EReadFindingKind::LowContrast;

			return bSmallerIsWorse ? A.MeasuredValue < B.MeasuredValue : A.MeasuredValue > B.MeasuredValue;
		}

		return A.WidgetPath < B.WidgetPath;
	});
}

FString UReadGuardStatics::Explain(const FReadGuardFinding& Finding)
{
	switch (Finding.Kind)
	{
	case EReadFindingKind::TooSmall:
		return FString::Printf(
			TEXT("Raise the font size, or the DPI curve at this resolution, until it clears %.0f px. It measures %.1f px at the target resolution."),
			Finding.Threshold, Finding.MeasuredValue);

	case EReadFindingKind::Clipped:
		return FString::Printf(
			TEXT("The box cuts %.0f px off this text. Give it more room, let it wrap, or shorten it - a translated string will be longer still."),
			Finding.MeasuredValue);

	case EReadFindingKind::Overflow:
		return FString::Printf(
			TEXT("This text draws %.0f px outside its box and over whatever is next to it. Widen the box or let it wrap."),
			Finding.MeasuredValue);

	case EReadFindingKind::LowContrast:
		if (Finding.bBackgroundVaries)
		{
			return FString::Printf(
				TEXT("The background under this text is not one colour. Against the worst of it the ratio is %.1f:1 against a %.1f:1 threshold - a shadow, an outline or a backing box would settle it."),
				Finding.MeasuredValue, Finding.Threshold);
		}

		return FString::Printf(
			TEXT("Darken the background or lighten the text until the ratio clears %.1f:1. It measures %.1f:1."),
			Finding.Threshold, Finding.MeasuredValue);

	case EReadFindingKind::BreaksAtLargeScale:
		return FString::Printf(
			TEXT("Fine at 100 percent, not at %.0f percent. Give this box room to grow, or let its text wrap, before somebody turns the text size up."),
			Finding.ScalePercent);

	default:
		return FString();
	}
}

FString UReadGuardStatics::FormatFinding(const FReadGuardFinding& Finding)
{
	TStringBuilder<256> Line;

	Line.Appendf(TEXT("%-7s %-21s %s"),
		*SeverityName(Finding.Severity),
		*KindName(Finding.Kind),
		*Finding.WidgetPath);

	switch (Finding.Kind)
	{
	case EReadFindingKind::TooSmall:
		Line.Appendf(TEXT("  %.1f px (limit %.0f)"), Finding.MeasuredValue, Finding.Threshold);
		break;

	case EReadFindingKind::LowContrast:
		Line.Appendf(TEXT("  %.1f:1 (limit %.1f)"), Finding.MeasuredValue, Finding.Threshold);
		if (Finding.bBackgroundVaries)
		{
			Line.Append(TEXT(" background varies"));
		}
		break;

	case EReadFindingKind::Clipped:
	case EReadFindingKind::Overflow:
		Line.Appendf(TEXT("  %.0f px past its box"), Finding.MeasuredValue);
		break;

	case EReadFindingKind::BreaksAtLargeScale:
		Line.Appendf(TEXT("  at %.0f%% scale"), Finding.ScalePercent);
		break;

	default:
		break;
	}

	if (Finding.bAtLargeScale && Finding.Kind != EReadFindingKind::BreaksAtLargeScale)
	{
		Line.Appendf(TEXT(" [%.0f%%]"), Finding.ScalePercent);
	}

	if (Finding.bExcluded)
	{
		Line.Append(TEXT("  [excluded by settings]"));
	}

	if (!Finding.TextPreview.IsEmpty())
	{
		Line.Appendf(TEXT("  \"%s\""), *Finding.TextPreview);
	}

	return Line.ToString();
}

FString UReadGuardStatics::Headline(const FReadGuardReport& Report)
{
	if (!Report.bHasRun)
	{
		return TEXT("ReadGuard has not scanned yet. Nothing here is a verdict.");
	}

	TStringBuilder<384> Line;

	Line.Appendf(TEXT("text blocks %d visited | errors %d  warnings %d"),
		Report.TextBlocksVisited, Report.ErrorCount, Report.WarningCount);

	if (Report.ExcludedCount > 0)
	{
		Line.Appendf(TEXT("  (%d excluded by settings)"), Report.ExcludedCount);
	}

	if (Report.SmallestPixelHeight >= 0.0f)
	{
		Line.Appendf(TEXT(" | smallest %.1f px at %dx%d (limit %.0f)"),
			Report.SmallestPixelHeight,
			Report.Thresholds.TargetResolution.X,
			Report.Thresholds.TargetResolution.Y,
			Report.Thresholds.MinimumPixelHeight);
	}

	if (Report.WorstContrast >= 0.0f)
	{
		Line.Appendf(TEXT(" | worst contrast %.1f:1 (limit %.1f)"),
			Report.WorstContrast, Report.Thresholds.NormalContrast);

		if (Report.bWorstContrastVaries)
		{
			Line.Append(TEXT(" background varies"));
		}
	}
	else
	{
		// Not measured is not the same as fine, and the header is the one place that difference has to be
		// impossible to miss.
		Line.Append(TEXT(" | contrast not measured"));
	}

	Line.Appendf(TEXT(" | scan %.1f ms"), Report.ScanMilliseconds);

	return Line.ToString();
}

FString UReadGuardStatics::FormatScreens(const FReadGuardReport& Report)
{
	if (Report.ScreensVisited.Num() == 0)
	{
		return TEXT("no screens were on display - nothing was checked");
	}

	TStringBuilder<384> Line;
	Line.Append(TEXT("screens checked: "));

	for (int32 Index = 0; Index < Report.ScreensVisited.Num(); ++Index)
	{
		const FReadGuardScreen& Screen = Report.ScreensVisited[Index];

		if (Index > 0)
		{
			Line.Append(TEXT(", "));
		}

		Line.Appendf(TEXT("%s (%d)"), *Screen.Name.ToString(), Screen.TextBlocks);
	}

	return Line.ToString();
}

FString UReadGuardStatics::TruncateText(const FString& Text, const int32 MaxCharacters)
{
	FString Flat = Text.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")).TrimStartAndEnd();

	if (MaxCharacters > 0 && Flat.Len() > MaxCharacters)
	{
		Flat.LeftInline(MaxCharacters, EAllowShrinking::No);
		Flat.Append(TEXT("..."));
	}

	return Flat;
}

FReadGuardReport UReadGuardStatics::GetLastReport(const UObject* WorldContextObject)
{
	if (const UReadGuardSubsystem* Subsystem = UReadGuardSubsystem::Get(WorldContextObject))
	{
		return Subsystem->GetReport();
	}

	return FReadGuardReport();
}

bool UReadGuardStatics::Scan(const UObject* WorldContextObject)
{
	UReadGuardSubsystem* Subsystem = UReadGuardSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->Scan();
}

bool UReadGuardStatics::ScanAtScale(const UObject* WorldContextObject, const float ScalePercent)
{
	UReadGuardSubsystem* Subsystem = UReadGuardSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->ScanAtScale(ScalePercent);
}
