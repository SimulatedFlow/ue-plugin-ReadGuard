// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/SlateRect.h"
#include "ReadGuardTypes.h"

class UGameInstance;

/**
 * One frame of the rendered interface, kept so the contrast measurement has something real to read.
 *
 * Nobody knows what is behind a piece of text without the finished image. The alternative - taking the
 * parent widget's brush colour and calling that the background - is wrong the moment there is a scene, a
 * gradient, a video or another widget in between, and it is wrong silently. So ReadGuard reads the pixels.
 *
 * Origin and SlateSize describe where this bitmap sits in Slate's coordinates, which is what turns a
 * widget's geometry into an index into Pixels. PixelsPerSlateUnit is derived from the two sizes rather
 * than assumed, so a window whose device pixels and Slate units differ still lands on the right pixels.
 */
struct READGUARD_API FReadGuardFrameCapture
{
	/** The captured frame, row-major, sRGB encoded as FColor always is. */
	TArray<FColor> Pixels;

	/** Width and height of the capture, in pixels. */
	FIntPoint Size = FIntPoint::ZeroValue;

	/** Where pixel (0,0) sits in Slate's window coordinates. */
	FVector2f Origin = FVector2f::ZeroVector;

	/** How large the captured region is in Slate units. */
	FVector2f SlateSize = FVector2f::ZeroVector;

	/** The frame this was taken on, so a stale capture can be recognised as stale. */
	uint64 FrameNumber = 0;

	bool IsValid() const
	{
		return Pixels.Num() > 0
			&& Size.X > 0
			&& Size.Y > 0
			&& Pixels.Num() >= Size.X * Size.Y
			&& SlateSize.X > 1.0f
			&& SlateSize.Y > 1.0f;
	}

	/** Slate window coordinates to a pixel index in this bitmap. */
	FVector2f SlateToPixel(const FVector2f& SlatePoint) const
	{
		const FVector2f Local = SlatePoint - Origin;
		return FVector2f(Local.X * (Size.X / SlateSize.X), Local.Y * (Size.Y / SlateSize.Y));
	}
};

/** What sampling the pixels under one text box produced. */
struct READGUARD_API FReadGuardBackgroundSample
{
	/** False when there was no capture, the box fell outside it, or too little background was left. */
	bool bMeasured = false;

	/** True when the background under the box was not one colour. */
	bool bVaries = false;

	/** The dominant background colour: the mean of the largest luminance band under the box. */
	FLinearColor Background = FLinearColor::Black;

	/** Contrast between the text and that dominant background. */
	float Contrast = 0.0f;

	/** The lowest contrast against any band of background that covered a meaningful part of the box. */
	float WorstContrast = 0.0f;

	/** How many pixels survived the glyph filter and were actually used. */
	int32 Samples = 0;
};

/**
 * The measuring itself: the widget walk, the pixel sampling, the rules, the report file.
 *
 * A free struct of static functions rather than a UObject, because none of this needs to be a UObject and
 * because the pieces that can be tested without a game should not have to be reached through one.
 */
struct READGUARD_API FReadGuardScanner
{
	/**
	 * Walk every visible text block the given game instance owns and measure it.
	 *
	 * Everything in the returned report is a measurement taken during this call - there is no cache, no
	 * incremental state and no memory of an earlier pass. The one exception is the captured frame, which is
	 * necessarily from a frame that has already been drawn, and which the report labels as such.
	 */
	static FReadGuardReport Run(
		const UGameInstance* GameInstance,
		const FReadGuardThresholds& Thresholds,
		const FReadGuardFrameCapture& Capture,
		float ScalePercent,
		int32 MaxSamplesPerBox);

	/**
	 * Sample the background under a text box and work out the contrast against it.
	 *
	 * The box in the capture contains the text as well as what is behind it, so the pixels that are the
	 * glyphs themselves are discarded first - they are known, because the text colour is known. What is
	 * left is put into a luminance histogram. The largest band is the background. Every band that covers a
	 * meaningful share of the box contributes a contrast ratio, the worst of those is kept, and if those
	 * bands are far enough apart in luminance the sample is marked as varying.
	 *
	 * That last flag is the entire honesty of this feature. Text over a moving scene has no single
	 * background, and a number that pretends otherwise would be worse than no number at all.
	 */
	static FReadGuardBackgroundSample SampleBackground(
		const FReadGuardFrameCapture& Capture,
		const FSlateRect& SlateRect,
		const FLinearColor& TextColor,
		float VariesSpread,
		int32 MaxSamples);

	/**
	 * Fold a large-text pass into the normal report.
	 *
	 * Only what is new counts. A label that was already too small at 100 percent is still too small at 200
	 * and has already been reported; a label that only stops fitting when somebody turns the text size up
	 * is the finding no other tool in the engine produces, and it gets its own kind so it can be read off
	 * the report at a glance.
	 */
	static void MergeLargeScalePass(FReadGuardReport& Normal, const FReadGuardReport& Large, const FReadGuardThresholds& Thresholds);

	/** Write the report as JSON. OutFullPath is filled in either way, so a failure can name the path. */
	static bool WriteReportFile(const FReadGuardReport& Report, const FString& Path, FString& OutFullPath);

	/** The report to the log: the headline and the screens always, every finding when asked. */
	static void LogReport(const FReadGuardReport& Report, bool bAllFindings);
};
