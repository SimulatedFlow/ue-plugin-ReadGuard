// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "ReadGuard.h"
#include "ReadGuardLog.h"

DEFINE_LOG_CATEGORY(LogReadGuard);

#define LOCTEXT_NAMESPACE "FReadGuardModule"

void FReadGuardModule::StartupModule()
{
	UE_LOG(LogReadGuard, Log, TEXT("ReadGuard started."));
}

void FReadGuardModule::ShutdownModule()
{
	UE_LOG(LogReadGuard, Log, TEXT("ReadGuard shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FReadGuardModule, ReadGuard)
