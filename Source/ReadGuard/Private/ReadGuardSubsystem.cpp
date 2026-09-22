// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "ReadGuardSubsystem.h"

#include "CanvasItem.h"
#include "CoreGlobals.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/HUD.h"
#include "GlobalRenderResources.h"
#include "HAL/PlatformMisc.h"
#include "Misc/StringBuilder.h"
#include "ReadGuardHUD.h"
#include "ReadGuardLog.h"
#include "ReadGuardSettings.h"
#include "ReadGuardStatics.h"
#include "SceneTypes.h"
#include "Stats/Stats.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

namespace ReadGuardPanel
{
	constexpr float LineHeight = 15.0f;
	constexpr float BoxPadding = 8.0f;

	const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.62f);
	const FLinearColor GoodColor(0.55f, 0.95f, 0.55f, 1.0f);
	const FLinearColor WarnColor(1.0f, 0.78f, 0.30f, 1.0f);
	const FLinearColor ErrorColor(1.0f, 0.42f, 0.38f, 1.0f);
	const FLinearColor BodyColor(0.90f, 0.90f, 0.90f, 1.0f);
	const FLinearColor FaintColor(0.62f, 0.62f, 0.66f, 1.0f);

	void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
}

void UReadGuardSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApplySettings();

	// The frame capture is how the contrast half of this plugin gets a real background instead of a guess.
	// The delegate is a global one and other systems take screenshots too, so every capture that arrives
	// while ReadGuard is not waiting for one is ignored.
	ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UReadGuardSubsystem::OnScreenshotCaptured);

	if (bAutoDrawOnAnyHUD)
	{
		HUDPostRenderHandle = AHUD::OnHUDPostRender.AddLambda([this](AHUD* HUD, UCanvas* Canvas)
		{
			if (IsValid(HUD) && Canvas != nullptr && bShowReport && !HUD->IsA<AReadGuardHUD>())
			{
				// AReadGuardHUD draws it itself. Without this test a project that both reparented its HUD
				// and switched the option on would get the panel twice, one on top of the other.
				DrawReport(Canvas, FVector2D(28.0f, 90.0f), 980.0f);
			}
		});
	}

	const UReadGuardSettings& Settings = UReadGuardSettings::Get();
	if (Settings.bScanOnBeginPlay)
	{
		// Not zero. Widgets are built in BeginPlay and have no geometry until Slate has ticked and laid
		// them out, so an immediate scan would measure an interface that does not exist yet and report
		// every one of its text blocks as skipped.
		SecondsUntilAutoScan = FMath::Max(Settings.AutoScanDelaySeconds, 0.0f);
	}

	bInitialized = true;

	UE_LOG(LogReadGuard, Log, TEXT("ReadGuard: target %dx%d, floor %.0f px, contrast %.1f:1 / %.1f:1."),
		Thresholds.TargetResolution.X, Thresholds.TargetResolution.Y,
		Thresholds.MinimumPixelHeight, Thresholds.NormalContrast, Thresholds.LargeContrast);
}

void UReadGuardSubsystem::Deinitialize()
{
	bInitialized = false;

	// Whatever happens, the project's own application scale goes back. A measuring tool that leaves the
	// interface at 200 percent because the level changed mid-pass would be worse than useless.
	RestoreApplicationScale();

	if (ScreenshotHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
		ScreenshotHandle.Reset();
	}

	if (HUDPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HUDPostRenderHandle);
		HUDPostRenderHandle.Reset();
	}

	Super::Deinitialize();
}

UReadGuardSubsystem* UReadGuardSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: WorldContextObject->GetWorld();

	if (World == nullptr)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UReadGuardSubsystem>() : nullptr;
}

TStatId UReadGuardSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UReadGuardSubsystem, STATGROUP_Tickables);
}

void UReadGuardSubsystem::ApplySettings()
{
	const UReadGuardSettings& Settings = UReadGuardSettings::Get();

	Thresholds = Settings.MakeThresholds();

	bUseExemptions = Settings.bUseExemptions;
	bMeasureContrast = Settings.bMeasureContrast;
	bShowReport = Settings.bShowReportByDefault;
	bAutoDrawOnAnyHUD = Settings.bAutoDrawOnAnyHUD;

	SettleFrames = FMath::Max(Settings.SettleFrames, 1);
	MaxSamplesPerBox = FMath::Max(Settings.MaxSamplesPerBox, 16);
	MaxReportRows = FMath::Max(Settings.MaxReportRows, 1);
	LargeScalePercent = FMath::Max(Settings.LargeScalePercent, 100.0f);
	AutoScanIntervalSeconds = FMath::Max(Settings.AutoScanIntervalSeconds, 0.0f);
	ReportPath = Settings.ReportPath;
}

void UReadGuardSubsystem::SetTargetResolution(const FIntPoint Resolution)
{
	Thresholds.TargetResolution = FIntPoint(FMath::Max(Resolution.X, 1), FMath::Max(Resolution.Y, 1));

	UE_LOG(LogReadGuard, Display, TEXT("ReadGuard: measuring against %dx%d (DPI scale %.3f there)."),
		Thresholds.TargetResolution.X, Thresholds.TargetResolution.Y,
		UReadGuardStatics::ProjectDPIScaleForResolution(Thresholds.TargetResolution));
}

void UReadGuardSubsystem::SetExemptionsEnabled(const bool bEnabled)
{
	bUseExemptions = bEnabled;
	Thresholds.bUseExemptions = bEnabled;

	// The list itself is re-read rather than remembered, so a change in Project Settings takes effect
	// without a restart - and so that turning exemptions back on cannot resurrect a stale list.
	Thresholds.ExemptWidgets = bEnabled ? UReadGuardSettings::Get().ExemptWidgets : TArray<FName>();
}

void UReadGuardSubsystem::SetReportVisible(const bool bVisible)
{
	bShowReport = bVisible;
}

// --------------------------------------------------------------------------------------------------
// Passes
// --------------------------------------------------------------------------------------------------

bool UReadGuardSubsystem::Scan()
{
	if (IsScanning())
	{
		return false;
	}

	PassQueue.Add(FPass{ 100.0f, false });
	return true;
}

bool UReadGuardSubsystem::ScanAtScale(const float ScalePercent)
{
	if (IsScanning())
	{
		return false;
	}

	PassQueue.Add(FPass{ FMath::Clamp(ScalePercent, 25.0f, 800.0f), ScalePercent > 100.5f });
	return true;
}

bool UReadGuardSubsystem::ScanBothScales()
{
	if (IsScanning())
	{
		return false;
	}

	PassQueue.Add(FPass{ 100.0f, false });
	PassQueue.Add(FPass{ LargeScalePercent, true });
	return true;
}

FReadGuardReport UReadGuardSubsystem::MeasureNow()
{
	Report = RunPass(100.0f);
	OnScanComplete.Broadcast(Report);
	return Report;
}

FReadGuardReport UReadGuardSubsystem::RunPass(const float ScalePercent) const
{
	// A capture from an older frame is still a capture of this interface as long as nothing has moved, and
	// the alternative is no contrast measurement at all. Two frames is the widest gap that can be defended;
	// past that the report says contrast was not measured instead of measuring the wrong frame.
	const bool bCaptureFresh = Capture.IsValid() && (GFrameCounter - Capture.FrameNumber) <= 2;

	return FReadGuardScanner::Run(
		GetGameInstance(),
		Thresholds,
		bCaptureFresh ? Capture : FReadGuardFrameCapture(),
		ScalePercent,
		MaxSamplesPerBox);
}

void UReadGuardSubsystem::BeginPass(const FPass& Pass)
{
	CurrentPass = Pass;

	if (!FMath::IsNearlyEqual(Pass.ScalePercent, 100.0f))
	{
		// The real thing, not a prediction. UUserInterfaceSettings::ApplicationScale is what the game
		// viewport's DPI scaler multiplies into every widget's layout scale, so setting it here makes Slate
		// lay the entire interface out at the new size - and it touches the game's interface only, never
		// the editor around it.
		if (UUserInterfaceSettings* UISettings = GetMutableDefault<UUserInterfaceSettings>())
		{
			if (!bApplicationScaleChanged)
			{
				SavedApplicationScale = UISettings->ApplicationScale;
				bApplicationScaleChanged = true;
			}

			UISettings->ApplicationScale = SavedApplicationScale * (Pass.ScalePercent / 100.0f);
		}

		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().InvalidateAllWidgets(false);
		}
	}

	Step = EStep::Settle;
	FramesRemaining = SettleFrames;
}

void UReadGuardSubsystem::RequestCapture()
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		bAwaitingCapture = false;
		return;
	}

	// With the UI shown and the capture restricted to the game viewport, what comes back is the interface
	// exactly as the player sees it - which is the only image in which the question "what is behind this
	// text" has an answer.
	FScreenshotRequest::RequestScreenshot(/*bShowUI=*/true, /*bRestrictToGameViewport=*/true);

	bAwaitingCapture = true;
	CaptureRequestedFrame = GFrameCounter;
	CaptureWaitFrames = 12;
	Step = EStep::AwaitCapture;
}

void UReadGuardSubsystem::OnScreenshotCaptured(const int32 Width, const int32 Height, const TArray<FColor>& Bitmap)
{
	if (!bAwaitingCapture || Width <= 0 || Height <= 0 || Bitmap.Num() < Width * Height)
	{
		return;
	}

	TSharedPtr<SViewport> ViewportWidget = GEngine && GEngine->GameViewport
		? GEngine->GameViewport->GetGameViewportWidget()
		: nullptr;

	if (!ViewportWidget.IsValid())
	{
		bAwaitingCapture = false;
		return;
	}

	const FSlateRect ViewportRect = ViewportWidget->GetTickSpaceGeometry().GetLayoutBoundingRect();

	Capture.Pixels = Bitmap;
	Capture.Size = FIntPoint(Width, Height);
	Capture.Origin = FVector2f(ViewportRect.Left, ViewportRect.Top);

	// Derived from the two sizes rather than assumed to be one-to-one. On a window whose device pixels and
	// Slate units differ, assuming would put every sample in the wrong place and the error would be silent.
	Capture.SlateSize = FVector2f(ViewportRect.GetSize());
	Capture.FrameNumber = GFrameCounter;

	bAwaitingCapture = false;
}

void UReadGuardSubsystem::RestoreApplicationScale()
{
	if (!bApplicationScaleChanged)
	{
		return;
	}

	if (UUserInterfaceSettings* UISettings = GetMutableDefault<UUserInterfaceSettings>())
	{
		UISettings->ApplicationScale = SavedApplicationScale;
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(false);
	}

	bApplicationScaleChanged = false;
}

void UReadGuardSubsystem::FinishPass()
{
	const FReadGuardReport PassReport = RunPass(CurrentPass.ScalePercent);

	if (CurrentPass.bLarge && NormalPassReport.bHasRun)
	{
		Report = NormalPassReport;
		FReadGuardScanner::MergeLargeScalePass(Report, PassReport, Thresholds);
	}
	else
	{
		Report = PassReport;
		NormalPassReport = PassReport;
	}

	RestoreApplicationScale();

	Step = EStep::Idle;
	bAwaitingCapture = false;

	if (PassQueue.Num() == 0)
	{
		FinalizeRun();
	}
}

void UReadGuardSubsystem::FinalizeRun()
{
	NormalPassReport = FReadGuardReport();

	// The headline always goes to the log, gate or no gate. A scan started from the console has to leave a
	// trace somewhere other than a panel that may not be visible.
	UE_LOG(LogReadGuard, Display, TEXT("ReadGuard: %s"), *UReadGuardStatics::Headline(Report));
	UE_LOG(LogReadGuard, Display, TEXT("ReadGuard: %s"), *UReadGuardStatics::FormatScreens(Report));

	OnScanComplete.Broadcast(Report);

	if (!bGateRunning)
	{
		return;
	}

	bGateRunning = false;

	FReadGuardScanner::LogReport(Report, /*bAllFindings=*/true);

	FString FullPath;
	const bool bWritten = FReadGuardScanner::WriteReportFile(Report, GatePath.IsEmpty() ? ReportPath : GatePath, FullPath);

	const int32 ExitCode = UReadGuardStatics::VerdictExitCode(Report.Verdict);

	UE_LOG(LogReadGuard, Display,
		TEXT("READGUARD GATE RESULT=%s visited=%d errors=%d warnings=%d excluded=%d largeScaleBreaks=%d exit=%d"),
		*UReadGuardStatics::VerdictName(Report.Verdict).ToUpper(),
		Report.TextBlocksVisited, Report.ErrorCount, Report.WarningCount,
		Report.ExcludedCount, Report.LargeScaleBreakCount, ExitCode);

	if (!bWritten)
	{
		UE_LOG(LogReadGuard, Error, TEXT("ReadGuard.Gate: the report could not be written. Treating that as a failure."));
	}

	if (!bGateExitWhenDone)
	{
		return;
	}

	// A report that could not be written is a gate that did not run, and a gate that did not run must never
	// be allowed to look like a gate that passed.
	const uint8 Status = bWritten ? static_cast<uint8>(ExitCode) : 2;
	FPlatformMisc::RequestExitWithStatus(/*Force=*/false, Status, TEXT("ReadGuard.Gate"));
}

void UReadGuardSubsystem::BeginGate(const FString& Path, const bool bExitWhenDone)
{
	if (bGateRunning)
	{
		UE_LOG(LogReadGuard, Warning, TEXT("ReadGuard.Gate: a gate run is already in progress."));
		return;
	}

	GatePath = Path;
	bGateExitWhenDone = bExitWhenDone;
	bGateRunning = true;

	if (!ScanBothScales())
	{
		// Something else is mid-scan. Queue behind it rather than refusing: a build step that asked for a
		// gate has to get one.
		PassQueue.Add(FPass{ 100.0f, false });
		PassQueue.Add(FPass{ LargeScalePercent, true });
	}

	UE_LOG(LogReadGuard, Display, TEXT("ReadGuard.Gate: checking at 100%% and at %.0f%%."), LargeScalePercent);
}

void UReadGuardSubsystem::Tick(const float DeltaTime)
{
	if (!bInitialized)
	{
		return;
	}

	if (SecondsUntilAutoScan >= 0.0f)
	{
		SecondsUntilAutoScan -= DeltaTime;
		if (SecondsUntilAutoScan < 0.0f)
		{
			Scan();
			SecondsUntilAutoScan = AutoScanIntervalSeconds > 0.0f ? AutoScanIntervalSeconds : -1.0f;
		}
	}

	switch (Step)
	{
	case EStep::Idle:
		if (PassQueue.Num() > 0)
		{
			const FPass Next = PassQueue[0];
			PassQueue.RemoveAt(0);
			BeginPass(Next);
		}
		break;

	case EStep::Settle:
		if (--FramesRemaining <= 0)
		{
			if (bMeasureContrast)
			{
				RequestCapture();
			}
			else
			{
				FinishPass();
			}
		}
		break;

	case EStep::AwaitCapture:
		if (!bAwaitingCapture)
		{
			FinishPass();
		}
		else if (--CaptureWaitFrames <= 0)
		{
			// The capture never came back. Measure everything that does not need it and let the report say
			// contrast was not measured - which is a true statement, unlike a green contrast line.
			UE_LOG(LogReadGuard, Warning, TEXT("ReadGuard: no frame capture came back; contrast was not measured for this pass."));
			bAwaitingCapture = false;
			FinishPass();
		}
		break;

	default:
		break;
	}
}

bool UReadGuardSubsystem::WriteReport(FString Path)
{
	FString FullPath;
	return FReadGuardScanner::WriteReportFile(Report, Path.IsEmpty() ? ReportPath : Path, FullPath);
}

// --------------------------------------------------------------------------------------------------
// The panel
// --------------------------------------------------------------------------------------------------

void UReadGuardSubsystem::DrawReport(UCanvas* Canvas, const FVector2D& Origin, const float Width) const
{
	using namespace ReadGuardPanel;

	if (Canvas == nullptr)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (Font == nullptr)
	{
		return;
	}

	const int32 ShownFindings = FMath::Min(Report.Findings.Num(), MaxReportRows);
	const bool bTruncated = Report.Findings.Num() > ShownFindings;
	const bool bClean = Report.bHasRun && Report.ErrorCount == 0 && Report.WarningCount == 0;

	// Worked out before anything is drawn, so a report with two findings does not sit in a panel sized for
	// twenty.
	int32 LineCount = 3;										// title, headline, screens
	LineCount += ShownFindings;
	LineCount += bTruncated ? 1 : 0;
	LineCount += bClean ? (1 + Report.ChecksRun.Num()) : 0;
	LineCount += Report.bHasRun ? 0 : 1;
	LineCount += IsScanning() ? 1 : 0;

	const float BoxHeight = LineCount * LineHeight + BoxPadding * 2.0f;
	DrawFilledRect(Canvas,
		FVector2D(Origin.X - BoxPadding, Origin.Y - BoxPadding),
		FVector2D(Width, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(Origin.Y);
	auto DrawLine = [&](FStringView Line, const FLinearColor& Color)
	{
		FCanvasTextStringViewItem Item(FVector2D(Origin.X, LineY), Line, Font, Color);
		Canvas->DrawItem(Item);
		LineY += LineHeight;
	};

	const FLinearColor& VerdictColor =
		(Report.Verdict == EReadVerdict::Fail) ? ErrorColor :
		(Report.Verdict == EReadVerdict::Warn) ? WarnColor : GoodColor;

	TStringBuilder<512> Line;

	// The title carries the verdict and the colour, so the answer is readable from across the room and
	// before a single finding has been read.
	Line.Reset();
	Line.Appendf(TEXT("ReadGuard   %s"), Report.bHasRun
		? *UReadGuardStatics::VerdictName(Report.Verdict).ToUpper()
		: TEXT("NOT RUN"));

	if (Report.bLargeScaleTested)
	{
		Line.Appendf(TEXT("   checked at 100%% and %.0f%%"), Report.LargeScalePercent);
	}

	DrawLine(Line.ToView(), Report.bHasRun ? VerdictColor : FaintColor);

	DrawLine(UReadGuardStatics::Headline(Report), BodyColor);
	DrawLine(UReadGuardStatics::FormatScreens(Report), FaintColor);

	if (!Report.bHasRun)
	{
		DrawLine(TEXT("ReadGuard.Scan measures what is on screen now. Nothing has been measured yet."), FaintColor);
	}

	if (IsScanning())
	{
		DrawLine(TEXT("scanning..."), FaintColor);
	}

	for (int32 Index = 0; Index < ShownFindings; ++Index)
	{
		const FReadGuardFinding& Finding = Report.Findings[Index];

		const FLinearColor& Color =
			(Finding.Severity == EReadSeverity::Error) ? ErrorColor :
			(Finding.Severity == EReadSeverity::Warning) ? WarnColor : FaintColor;

		DrawLine(UReadGuardStatics::FormatFinding(Finding), Color);
	}

	if (bTruncated)
	{
		Line.Reset();
		Line.Appendf(TEXT("... and %d more. ReadGuard.Dump prints all of them."), Report.Findings.Num() - ShownFindings);
		DrawLine(Line.ToView(), FaintColor);
	}

	if (bClean)
	{
		// Green has to say what it looked at, or green means nothing. This is the whole reason the checks
		// are listed rather than a single "no issues found".
		DrawLine(TEXT("Nothing to report. What was checked:"), GoodColor);

		for (const FString& Check : Report.ChecksRun)
		{
			Line.Reset();
			Line.Appendf(TEXT("   %s"), *Check);
			DrawLine(Line.ToView(), FaintColor);
		}
	}
}
