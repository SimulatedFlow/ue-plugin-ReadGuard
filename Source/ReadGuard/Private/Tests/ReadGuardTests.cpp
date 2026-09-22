// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "ReadGuardStatics.h"
#include "ReadGuardTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ReadGuardTests
{
	// CommandletContext as well as EditorContext. Every rule in this plugin is a place it can be quietly
	// wrong, and a test that only runs when somebody has the editor open is a test that will not be there
	// on the build machine - which is exactly where the gate lives.
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::EngineFilter;

	FReadGuardFinding MakeFinding(const EReadFindingKind Kind, const EReadSeverity Severity, const TCHAR* Widget)
	{
		FReadGuardFinding Finding;
		Finding.Kind = Kind;
		Finding.Severity = Severity;
		Finding.WidgetName = FName(Widget);
		Finding.WidgetPath = FString(TEXT("WBP_Test.")) + Widget;
		return Finding;
	}

	int32 CountSeverity(const TArray<FReadGuardFinding>& Findings, const EReadSeverity Severity)
	{
		int32 Count = 0;
		for (const FReadGuardFinding& Finding : Findings)
		{
			Count += (Finding.Severity == Severity) ? 1 : 0;
		}
		return Count;
	}
}

//
// (1) Relative luminance hits the two ends exactly.
//
// The three coefficients add up to one, and this test is what keeps that true. If somebody ever rounds
// them, white stops measuring 1, the contrast ceiling stops being 21, and every threshold in the plugin
// quietly moves - without a single failure anywhere else.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadGuardLuminanceTest,
	"ReadGuard.Math.RelativeLuminanceHitsBlackAndWhiteExactly",
	ReadGuardTests::TestFlags)

bool FReadGuardLuminanceTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("black is 0"), UReadGuardStatics::RelativeLuminance(FLinearColor::Black), 0.0f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("white is 1"), UReadGuardStatics::RelativeLuminance(FLinearColor::White), 1.0f, KINDA_SMALL_NUMBER);

	// Green carries most of the weight, and blue almost none. Anybody who swaps two coefficients by
	// accident produces a plugin that judges red text as brightly as green text.
	const float Red = UReadGuardStatics::RelativeLuminance(FLinearColor::Red);
	const float Green = UReadGuardStatics::RelativeLuminance(FLinearColor::Green);
	const float Blue = UReadGuardStatics::RelativeLuminance(FLinearColor::Blue);

	TestTrue(TEXT("green is the brightest primary"), Green > Red && Green > Blue);
	TestTrue(TEXT("blue is the darkest primary"), Blue < Red);
	TestEqual(TEXT("the three primaries add up to white"), Red + Green + Blue, 1.0f, KINDA_SMALL_NUMBER);

	// Out-of-range channels are clamped rather than trusted. A captured HDR frame can carry them, and a
	// luminance above 1 would push a contrast ratio past the ceiling and read as a pass.
	TestEqual(TEXT("an over-bright colour still measures 1"),
		UReadGuardStatics::RelativeLuminance(FLinearColor(4.0f, 4.0f, 4.0f, 1.0f)), 1.0f, KINDA_SMALL_NUMBER);

	return true;
}

//
// (2) White against black is exactly 21.
//
// The one number in the whole of accessibility contrast that everybody knows by heart, which makes it the
// one number a reviewer will check by hand. It has to be 21 and not 20.98.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadGuardContrastCeilingTest,
	"ReadGuard.Math.ContrastRatioOfWhiteOnBlackIsExactlyTwentyOne",
	ReadGuardTests::TestFlags)

bool FReadGuardContrastCeilingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("white on black is 21"),
		UReadGuardStatics::ContrastRatio(FLinearColor::White, FLinearColor::Black), 21.0f, 0.001f);

	TestEqual(TEXT("a colour against itself is 1"),
		UReadGuardStatics::ContrastRatio(FLinearColor::Gray, FLinearColor::Gray), 1.0f, 0.001f);

	// The failure the demo screen is built around: light grey on white. It has to come out under 4.5, or
	// the plugin would call the worst label in the demo acceptable.
	const float GreyOnWhite = UReadGuardStatics::ContrastRatio(
		FLinearColor(0.72f, 0.72f, 0.72f, 1.0f), FLinearColor::White);

	TestTrue(TEXT("light grey on white does not clear 4.5:1"), GreyOnWhite < 4.5f);
	TestTrue(TEXT("light grey on white is still above 1"), GreyOnWhite > 1.0f);

	return true;
}

//
// (3) The ratio is symmetric.
//
// Which of the two colours is the text and which is the background is a fact about the widget, not about
// the arithmetic. If the order of the arguments could change the answer, every finding in the plugin would
// depend on the order the sampler happened to visit the pixels in.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadGuardContrastSymmetryTest,
	"ReadGuard.Math.ContrastRatioIsSymmetric",
	ReadGuardTests::TestFlags)

bool FReadGuardContrastSymmetryTest::RunTest(const FString& Parameters)
{
	const FLinearColor Pairs[][2] =
	{
		{ FLinearColor::White, FLinearColor::Black },
		{ FLinearColor(0.1f, 0.2f, 0.3f), FLinearColor(0.9f, 0.8f, 0.1f) },
		{ FLinearColor::Red, FLinearColor::Green },
		{ FLinearColor(0.5f, 0.5f, 0.5f), FLinearColor(0.5f, 0.5f, 0.5f) },
	};

	for (const FLinearColor(&Pair)[2] : Pairs)
	{
		TestEqual(TEXT("the ratio does not depend on the order"),
			UReadGuardStatics::ContrastRatio(Pair[0], Pair[1]),
			UReadGuardStatics::ContrastRatio(Pair[1], Pair[0]),
			0.0001f);
	}

	TestEqual(TEXT("the luminance form is symmetric too"),
		UReadGuardStatics::ContrastRatioFromLuminance(0.13f, 0.77f),
		UReadGuardStatics::ContrastRatioFromLuminance(0.77f, 0.13f),
		0.0001f);

	return true;
}

//
// (4) A known DPI curve is converted onto 1280x720 correctly.
//
// The conversion is the whole claim of the plugin - "how tall is this at the smallest resolution you
// support" - and it is one division, which is exactly the kind of thing that gets inverted once and then
// believed for a year. The arithmetic is tested against numbers worked out by hand, and the composition on
// top of the project's own curve is tested separately so that a project with a hand-authored curve is
// still covered.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadGuardPixelHeightTest,
	"ReadGuard.Math.EffectivePixelHeightConvertsOntoTheTargetResolution",
	ReadGuardTests::TestFlags)

bool FReadGuardPixelHeightTest::RunTest(const FString& Parameters)
{
	// 24 px measured where the curve says 1.0, converted to where it says 0.6666: two thirds of the way
	// down, which is 16 px. This is the engine's default curve between 1080p and 720p.
	TestEqual(TEXT("24 px at scale 1.0 becomes 16 px at scale 0.6666"),
		UReadGuardStatics::ScalePixelHeight(24.0f, 1.0f, 0.66666f), 16.0f, 0.01f);

	// And back up again, so the conversion cannot be one-directional.
	TestEqual(TEXT("16 px at scale 0.6666 becomes 24 px at scale 1.0"),
		UReadGuardStatics::ScalePixelHeight(16.0f, 0.66666f, 1.0f), 24.0f, 0.01f);

	TestEqual(TEXT("the same scale on both sides changes nothing"),
		UReadGuardStatics::ScalePixelHeight(18.0f, 1.25f, 1.25f), 18.0f, 0.001f);

	// A source scale of zero is a measurement that never happened, not a division by zero.
	TestEqual(TEXT("a zero source scale returns the height unchanged"),
		UReadGuardStatics::ScalePixelHeight(18.0f, 0.0f, 2.0f), 18.0f, 0.001f);

	const FIntPoint Target(1280, 720);
	const float TargetScale = UReadGuardStatics::ProjectDPIScaleForResolution(Target);

	TestTrue(TEXT("the project curve gives a usable scale at 720p"), TargetScale > 0.0f && TargetScale <= 10.0f);
	TestEqual(TEXT("the project curve is stable between calls"),
		UReadGuardStatics::ProjectDPIScaleForResolution(Target), TargetScale, KINDA_SMALL_NUMBER);

	// The full function is the composition of the two, and this is what pins them together: a font that
	// produces 20 px at scale 1, drawn at a widget scale of 1.5, measured where the curve says 1.0.
	TestEqual(TEXT("EffectivePixelHeight is the widget scale times the curve conversion"),
		UReadGuardStatics::EffectivePixelHeight(20.0f, 1.0f, 1.5f, Target),
		UReadGuardStatics::ScalePixelHeight(30.0f, 1.0f, TargetScale),
		0.001f);

	return true;
}

//
// (5) The large-text threshold splits at the right place, and bold counts as large earlier.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadGuardLargeTextTest,
	"ReadGuard.Math.IsLargeTextSplitsAtTheThresholdAndBoldCountsEarlier",
	ReadGuardTests::TestFlags)

bool FReadGuardLargeTextTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("exactly at the threshold is large"), UReadGuardStatics::IsLargeText(24.0f, false));
	TestFalse(TEXT("a hair under the threshold is not"), UReadGuardStatics::IsLargeText(23.99f, false));

	TestTrue(TEXT("bold is large at 19 px"), UReadGuardStatics::IsLargeText(19.0f, true));
	TestFalse(TEXT("the same size is not large when it is not bold"), UReadGuardStatics::IsLargeText(19.0f, false));
	TestFalse(TEXT("bold is still not large at 18 px"), UReadGuardStatics::IsLargeText(18.0f, true));

	// The consequence: which threshold the contrast rule uses. Large text is allowed a lower ratio, and
	// getting the split wrong would apply the strict threshold to a headline or the loose one to body text.
	FReadGuardThresholds Thresholds;

	TestEqual(TEXT("body text is judged against the normal ratio"),
		UReadGuardStatics::RequiredContrast(12.0f, false, Thresholds), Thresholds.NormalContrast, 0.001f);

	TestEqual(TEXT("a headline is judged against the large ratio"),
		UReadGuardStatics::RequiredContrast(32.0f, false, Thresholds), Thresholds.LargeContrast, 0.001f);

	TestEqual(TEXT("bold body text at 19 px gets the large ratio"),
		UReadGuardStatics::RequiredContrast(19.0f, true, Thresholds), Thresholds.LargeContrast, 0.001f);

	return true;
}

//
// (6) An exempted finding stops being an error and does not stop being counted.
//
// This is the promise the exemption list is sold on, and it is the promise that would be easiest to break
// by "optimising" the exempt case into an early continue. The moment an exemption can remove a finding
// from the report, the settings page becomes a way to make a project look clean.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadGuardExemptionTest,
	"ReadGuard.Rules.AnExemptedFindingIsDefangedButStillCounted",
	ReadGuardTests::TestFlags)

bool FReadGuardExemptionTest::RunTest(const FString& Parameters)
{
	TArray<FReadGuardFinding> Findings;
	Findings.Add(ReadGuardTests::MakeFinding(EReadFindingKind::TooSmall, EReadSeverity::Error, TEXT("VersionStamp")));
	Findings.Add(ReadGuardTests::MakeFinding(EReadFindingKind::LowContrast, EReadSeverity::Error, TEXT("AmmoCounter")));
	Findings.Add(ReadGuardTests::MakeFinding(EReadFindingKind::Overflow, EReadSeverity::Warning, TEXT("VersionStamp")));

	const TArray<FName> Exempt = { FName(TEXT("VersionStamp")) };
	const int32 Excluded = UReadGuardStatics::ApplyExemptions(Findings, Exempt);

	TestEqual(TEXT("both of the exempt widget's findings were excluded"), Excluded, 2);
	TestEqual(TEXT("nothing was removed from the list"), Findings.Num(), 3);
	TestEqual(TEXT("only the finding that was not exempt is still an error"),
		ReadGuardTests::CountSeverity(Findings, EReadSeverity::Error), 1);

	for (const FReadGuardFinding& Finding : Findings)
	{
		if (Finding.WidgetName == FName(TEXT("VersionStamp")))
		{
			TestTrue(TEXT("the exempt finding is marked as excluded"), Finding.bExcluded);
			TestEqual(TEXT("the exempt finding is Info"), Finding.Severity, EReadSeverity::Info);
		}
		else
		{
			TestFalse(TEXT("the other finding is untouched"), Finding.bExcluded);
		}
	}

	// With the one real error exempted as well, the verdict has to go clean - the point of the list - and
	// the findings still have to be there to be printed.
	const TArray<FName> ExemptAll = { FName(TEXT("VersionStamp")), FName(TEXT("AmmoCounter")) };
	UReadGuardStatics::ApplyExemptions(Findings, ExemptAll);

	TestEqual(TEXT("everything is still in the list"), Findings.Num(), 3);
	TestEqual(TEXT("and the verdict is now clean"), UReadGuardStatics::Judge(Findings), EReadVerdict::Ok);

	return true;
}

//
// (7) Fail means at least one error, and nothing else does.
//
// The verdict is what the gate turns into an exit code, so a build either stops or does not stop on the
// strength of this one function.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadGuardVerdictTest,
	"ReadGuard.Rules.JudgeFailsOnlyWhenThereIsAnError",
	ReadGuardTests::TestFlags)

bool FReadGuardVerdictTest::RunTest(const FString& Parameters)
{
	TArray<FReadGuardFinding> Findings;

	TestEqual(TEXT("nothing at all is Ok"), UReadGuardStatics::Judge(Findings), EReadVerdict::Ok);

	Findings.Add(ReadGuardTests::MakeFinding(EReadFindingKind::LowContrast, EReadSeverity::Info, TEXT("Subtitle")));
	TestEqual(TEXT("info alone is still Ok"), UReadGuardStatics::Judge(Findings), EReadVerdict::Ok);

	// The one that matters most: a "background varies" advisory is emitted generously and must never be
	// able to stop a build on its own.
	for (int32 Index = 0; Index < 20; ++Index)
	{
		Findings.Add(ReadGuardTests::MakeFinding(EReadFindingKind::LowContrast, EReadSeverity::Info, TEXT("Subtitle")));
	}
	TestEqual(TEXT("twenty advisories are still Ok"), UReadGuardStatics::Judge(Findings), EReadVerdict::Ok);

	Findings.Add(ReadGuardTests::MakeFinding(EReadFindingKind::Overflow, EReadSeverity::Warning, TEXT("Label")));
	TestEqual(TEXT("a warning is Warn"), UReadGuardStatics::Judge(Findings), EReadVerdict::Warn);

	Findings.Add(ReadGuardTests::MakeFinding(EReadFindingKind::TooSmall, EReadSeverity::Error, TEXT("Tiny")));
	TestEqual(TEXT("an error is Fail"), UReadGuardStatics::Judge(Findings), EReadVerdict::Fail);

	TestEqual(TEXT("Ok exits 0"), UReadGuardStatics::VerdictExitCode(EReadVerdict::Ok), 0);
	TestEqual(TEXT("Warn exits 1"), UReadGuardStatics::VerdictExitCode(EReadVerdict::Warn), 1);
	TestEqual(TEXT("Fail exits 2"), UReadGuardStatics::VerdictExitCode(EReadVerdict::Fail), 2);

	return true;
}

//
// (8) Errors sort above warnings, and the worst measurement leads its group.
//
// The panel shows a limited number of rows. If the sort were unstable or the wrong way round, the finding
// that gets cut off the bottom would be the one somebody most needed to see.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadGuardSortTest,
	"ReadGuard.Rules.FindingsSortWorstFirst",
	ReadGuardTests::TestFlags)

bool FReadGuardSortTest::RunTest(const FString& Parameters)
{
	TArray<FReadGuardFinding> Findings;

	FReadGuardFinding Warning = ReadGuardTests::MakeFinding(EReadFindingKind::Overflow, EReadSeverity::Warning, TEXT("Wide"));
	Warning.MeasuredValue = 40.0f;

	FReadGuardFinding SmallError = ReadGuardTests::MakeFinding(EReadFindingKind::TooSmall, EReadSeverity::Error, TEXT("Small"));
	SmallError.MeasuredValue = 11.0f;

	FReadGuardFinding SmallestError = ReadGuardTests::MakeFinding(EReadFindingKind::TooSmall, EReadSeverity::Error, TEXT("Smallest"));
	SmallestError.MeasuredValue = 8.0f;

	Findings.Add(Warning);
	Findings.Add(SmallError);
	Findings.Add(SmallestError);

	UReadGuardStatics::SortFindings(Findings);

	TestEqual(TEXT("the smallest text leads"), Findings[0].WidgetName, FName(TEXT("Smallest")));
	TestEqual(TEXT("the other error is second"), Findings[1].WidgetName, FName(TEXT("Small")));
	TestEqual(TEXT("the warning is last"), Findings[2].WidgetName, FName(TEXT("Wide")));

	return true;
}

//
// (9) Every finding gets a sentence, and the sentence says what to do.
//
// A report that only describes the fault makes the reader do the translation every single time. This test
// exists so that adding a kind without adding its sentence is a failure and not a silent blank.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadGuardExplainTest,
	"ReadGuard.Report.EveryKindOfFindingHasASentence",
	ReadGuardTests::TestFlags)

bool FReadGuardExplainTest::RunTest(const FString& Parameters)
{
	const EReadFindingKind Kinds[] =
	{
		EReadFindingKind::TooSmall,
		EReadFindingKind::Clipped,
		EReadFindingKind::Overflow,
		EReadFindingKind::LowContrast,
		EReadFindingKind::BreaksAtLargeScale,
	};

	for (const EReadFindingKind Kind : Kinds)
	{
		FReadGuardFinding Finding = ReadGuardTests::MakeFinding(Kind, EReadSeverity::Error, TEXT("Label"));
		Finding.MeasuredValue = 9.0f;
		Finding.Threshold = 14.0f;
		Finding.ScalePercent = 200.0f;

		TestFalse(TEXT("the kind has a name"), UReadGuardStatics::KindName(Kind).IsEmpty());
		TestFalse(TEXT("the kind has a sentence"), UReadGuardStatics::Explain(Finding).IsEmpty());
		TestFalse(TEXT("the finding formats to a line"), UReadGuardStatics::FormatFinding(Finding).IsEmpty());
	}

	// A varying background gets its own wording, because the advice is different: an outline or a backing
	// box, rather than "darken the background" over something you do not control.
	FReadGuardFinding Varying = ReadGuardTests::MakeFinding(EReadFindingKind::LowContrast, EReadSeverity::Info, TEXT("Subtitle"));
	Varying.bBackgroundVaries = true;
	Varying.MeasuredValue = 2.1f;
	Varying.Threshold = 4.5f;

	TestTrue(TEXT("the varying-background sentence says the background is not one colour"),
		UReadGuardStatics::Explain(Varying).Contains(TEXT("not one colour")));

	return true;
}

//
// (10) A report that has not run says so, and a report that has run says what it looked at.
//
// The single most important line in this plugin is the visit count, because it is what stops a green
// verdict from being read as proof. A default-constructed report must never look like a clean one.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReadGuardHeadlineTest,
	"ReadGuard.Report.GreenNeverMeansNobodyLooked",
	ReadGuardTests::TestFlags)

bool FReadGuardHeadlineTest::RunTest(const FString& Parameters)
{
	const FReadGuardReport NeverRan;

	TestFalse(TEXT("a fresh report has not run"), NeverRan.bHasRun);
	TestTrue(TEXT("and its headline says so"),
		UReadGuardStatics::Headline(NeverRan).Contains(TEXT("has not scanned")));
	TestTrue(TEXT("and it names no screens"),
		UReadGuardStatics::FormatScreens(NeverRan).Contains(TEXT("nothing was checked")));

	FReadGuardReport Clean;
	Clean.bHasRun = true;
	Clean.TextBlocksVisited = 47;
	Clean.SmallestPixelHeight = 16.2f;
	Clean.WorstContrast = 7.4f;

	FReadGuardScreen Screen;
	Screen.Name = FName(TEXT("WBP_PlayerHUD"));
	Screen.TextBlocks = 47;
	Clean.ScreensVisited.Add(Screen);

	const FString Headline = UReadGuardStatics::Headline(Clean);
	TestTrue(TEXT("the headline leads with the visit count"), Headline.Contains(TEXT("47 visited")));
	TestTrue(TEXT("the headline carries the smallest height"), Headline.Contains(TEXT("smallest")));
	TestTrue(TEXT("the screens line names the screen"),
		UReadGuardStatics::FormatScreens(Clean).Contains(TEXT("WBP_PlayerHUD")));

	// Contrast that was not measured is called out rather than left silent, because silence in a green
	// report reads as "fine".
	FReadGuardReport NoContrast = Clean;
	NoContrast.WorstContrast = -1.0f;
	TestTrue(TEXT("unmeasured contrast is stated in words"),
		UReadGuardStatics::Headline(NoContrast).Contains(TEXT("contrast not measured")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
