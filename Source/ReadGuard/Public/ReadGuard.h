// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * ReadGuard's one runtime module.
 *
 * It owns nothing and starts nothing. The measuring lives in UReadGuardSubsystem, which the engine creates
 * with the game instance, and the arithmetic lives in UReadGuardStatics, which needs no world at all.
 */
class FReadGuardModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
