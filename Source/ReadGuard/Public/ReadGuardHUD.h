// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ReadGuardHUD.generated.h"

/**
 * Draws the report.
 *
 * On UCanvas and not in UMG, and here that is not a portability argument but a correctness one. ReadGuard
 * measures every text block the interface is drawing. A report built out of UMG widgets would put its own
 * text blocks into that interface, and they would be measured too - a tool that changes the thing it is
 * measuring. Drawing on the canvas from AHUD::DrawHUD keeps the instrument out of the sample, and as a
 * bonus keeps the report alive in a cooked Shipping build with no widget assets loaded at all.
 *
 * Set this as the HUD class on your game mode, or leave your own HUD alone and turn on
 * "Auto Draw On Any HUD" in Project Settings, which routes the identical panel through
 * AHUD::OnHUDPostRender instead. The two paths know about each other and cannot draw twice.
 */
UCLASS()
class READGUARD_API AReadGuardHUD : public AHUD
{
	GENERATED_BODY()

public:
	AReadGuardHUD();

	//~ AHUD interface
	virtual void DrawHUD() override;

	/** Show or hide the panel. ReadGuard.Show and ReadGuard.Hide flip the same switch. */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	void ToggleReport();

	UFUNCTION(BlueprintPure, Category = "ReadGuard")
	bool IsReportVisible() const;

	/** Start a scan at the normal scale. */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	void ScanNow();

	/** Start a scan at 100 percent and then at the large text scale, and merge the two. */
	UFUNCTION(BlueprintCallable, Category = "ReadGuard")
	void ScanAtLargeScale();

	/** Top-left corner of the panel, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReadGuard")
	FVector2D PanelOrigin = FVector2D(28.0f, 90.0f);

	/**
	 * Panel width in pixels.
	 *
	 * Wide by default: a finding names a screen, a widget and a measurement against a threshold, and a
	 * report whose most important lines wrap is a report nobody reads twice.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReadGuard")
	float PanelWidth = 980.0f;
};
