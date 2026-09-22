// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "ReadGuardScanner.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Layout/Geometry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "ReadGuardLog.h"
#include "ReadGuardStatics.h"
#include "Serialization/JsonWriter.h"
#include "Styling/SlateTypes.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/SWidget.h"

namespace ReadGuardScan
{
	/** How far up the Slate hierarchy the clipping search walks before it gives up. */
	constexpr int32 MaxParentWalk = 128;

	/** Luminance bands the background histogram is built from. */
	constexpr int32 LuminanceBuckets = 24;

	/**
	 * How close a pixel has to be to the text colour to be treated as a glyph and thrown away.
	 *
	 * Generous on purpose. The cores of the glyphs are exactly the text colour and are removed cleanly; the
	 * antialiased rim is not, and no tolerance removes it without also removing background. What saves the
	 * measurement is the band rule further down - a thin rim of intermediate colours is spread across many
	 * bands and none of them reaches the share a band needs to count.
	 */
	constexpr float GlyphColorTolerance = 0.18f;

	/** The share of the sampled background a luminance band needs before it is allowed to set the verdict. */
	constexpr float SignificantBandShare = 0.05f;

	/** Fewer than this many background pixels is not a measurement, and is reported as one that did not happen. */
	constexpr int32 MinimumBackgroundSamples = 12;

	/** WBP_PlayerHUD_C is the class; WBP_PlayerHUD is what the person who made it called it. */
	FName MakeDisplayName(const UClass* Class)
	{
		if (Class == nullptr)
		{
			return NAME_None;
		}

		FString Name = Class->GetName();
		if (Name.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
		{
			Name.LeftChopInline(2, EAllowShrinking::No);
		}
		return FName(*Name);
	}

	/** Everything about a text widget the rules need, read out of whichever UMG class it happens to be. */
	struct FTextWidgetInfo
	{
		FSlateFontInfo Font;
		FLinearColor Color = FLinearColor::White;
		bool bColorKnown = false;
		bool bAutoWrap = false;
		FString Text;
	};

	/**
	 * Read a UMG widget as text, or say it is not one.
	 *
	 * Two classes are covered, and they are the two that carry the overwhelming majority of the words in
	 * any interface. Editable text and combo boxes draw text as well; they are deliberately not here,
	 * because their content is the player's rather than the project's and a report full of findings about
	 * whatever somebody last typed into a name field would be noise.
	 */
	bool ReadTextWidget(const UWidget* Widget, FTextWidgetInfo& Out)
	{
		if (const UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
		{
			Out.Font = TextBlock->GetFont();

			const FSlateColor SlateColor = TextBlock->GetColorAndOpacity();
			Out.bColorKnown = SlateColor.IsColorSpecified();
			if (Out.bColorKnown)
			{
				Out.Color = SlateColor.GetSpecifiedColor();
			}

			Out.bAutoWrap = TextBlock->GetAutoWrapText();
			Out.Text = TextBlock->GetText().ToString();
			return true;
		}

		if (const URichTextBlock* RichTextBlock = Cast<URichTextBlock>(Widget))
		{
			// The default style, not the style of any one run. A rich text block whose decorators change
			// colour mid-sentence has no single text colour, and the honest thing is to measure the default
			// and let the varying-background rule catch the rest.
			const FTextBlockStyle& Style = RichTextBlock->GetDefaultTextStyle();

			Out.Font = Style.Font;
			Out.bColorKnown = Style.ColorAndOpacity.IsColorSpecified();
			if (Out.bColorKnown)
			{
				Out.Color = Style.ColorAndOpacity.GetSpecifiedColor();
			}

			Out.bAutoWrap = RichTextBlock->GetAutoWrapText();
			Out.Text = RichTextBlock->GetText().ToString();
			return true;
		}

		return false;
	}

	/**
	 * Does anything between this widget and the window clip it?
	 *
	 * The difference between losing characters and drawing over the neighbours, and it cannot be answered
	 * from the text block alone - the scroll box three levels up is usually the one doing the cutting.
	 */
	bool IsClipped(const TSharedPtr<SWidget>& Widget)
	{
		TSharedPtr<SWidget> Current = Widget;
		int32 Depth = 0;

		while (Current.IsValid() && Depth < MaxParentWalk)
		{
			const EWidgetClipping Clipping = Current->GetClipping();
			if (Clipping == EWidgetClipping::ClipToBounds
				|| Clipping == EWidgetClipping::ClipToBoundsAlways
				|| Clipping == EWidgetClipping::ClipToBoundsWithoutIntersecting
				|| Clipping == EWidgetClipping::OnDemand)
			{
				return true;
			}

			Current = Current->GetParentWidget();
			++Depth;
		}

		return false;
	}

	/** The typeface name is the only place the engine records that a font is bold. */
	bool IsBold(const FSlateFontInfo& Font)
	{
		return Font.TypefaceFontName.ToString().Contains(TEXT("Bold"), ESearchCase::IgnoreCase);
	}
}

FReadGuardBackgroundSample FReadGuardScanner::SampleBackground(
	const FReadGuardFrameCapture& Capture,
	const FSlateRect& SlateRect,
	const FLinearColor& TextColor,
	const float VariesSpread,
	const int32 MaxSamples)
{
	FReadGuardBackgroundSample Result;

	if (!Capture.IsValid())
	{
		return Result;
	}

	const FVector2f TopLeft = Capture.SlateToPixel(FVector2f(SlateRect.Left, SlateRect.Top));
	const FVector2f BottomRight = Capture.SlateToPixel(FVector2f(SlateRect.Right, SlateRect.Bottom));

	const int32 MinX = FMath::Clamp(FMath::FloorToInt32(TopLeft.X), 0, Capture.Size.X - 1);
	const int32 MinY = FMath::Clamp(FMath::FloorToInt32(TopLeft.Y), 0, Capture.Size.Y - 1);
	const int32 MaxX = FMath::Clamp(FMath::CeilToInt32(BottomRight.X), 0, Capture.Size.X - 1);
	const int32 MaxY = FMath::Clamp(FMath::CeilToInt32(BottomRight.Y), 0, Capture.Size.Y - 1);

	const int32 Width = MaxX - MinX + 1;
	const int32 Height = MaxY - MinY + 1;

	if (Width < 2 || Height < 2)
	{
		// The box is off the capture, or it is a sliver. Either way there is nothing here to measure, and
		// saying so is better than averaging two pixels and calling it a background.
		return Result;
	}

	// Sample on a grid. A headline across a 4K screen is a quarter of a million pixels and the answer does
	// not get better for reading all of them.
	const int32 Area = Width * Height;
	const int32 Budget = FMath::Max(MaxSamples, 16);
	const int32 Stride = FMath::Max(1, FMath::FloorToInt32(FMath::Sqrt(static_cast<float>(Area) / static_cast<float>(Budget))));

	int32 BucketCounts[ReadGuardScan::LuminanceBuckets] = { 0 };
	FLinearColor BucketSums[ReadGuardScan::LuminanceBuckets];
	for (FLinearColor& Sum : BucketSums)
	{
		Sum = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	}

	int32 Kept = 0;

	for (int32 Y = MinY; Y <= MaxY; Y += Stride)
	{
		for (int32 X = MinX; X <= MaxX; X += Stride)
		{
			// FLinearColor from FColor applies the sRGB decode, which is what the luminance formula wants.
			const FLinearColor Pixel(Capture.Pixels[Y * Capture.Size.X + X]);

			const float DistanceToText = FMath::Abs(Pixel.R - TextColor.R)
				+ FMath::Abs(Pixel.G - TextColor.G)
				+ FMath::Abs(Pixel.B - TextColor.B);

			if (DistanceToText < ReadGuardScan::GlyphColorTolerance * 3.0f)
			{
				// This is the text, not what is behind it.
				continue;
			}

			const float Luminance = UReadGuardStatics::RelativeLuminance(Pixel);
			const int32 Bucket = FMath::Clamp(
				FMath::FloorToInt32(Luminance * ReadGuardScan::LuminanceBuckets),
				0,
				ReadGuardScan::LuminanceBuckets - 1);

			++BucketCounts[Bucket];
			BucketSums[Bucket] += Pixel;
			++Kept;
		}
	}

	if (Kept < ReadGuardScan::MinimumBackgroundSamples)
	{
		// A box so full of glyphs that nothing is left, or a box that is mostly off screen. Not a failure
		// and not a pass - a measurement that did not happen, reported as such.
		return Result;
	}

	const int32 SignificantCount = FMath::Max(1, FMath::CeilToInt32(Kept * ReadGuardScan::SignificantBandShare));

	int32 DominantBucket = INDEX_NONE;
	int32 DominantCount = 0;
	float LowestBandLuminance = 1.0f;
	float HighestBandLuminance = 0.0f;
	int32 SignificantBands = 0;

	for (int32 Bucket = 0; Bucket < ReadGuardScan::LuminanceBuckets; ++Bucket)
	{
		if (BucketCounts[Bucket] > DominantCount)
		{
			DominantCount = BucketCounts[Bucket];
			DominantBucket = Bucket;
		}

		if (BucketCounts[Bucket] >= SignificantCount)
		{
			const FLinearColor BandColor = BucketSums[Bucket] / static_cast<float>(BucketCounts[Bucket]);
			const float BandLuminance = UReadGuardStatics::RelativeLuminance(BandColor);

			LowestBandLuminance = FMath::Min(LowestBandLuminance, BandLuminance);
			HighestBandLuminance = FMath::Max(HighestBandLuminance, BandLuminance);
			++SignificantBands;
		}
	}

	if (DominantBucket == INDEX_NONE)
	{
		return Result;
	}

	Result.bMeasured = true;
	Result.Samples = Kept;
	Result.Background = BucketSums[DominantBucket] / static_cast<float>(DominantCount);
	Result.Background.A = 1.0f;

	// The text colour is blended against the background it is drawn over before the ratio is taken, because
	// a label at 40 percent opacity is not the colour that was authored - it is that colour mixed with
	// whatever is underneath, and that mixture is what a reader has to see.
	const float TextAlpha = FMath::Clamp(TextColor.A, 0.0f, 1.0f);
	const FLinearColor EffectiveText = FMath::Lerp(Result.Background, TextColor, TextAlpha);
	const float TextLuminance = UReadGuardStatics::RelativeLuminance(EffectiveText);

	Result.Contrast = UReadGuardStatics::ContrastRatioFromLuminance(
		TextLuminance,
		UReadGuardStatics::RelativeLuminance(Result.Background));

	Result.WorstContrast = Result.Contrast;

	for (int32 Bucket = 0; Bucket < ReadGuardScan::LuminanceBuckets; ++Bucket)
	{
		if (BucketCounts[Bucket] < SignificantCount)
		{
			continue;
		}

		const FLinearColor BandColor = BucketSums[Bucket] / static_cast<float>(BucketCounts[Bucket]);
		const float BandContrast = UReadGuardStatics::ContrastRatioFromLuminance(
			TextLuminance,
			UReadGuardStatics::RelativeLuminance(BandColor));

		Result.WorstContrast = FMath::Min(Result.WorstContrast, BandContrast);
	}

	// Two or more bands far enough apart means there is no single background here, and every number above
	// is an average over something that is not uniform. The flag is what stops that average being read as
	// a fact - and it is why a finding carrying it can inform but never fail a build.
	Result.bVaries = SignificantBands > 1 && (HighestBandLuminance - LowestBandLuminance) > VariesSpread;

	return Result;
}

FReadGuardReport FReadGuardScanner::Run(
	const UGameInstance* GameInstance,
	const FReadGuardThresholds& Thresholds,
	const FReadGuardFrameCapture& Capture,
	const float ScalePercent,
	const int32 MaxSamplesPerBox)
{
	const double StartSeconds = FPlatformTime::Seconds();

	FReadGuardReport Report;
	Report.Thresholds = Thresholds;
	Report.ScalePercent = ScalePercent;
	Report.bHasRun = true;

	FIntPoint ViewportSize = FIntPoint::ZeroValue;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();
	}

	Report.ViewportResolution = ViewportSize;
	Report.ViewportDPIScale = UReadGuardStatics::ProjectDPIScaleForResolution(
		ViewportSize.X > 0 && ViewportSize.Y > 0 ? ViewportSize : Thresholds.TargetResolution);
	Report.TargetDPIScale = UReadGuardStatics::ProjectDPIScaleForResolution(Thresholds.TargetResolution);

	Report.ChecksRun.Add(FString::Printf(
		TEXT("glyph height against %.0f px, converted onto %dx%d through the project DPI curve (%.3f here, %.3f there)"),
		Thresholds.MinimumPixelHeight,
		Thresholds.TargetResolution.X,
		Thresholds.TargetResolution.Y,
		Report.ViewportDPIScale,
		Report.TargetDPIScale));

	Report.ChecksRun.Add(TEXT("desired size against allotted size - clipped text and text drawn outside its box"));

	if (Capture.IsValid())
	{
		Report.ChecksRun.Add(FString::Printf(
			TEXT("contrast against the rendered frame, %.1f:1 for normal text and %.1f:1 for large"),
			Thresholds.NormalContrast, Thresholds.LargeContrast));
	}
	else
	{
		// Never quietly. A pass with no captured frame has not proved anything about contrast, and a report
		// that stayed silent about it would be read as a report that found nothing wrong.
		Report.ChecksRun.Add(TEXT("contrast: NOT measured - no frame was captured for this pass"));
	}

	if (GameInstance == nullptr || !FSlateApplication::IsInitialized())
	{
		Report.ChecksRun.Add(TEXT("nothing was measured - there was no running game with a Slate application"));
		Report.ScanMilliseconds = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		return Report;
	}

	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	TMap<FName, int32> ScreenLookup;

	auto FindOrAddScreen = [&Report, &ScreenLookup](const FName ScreenName) -> FReadGuardScreen&
	{
		if (const int32* Existing = ScreenLookup.Find(ScreenName))
		{
			return Report.ScreensVisited[*Existing];
		}

		const int32 Index = Report.ScreensVisited.AddDefaulted();
		Report.ScreensVisited[Index].Name = ScreenName;
		ScreenLookup.Add(ScreenName, Index);
		return Report.ScreensVisited[Index];
	};

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* UserWidget = *It;

		if (!IsValid(UserWidget) || UserWidget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			continue;
		}

		const UWorld* WidgetWorld = UserWidget->GetWorld();
		if (WidgetWorld == nullptr || WidgetWorld->GetGameInstance() != GameInstance)
		{
			// The world filter is also the editor filter: a widget open in the UMG designer lives in a
			// preview world with no game instance, and measuring it would put a screen in the report that
			// no player will ever see.
			continue;
		}

		if (UserWidget->WidgetTree == nullptr || !UserWidget->GetCachedWidget().IsValid())
		{
			continue;
		}

		if (!UserWidget->IsVisible())
		{
			continue;
		}

		// The screen is the outermost user widget in the ownership chain. A health bar nested three deep
		// inside WBP_PlayerHUD belongs to WBP_PlayerHUD, because that is the thing somebody opens.
		const UUserWidget* Outermost = UserWidget;
		while (UUserWidget* Parent = Outermost->GetTypedOuter<UUserWidget>())
		{
			Outermost = Parent;
		}

		const FName ScreenName = ReadGuardScan::MakeDisplayName(Outermost->GetClass());
		const FString ScreenPrefix = ScreenName.ToString();

		FReadGuardScreen& Screen = FindOrAddScreen(ScreenName);

		UserWidget->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (!IsValid(Widget))
			{
				return;
			}

			++Report.WidgetsWalked;

			ReadGuardScan::FTextWidgetInfo Info;
			if (!ReadGuardScan::ReadTextWidget(Widget, Info))
			{
				return;
			}

			if (Info.Text.IsEmpty())
			{
				// An empty label is not unreadable, it is absent. Counting it would inflate the visit
				// counter with things nobody could have read either way.
				return;
			}

			const TSharedPtr<SWidget> Slate = Widget->GetCachedWidget();
			if (!Slate.IsValid() || !Widget->IsVisible())
			{
				++Report.TextBlocksSkipped;
				return;
			}

			const FGeometry& Geometry = Slate->GetTickSpaceGeometry();
			const FVector2D LocalSize = FVector2D(Geometry.GetLocalSize());

			if (LocalSize.X < 1.0 || LocalSize.Y < 1.0)
			{
				// Laid out to nothing: collapsed, or built this frame and not arranged yet.
				++Report.TextBlocksSkipped;
				return;
			}

			const float LayoutScale = Geometry.GetAccumulatedLayoutTransform().GetScale();

			FReadGuardMeasurement Measurement;
			Measurement.WidgetName = Widget->GetFName();
			Measurement.ScreenName = ScreenName;
			Measurement.WidgetPath = ScreenPrefix + TEXT(".") + Widget->GetName();
			Measurement.TextPreview = UReadGuardStatics::TruncateText(Info.Text, 40);
			Measurement.FontSize = Info.Font.Size;
			Measurement.LayoutScale = LayoutScale;
			Measurement.bBold = ReadGuardScan::IsBold(Info.Font);
			Measurement.bAutoWrap = Info.bAutoWrap;
			Measurement.bClips = ReadGuardScan::IsClipped(Slate);

			// The measurement, and the whole reason this plugin exists: what the font cache actually
			// produces at the scale Slate is actually drawing at, in device pixels - not the number
			// somebody typed into the Size field, which says nothing about what reaches the screen.
			Measurement.PixelHeightNow = static_cast<float>(FontMeasure->GetMaxCharacterHeight(Info.Font, LayoutScale));
			Measurement.PixelHeightAtTarget = UReadGuardStatics::ScalePixelHeight(
				Measurement.PixelHeightNow, Report.ViewportDPIScale, Report.TargetDPIScale);

			const FVector2D DesiredSize = FVector2D(Slate->GetDesiredSize());
			Measurement.OverflowX = static_cast<float>(DesiredSize.X - LocalSize.X);
			Measurement.OverflowY = static_cast<float>(DesiredSize.Y - LocalSize.Y);

			const FLinearColor Authored = Info.bColorKnown ? Info.Color : FLinearColor::White;
			FLinearColor TextColor = Authored;
			TextColor.A = FMath::Clamp(Authored.A * Slate->GetRenderOpacity(), 0.0f, 1.0f);
			Measurement.TextColor = TextColor;

			if (Info.bColorKnown)
			{
				const FReadGuardBackgroundSample Sample = FReadGuardScanner::SampleBackground(
					Capture, Geometry.GetLayoutBoundingRect(), TextColor, Thresholds.BackgroundVariesSpread, MaxSamplesPerBox);

				Measurement.bContrastMeasured = Sample.bMeasured;
				Measurement.bBackgroundVaries = Sample.bVaries;
				Measurement.Contrast = Sample.Contrast;
				Measurement.WorstContrast = Sample.WorstContrast;
				Measurement.BackgroundColor = Sample.Background;
				Measurement.BackgroundSamples = Sample.Samples;
			}

			++Report.TextBlocksVisited;
			++Screen.TextBlocks;

			int32 FindingsHere = 0;

			auto AddFinding = [&](const EReadFindingKind Kind, const EReadSeverity Severity, const float Value, const float Threshold, const bool bVaries)
			{
				FReadGuardFinding Finding;
				Finding.Kind = Kind;
				Finding.Severity = Severity;
				Finding.WidgetName = Measurement.WidgetName;
				Finding.ScreenName = Measurement.ScreenName;
				Finding.WidgetPath = Measurement.WidgetPath;
				Finding.TextPreview = Measurement.TextPreview;
				Finding.MeasuredValue = Value;
				Finding.Threshold = Threshold;
				Finding.ScalePercent = ScalePercent;
				Finding.bAtLargeScale = ScalePercent > 100.5f;
				Finding.bBackgroundVaries = bVaries;
				Finding.Detail = UReadGuardStatics::Explain(Finding);

				Report.Findings.Add(MoveTemp(Finding));
				++FindingsHere;
			};

			// (1) How tall is it, really, on the smallest screen this project supports?
			if (Measurement.PixelHeightAtTarget < Thresholds.MinimumPixelHeight - KINDA_SMALL_NUMBER)
			{
				AddFinding(EReadFindingKind::TooSmall, Thresholds.TooSmallSeverity,
					Measurement.PixelHeightAtTarget, Thresholds.MinimumPixelHeight, false);
			}

			// (2) Does it fit its box? The overflow that matters is the larger of the two axes, because a
			// label can be cut off sideways or have its second line eaten and both lose the same words.
			const float Overflow = FMath::Max(Measurement.OverflowX, Measurement.OverflowY);
			if (Overflow > Thresholds.OverflowTolerance)
			{
				AddFinding(
					Measurement.bClips ? EReadFindingKind::Clipped : EReadFindingKind::Overflow,
					Measurement.bClips ? Thresholds.ClippedSeverity : Thresholds.OverflowSeverity,
					Overflow, Thresholds.OverflowTolerance, false);
			}

			// (4) Contrast. The 200 percent pass is (3), and it is this same function run a second time.
			if (Measurement.bContrastMeasured)
			{
				++Report.ContrastMeasuredCount;
				Report.bContrastMeasured = true;

				if (Measurement.bBackgroundVaries)
				{
					++Report.BackgroundVariesCount;
				}

				const float Required = UReadGuardStatics::RequiredContrast(
					Measurement.PixelHeightAtTarget, Measurement.bBold, Thresholds);

				const float Ratio = Measurement.bBackgroundVaries ? Measurement.WorstContrast : Measurement.Contrast;

				if (Ratio < Required)
				{
					// A varying background can never produce an error. See the documentation section
					// "Why contrast over a moving background is a hint and not an error" - the value is
					// real, the single number it would have to be compared against is not.
					AddFinding(EReadFindingKind::LowContrast,
						Measurement.bBackgroundVaries ? EReadSeverity::Info : Thresholds.LowContrastSeverity,
						Ratio, Required, Measurement.bBackgroundVaries);
				}

				if (Report.WorstContrast < 0.0f || Ratio < Report.WorstContrast)
				{
					Report.WorstContrast = Ratio;
					Report.WorstContrastWidget = Measurement.WidgetPath;
					Report.bWorstContrastVaries = Measurement.bBackgroundVaries;
				}
			}

			if (Report.SmallestPixelHeight < 0.0f || Measurement.PixelHeightAtTarget < Report.SmallestPixelHeight)
			{
				Report.SmallestPixelHeight = Measurement.PixelHeightAtTarget;
				Report.SmallestPixelHeightWidget = Measurement.WidgetPath;
			}

			Screen.Findings += FindingsHere;
			Report.Measurements.Add(MoveTemp(Measurement));
		});
	}

	// Screens in a stable order, so two screenshots of the same interface produce the same report.
	Report.ScreensVisited.Sort([](const FReadGuardScreen& A, const FReadGuardScreen& B)
	{
		return A.Name.LexicalLess(B.Name);
	});

	Report.ExcludedCount = UReadGuardStatics::ApplyExemptions(Report.Findings, Thresholds.ExemptWidgets);
	UReadGuardStatics::SortFindings(Report.Findings);

	for (const FReadGuardFinding& Finding : Report.Findings)
	{
		switch (Finding.Severity)
		{
		case EReadSeverity::Error:		++Report.ErrorCount; break;
		case EReadSeverity::Warning:	++Report.WarningCount; break;
		default:						++Report.InfoCount; break;
		}
	}

	Report.Verdict = UReadGuardStatics::Judge(Report.Findings);
	Report.ScanMilliseconds = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);

	return Report;
}

void FReadGuardScanner::MergeLargeScalePass(FReadGuardReport& Normal, const FReadGuardReport& Large, const FReadGuardThresholds& Thresholds)
{
	Normal.bLargeScaleTested = true;
	Normal.LargeScalePercent = Large.ScalePercent;

	// What was already wrong at 100 percent is already in the report. Repeating it at 200 would double
	// every line and bury the one finding this pass exists to produce.
	TSet<FString> AlreadyKnown;
	AlreadyKnown.Reserve(Normal.Findings.Num());
	for (const FReadGuardFinding& Finding : Normal.Findings)
	{
		AlreadyKnown.Add(Finding.WidgetPath + TEXT("|") + UReadGuardStatics::KindName(Finding.Kind));
	}

	int32 Added = 0;

	for (const FReadGuardFinding& Finding : Large.Findings)
	{
		// Text that is too small at 100 percent is larger at 200, never smaller, so a size finding cannot
		// be new here. Contrast does not change with scale either. What the large pass really answers is
		// whether the boxes still hold their text - so that is what is carried across.
		if (Finding.Kind != EReadFindingKind::Clipped && Finding.Kind != EReadFindingKind::Overflow)
		{
			continue;
		}

		if (AlreadyKnown.Contains(Finding.WidgetPath + TEXT("|") + UReadGuardStatics::KindName(Finding.Kind)))
		{
			continue;
		}

		FReadGuardFinding Break = Finding;
		Break.Kind = EReadFindingKind::BreaksAtLargeScale;
		Break.Severity = Finding.bExcluded ? EReadSeverity::Info : Thresholds.BreaksAtLargeScaleSeverity;
		Break.bAtLargeScale = true;
		Break.ScalePercent = Large.ScalePercent;
		Break.Detail = FString::Printf(
			TEXT("%s Fine at 100 percent; at %.0f percent this text runs %.0f px past its box."),
			*UReadGuardStatics::Explain(Break), Large.ScalePercent, Finding.MeasuredValue);

		Normal.Findings.Add(MoveTemp(Break));
		++Added;
	}

	Normal.LargeScaleBreakCount = Added;

	Normal.ChecksRun.Add(FString::Printf(
		TEXT("the same two size checks again at %.0f%% text scale - %d text block(s) seen, %d thing(s) broke that were fine at 100%%"),
		Large.ScalePercent, Large.TextBlocksVisited, Added));

	UReadGuardStatics::SortFindings(Normal.Findings);

	Normal.ErrorCount = 0;
	Normal.WarningCount = 0;
	Normal.InfoCount = 0;

	for (const FReadGuardFinding& Finding : Normal.Findings)
	{
		switch (Finding.Severity)
		{
		case EReadSeverity::Error:		++Normal.ErrorCount; break;
		case EReadSeverity::Warning:	++Normal.WarningCount; break;
		default:						++Normal.InfoCount; break;
		}
	}

	Normal.Verdict = UReadGuardStatics::Judge(Normal.Findings);
	Normal.ScanMilliseconds += Large.ScanMilliseconds;
}

bool FReadGuardScanner::WriteReportFile(const FReadGuardReport& Report, const FString& Path, FString& OutFullPath)
{
	const FString Relative = Path.IsEmpty() ? TEXT("Saved/ReadGuard/report.json") : Path;

	OutFullPath = FPaths::IsRelative(Relative)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Relative)
		: Relative;

	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Json);

	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("plugin"), TEXT("ReadGuard"));
	Writer->WriteValue(TEXT("reportVersion"), 1);
	Writer->WriteValue(TEXT("verdict"), UReadGuardStatics::VerdictName(Report.Verdict));
	Writer->WriteValue(TEXT("exitCode"), UReadGuardStatics::VerdictExitCode(Report.Verdict));
	Writer->WriteValue(TEXT("hasRun"), Report.bHasRun);

	// Coverage first, and in the file as well as on the screen. A build script that wants to refuse a
	// report which checked four text blocks has to be able to see that it checked four text blocks.
	Writer->WriteValue(TEXT("textBlocksVisited"), Report.TextBlocksVisited);
	Writer->WriteValue(TEXT("textBlocksSkipped"), Report.TextBlocksSkipped);
	Writer->WriteValue(TEXT("widgetsWalked"), Report.WidgetsWalked);

	Writer->WriteValue(TEXT("errors"), Report.ErrorCount);
	Writer->WriteValue(TEXT("warnings"), Report.WarningCount);
	Writer->WriteValue(TEXT("infos"), Report.InfoCount);
	Writer->WriteValue(TEXT("excludedBySettings"), Report.ExcludedCount);

	Writer->WriteValue(TEXT("scalePercent"), Report.ScalePercent);
	Writer->WriteValue(TEXT("largeScaleTested"), Report.bLargeScaleTested);
	Writer->WriteValue(TEXT("largeScalePercent"), Report.LargeScalePercent);
	Writer->WriteValue(TEXT("largeScaleBreaks"), Report.LargeScaleBreakCount);

	Writer->WriteValue(TEXT("targetWidth"), Report.Thresholds.TargetResolution.X);
	Writer->WriteValue(TEXT("targetHeight"), Report.Thresholds.TargetResolution.Y);
	Writer->WriteValue(TEXT("targetDpiScale"), Report.TargetDPIScale);
	Writer->WriteValue(TEXT("viewportWidth"), Report.ViewportResolution.X);
	Writer->WriteValue(TEXT("viewportHeight"), Report.ViewportResolution.Y);
	Writer->WriteValue(TEXT("viewportDpiScale"), Report.ViewportDPIScale);

	Writer->WriteValue(TEXT("minimumPixelHeight"), Report.Thresholds.MinimumPixelHeight);
	Writer->WriteValue(TEXT("normalContrastThreshold"), Report.Thresholds.NormalContrast);
	Writer->WriteValue(TEXT("largeContrastThreshold"), Report.Thresholds.LargeContrast);

	// Negative means it could not be measured, and that is written as null rather than as a number so a
	// build script can never mistake "not measured" for "measured and fine".
	if (Report.SmallestPixelHeight >= 0.0f)
	{
		Writer->WriteValue(TEXT("smallestPixelHeight"), Report.SmallestPixelHeight);
		Writer->WriteValue(TEXT("smallestPixelHeightWidget"), Report.SmallestPixelHeightWidget);
	}
	else
	{
		Writer->WriteNull(TEXT("smallestPixelHeight"));
		Writer->WriteNull(TEXT("smallestPixelHeightWidget"));
	}

	Writer->WriteValue(TEXT("contrastMeasured"), Report.bContrastMeasured);
	Writer->WriteValue(TEXT("contrastMeasuredCount"), Report.ContrastMeasuredCount);
	Writer->WriteValue(TEXT("backgroundVariesCount"), Report.BackgroundVariesCount);

	if (Report.WorstContrast >= 0.0f)
	{
		Writer->WriteValue(TEXT("worstContrast"), Report.WorstContrast);
		Writer->WriteValue(TEXT("worstContrastWidget"), Report.WorstContrastWidget);
		Writer->WriteValue(TEXT("worstContrastBackgroundVaries"), Report.bWorstContrastVaries);
	}
	else
	{
		Writer->WriteNull(TEXT("worstContrast"));
		Writer->WriteNull(TEXT("worstContrastWidget"));
		Writer->WriteValue(TEXT("worstContrastBackgroundVaries"), false);
	}

	Writer->WriteValue(TEXT("scanMilliseconds"), Report.ScanMilliseconds);

	Writer->WriteArrayStart(TEXT("checksRun"));
	for (const FString& Check : Report.ChecksRun)
	{
		Writer->WriteValue(Check);
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("screensChecked"));
	for (const FReadGuardScreen& Screen : Report.ScreensVisited)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Screen.Name.ToString());
		Writer->WriteValue(TEXT("textBlocks"), Screen.TextBlocks);
		Writer->WriteValue(TEXT("findings"), Screen.Findings);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("findings"));
	for (const FReadGuardFinding& Finding : Report.Findings)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("kind"), UReadGuardStatics::KindName(Finding.Kind));
		Writer->WriteValue(TEXT("severity"), UReadGuardStatics::SeverityName(Finding.Severity));
		Writer->WriteValue(TEXT("widget"), Finding.WidgetName.ToString());
		Writer->WriteValue(TEXT("screen"), Finding.ScreenName.ToString());
		Writer->WriteValue(TEXT("path"), Finding.WidgetPath);
		Writer->WriteValue(TEXT("text"), Finding.TextPreview);
		Writer->WriteValue(TEXT("measured"), Finding.MeasuredValue);
		Writer->WriteValue(TEXT("threshold"), Finding.Threshold);
		Writer->WriteValue(TEXT("scalePercent"), Finding.ScalePercent);
		Writer->WriteValue(TEXT("atLargeScale"), Finding.bAtLargeScale);
		Writer->WriteValue(TEXT("backgroundVaries"), Finding.bBackgroundVaries);
		Writer->WriteValue(TEXT("excludedBySettings"), Finding.bExcluded);
		Writer->WriteValue(TEXT("detail"), Finding.Detail);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("measurements"));
	for (const FReadGuardMeasurement& Measurement : Report.Measurements)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("path"), Measurement.WidgetPath);
		Writer->WriteValue(TEXT("text"), Measurement.TextPreview);
		Writer->WriteValue(TEXT("fontSize"), Measurement.FontSize);
		Writer->WriteValue(TEXT("bold"), Measurement.bBold);
		Writer->WriteValue(TEXT("layoutScale"), Measurement.LayoutScale);
		Writer->WriteValue(TEXT("pixelHeightNow"), Measurement.PixelHeightNow);
		Writer->WriteValue(TEXT("pixelHeightAtTarget"), Measurement.PixelHeightAtTarget);
		Writer->WriteValue(TEXT("overflowX"), Measurement.OverflowX);
		Writer->WriteValue(TEXT("overflowY"), Measurement.OverflowY);
		Writer->WriteValue(TEXT("clips"), Measurement.bClips);
		Writer->WriteValue(TEXT("autoWrap"), Measurement.bAutoWrap);
		Writer->WriteValue(TEXT("contrastMeasured"), Measurement.bContrastMeasured);

		if (Measurement.bContrastMeasured)
		{
			Writer->WriteValue(TEXT("contrast"), Measurement.Contrast);
			Writer->WriteValue(TEXT("worstContrast"), Measurement.WorstContrast);
			Writer->WriteValue(TEXT("backgroundVaries"), Measurement.bBackgroundVaries);
			Writer->WriteValue(TEXT("backgroundSamples"), Measurement.BackgroundSamples);
		}
		else
		{
			Writer->WriteNull(TEXT("contrast"));
			Writer->WriteNull(TEXT("worstContrast"));
			Writer->WriteValue(TEXT("backgroundVaries"), false);
			Writer->WriteValue(TEXT("backgroundSamples"), 0);
		}

		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteObjectEnd();
	Writer->Close();

	if (!FFileHelper::SaveStringToFile(Json, *OutFullPath))
	{
		UE_LOG(LogReadGuard, Error, TEXT("ReadGuard: could not write the report to '%s'."), *OutFullPath);
		return false;
	}

	UE_LOG(LogReadGuard, Display, TEXT("ReadGuard: report written to '%s'."), *OutFullPath);
	return true;
}

void FReadGuardScanner::LogReport(const FReadGuardReport& Report, const bool bAllFindings)
{
	UE_LOG(LogReadGuard, Display, TEXT("--- ReadGuard ---"));
	UE_LOG(LogReadGuard, Display, TEXT("  %s"), *UReadGuardStatics::Headline(Report));
	UE_LOG(LogReadGuard, Display, TEXT("  %s"), *UReadGuardStatics::FormatScreens(Report));

	for (const FString& Check : Report.ChecksRun)
	{
		UE_LOG(LogReadGuard, Display, TEXT("  checked: %s"), *Check);
	}

	const int32 Shown = bAllFindings ? Report.Findings.Num() : FMath::Min(Report.Findings.Num(), 8);

	for (int32 Index = 0; Index < Shown; ++Index)
	{
		const FReadGuardFinding& Finding = Report.Findings[Index];
		UE_LOG(LogReadGuard, Display, TEXT("  %s"), *UReadGuardStatics::FormatFinding(Finding));

		if (bAllFindings && !Finding.Detail.IsEmpty())
		{
			UE_LOG(LogReadGuard, Display, TEXT("          %s"), *Finding.Detail);
		}
	}

	if (Shown < Report.Findings.Num())
	{
		UE_LOG(LogReadGuard, Display, TEXT("  ... and %d more. ReadGuard.Dump prints all of them."),
			Report.Findings.Num() - Shown);
	}

	UE_LOG(LogReadGuard, Display, TEXT("  Verdict %s."), *UReadGuardStatics::VerdictName(Report.Verdict));
}
