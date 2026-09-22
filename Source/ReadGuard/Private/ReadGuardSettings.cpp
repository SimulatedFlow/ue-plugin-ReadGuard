// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "ReadGuardSettings.h"

UReadGuardSettings::UReadGuardSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("ReadGuard");
}

FName UReadGuardSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UReadGuardSettings::GetSectionName() const
{
	return TEXT("ReadGuard");
}

const UReadGuardSettings& UReadGuardSettings::Get()
{
	const UReadGuardSettings* Settings = GetDefault<UReadGuardSettings>();
	check(Settings);
	return *Settings;
}

FReadGuardThresholds UReadGuardSettings::MakeThresholds() const
{
	FReadGuardThresholds Thresholds;

	Thresholds.TargetResolution = FIntPoint(FMath::Max(TargetResolution.X, 1), FMath::Max(TargetResolution.Y, 1));
	Thresholds.MinimumPixelHeight = MinimumPixelHeight;
	Thresholds.NormalContrast = NormalContrastRatio;
	Thresholds.LargeContrast = LargeContrastRatio;
	Thresholds.LargeTextPixels = LargeTextPixels;
	Thresholds.LargeTextBoldPixels = LargeTextBoldPixels;
	Thresholds.OverflowTolerance = OverflowTolerance;
	Thresholds.BackgroundVariesSpread = BackgroundVariesSpread;

	Thresholds.TooSmallSeverity = TooSmallSeverity;
	Thresholds.ClippedSeverity = ClippedSeverity;
	Thresholds.OverflowSeverity = OverflowSeverity;
	Thresholds.LowContrastSeverity = LowContrastSeverity;
	Thresholds.BreaksAtLargeScaleSeverity = BreaksAtLargeScaleSeverity;

	// The master switch is applied here rather than in the rules, so that everything downstream sees one
	// list and never has to remember to check a second flag before trusting it.
	Thresholds.bUseExemptions = bUseExemptions;
	if (bUseExemptions)
	{
		Thresholds.ExemptWidgets = ExemptWidgets;
	}

	return Thresholds;
}
