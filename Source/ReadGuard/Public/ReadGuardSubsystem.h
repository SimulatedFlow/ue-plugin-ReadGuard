// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReadGuardScanner.h"
#include "ReadGuardTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "ReadGuardSubsystem.generated.h"

class UCanvas;

/** Fired when a scan - including the large-text pass, when one was asked for - has finished. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReadGuardScanComplete, const FReadGuardReport&, Report);

/**
 * The measurer.
 *
 * It does not run every frame. Widgets are measured when somebody asks - a console command, a Blueprint
 * call, a button in the demo - or on a timer that is off by default, because a checking tool that quietly
 * spends frames on every frame is a checking tool that gets deleted.
 *
 * A scan is not instant, and that is a consequence of measuring rather than guessing. Contrast needs the
 * rendered frame, which arrives after the frame has been drawn; the large-text pass needs Slate to lay the
 * whole interface out again at the new scale, which needs a tick and a draw. So Scan() starts a pass and
 * OnScanComplete says when it is over. MeasureNow() is there for the cases where an answer is wanted this
 * instant and a one-frame-old background is acceptable.
 */
UCLASS()
class READGUARD_API UReadGuardSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	//~ UGameInstanceSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bInitialized; }
	virtual bool IsTickableWhenPaused() const override { return true; }

	/** The subsystem for the world this object lives in, or null. */
	static UReadGuardSubsystem* Get(const UObject* WorldContextObject);

	//~ Scanning -------------------------------------------------------------------------------------

	/**
	 * Start a scan at the normal text scale.
	 *
	 * Returns false when one is already running. The result arrives on OnScanComplete and stays available
	 * from GetReport().
	 */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	bool Scan();

	/**
	 * Start a scan at the given percentage of the normal text scale.
	 *
	 * 200 is the case that matters. ReadGuard really sets the application scale, lets the interface lay
	 * itself out again and measures it there - it does not predict what 200 percent would do, because a
	 * prediction is exactly the thing this plugin exists to replace.
	 */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	bool ScanAtScale(float ScalePercent);

	/**
	 * Scan at 100 percent and then at the large scale, and merge the two.
	 *
	 * This is what the gate runs, and it is what the demo's "check at 200 percent" button runs, because a
	 * large-scale report on its own cannot tell you which of its findings are new.
	 */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	bool ScanBothScales();

	/**
	 * Measure right now, with whatever frame was captured last, and return the result.
	 *
	 * Synchronous. The geometry is current; the background is from the most recently captured frame, which
	 * may be several frames old or may not exist at all - in which case the report says contrast was not
	 * measured rather than reporting it as fine.
	 */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	FReadGuardReport MeasureNow();

	/** True while a pass is in flight. */
	UFUNCTION(BlueprintPure, Category = "ReadGuard")
	bool IsScanning() const { return Step != EStep::Idle || PassQueue.Num() > 0; }

	//~ Results --------------------------------------------------------------------------------------

	/** The last completed report. Its bHasRun is false until one has completed. */
	UFUNCTION(BlueprintPure, Category = "ReadGuard")
	const FReadGuardReport& GetReport() const { return Report; }

	UFUNCTION(BlueprintPure, Category = "ReadGuard")
	TArray<FReadGuardFinding> GetFindings() const { return Report.Findings; }

	UFUNCTION(BlueprintPure, Category = "ReadGuard")
	EReadVerdict GetVerdict() const { return Report.Verdict; }

	/** Write the last report as JSON. An empty path means the one in Project Settings. */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	bool WriteReport(FString Path);

	UPROPERTY(BlueprintAssignable, Category = "ReadGuard")
	FReadGuardScanComplete OnScanComplete;

	//~ The on-screen panel ---------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	void SetReportVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "ReadGuard")
	bool IsReportVisible() const { return bShowReport; }

	/** Draw the report. Called by AReadGuardHUD, and by the auto-draw hook on any other HUD. */
	void DrawReport(UCanvas* Canvas, const FVector2D& Origin, float Width) const;

	//~ Live overrides -------------------------------------------------------------------------------

	/** The resolution every pixel height is converted onto. The demo's 720p / 1080p button. */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	void SetTargetResolution(FIntPoint Resolution);

	UFUNCTION(BlueprintPure, Category = "ReadGuard")
	FIntPoint GetTargetResolution() const { return Thresholds.TargetResolution; }

	/** Use the exemption list, or do not, for this session. The report shows the count either way. */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	void SetExemptionsEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "ReadGuard")
	bool AreExemptionsEnabled() const { return bUseExemptions; }

	/** Re-read Project Settings, discarding any live override. */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	void ApplySettings();

	//~ The gate --------------------------------------------------------------------------------------

	/**
	 * Scan at both scales, write the report and - unless told not to - end the process with 0, 1 or 2.
	 *
	 * The same three codes as LocaleGuard, AssetWarden, WidgetLedger, LoadLens, HeapCensus and BindGuard,
	 * meaning the same three things.
	 */
	void BeginGate(const FString& Path, bool bExitWhenDone);

private:
	/** Where a pass is in its life. Measuring properly is not instantaneous, and this is why. */
	enum class EStep : uint8
	{
		/** Nothing in flight. */
		Idle,

		/** The scale has been set; waiting for Slate to lay the interface out again and draw it. */
		Settle,

		/** A frame capture has been requested; waiting for it to come back. */
		AwaitCapture,
	};

	/** One pass: a scale to measure at, and whether it is the large-text one. */
	struct FPass
	{
		float ScalePercent = 100.0f;
		bool bLarge = false;
	};

	void BeginPass(const FPass& Pass);
	void FinishPass();
	void FinalizeRun();

	void RequestCapture();
	void OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap);

	/** Restore whatever the project's application scale was before a scaled pass touched it. */
	void RestoreApplicationScale();

	FReadGuardReport RunPass(float ScalePercent) const;

	//~ State -----------------------------------------------------------------------------------------

	/** The merged result of the last completed run. */
	UPROPERTY()
	FReadGuardReport Report;

	/** The normal-scale half of the run in progress. */
	FReadGuardReport NormalPassReport;

	/** The most recently captured frame. Shared by every pass until a newer one replaces it. */
	FReadGuardFrameCapture Capture;

	FReadGuardThresholds Thresholds;

	TArray<FPass> PassQueue;
	FPass CurrentPass;
	EStep Step = EStep::Idle;

	int32 FramesRemaining = 0;
	int32 CaptureWaitFrames = 0;
	uint64 CaptureRequestedFrame = 0;

	/** The application scale before ReadGuard touched it, and whether it needs putting back. */
	float SavedApplicationScale = 1.0f;
	bool bApplicationScaleChanged = false;

	bool bAwaitingCapture = false;
	bool bInitialized = false;
	bool bShowReport = true;
	bool bMeasureContrast = true;
	bool bUseExemptions = true;
	bool bAutoDrawOnAnyHUD = false;

	int32 SettleFrames = 3;
	int32 MaxSamplesPerBox = 512;
	int32 MaxReportRows = 14;
	float LargeScalePercent = 200.0f;
	float AutoScanIntervalSeconds = 0.0f;
	float SecondsUntilAutoScan = -1.0f;
	FString ReportPath;

	/** Gate state. */
	bool bGateRunning = false;
	bool bGateExitWhenDone = true;
	FString GatePath;

	FDelegateHandle ScreenshotHandle;
	FDelegateHandle HUDPostRenderHandle;
};
