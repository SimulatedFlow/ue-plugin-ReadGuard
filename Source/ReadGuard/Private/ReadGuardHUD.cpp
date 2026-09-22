// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "ReadGuardHUD.h"

#include "Engine/Canvas.h"
#include "ReadGuardSubsystem.h"

AReadGuardHUD::AReadGuardHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AReadGuardHUD::ToggleReport()
{
	if (UReadGuardSubsystem* Subsystem = UReadGuardSubsystem::Get(this))
	{
		Subsystem->SetReportVisible(!Subsystem->IsReportVisible());
	}
}

bool AReadGuardHUD::IsReportVisible() const
{
	const UReadGuardSubsystem* Subsystem = UReadGuardSubsystem::Get(this);
	return Subsystem && Subsystem->IsReportVisible();
}

void AReadGuardHUD::ScanNow()
{
	if (UReadGuardSubsystem* Subsystem = UReadGuardSubsystem::Get(this))
	{
		Subsystem->Scan();
	}
}

void AReadGuardHUD::ScanAtLargeScale()
{
	if (UReadGuardSubsystem* Subsystem = UReadGuardSubsystem::Get(this))
	{
		Subsystem->ScanBothScales();
	}
}

void AReadGuardHUD::DrawHUD()
{
	Super::DrawHUD();

	if (Canvas == nullptr)
	{
		return;
	}

	// Read from the subsystem on the frame it is drawn. Nothing is cached here, so the panel cannot show
	// one verdict while the last scan holds another.
	const UReadGuardSubsystem* Subsystem = UReadGuardSubsystem::Get(this);
	if (Subsystem == nullptr || !Subsystem->IsReportVisible())
	{
		return;
	}

	Subsystem->DrawReport(Canvas, PanelOrigin, PanelWidth);
}
