// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "ReadGuardLog.h"
#include "ReadGuardScanner.h"
#include "ReadGuardSettings.h"
#include "ReadGuardStatics.h"
#include "ReadGuardSubsystem.h"

/**
 * The console surface.
 *
 * Every one of these needs a running game, and unlike the asset checkers in this range that is not a
 * limitation to work around - it is the definition of the tool. There is no such thing as measuring the
 * text a game is drawing when the game is not drawing anything. A command that quietly produced a report
 * without a game would be producing a report about nothing.
 *
 * So when there is no game, these say so and do nothing, which is the only honest answer available.
 */
namespace ReadGuardCommands
{
	static bool ParseBool(const TArray<FString>& Args, const bool bDefault)
	{
		if (Args.Num() == 0)
		{
			return bDefault;
		}

		return Args[0].ToBool() || Args[0] == TEXT("1");
	}

	/** The live subsystem, if any game instance anywhere has one. */
	static UReadGuardSubsystem* FindSubsystem()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			// Game and PIE only. An editor world has no game instance and nothing on screen to measure.
			if (Context.WorldType != EWorldType::Game && Context.WorldType != EWorldType::PIE)
			{
				continue;
			}

			if (UGameInstance* Instance = Context.OwningGameInstance)
			{
				if (UReadGuardSubsystem* Subsystem = Instance->GetSubsystem<UReadGuardSubsystem>())
				{
					return Subsystem;
				}
			}
		}

		return nullptr;
	}

	static UReadGuardSubsystem* RequireSubsystem(const TCHAR* CommandName)
	{
		UReadGuardSubsystem* Subsystem = FindSubsystem();
		if (Subsystem == nullptr)
		{
			UE_LOG(LogReadGuard, Warning,
				TEXT("%s needs a running game. ReadGuard measures the text that is on screen; with nothing on screen there is nothing to measure."),
				CommandName);
		}

		return Subsystem;
	}

	static FAutoConsoleCommand GScan(
		TEXT("ReadGuard.Scan"),
		TEXT("ReadGuard.Scan - measure every visible text block now. The result appears on the panel and in the log."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			if (UReadGuardSubsystem* Subsystem = RequireSubsystem(TEXT("ReadGuard.Scan")))
			{
				Subsystem->Scan();
			}
		}));

	static FAutoConsoleCommand GScanAt200(
		TEXT("ReadGuard.ScanAt200"),
		TEXT("ReadGuard.ScanAt200 - measure at 100 percent and then at the large text scale, and report what broke in between."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			if (UReadGuardSubsystem* Subsystem = RequireSubsystem(TEXT("ReadGuard.ScanAt200")))
			{
				Subsystem->ScanBothScales();
			}
		}));

	static FAutoConsoleCommand GShow(
		TEXT("ReadGuard.Show"),
		TEXT("ReadGuard.Show [0|1] - show the on-screen report."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (UReadGuardSubsystem* Subsystem = RequireSubsystem(TEXT("ReadGuard.Show")))
			{
				Subsystem->SetReportVisible(ParseBool(Args, true));
			}
		}));

	static FAutoConsoleCommand GHide(
		TEXT("ReadGuard.Hide"),
		TEXT("ReadGuard.Hide - hide the on-screen report."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			if (UReadGuardSubsystem* Subsystem = FindSubsystem())
			{
				Subsystem->SetReportVisible(false);
			}
		}));

	static FAutoConsoleCommand GDump(
		TEXT("ReadGuard.Dump"),
		TEXT("ReadGuard.Dump - the whole last report to the log: every finding, its sentence, and what was checked."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			if (const UReadGuardSubsystem* Subsystem = RequireSubsystem(TEXT("ReadGuard.Dump")))
			{
				FReadGuardScanner::LogReport(Subsystem->GetReport(), /*bAllFindings=*/true);
			}
		}));

	static FAutoConsoleCommand GReport(
		TEXT("ReadGuard.Report"),
		TEXT("ReadGuard.Report [path] - write the last report as JSON. Default: the path in Project Settings."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (UReadGuardSubsystem* Subsystem = RequireSubsystem(TEXT("ReadGuard.Report")))
			{
				Subsystem->WriteReport(Args.Num() > 0 ? Args[0] : FString());
			}
		}));

	static FAutoConsoleCommand GTarget(
		TEXT("ReadGuard.Target"),
		TEXT("ReadGuard.Target <width> <height> - measure against another resolution for this session, e.g. ReadGuard.Target 1920 1080."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			UReadGuardSubsystem* Subsystem = RequireSubsystem(TEXT("ReadGuard.Target"));
			if (Subsystem == nullptr)
			{
				return;
			}

			if (Args.Num() < 2)
			{
				const FIntPoint Current = Subsystem->GetTargetResolution();
				UE_LOG(LogReadGuard, Display, TEXT("ReadGuard: currently measuring against %dx%d."), Current.X, Current.Y);
				return;
			}

			Subsystem->SetTargetResolution(FIntPoint(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1])));
			Subsystem->Scan();
		}));

	static FAutoConsoleCommand GExempt(
		TEXT("ReadGuard.Exempt"),
		TEXT("ReadGuard.Exempt [0|1] - use the exemption list, or do not, for this session. Rescans immediately."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			UReadGuardSubsystem* Subsystem = RequireSubsystem(TEXT("ReadGuard.Exempt"));
			if (Subsystem == nullptr)
			{
				return;
			}

			Subsystem->SetExemptionsEnabled(ParseBool(Args, true));
			UE_LOG(LogReadGuard, Display, TEXT("ReadGuard: exemption list %s."),
				Subsystem->AreExemptionsEnabled() ? TEXT("on") : TEXT("off"));

			Subsystem->Scan();
		}));

	/**
	 * The gate.
	 *
	 * Measures at 100 percent and at the large scale, writes Saved/ReadGuard/report.json and ends the
	 * process with 0 when there is nothing to say, 1 when there are only warnings and 2 when there is an
	 * error. Those are the same three numbers LocaleGuard, AssetWarden, WidgetLedger, LoadLens, HeapCensus
	 * and BindGuard return, and they mean the same three things.
	 *
	 * Two things a build script has to know about this one. It is not instantaneous - the two passes need
	 * frames to settle and a frame to capture - so the exit happens some frames after the command. And it
	 * only ever judges the screens that were on display when it ran, which is why the report writes down
	 * how many text blocks it saw and names them: a gate that passes on an empty screen has proved nothing,
	 * and the file says so plainly enough for a script to refuse it.
	 *
	 * -noexit reports without ending the process, which is what you want when you are typing this into the
	 * console rather than running it from a script.
	 */
	static void RunGate(const TArray<FString>& Args)
	{
		bool bExitWhenDone = true;
		FString Path;

		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("-noexit"), ESearchCase::IgnoreCase))
			{
				bExitWhenDone = false;
			}
			else if (!Arg.StartsWith(TEXT("-")))
			{
				Path = Arg;
			}
		}

		UReadGuardSubsystem* Subsystem = FindSubsystem();
		if (Subsystem == nullptr)
		{
			UE_LOG(LogReadGuard, Error,
				TEXT("ReadGuard.Gate: no running game. Nothing was measured, so this is a failure and not a pass."));

			if (bExitWhenDone)
			{
				FPlatformMisc::RequestExitWithStatus(/*Force=*/true, 2, TEXT("ReadGuard.Gate"));
			}
			return;
		}

		Subsystem->BeginGate(Path, bExitWhenDone);
	}

	static FAutoConsoleCommand GGate(
		TEXT("ReadGuard.Gate"),
		TEXT("ReadGuard.Gate [path] [-noexit] - measure at 100%% and 200%%, write the report, exit 0 clean / 1 warnings / 2 errors."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunGate));
}
